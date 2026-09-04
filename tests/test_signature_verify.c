#include "infra/signature_verify.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned char *read_all(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long n = ftell(fp);
    if (n <= 0) { fclose(fp); return NULL; }
    rewind(fp);

    unsigned char *buf = malloc((size_t)n);
    if (!buf) { fclose(fp); return NULL; }

    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *len = (size_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <public.pem> <manifest.txt> <signature_hex>\n", argv[0]);
        return 2;
    }

    size_t manifest_len = 0;
    unsigned char *manifest = read_all(argv[2], &manifest_len);
    if (!manifest) {
        fprintf(stderr, "failed to read manifest\n");
        return 2;
    }

    char err[256] = {0};
    bool ok = signature_verify_rsa_pss_sha256(
        manifest, manifest_len, argv[3], argv[1], err, sizeof(err));
    free(manifest);

    if (!ok) {
        fprintf(stderr, "verify failed: %s\n", err);
        return 1;
    }

    puts("signature verified");
    return 0;
}
