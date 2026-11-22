/* The boot logo: tools/bootlogo.py output from the user's own IPSW (wsl/make_phone_payload.sh),
 * empty when there is none. Apple's image: built in, never committed (out/ is ignored). */
#ifndef LOGO
#define LOGO "out/logo.bin"
#endif
.section .logo, "a"
.balign 16
.incbin LOGO
