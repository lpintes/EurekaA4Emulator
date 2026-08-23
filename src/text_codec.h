#ifndef EUREKA_TEXT_CODEC_H
#define EUREKA_TEXT_CODEC_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

std::vector<uint8_t> EncodeKamenicky(std::wstring_view text);
std::wstring DecodeKamenicky(const uint8_t* bytes, std::size_t length);

#endif
