#!/usr/bin/env python3
"""Emit 0021: mirror the kernel console into a DRAM buffer for the loader.

Three touch points, all required by the log-capture recipe:

  * osfmk/console/serial_general.c  -- owns the buffer and nxl_console_append(),
    which every console writer calls; each append cleans the touched line with
    dc cvac because the efisp loader later reads DRAM with caches off.
  * osfmk/kern/printf.c             -- consdebug_putc() routes here as well, which
    is the path panic() uses (debug.c calls _doprnt(..., consdebug_putc, ...)).
  * osfmk/kern/debug.c              -- nxl_console_sync() (a full dc civac of the
    buffer) right before the panic text is emitted, so DRAM holds everything even
    if the machine resets during the panic.

The loader scans DRAM for the NXLOG1 magic on the next boot and copies the buffer
to rawdump, where the host reads it over 9008.
"""
import pathlib
import subprocess

X = pathlib.Path('/opt/work/nx741j-build/UniXNU/out/nx741j/ios-16.7.16-20H392/nx741j/tree/.cache/xnu-work')
R = pathlib.Path('/mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j')
TARGET = R / 'patches/xnu/0021-console-log-capture.patch'
FILES = ['osfmk/console/serial_general.c', 'osfmk/kern/printf.c', 'osfmk/kern/debug.c']

for name in FILES:
    subprocess.run(['git', 'checkout', '--', name], cwd=X, check=True)

# --- serial_general.c -----------------------------------------------------
f = X / 'osfmk/console/serial_general.c'
s = f.read_text()
anchor = "void\nconsole_printbuf_putc(int ch, void * arg)\n{\n"
assert anchor in s, 'serial_general.c anchor missing'
block = """/*
 * UniXNU,NX741J: in-memory console capture (this board has no UART).  The buffer
 * lives in BSS, so the magic is written on the first character instead of being a
 * static initialiser.  Every append is cleaned to the point of coherency because
 * the efisp loader reads DRAM with the MMU and caches off.
 */
volatile unsigned char nxl_console_log[0x20000] __attribute__((aligned(4096)));
unsigned int nxl_console_len;

void
nxl_console_append(int ch)
{
	if (nxl_console_len < sizeof(nxl_console_log) - 12) {
		if (nxl_console_len == 0) {
			nxl_console_log[0] = 0x4E; nxl_console_log[1] = 0x58;
			nxl_console_log[2] = 0x4C; nxl_console_log[3] = 0x4F;
			nxl_console_log[4] = 0x47; nxl_console_log[5] = 0x31;
			nxl_console_log[6] = 0;    nxl_console_log[7] = 0;
		}
		nxl_console_log[12 + nxl_console_len] = (unsigned char)ch;
		nxl_console_len++;
		nxl_console_log[8] = (unsigned char)(nxl_console_len);
		nxl_console_log[9] = (unsigned char)(nxl_console_len >> 8);
		nxl_console_log[10] = (unsigned char)(nxl_console_len >> 16);
		nxl_console_log[11] = (unsigned char)(nxl_console_len >> 24);
		__asm__ volatile ("dc cvac, %0" :: "r" ((unsigned long)&nxl_console_log[12 + nxl_console_len] & ~63UL) : "memory");
		__asm__ volatile ("dc cvac, %0" :: "r" ((unsigned long)nxl_console_log & ~63UL) : "memory");
	}
}

void
nxl_console_sync(void)
{
	unsigned long at = (unsigned long)nxl_console_log & ~63UL;
	unsigned long end = at + sizeof(nxl_console_log);
	for (; at < end; at += 64) {
		__asm__ volatile ("dc civac, %0" :: "r" (at) : "memory");
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

void
console_printbuf_putc(int ch, void * arg)
{
	nxl_console_append(ch);
"""
s = s.replace(anchor, block, 1)
f.write_text(s)

# --- printf.c -------------------------------------------------------------
f = X / 'osfmk/kern/printf.c'
s = f.read_text()
anchor = "void\nconsdebug_putc(char c)\n{\n"
assert anchor in s, 'printf.c anchor missing'
s = s.replace(anchor, anchor + "\tnxl_console_append((int)c);\n", 1)
decl_anchor = "void\nconsdebug_putc(char c)"
s = s.replace(decl_anchor, "extern void nxl_console_append(int ch);\n\n" + decl_anchor, 1)
f.write_text(s)

# --- debug.c --------------------------------------------------------------
f = X / 'osfmk/kern/debug.c'
s = f.read_text()
anchor = "_doprnt(debugger_panic_str, debugger_panic_args, consdebug_putc, 0);"
assert s.count(anchor) >= 1, 'debug.c anchor missing'
s = s.replace(anchor,
              "extern void nxl_console_sync(void);\n\t\t\tnxl_console_sync();\n\t\t\t" + anchor,
              1)
f.write_text(s)

diff = subprocess.run(['git', 'diff', '--'] + FILES, cwd=X, capture_output=True, text=True)
TARGET.write_text(diff.stdout)
for name in FILES:
    subprocess.run(['git', 'checkout', '--', name], cwd=X, check=True)
check = subprocess.run(['git', 'apply', '--check', str(TARGET)], cwd=X, capture_output=True, text=True)
print('patch lines:', len(diff.stdout.splitlines()), 'apply_rc:', check.returncode, check.stderr[:300])
