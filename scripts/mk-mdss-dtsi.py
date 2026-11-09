
#!/usr/bin/env python3
"""Generate dts/nx741j-mdss.dtsi from the device's live Android FDT.

The stock FDT (adb pull /sys/firmware/fdt) describes the whole MSM display
stack: the SDE/MDP controller with its pipe configuration, the two DSI
controllers and PHYs, the display connectors (DSI/writeback/DP/CESTA), the
panel power supplies and the PLL-code region.  Every link between those nodes
is a raw phandle number, which is useless in our tree because dtc re-assigns
phandles at compile time.

This script walks the display subtree, rewrites the phandle-list properties
(clocks, interconnects, power-domains, iommus, nvmem-cells, *-supply,
*-gpios, connectors, ...) as label references, and emits placeholder nodes
for the providers outside the subtree (GCC, DISPCC, RPMh clock, NoC, PMIC
regulators, qfprom cells, SMMU, SOCCP).  The placeholders keep the binding
documented and let the fragment compile on its own; they are
status = "disabled" so no driver ever matches them.

The panel node itself stays out of this file: nx741j-panel-bf375.dtsi
describes it below &dsi0 and exports the "panel" label used by the DSI
display connectors.

Usage:
    mk-mdss-dtsi.py --fdt live-fdt.dtb --out dts/nx741j-mdss.dtsi
"""
from __future__ import annotations

import argparse
import collections
import re
import subprocess
import sys
from pathlib import Path

INDENT = '\t'


# ---------------------------------------------------------------- dts parser


class Node:
    __slots__ = ('name', 'labels', 'props', 'children', 'path', 'parent')

    def __init__(self, name, path, parent=None):
        self.name = name
        self.path = path
        self.parent = parent
        self.labels = []
        self.props = collections.OrderedDict()
        self.children = []


def _new_node(stack, header):
    labels = []
    head = header.strip()
    while True:
        m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*):\s*(.*)$', head)
        if not m:
            break
        labels.append(m.group(1))
        head = m.group(2)
    name = head.strip()
    parent = stack[-1]
    path = '/' if name == '/' else parent.path.rstrip('/') + '/' + name
    node = Node(name, path, parent)
    node.labels = labels
    parent.children.append(node)
    return node


def parse_dts(text):
    root = Node('', '/')
    stack = [root]
    buf = None
    for raw in text.splitlines():
        s = raw.strip()
        if not s:
            continue
        if buf is not None:
            buf += ' ' + s
            if buf.endswith(';'):
                m = re.match(r'^([^=]+?)(?:\s*=\s*(.*))?;$', buf)
                if m:
                    stack[-1].props[m.group(1).strip()] = (m.group(2) or '').strip()
                buf = None
            continue
        if s.startswith('/*') or s.startswith('*') or s.startswith('//'):
            continue
        if s in ('};', '}'):
            if len(stack) > 1:
                stack.pop()
            continue
        if s.endswith('{'):
            stack.append(_new_node(stack, s[:-1]))
            continue
        if s.endswith(';'):
            if s.startswith('/dts-v1/') or s.startswith('/memreserve/') or s.startswith('/plugin/'):
                continue
            m = re.match(r'^([^=]+?)(?:\s*=\s*(.*))?;$', s)
            if m:
                stack[-1].props[m.group(1).strip()] = (m.group(2) or '').strip()
            continue
        buf = s
    return root


def walk_nodes(node, out):
    out.append(node)
    for child in node.children:
        walk_nodes(child, out)


# --------------------------------------------------------- phandle cell walk

CELL_PROPS = {
    'clocks': '#clock-cells',
    'assigned-clocks': '#clock-cells',
    'assigned-clock-parents': '#clock-cells',
    'interconnects': '#interconnect-cells',
    'power-domains': '#power-domain-cells',
    'iommus': '#iommu-cells',
    'phys': '#phy-cells',
    'nvmem-cells': '#nvmem-cell-cells',
    'mboxes': '#mbox-cells',
    'resets': '#reset-cells',
    'interrupts-extended': None,
}
ZERO_CELL_PROPS = {
    'connectors', 'qcom,mdp', 'pll_codes_region', 'qcom,sde-soccp-controller',
    'interrupt-parent', 'qcom,dsi-panel', 'qcom,dsi-default-panel',
    'qcom,dsi-phy', 'qcom,dsi-ctrl', 'pinctrl-0', 'pinctrl-1', 'pinctrl-2', 'pinctrl-3',
    'memory-region',
}


