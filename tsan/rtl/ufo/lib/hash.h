

//
// Created by cbw on 9/26/16.
//
//  fast hash algorithms (Bowen 2017-10-13)
//
//

#ifndef UFO_HASH_H
#define UFO_HASH_H

#include "../ufo.h"


namespace bw {
namespace ufo {


ALWAYS_INLINE
u64 hash_128_to_64(const u64 upper, const u64 lower) {
  // Murmur-inspired hashing.
  const static u64 kMul = 0x9ddfea08eb382d69ULL;
  u64 a = (lower ^ upper) * kMul;
  a ^= (a >> 47);
  u64 b = (upper ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

/*
 * Thomas Wang 64 bit mix hash function
 */

ALWAYS_INLINE u64 twang_mix64(u64 key) {
  key = (~key) + (key << 21);  // key *= (1 << 21) - 1; key -= 1;
  key = key ^ (key >> 24);
  key = key + (key << 3) + (key << 8);  // key *= 1 + (1 << 3) + (1 << 8)
  key = key ^ (key >> 14);
  key = key + (key << 2) + (key << 4);  // key *= 1 + (1 << 2) + (1 << 4)
  key = key ^ (key >> 28);
  key = key + (key << 31);  // key *= 1 + (1 << 31)
  return key;
}

/*
 * Inverse of twang_mix64
 *
 * Note that twang_unmix64 is significantly slower than twang_mix64.
 */

ALWAYS_INLINE u64 twang_unmix64(u64 key) {
  // See the comments in jenkins_rev_unmix32 for an explanation as to how this
  // was generated
  key *= 4611686016279904257U;
  key ^= (key >> 28) ^ (key >> 56);
  key *= 14933078535860113213U;
  key ^= (key >> 14) ^ (key >> 28) ^ (key >> 42) ^ (key >> 56);
  key *= 15244667743933553977U;
  key ^= (key >> 24) ^ (key >> 48);
  key = (key + 1) * 9223367638806167551U;
  return key;
}

/*
 * Thomas Wang downscaling hash function
 */

ALWAYS_INLINE u32 twang_32from64(u64 key) {
  key = (~key) + (key << 18);
  key = key ^ (key >> 31);
  key = key * 21;
  key = key ^ (key >> 11);
  key = key + (key << 6);
  key = key ^ (key >> 22);
  return (u32) key;
}

ALWAYS_INLINE u32 jenkins_rev_mix32(u32 key) {
  key += (key << 12);  // key *= (1 + (1 << 12))
  key ^= (key >> 22);
  key += (key << 4);   // key *= (1 + (1 << 4))
  key ^= (key >> 9);
  key += (key << 10);  // key *= (1 + (1 << 10))
  key ^= (key >> 2);
  // key *= (1 + (1 << 7)) * (1 + (1 << 12))
  key += (key << 7);
  key += (key << 12);
  return key;
}

const u32 FNV_32_HASH_START = 2166136261UL;
const u64 FNV_64_HASH_START = 14695981039346656037ULL;

ALWAYS_INLINE u32 fnv32(const char* s,
                      u32 hash = FNV_32_HASH_START) {
  for (; *s; ++s) {
    hash += (hash << 1) + (hash << 4) + (hash << 7) +
            (hash << 8) + (hash << 24);
    hash ^= *s;
  }
  return hash;
}

ALWAYS_INLINE u32 fnv32_buf(const void* buf,
                          u32 n,
                          u32 hash = FNV_32_HASH_START) {
  // forcing signed char, since other platforms can use unsigned
  const signed char* char_buf = reinterpret_cast<const signed char*>(buf);

  for (u32 i = 0; i < n; ++i) {
    hash += (hash << 1) + (hash << 4) + (hash << 7) +
            (hash << 8) + (hash << 24);
    hash ^= char_buf[i];
  }

  return hash;
}


ALWAYS_INLINE u64 fnv64(const char* s,
                      u64 hash = FNV_64_HASH_START) {
  for (; *s; ++s) {
    hash += (hash << 1) + (hash << 4) + (hash << 5) + (hash << 7) +
            (hash << 8) + (hash << 40);
    hash ^= *s;
  }
  return hash;
}

ALWAYS_INLINE u64 fnv64_buf(const void* buf,
                          u32 n,
                          u64 hash = FNV_64_HASH_START) {
  // forcing signed char, since other platforms can use unsigned
  const signed char* char_buf = reinterpret_cast<const signed char*>(buf);

  for (u32 i = 0; i < n; ++i) {
    hash += (hash << 1) + (hash << 4) + (hash << 5) + (hash << 7) +
            (hash << 8) + (hash << 40);
    hash ^= char_buf[i];
  }
  return hash;
}

/*
 * Robert Jenkins' reversible 32 bit mix hash function
 */
struct Jenkins_mix32 {

  ALWAYS_INLINE
  u32 operator()(u32 key) {
    return jenkins_rev_mix32(key);
  }
};

struct TWang_32from64 {

  ALWAYS_INLINE
  u32 operator()(u64 key) {
    return twang_32from64(key);
  }
};


}
}

#endif //PROJECT_TREE_HASH_H
