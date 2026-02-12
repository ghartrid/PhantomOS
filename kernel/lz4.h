/*
 * PhantomOS LZ4 Block Compression
 * Freestanding implementation, no external dependencies.
 *
 * Simplified LZ4 block format:
 *   [token][extra_lit_len...][literals...][offset_lo][offset_hi][extra_match_len...]
 *   token: high nibble = literal length, low nibble = match length - 4
 *   15 in either nibble means "more bytes follow" (each 255 = +255, final < 255)
 */

#ifndef PHANTOMOS_KERNEL_LZ4_H
#define PHANTOMOS_KERNEL_LZ4_H

#include <stdint.h>
#include <stddef.h>

/*
 * Compress src into dst using LZ4 block format.
 * Returns 0 on success, -1 on failure (output buffer too small).
 * compressed_len_out receives the compressed size.
 */
int lz4_compress(const uint8_t *src, size_t src_len,
                 uint8_t *dst, size_t dst_max,
                 size_t *compressed_len_out);

/*
 * Decompress src into dst.
 * Returns 0 on success, -1 on failure (corrupt data or output buffer too small).
 * decompressed_len_out receives the decompressed size.
 */
int lz4_decompress(const uint8_t *src, size_t src_len,
                   uint8_t *dst, size_t dst_max,
                   size_t *decompressed_len_out);

#endif /* PHANTOMOS_KERNEL_LZ4_H */
