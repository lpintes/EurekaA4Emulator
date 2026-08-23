#include <iostream>
#include <string>

#include "text_codec.h"

int main() {
  const std::wstring original = L"Příliš žluťoučký kôň, ľalia, ŕieka";
  const auto encoded = EncodeKamenicky(original);
  const std::wstring decoded = DecodeKamenicky(encoded.data(), encoded.size());
  const auto punctuation = EncodeKamenicky(L"A–B… ‘text’");
  const std::wstring normalized =
      DecodeKamenicky(punctuation.data(), punctuation.size());
  const bool passed = decoded == original && normalized == L"A-B... 'text'";
  std::cout << (passed ? "PASS" : "FAIL")
            << " encoded_bytes=" << encoded.size() << "\n";
  return passed ? 0 : 1;
}
