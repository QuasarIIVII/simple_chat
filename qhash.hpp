#ifndef QHASH_SHA3_512_HPP
#define QHASH_SHA3_512_HPP

#include <array>
#include <cstdint>

namespace qhash {

using u8  = std::uint8_t;
using u64 = std::uint64_t;

/*
 * Computes SHA3-512 hash.
 * Parameters:
 *   data   : Pointer to the input buffer (may be nullptr if bitlen == 0).
 *   bitlen : Length of the input measured in bits.
 *
 * Returns:
 *   std::array<u8, 64> containing the 512-bit (64 bytes) digest.
 *
 * Requirements:
 *   The memory pointed to by `data` must be valid for at least ceil(bitlen / 8) bytes.
 */
[[nodiscard]] std::array<u8, 64>
sha3_512(const void* data, u64 bitlen);


/*
 * Convenience overload when bit precision is not needed.
 * This processes the input as bytes, meaning bitlen = bytelen * 8.
 */
[[nodiscard]] inline std::array<u8, 64>
sha3_512_bytes(const void* data, std::size_t bytelen) {
    return sha3_512(data, static_cast<u64>(bytelen) * 8);
}

} // namespace qhash

#endif // QHASH_SHA3_512_HPP
