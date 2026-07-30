/**
 * @file crc32.h
 * @brief CRC32 checksum calculation for data integrity verification.
 * @author Toan Tran
 * @version 1.0.0
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute the CRC32 checksum of a data buffer.
 *
 * Uses the standard IEEE CRC-32 polynomial (0xEDB88320).
 *
 * @param data  Pointer to the data buffer.
 * @param len   Length of the data in bytes.
 * @return The 32-bit CRC32 checksum.
 */
uint32_t crc32_compute(const void *data, size_t len);

/**
 * @brief Compute a running CRC32 checksum, continuing from a previous value.
 *
 * Useful for computing CRC over multiple non-contiguous buffers.
 *
 * @param crc   Previous CRC32 value (use 0 for initial call).
 * @param data  Pointer to the data buffer.
 * @param len   Length of the data in bytes.
 * @return The updated 32-bit CRC32 checksum.
 */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
