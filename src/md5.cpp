#include "md5.h"

#include <cstring>

// A straight transcription of RFC 1321.  No streaming interface: the only
// caller hashes one contiguous buffer (the 256 KiB ROM), so the whole message
// is in hand and padding can be done once into a small tail block.

namespace {

constexpr uint32_t kInit[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu,
                               0x10325476u};

constexpr uint32_t kK[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu,
    0x4787c62au, 0xa8304613u, 0xfd469501u, 0x698098d8u, 0x8b44f7afu,
    0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu,
    0x49b40821u, 0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u,
    0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u,
    0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u,
    0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u,
    0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u, 0x432aff97u,
    0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du,
    0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};

constexpr uint32_t kS[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                             7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20,
                             5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23,
                             4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                             6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
                             6, 10, 15, 21};

inline uint32_t RotateLeft(uint32_t value, uint32_t bits) {
  return (value << bits) | (value >> (32 - bits));
}

void ProcessBlock(const uint8_t* block, uint32_t state[4]) {
  uint32_t m[16];
  for (int i = 0; i < 16; ++i)
    m[i] = static_cast<uint32_t>(block[i * 4]) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 3]) << 24);

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t f;
    uint32_t g;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) % 16;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) % 16;
    }
    f += a + kK[i] + m[g];
    a = d;
    d = c;
    c = b;
    b += RotateLeft(f, kS[i]);
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

}  // namespace

std::array<uint8_t, 16> Md5(const void* data, std::size_t length) {
  uint32_t state[4] = {kInit[0], kInit[1], kInit[2], kInit[3]};
  const auto* bytes = static_cast<const uint8_t*>(data);

  const std::size_t fullBlocks = length / 64;
  for (std::size_t i = 0; i < fullBlocks; ++i)
    ProcessBlock(bytes + i * 64, state);

  // The remainder plus the 0x80 marker, zero padding and the 64-bit little
  // endian bit length.  That is at most 64 + 64 bytes: one tail block, or two
  // when the remainder leaves no room for the length.
  uint8_t tail[128] = {};
  const std::size_t remainder = length - fullBlocks * 64;
  std::memcpy(tail, bytes + fullBlocks * 64, remainder);
  tail[remainder] = 0x80;
  const std::size_t tailBlocks = (remainder + 1 + 8 > 64) ? 2 : 1;
  const uint64_t bitLength = static_cast<uint64_t>(length) * 8;
  for (int i = 0; i < 8; ++i)
    tail[tailBlocks * 64 - 8 + i] =
        static_cast<uint8_t>((bitLength >> (i * 8)) & 0xff);
  for (std::size_t i = 0; i < tailBlocks; ++i)
    ProcessBlock(tail + i * 64, state);

  std::array<uint8_t, 16> digest{};
  for (int i = 0; i < 4; ++i) {
    digest[i * 4] = static_cast<uint8_t>(state[i] & 0xff);
    digest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 8) & 0xff);
    digest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 16) & 0xff);
    digest[i * 4 + 3] = static_cast<uint8_t>((state[i] >> 24) & 0xff);
  }
  return digest;
}
