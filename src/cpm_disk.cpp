#include "cpm_disk.h"

namespace fs = std::filesystem;

namespace cpm {

namespace {

// The diskette's alphabet has no accents, and dropping them outright would
// turn PRÍBEH into PRBEH -- unreadable to the one person who has to find the
// file again.  Folding keeps the word.
wchar_t FoldLatin(wchar_t ch) {
  switch (ch) {
    case L'Á': case L'Ä': case L'á': case L'ä': return L'A';
    case L'Č': case L'č': return L'C';
    case L'Ď': case L'ď': return L'D';
    case L'É': case L'Ě': case L'é': case L'ě': return L'E';
    case L'Í': case L'í': return L'I';
    case L'Ĺ': case L'Ľ': case L'ĺ': case L'ľ': return L'L';
    case L'Ň': case L'ň': return L'N';
    case L'Ó': case L'Ô': case L'Ö': case L'ó': case L'ô': case L'ö': return L'O';
    case L'Ŕ': case L'Ř': case L'ŕ': case L'ř': return L'R';
    case L'Š': case L'š': return L'S';
    case L'Ť': case L'ť': return L'T';
    case L'Ú': case L'Ů': case L'Ü': case L'ú': case L'ů': case L'ü': return L'U';
    case L'Ý': case L'ý': return L'Y';
    case L'Ž': case L'ž': return L'Z';
    default: return ch;
  }
}

}  // namespace

std::string NamePart(const std::wstring& source, std::size_t maximum) {
  std::string result;
  for (wchar_t original : source) {
    wchar_t folded = FoldLatin(original);
    if (folded >= L'a' && folded <= L'z') folded -= L'a' - L'A';
    char out = '_';
    if ((folded >= L'A' && folded <= L'Z') ||
        (folded >= L'0' && folded <= L'9')) {
      out = static_cast<char>(folded);
    } else if (folded == L'_' || folded == L'-' || folded == L'$' ||
               folded == L'#' || folded == L'@' || folded == L'!' ||
               folded == L'%' || folded == L'&' || folded == L'~' ||
               folded == L'^') {
      out = static_cast<char>(folded);
    }
    if (result.empty() || result.back() != '_' || out != '_') result.push_back(out);
    if (result.size() == maximum) break;
  }
  while (!result.empty() && (result.back() == '_' || result.back() == ' ')) result.pop_back();
  return result.empty() ? "FILE" : result;
}

std::string MakeName(const fs::path& path) {
  const std::string stem = NamePart(path.stem().wstring(), 8);
  const std::string type = NamePart(path.extension().wstring().substr(
      path.extension().wstring().empty() ? 0 : 1), 3);
  return type.empty() || path.extension().empty() ? stem : stem + "." + type;
}

std::string UniqueName(const std::string& requested, const std::set<std::string>& used) {
  if (!used.contains(requested)) return requested;
  const std::size_t dot = requested.find('.');
  std::string stem = requested.substr(0, dot);
  const std::string type = dot == std::string::npos ? "" : requested.substr(dot);
  for (unsigned number = 1; number < 1000; ++number) {
    const std::string suffix = "~" + std::to_string(number);
    const std::string candidate = stem.substr(0, 8 - std::min<std::size_t>(8, suffix.size())) +
                                  suffix + type;
    if (!used.contains(candidate)) return candidate;
  }
  return "COLLIDE.$$$";
}

}  // namespace cpm
