/* The XNU payload built by wsl/make_phone_payload.sh (tools/mkpayload.py format). */
#ifndef PAYLOAD
#define PAYLOAD "out/payload.bin"
#endif
.section .payload, "a"
.balign 4096
.incbin PAYLOAD