PINCTRL_RE = re.compile(r'^pinctrl(-\d+)?$')


def is_ref_prop(name):
    return (name in CELL_PROPS or name in ZERO_CELL_PROPS
            or bool(PINCTRL_RE.match(name))
            or name.endswith('-supply') or name.endswith('-supply-entries')
            or name.endswith('-gpio') or name.endswith('-gpios'))


def _first_int(value):
    m = re.search(r'<0x([0-9a-fA-F]+)>', value)
    if m:
        return int(m.group(1), 16)
    m = re.search(r'<(\d+)>', value)
    return int(m.group(1)) if m else 0


def cell_count(target, prop):
    if (prop.endswith('-supply') or prop.endswith('-supply-entries')
            or PINCTRL_RE.match(prop) or prop in ZERO_CELL_PROPS):
        return 0
    if prop.endswith('-gpios'):
        value = target.props.get('#gpio-cells')
        return _first_int(value) if value else 2
    cells = CELL_PROPS.get(prop)
    if cells is None:
        return 1
    value = target.props.get(cells)
    return _first_int(value) if value else 0


def split_cell_segments(value):
    segments = []
    i = 0
    while i < len(value):
        ch = value[i]
        if ch == '<':
            end = value.index('>', i)
            segments.append(('cells', value[i + 1:end].split()))
            i = end + 1
        elif ch == '"':
            end = value.index('"', i + 1)
            segments.append(('text', value[i:end + 1]))
            i = end + 1
        elif ch == '[':
            end = value.index(']', i)
            segments.append(('text', value[i:end + 1]))
            i = end + 1
        else:
            end = i
            while end < len(value) and value[end] not in '<"[':
                end += 1
            segments.append(('text', value[i:end]))
            i = end
    return segments


def resolve_refs(node, ph2node):
    """[(prop, [(cell-index, target-node), ...]), ...]"""
    found = []
    for prop, value in node.props.items():
        if not is_ref_prop(prop) or '<' not in value:
            continue
        tokens = [tok for kind, toks in split_cell_segments(value) if kind == 'cells' for tok in toks]
        refs = []
        i = 0
        while i < len(tokens):
            try:
                number = int(tokens[i], 16) if tokens[i].lower().startswith('0x') else int(tokens[i], 0)
            except ValueError:
                i += 1
                continue
            target = ph2node.get(number)
            if target is None:
                i += 1
                continue
            refs.append((i, target))
            i += 1 + cell_count(target, prop)
        if refs:
            found.append((prop, refs))
    return found


def rewrite_value(value, refs, label_of, drop=frozenset()):
    """Rewrite phandle cells as &label; references in drop are removed."""
    labels = {}
    for index, target in refs:
        labels[index] = None if target.path in drop else label_of(target)
    out = []
    offset = 0
    for kind, payload in split_cell_segments(value):
        if kind != 'cells':
            out.append(payload)
            continue
        tokens = []
        for token in payload:
            if offset in labels:
                label = labels[offset]
                if label is not None:
                    tokens.append('&' + label)
            else:
                tokens.append(token)
            offset += 1
        out.append('<' + ' '.join(tokens) + '>')
    return ''.join(out)


# ------------------------------------------------------------------ topology

ROOT_ORDER = [
    '/soc/qcom,mdss_mdp@9800000',
    '/soc/qcom,mdss_dsi_ctrl0@9ac0000',
    '/soc/qcom,mdss_dsi_ctrl1@9ac3000',
    '/soc/qcom,mdss_dsi_phy0@9ac1500',
    '/soc/qcom,mdss_dsi_phy1@9ac4500',
    '/soc/dsi_pll_codes',
    '/soc/dsi_panel_pwr_supply',
    '/soc/qcom,dsi-display-primary',
    '/soc/qcom,dsi-display-secondary',
    '/soc/qcom,smmu_sde_unsec_cb',
    '/soc/qcom,smmu_sde_sec_cb',
    '/soc/qcom,wb-display@0',
    '/soc/qcom,wb-display@1',
]

