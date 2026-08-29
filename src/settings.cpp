#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <cwctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Keys are ASCII on purpose even though the file is Slovak otherwise: they are
// compared byte for byte, and a key the user retypes without diacritics would
// otherwise stop matching.
constexpr char kLastDiskKey[] = "posledna-disketa";
constexpr char kSlotPrefix[] = "slot";
// A locked diskette, one per line: "zamok1=C:\Hry".  Numbered rather than
// repeated under one key, because the parser here takes the last value for a
// key and a repeated one would quietly keep only the final lock.
constexpr char kLockedPrefix[] = "zamok";
constexpr char kBom[] = "\xef\xbb\xbf";

std::string ToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0, nullptr, nullptr);
  if (length <= 0) return {};
  std::string result(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      result.data(), length, nullptr, nullptr);
  return result;
}

std::wstring FromUtf8(const std::string& text) {
  if (text.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      result.data(), length);
  return result;
}

std::string Trim(const std::string& text) {
  const auto isSpace = [](char ch) { return ch == ' ' || ch == '\t'; };
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && isSpace(text[begin])) ++begin;
  while (end > begin && isSpace(text[end - 1])) --end;
  return text.substr(begin, end - begin);
}

// %APPDATA%.  Asked of the shell rather than read from the environment: an
// environment variable is inherited and can arrive already wrong from whatever
// launched us, and this is the path the file is written to.
fs::path RoamingFolder() {
  PWSTR path = nullptr;
  if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path) != S_OK)
    return {};
  fs::path result(path);
  CoTaskMemFree(path);
  return result;
}

}  // namespace

// Every marker, not just this one exactly: an older settings file may hold
// *pamat-nenaformatovana, which this version no longer makes.  Treating it as
// the memory slot is right and, more to the point, keeps it from being mounted
// as a folder called "*pamat-nenaformatovana".
bool SlotIsRam(const std::wstring& slot) {
  return !slot.empty() && slot.front() == L'*';
}

std::wstring SlotDisplayName(const std::wstring& slot) {
  if (slot.empty()) return L"(prázdny)";
  if (SlotIsRam(slot)) return L"nová prázdna v pamäti";
  const fs::path path(slot);
  fs::path leaf = path.filename();
  // A trailing separator ("C:\disky\eureka\") leaves filename() empty.
  if (leaf.empty()) leaf = path.parent_path().filename();
  return leaf.empty() ? slot : leaf.wstring();
}

fs::path ExecutableDirectory() {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
  buffer.resize(length);
  return fs::path(buffer).parent_path();
}

fs::path Settings::FindFile() {
  std::error_code ec;
  const fs::path portable = ExecutableDirectory() / L"config";
  if (fs::is_directory(portable, ec)) return portable / L"nastavenia.txt";
  const fs::path roaming = RoamingFolder();
  // Deliberately not falling back to the portable path: that folder exists
  // only when the user made it, and creating it here would switch portable
  // mode on behind their back.  An empty path means Save says why instead.
  if (roaming.empty()) return {};
  return roaming / L"EurekaA4" / L"nastavenia.txt";
}

void Settings::Load() {
  std::ifstream input(file_, std::ios::binary);
  if (!input) return;
  std::string bytes((std::istreambuf_iterator<char>(input)),
                    std::istreambuf_iterator<char>());
  // Written with a BOM, tolerated without one: an editor may add or drop it
  // and neither should change what the file means.
  if (bytes.starts_with(kBom)) bytes.erase(0, 3);

  std::istringstream lines(bytes);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::size_t equals = line.find('=');
    // Blank lines and comments have no '=' and land here; a comment that does
    // contain one is caught by the '#' test below.
    if (equals == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, equals));
    if (key.empty() || key.front() == '#') continue;
    std::wstring value = FromUtf8(Trim(line.substr(equals + 1)));

    if (key == kLastDiskKey) {
      lastDisk_ = std::move(value);
      continue;
    }
    // Before the slot test: "zamok" and "slot" do not overlap, but the lock
    // lines are the ones a hand-editing user is most likely to duplicate, and
    // reading them first keeps that path short.
    if (key.starts_with(kLockedPrefix)) {
      if (!value.empty()) SetDiskLocked(value, true);
      continue;
    }
    if (key.starts_with(kSlotPrefix)) {
      const std::string number = key.substr(sizeof(kSlotPrefix) - 1);
      if (number.size() == 1 && number.front() >= '1' &&
          number.front() <= '0' + kSlots)
        slots_[static_cast<std::size_t>(number.front() - '1')] = std::move(value);
    }
    // Anything else is a key from a version that knows more than this one.
    // Ignored rather than guessed at -- and lost on the next save, which the
    // header in the file says out loud.
  }
}

