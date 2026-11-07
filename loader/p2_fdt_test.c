// Host-side unit test for the FDT writer in p2.c
// Build: gcc -O1 -o fdt_test p2_fdt_test.c && ./fdt_test out.dtb
#include <stdio.h>
#include <stdlib.h>

#define EfiMain EfiMain_unused_on_host
#include "p2.c"
#undef EfiMain

int main(int argc, char **argv)
{
    UINT64 size = 0;
    UINT8 *fdt = fdt_build(0x81234567ULL, 0x85234567ULL, &size);
    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror("fopen"); return 1; }
    if (fwrite(fdt, 1, size, f) != size) { perror("fwrite"); return 1; }
    fclose(f);
    printf("wrote %llu bytes to %s\n", (unsigned long long)size, argv[1]);
    return 0;
}