ROOT_LABELS = {
    '/soc/qcom,mdss_mdp@9800000': 'mdss',
    '/soc/qcom,mdss_dsi_ctrl0@9ac0000': 'dsi0',
    '/soc/qcom,mdss_dsi_ctrl1@9ac3000': 'dsi1',
    '/soc/qcom,mdss_dsi_phy0@9ac1500': 'dsi_phy0',
    '/soc/qcom,mdss_dsi_phy1@9ac4500': 'dsi_phy1',
    '/soc/dsi_pll_codes': 'dsi_pll_codes',
    '/soc/dsi_panel_pwr_supply': 'dsi_panel_pwr_supply',
    '/soc/qcom,dsi-display-primary': 'dsi_display_primary',
    '/soc/qcom,dsi-display-secondary': 'dsi_display_secondary',
    '/soc/qcom,smmu_sde_unsec_cb': 'smmu_sde_unsec',
    '/soc/qcom,smmu_sde_sec_cb': 'smmu_sde_sec',
    '/soc/qcom,wb-display@0': 'wb_display0',
    '/soc/qcom,wb-display@1': 'wb_display1',
}

# Providers that already exist in our tree (sm8850.dtsi / nx741j-hw.dtsi).
ALIAS_LABELS = {
    '/soc/pinctrl@f000000': 'tlmm',
    '/soc/interrupt-controller@17000000': 'intc',
    '/soc/apps-smmu@15000000': 'apps_smmu',
}

# The stock tree points qcom,dsi-default-panel at the nt37801 reference panel
# that ships with the vendor kernel; this board uses the BF375 panel, so the
# reference is redirected to the node nx741j-panel-bf375.dtsi exports.
PANEL_ALIASES = {
    '/soc/qcom,mdss_mdp@9800000/qcom,mdss_dsi_nt37801_wqhd_plus_cmd': 'panel',
}

# Connector nodes that stay out of this file (DisplayPort and its HDCP block drag
# in the USB/alt-mode graph).  Their entries in mdss "connectors" are dropped.
EXCLUDED_REFS = {
    '/soc/qcom,dp_display@9ad2000': 'dp-display',
    '/soc/qcom,sde_cesta@0x9bd8000': 'sde-cesta',
}

# Properties that reference hardware this file does not describe yet: they are
# emitted as comments so the loss stays visible instead of turning into a
# dangling phandle.
DROP_PROPS = {
    'pinctrl-0': 'TLMM pin state, see nx741j-hw.dtsi',
    'pinctrl-1': 'TLMM pin state, see nx741j-hw.dtsi',
    'pinctrl-2': 'TLMM pin state, see nx741j-hw.dtsi',
    'pinctrl-3': 'TLMM pin state, see nx741j-hw.dtsi',
    'pinctrl-names': 'only meaningful together with pinctrl-0..3',
    'memory-region': 'reserved-memory region, not modelled yet',
}

PANEL_PATH = '/soc/qcom,mdss_mdp@9800000/qcom,mdss_dsi_bf375_rm692h5_6p8_dev0_dsc_cmd'
PANEL_LABEL = 'panel'

STUB_ORDER = [
    '/soc/clock-controller@100000',
    '/soc/clock-controller@1fd5044',
    '/soc/clock-controller@9ba2000',
    '/soc/clock-controller@9ba2000/clock-controller@9ba4000',
    '/soc/rsc@18900000/drv@2/clock-controller',
    '/soc/qcom,dp_pll@88eb000',
    '/soc/interconnect@1',
    '/soc/interconnect@1600000',
    '/soc/interconnect@1780000',
    '/soc/interconnect@31100000',
    '/soc/apps-smmu@15000000',
    '/soc/remoteproc-soccp@a3380000',
    '/soc/qfprom@221c8000/ssip_config@148',
    '/soc/qfprom@221c8000/disp_qultiv_fuse@144',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-mmcxlvl/regulator-pmh0110-d-s7-level',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l1d-e0/regulator-pmh0110-d-l1',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l3d-e0/regulator-pmh0110-d-l3',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l4b-e0/regulator-pmh0101-l4',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l11b-e0/regulator-pmh0101-l11',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l12b-e0/regulator-pmh0101-l12',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l13b-e0/regulator-pmh0101-l13',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l4f-e0/regulator-pmh0110-f-l4',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-mxlvl/regulator-pmh0110-i-s10-level',
]