bool Settings::Save(std::wstring& error) const {
  if (file_.empty()) {
    error = L"Nie je kam uložiť nastavenia: systém nepovedal, kde je "
            L"priečinok aplikačných dát.";
    return false;
  }
  std::error_code ec;
  // Not checked: the folder may well exist already, and the write below is
  // what actually decides whether this worked.
  fs::create_directories(file_.parent_path(), ec);

  // CRLF, because this file is meant to be opened in a plain Windows editor.
  std::wstring text =
      L"# Nastavenia emulátora Eureka A4.\r\n"
      L"# Súbor prepisuje emulátor, vlastné riadky v ňom neprežijú.\r\n"
      L"\r\n";
  if (!lastDisk_.empty()) text += L"posledna-disketa=" + lastDisk_ + L"\r\n";
  for (int number = 1; number <= kSlots; ++number) {
    const auto index = static_cast<std::size_t>(number - 1);
    if (slots_[index].empty()) continue;
    text += L"slot" + std::to_wstring(number) + L"=" + slots_[index] + L"\r\n";
  }
  // The locked diskettes, as paths.  They are not tied to the slots: a
  // diskette put in from the folder picker can be locked too, and the ROM's
  // bulk copy needs that lock back when the diskette goes in again.
  int locked = 0;
  for (const std::wstring& folder : lockedDisks_)
    text += L"zamok" + std::to_wstring(++locked) + L"=" + folder + L"\r\n";

  std::ofstream output(file_, std::ios::binary | std::ios::trunc);
  const std::string bytes = kBom + ToUtf8(text);
  if (!output || !output.write(bytes.data(),
                               static_cast<std::streamsize>(bytes.size()))) {
    error = L"Nastavenia sa nepodarilo zapísať do súboru\r\n" +
            file_.wstring() +
            L"\r\n\r\nZmena platí pre tento beh, ale ďalší štart o nej "
            L"nebude vedieť.";
    return false;
  }
  return true;
}

void Settings::SetLastDisk(std::wstring path) { lastDisk_ = std::move(path); }

const std::wstring& Settings::slot(int number) const {
  static const std::wstring empty;
  if (number < 1 || number > kSlots) return empty;
  return slots_[static_cast<std::size_t>(number - 1)];
}

void Settings::SetSlot(int number, std::wstring path) {
  if (number < 1 || number > kSlots) return;
  slots_[static_cast<std::size_t>(number - 1)] = std::move(path);
}

// Two spellings of one folder are one diskette.  Windows paths are
// case-insensitive and take either slash, and the picker, a slot typed by
// hand and VirtualDisk's canonical form differ in exactly those ways -- so
// comparing them raw would lose a lock the moment the same diskette arrived
// by another road.
std::wstring Settings::NormalizePath(const std::wstring& folder) {
  std::wstring result;
  result.reserve(folder.size());
  for (wchar_t ch : folder)
    result.push_back(ch == L'/' ? L'\\'
                                : static_cast<wchar_t>(std::towlower(ch)));
  // A trailing separator is the same folder; a bare root ("C:\") is not, so
  // one character is always left.
  while (result.size() > 1 && result.back() == L'\\') result.pop_back();
  return result;
}

bool Settings::SameDisk(const std::wstring& left, const std::wstring& right) {
  if (left.empty() || right.empty()) return false;
  return NormalizePath(left) == NormalizePath(right);
}

bool Settings::disk_locked(const std::wstring& folder) const {
  if (folder.empty()) return false;
  for (const std::wstring& locked : lockedDisks_)
    if (SameDisk(locked, folder)) return true;
  return false;
}

void Settings::SetDiskLocked(const std::wstring& folder, bool locked) {
  // A diskette in memory cannot be remembered: it exists nowhere but in this
  // process, so a line in the file would point at nothing.  Its lock lasts as
  // long as the diskette does, which is the honest span for it.
  if (folder.empty() || SlotIsRam(folder)) return;
  for (auto it = lockedDisks_.begin(); it != lockedDisks_.end(); ++it) {
    if (!SameDisk(*it, folder)) continue;
    if (!locked) lockedDisks_.erase(it);
    return;  // Already there: locking again must not add a second line.
  }
  if (locked) lockedDisks_.push_back(folder);
}
