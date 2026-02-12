/*
 * PhantomOS LZ4 Block Compression
 * Freestanding implementation for kernel use.
 *
 * Based on the LZ4 block format specification.
 * Uses a 4096-entry hash table for match finding (~8KB stack).
 */

#include "lz4.h"

extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);

#define LZ4_HASH_BITS   12
#define LZ4_HASH_SIZE   (1 << LZ4_HASH_BITS)
#define LZ4_MIN_MATCH   4
#define LZ4_MAX_OFFSET  65535
#define LZ4_LAST_LITERALS 5  /* Last 5 bytes are always literals */

/* Hash function for 4-byte sequences */
static uint32_t lz4_hash4(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (v * 2654435761U) >> (32 - LZ4_HASH_BITS);
}

/* Write variable-length extra bytes (for lengths >= 15) */
static int write_extra_len(uint8_t *dst, size_t dst_max, size_t *op, size_t extra)
{
    while (extra >= 255) {
        if (*op >= dst_max) return -1;
        dst[(*op)++] = 255;
        extra -= 255;
    }
    if (*op >= dst_max) return -1;
    dst[(*op)++] = (uint8_t)extra;
    return 0;
}

int lz4_compress(const uint8_t *src, size_t src_len,
                 uint8_t *dst, size_t dst_max,
                 size_t *compressed_len_out)
{
    if (!src || !dst || !compressed_len_out) return -1;

    if (src_len == 0) {
        *compressed_len_out = 0;
        return 0;
    }

    /* For very small inputs, just emit as literals */
    if (src_len < LZ4_MIN_MATCH + LZ4_LAST_LITERALS) {
        size_t op = 0;
        size_t lit_len = src_len;
        uint8_t token = (uint8_t)((lit_len < 15 ? lit_len : 15) << 4);
        if (op >= dst_max) return -1;
        dst[op++] = token;
        if (lit_len >= 15) {
            if (write_extra_len(dst, dst_max, &op, lit_len - 15) != 0)
                return -1;
        }
        if (op + lit_len > dst_max) return -1;
        memcpy(dst + op, src, lit_len);
        op += lit_len;
        *compressed_len_out = op;
        return 0;
    }

    uint16_t hash_table[LZ4_HASH_SIZE];
    memset(hash_table, 0xFF, sizeof(hash_table)); /* 0xFFFF = invalid */

    size_t ip = 0;        /* input position */
    size_t op = 0;        /* output position */
    size_t anchor = 0;    /* start of current literal run */
    size_t match_limit = src_len - LZ4_LAST_LITERALS;

    while (ip < match_limit) {
        uint32_t h = lz4_hash4(src + ip);
        size_t ref = hash_table[h];
        hash_table[h] = (uint16_t)(ip & 0xFFFF);

        /* Check for match */
        if (ref != 0xFFFF) {
            /* Reconstruct full offset: ref is 16-bit, handle wrapping */
            size_t ref_pos = ref;
            /* For inputs > 64KB, the hash table only stores low 16 bits.
             * We try the most recent possible position. */
            if (ip > 0xFFFF) {
                size_t base = ip & ~(size_t)0xFFFF;
                ref_pos = base | ref;
                if (ref_pos >= ip) {
                    if (base > 0) ref_pos -= 0x10000;
                    else ref_pos = 0xFFFF; /* invalid */
                }
            }

            size_t offset = ip - ref_pos;

            if (offset > 0 && offset <= LZ4_MAX_OFFSET &&
                ref_pos + LZ4_MIN_MATCH <= src_len &&
                src[ref_pos] == src[ip] &&
                src[ref_pos + 1] == src[ip + 1] &&
                src[ref_pos + 2] == src[ip + 2] &&
                src[ref_pos + 3] == src[ip + 3]) {

                /* Found match — calculate match length */
                size_t match_len = LZ4_MIN_MATCH;
                while (ip + match_len < src_len &&
                       ref_pos + match_len < src_len &&
                       src[ref_pos + match_len] == src[ip + match_len])
                    match_len++;

                /* Emit literal run + match */
                size_t lit_len = ip - anchor;
                size_t ml = match_len - LZ4_MIN_MATCH;

                /* Token byte */
                uint8_t token = (uint8_t)(((lit_len < 15 ? lit_len : 15) << 4) |
                                          (ml < 15 ? ml : 15));
                if (op >= dst_max) return -1;
                dst[op++] = token;

                /* Extra literal length */
                if (lit_len >= 15) {
                    if (write_extra_len(dst, dst_max, &op, lit_len - 15) != 0)
                        return -1;
                }

                /* Literal bytes */
                if (op + lit_len > dst_max) return -1;
                memcpy(dst + op, src + anchor, lit_len);
                op += lit_len;

                /* Match offset (little-endian 16-bit) */
                if (op + 2 > dst_max) return -1;
                dst[op++] = (uint8_t)(offset & 0xFF);
                dst[op++] = (uint8_t)((offset >> 8) & 0xFF);

                /* Extra match length */
                if (ml >= 15) {
                    if (write_extra_len(dst, dst_max, &op, ml - 15) != 0)
                        return -1;
                }

                ip += match_len;
                anchor = ip;

                /* Hash the next position for better compression */
                if (ip < match_limit) {
                    hash_table[lz4_hash4(src + ip)] = (uint16_t)(ip & 0xFFFF);
                }
                continue;
            }
        }

        ip++;
    }

    /* Emit final literal run (everything from anchor to end) */
    {
        size_t lit_len = src_len - anchor;
        uint8_t token = (uint8_t)((lit_len < 15 ? lit_len : 15) << 4);
        if (op >= dst_max) return -1;
        dst[op++] = token;

        if (lit_len >= 15) {
            if (write_extra_len(dst, dst_max, &op, lit_len - 15) != 0)
                return -1;
        }

        if (op + lit_len > dst_max) return -1;
        memcpy(dst + op, src + anchor, lit_len);
        op += lit_len;
    }

    *compressed_len_out = op;
    return 0;
}

