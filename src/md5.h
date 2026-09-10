#ifndef EUREKA_MD5_H
#define EUREKA_MD5_H

#include <array>
#include <cstddef>
#include <cstdint>

// RFC 1321 MD5.  Here for one job: fingerprinting the ROM image so a saved RAM
// snapshot can refuse to load over a different firmware (ea4-9fq).  A snapshot
// is full of pointers into FCBs and the directory, so restoring it under
// another ROM is restoring a state whose pointers now aim elsewhere.  MD5
// rather than a cheaper hash because the known-good value is written down as
// one -- HANDOFF names 9aa101ab69fc367e114e1a84b08feea1 -- so a stored digest
// can be eyeballed against it.
std::array<uint8_t, 16> Md5(const void* data, std::size_t length);

#endif  // EUREKA_MD5_H