STUB_LABELS = {
    '/soc/clock-controller@100000': 'gcc',
    '/soc/clock-controller@1fd5044': 'tcsrcc',
    '/soc/clock-controller@9ba2000': 'dispcc',
    '/soc/clock-controller@9ba2000/clock-controller@9ba4000': 'dispcc_mx',
    '/soc/rsc@18900000/drv@2/clock-controller': 'rpmh_clk',
    '/soc/qcom,dp_pll@88eb000': 'dp_pll',
    '/soc/interconnect@1': 'mc_virt',
    '/soc/interconnect@1600000': 'cnoc_cfg',
    '/soc/interconnect@1780000': 'mmss_noc',
    '/soc/interconnect@31100000': 'gem_noc',
    '/soc/apps-smmu@15000000': 'apps_smmu',
    '/soc/remoteproc-soccp@a3380000': 'soccp',
    '/soc/qfprom@221c8000/ssip_config@148': 'qfprom_ssip_config',
    '/soc/qfprom@221c8000/disp_qultiv_fuse@144': 'qfprom_disp_qultiv_fuse',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-mmcxlvl/regulator-pmh0110-d-s7-level': 'pmh0110_d_s7_level',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l1d-e0/regulator-pmh0110-d-l1': 'pmh0110_d_l1',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l3d-e0/regulator-pmh0110-d-l3': 'pmh0110_d_l3',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l4b-e0/regulator-pmh0101-l4': 'pmh0101_l4',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l11b-e0/regulator-pmh0101-l11': 'pmh0101_l11',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l12b-e0/regulator-pmh0101-l12': 'pmh0101_l12',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l13b-e0/regulator-pmh0101-l13': 'pmh0101_l13',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-l4f-e0/regulator-pmh0110-f-l4': 'pmh0110_f_l4',
    '/soc/rsc@18900000/drv@2/rpmh-regulator-mxlvl/regulator-pmh0110-i-s10-level': 'pmh0110_i_s10_level',
}

# Properties the stock node lacks but the placement in this tree needs: the DSI
# controllers get the panel as a child, so they need address/size cells.
EXTRA_PROPS = {
    '/soc/qcom,mdss_dsi_ctrl0@9ac0000': [('#address-cells', '<1>'), ('#size-cells', '<0>')],
    '/soc/qcom,mdss_dsi_ctrl1@9ac3000': [('#address-cells', '<1>'), ('#size-cells', '<0>')],
}

# Placeholder properties that only make sense below their real parent node.
STUB_DROP = {
    '/soc/qfprom@221c8000/ssip_config@148': {'reg'},
    '/soc/qfprom@221c8000/disp_qultiv_fuse@144': {'reg'},
}

# Properties a placeholder must not inherit: they would drag in another layer
# of providers, which is exactly what this file is not modelling yet.
STUB_SKIP = {'clocks', 'interconnects', 'power-domains', 'iommus', 'phys', 'nvmem-cells',
             'interrupts-extended', 'resets', 'mboxes', 'assigned-clocks',
             'assigned-clock-parents', 'qcom,dsi-panel', 'qcom,mdp', 'connectors',
             'memory-region'}


# ----------------------------------------------------------------- emission


def split_atoms(value):
    """Split on spaces that are outside quoted strings."""
    atoms, cur, quoted = [], '', False
    for ch in value:
        if ch == '"':
            quoted = not quoted
            cur += ch
        elif ch == ' ' and not quoted:
            if cur:
                atoms.append(cur)
                cur = ''
        else:
            cur += ch
    if cur:
        atoms.append(cur)
    return atoms


def format_prop(depth, name, value, limit=104):
    pad = INDENT * depth
    if not value:                       # boolean property
        return ['%s%s;' % (pad, name)]
    head = '%s%s = ' % (pad, name)
    if len(head + value + ';') <= limit:
        return [head + value + ';']
    cont = pad + INDENT * 2
    lines, cur, first = [], head, True
    for atom in split_atoms(value):
        candidate = cur + atom if cur.endswith(' ') else cur + ' ' + atom
        if len(candidate) + 1 > limit and not first:
            lines.append(cur)
            cur = cont + atom
        else:
            cur = candidate
        first = False
    lines.append(cur + ';')
    return lines


