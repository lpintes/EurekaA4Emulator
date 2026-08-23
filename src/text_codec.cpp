#include "text_codec.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

namespace {

// Exact KEYBCS2/Kamenicky table used by the supplied Czech A4 ROM. Windows
// does not expose this historical character set as a dependable code page.
constexpr wchar_t kKamenickyHigh[] =
    L"ČüéďäĎŤčěĚĹÍľĺÄÁÉžŽôöÓůÚýÖÜŠĽÝŘť"
    L"áíóúňŇŮÔšřŕŔ¼§«»"
    L"░▒▓│┤╡╢╖╕╣║╗╝╜╛┐"
    L"└┴┬├─┼╞╟╚╔╩╦╠═╬╧"
    L"╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀"
    L"αßΓπΣσµτΦΘΩδ∞φε∩"
    L"≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ";
static_assert(std::size(kKamenickyHigh) == 129);

bool EncodeOne(wchar_t ch, uint8_t& output) {
  if (ch < 0x80) {
    output = static_cast<uint8_t>(ch);
    return true;
  }
  const wchar_t* found = std::find(std::begin(kKamenickyHigh),
                                   std::end(kKamenickyHigh) - 1, ch);
  if (found == std::end(kKamenickyHigh) - 1) return false;
  output = static_cast<uint8_t>(0x80 + (found - std::begin(kKamenickyHigh)));
  return true;
}

void AppendReplacement(wchar_t ch, std::wstring& normalized) {
  switch (ch) {
    case 0x00a0: case 0x202f: normalized.push_back(L' '); break;
    case 0x2018: case 0x2019: case 0x201a: normalized.push_back(L'\''); break;
    case 0x201c: case 0x201d: case 0x201e: normalized.push_back(L'"'); break;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013:
    case 0x2014: case 0x2212: normalized.push_back(L'-'); break;
    case 0x2026: normalized.append(L"..."); break;
    default: normalized.push_back(ch); break;
  }
}

}  // namespace

std::vector<uint8_t> EncodeKamenicky(std::wstring_view text) {
  std::wstring cleaned;
  cleaned.reserve(text.size());
  for (wchar_t ch : text) AppendReplacement(ch, cleaned);

  std::vector<uint8_t> encoded;
  encoded.reserve(cleaned.size());
  for (wchar_t ch : cleaned) {
    uint8_t byte = 0;
    if (EncodeOne(ch, byte)) {
      encoded.push_back(byte);
      continue;
    }

    // For a Unicode character outside KEYBCS2, remove its combining marks and
    // use the base character where possible. This keeps modern Windows text
    // readable instead of feeding arbitrary CP1250/CP852 bytes to the ROM.
    wchar_t decomposed[16]{};
    const int count = NormalizeString(NormalizationD, &ch, 1, decomposed,
                                      static_cast<int>(std::size(decomposed)));
    bool used = false;
    for (int index = 0; index < count && !used; ++index) {
      const WORD type = [&] {
        WORD result = 0;
        GetStringTypeW(CT_CTYPE3, &decomposed[index], 1, &result);
        return result;
      }();
      if ((type & (C3_NONSPACING | C3_DIACRITIC)) != 0) continue;
      if (EncodeOne(decomposed[index], byte)) {
        encoded.push_back(byte);
        used = true;
      }
    }
    if (!used) encoded.push_back('?');
  }
  return encoded;
}

std::wstring DecodeKamenicky(const uint8_t* bytes, std::size_t length) {
  std::wstring result;
  result.reserve(length);
  for (std::size_t index = 0; index < length; ++index) {
    const uint8_t byte = bytes[index];
    result.push_back(byte < 0x80 ? static_cast<wchar_t>(byte)
                                : kKamenickyHigh[byte - 0x80]);
  }
  return result;
}
