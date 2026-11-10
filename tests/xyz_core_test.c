/* The upstream cross-platform oracle must agree before native cars use its wire. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int roam_selftest(uint8_t *out, int cap);
int main(int argc, char **argv) {
    const int cap = 1 << 20;
    uint8_t *b = malloc(cap);
    if (!b) return 2;
    int n = roam_selftest(b, cap);
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < n; ++i) { h ^= b[i]; h *= 0x01000193u; }
    printf("roam_selftest: %d bytes, FNV-1a %08x\n", n, h);
    if (argc == 2) {
        FILE *f = fopen(argv[1], "wb");
        if (!f) { free(b); return 2; }
        size_t written = fwrite(b, 1, n, f);
        int closed = fclose(f);
        if (written != (size_t)n || closed) { free(b); return 2; }
    }
    free(b);
    return n == 37880 && h == 0x5b4b09dcu ? 0 : 1;
}