def emit_node(node, depth, labels, refs, label_of, out, keep, skip=(), drop=frozenset()):
    pad = INDENT * depth
    label = labels.get(node.path)
    header = '%s: %s {' % (label, node.name) if label else '%s {' % node.name
    out.append(pad + header)
    node_refs = refs.get(node.path, {})
    for prop, value in node.props.items():
        if prop in ('phandle', 'linux,phandle'):
            continue
        if prop in DROP_PROPS:
            shown = value if len(value) <= 88 else value[:85] + '...'
            out.append('%s/* %s = %s;  -- %s */' % (INDENT * (depth + 1), prop, shown, DROP_PROPS[prop]))
            continue
        if prop in node_refs:
            value = rewrite_value(value, node_refs[prop], label_of, drop)
        out.extend(format_prop(depth + 1, prop, value))
    for prop, value in EXTRA_PROPS.get(node.path, ()):
        out.extend(format_prop(depth + 1, prop, value))
    for child in node.children:
        if child.path in skip or child.path not in keep:
            continue
        emit_node(child, depth + 1, labels, refs, label_of, out, keep, skip, drop)
    out.append(pad + '};')


def emit_stub(node, label, out):
    out.append('\t/* stock: %s */' % node.path)
    out.append('\t%s: %s {' % (label, node.name))
    skip = STUB_DROP.get(node.path, set())
    props = [(p, v) for p, v in node.props.items()
             if p not in ('phandle', 'linux,phandle') and p not in STUB_SKIP
             and p not in skip and not is_ref_prop(p)]
    props = [(p, v) for p, v in props if p != 'status'] + [('status', '"disabled"')]
    for prop, value in props:
        out.extend(format_prop(2, prop, value))
    out.append('\t};')