/* Read variable-length extra bytes */
static size_t read_extra_len(const uint8_t *src, size_t src_len, size_t *ip)
{
    size_t len = 0;
    uint8_t b;
    do {
        if (*ip >= src_len) return (size_t)-1;
        b = src[(*ip)++];
        len += b;
    } while (b == 255);
    return len;
}

int lz4_decompress(const uint8_t *src, size_t src_len,
                   uint8_t *dst, size_t dst_max,
                   size_t *decompressed_len_out)
{
    if (!src || !dst || !decompressed_len_out) return -1;

    if (src_len == 0) {
        *decompressed_len_out = 0;
        return 0;
    }

    size_t ip = 0;  /* input position */
    size_t op = 0;  /* output position */

    while (ip < src_len) {
        /* Read token */
        uint8_t token = src[ip++];
        size_t lit_len = (token >> 4) & 0x0F;
        size_t match_len = token & 0x0F;

        /* Read extra literal length */
        if (lit_len == 15) {
            size_t extra = read_extra_len(src, src_len, &ip);
            if (extra == (size_t)-1) return -1;
            lit_len += extra;
        }

        /* Copy literals */
        if (ip + lit_len > src_len) return -1;
        if (op + lit_len > dst_max) return -1;
        memcpy(dst + op, src + ip, lit_len);
        ip += lit_len;
        op += lit_len;

        /* Check if this is the last token (no match follows) */
        if (ip >= src_len) break;

        /* Read match offset (little-endian 16-bit) */
        if (ip + 2 > src_len) return -1;
        size_t offset = (size_t)src[ip] | ((size_t)src[ip + 1] << 8);
        ip += 2;

        if (offset == 0 || offset > op) return -1;  /* Invalid offset */

        /* Read extra match length */
        match_len += LZ4_MIN_MATCH;
        if ((token & 0x0F) == 15) {
            size_t extra = read_extra_len(src, src_len, &ip);
            if (extra == (size_t)-1) return -1;
            match_len += extra;
        }

        /* Copy match (may overlap — byte-by-byte for overlapping copies) */
        if (op + match_len > dst_max) return -1;
        size_t match_pos = op - offset;
        for (size_t i = 0; i < match_len; i++) {
            dst[op + i] = dst[match_pos + i];
        }
        op += match_len;
    }

    *decompressed_len_out = op;
    return 0;
}
