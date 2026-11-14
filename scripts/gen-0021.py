#!/usr/bin/env python3
"""Insert the console capture block into serial_general.c and emit 0021."""
import pathlib, subprocess

X = pathlib.Path('/opt/work/nx741j-build/UniXNU/out/nx741j/ios-16.7.16-20H392/nx741j/tree/.cache/xnu-work')
R = pathlib.Path('/mnt/e/default-workspace/z80u-collect/repos/UniXNU-nx741j')
F = X / 'osfmk/console/serial_general.c'
TARGET = R / 'patches/xnu/0021-console-log-capture.patch'

subprocess.run(['git', 'checkout', '--', 'osfmk/console/serial_general.c'], cwd=X, check=True)
s = F.read_text()

anchor = "void\nconsole_printbuf_putc(int ch, void * arg)\n{\n"
assert anchor in s, 'anchor missing'

block = """/*
 * UniXNU,NX741J: in-memory console capture (this board has no UART).
 * The buffer lives in BSS, so the magic is written on the first character
 * instead of being a static initialiser; the efisp loader scans DRAM for that
 * magic on the next boot and copies the buffer to rawdump.  Every append is
 * cleaned to the point of coherency because the loader reads DRAM with the MMU
 * and caches off: without the dc cvac the copied text shows bit flips.
 */
volatile unsigned char nxl_console_log[0x20000] __attribute__((aligned(4096)));
static unsigned int nxl_console_len;

void
console_printbuf_putc(int ch, void * arg)
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
"""

s = s.replace(anchor, block, 1)
F.write_text(s)

diff = subprocess.run(['git', 'diff', '--', 'osfmk/console/serial_general.c'], cwd=X, capture_output=True, text=True)
TARGET.write_text(diff.stdout)
subprocess.run(['git', 'checkout', '--', 'osfmk/console/serial_general.c'], cwd=X, check=True)
check = subprocess.run(['git', 'apply', '--check', str(TARGET)], cwd=X, capture_output=True, text=True)
print('patch lines:', len(diff.stdout.splitlines()), 'apply_check_rc:', check.returncode, check.stderr[:200])