def decompile(fdt, live):
    subprocess.run(['dtc', '-I', 'dtb', '-O', 'dts', '-o', str(live), str(fdt)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not live.exists():
        sys.exit('dtc failed on %s' % fdt)


# --------------------------------------------------------------------- main


def audit(included, ph2node):
    """Report non-reference properties whose cells happen to hold live phandles."""
    candidates = collections.Counter()
    samples = {}
    for node in included:
        for prop, value in node.props.items():
            if (prop in ('phandle', 'linux,phandle') or prop in DROP_PROPS
                    or is_ref_prop(prop) or '<' not in value):
                continue
            tokens = [tok for kind, toks in split_cell_segments(value) if kind == 'cells' for tok in toks]
            hits = 0
            for token in tokens:
                try:
                    number = int(token, 16) if token.lower().startswith('0x') else int(token, 0)
                except ValueError:
                    continue
                if number in ph2node:
                    hits += 1
            if hits:
                candidates[prop] += 1
                samples.setdefault(prop, (node.path, value[:80], hits, len(tokens)))
    for prop, count in candidates.most_common():
        path, sample, hits, total = samples[prop]
        print('%-34s nodes=%-3d hits=%d/%d %s | %s' % (prop, count, hits, total, path[-44:], sample))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fdt', required=True, help='live FDT dumped from the device')
    ap.add_argument('--out', required=True, help='dtsi to write')
    ap.add_argument('--live', help='cached decompiled dts (default: alongside --fdt)')
    ap.add_argument('--audit', action='store_true', help='list properties that still hold raw phandles')
    args = ap.parse_args()

    fdt = Path(args.fdt)
    live = Path(args.live) if args.live else fdt.with_suffix('.dts')
    if not live.exists():
        if not fdt.exists():
            sys.exit('need --fdt or a cached --live file')
        decompile(fdt, live)
    elif fdt.exists() and live.stat().st_mtime < fdt.stat().st_mtime:
        decompile(fdt, live)

    root = parse_dts(live.read_text(errors='ignore'))
    nodes = []
    walk_nodes(root, nodes)
    bypath = {n.path: n for n in nodes}
    ph2node = {}
    for node in nodes:
        for key in ('phandle', 'linux,phandle'):
            value = node.props.get(key)
            if value:
                m = re.search(r'<0x([0-9a-fA-F]+)>', value)
                if m:
                    ph2node[int(m.group(1), 16)] = node

    missing = [p for p in ROOT_ORDER if p not in bypath]
    if missing:
        sys.exit('missing display nodes in %s:\n  %s' % (live, '\n  '.join(missing)))

    included, seen = [], set()

    def add(node):
        if node.path in seen:
            return
        seen.add(node.path)
        included.append(node)
        for child in node.children:
            if child.path == PANEL_PATH:
                continue
            # mdss carries the vendor's whole panel library (one qcom,mdss_dsi_*
            # child per reference panel).  Our panel lives in nx741j-panel-bf375.dtsi.
            if node.path == '/soc/qcom,mdss_mdp@9800000' and child.name.startswith('qcom,mdss_dsi_'):
                continue
            add(child)

    for path in ROOT_ORDER:
        add(bypath[path])

    if args.audit:
        audit(included, ph2node)
        return

    # every reference that leaves the subtree needs a label
    external = collections.OrderedDict()
    for node in included:
        for prop, refs in resolve_refs(node, ph2node):
            if prop in DROP_PROPS:
                continue
            for _, target in refs:
                if (target.path in seen or target.path == PANEL_PATH
                        or target.path in EXCLUDED_REFS or target.path in ALIAS_LABELS
                        or target.path in PANEL_ALIASES):
                    continue
                external.setdefault(target.path, target)

    unknown = [p for p in external if p not in STUB_LABELS]
    if unknown:
        sys.exit('unmapped provider references:\n  %s' % '\n  '.join(unknown))

    labels = dict(ROOT_LABELS)
    labels[PANEL_PATH] = PANEL_LABEL
    labels.update(PANEL_ALIASES)
    labels.update(ALIAS_LABELS)
    labels.update(STUB_LABELS)

    def label_of(node):
        return labels[node.path]

    refs = {n.path: {p: r for p, r in resolve_refs(n, ph2node) if p not in DROP_PROPS}
            for n in included}
    for path, per_prop in refs.items():
        for prop, items in per_prop.items():
            for _, target in items:
                if target.path in EXCLUDED_REFS or target.path in labels:
                    continue
                sys.exit('reference to unlabelled %s from %s:%s' % (target.path, path, prop))

    out = ['/*']
    out.append(' * Nubia Z80 Ultra (NX741J) MSM display controller (SDE/MDP + DSI).')
    out.append(' *')
    out.append(' * Generated by scripts/mk-mdss-dtsi.py from the device live FDT')
    out.append(' * (adb pull /sys/firmware/fdt); do not hand-edit, re-run the script instead.')
    out.append(' *')
    out.append(' * The stock tree keeps these nodes under /soc; they are placed at the root here')
    out.append(' * because sm8850.dtsi does not describe a /soc container yet.')
    out.append(' *')
    out.append(' * Providers outside the display block (GCC, DISPCC, RPMh clock, NoC, PMIC')
    out.append(' * regulators, qfprom cells, SMMU, SOCCP) are placeholders that carry the real')
    out.append(' * compatible/binding properties with status = "disabled": they exist so the')
    out.append(' * phandle lists keep their shape and the fragment compiles on its own.  Replace')
    out.append(' * them with the real driver nodes when those drivers are ported.')
    out.append(' *')
    out.append(' * Excluded on purpose: the DisplayPort connector (qcom,dp_display@9ad2000) and the')
    out.append(' * CESTA/HDCP block (qcom,sde_cesta@0x9bd8000) pull in the USB/alt-mode graph, so')
    out.append(' * their entries in mdss "connectors" are dropped; the TLMM pinctrl states of the')
    out.append(' * DSI connectors are dropped too (pinmux is a separate step).')
    out.append(' *')
    out.append(' * Nothing here is exported to the ADT yet: no node carries unixnu,adt-name.')
    out.append(' */')
    out.append('')
    out.append('/ {')

    out.append('\t/* ---- display providers (placeholders) ---- */')
    for path in STUB_ORDER:
        if path in external:
            emit_stub(external[path], STUB_LABELS[path], out)

    out.append('')
    out.append('\t/* ---- display controller ---- */')
    for path in ROOT_ORDER:
        emit_node(bypath[path], 1, labels, refs, label_of, out, seen, skip={PANEL_PATH},
                  drop=frozenset(EXCLUDED_REFS))
    out.append('};')
    out.append('')

    Path(args.out).write_text('\n'.join(out))
    print('wrote %s: %d nodes, %d placeholders, %d bytes'
          % (args.out, len(included), len(external), Path(args.out).stat().st_size))


if __name__ == '__main__':
    main()
