#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_LEN 32
#define SHA256_HEX_LEN    65   /* 64 characters plus the terminator */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;
} Sha256;

void sha256_init(Sha256 *ctx);
void sha256_update(Sha256 *ctx, const void *data, size_t len);
void sha256_final(Sha256 *ctx, uint8_t out[SHA256_DIGEST_LEN]);

/* Hashes a file. Returns 0 on success, -1 if it could not be read. */
int  sha256_file(const char *path, char out_hex[SHA256_HEX_LEN]);

/* CRC-32 as used by zip and gzip (reflected, polynomial 0xEDB88320). */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
int      crc32_file(const char *path, uint32_t *out);

#endif
