#include "disk_split.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <set>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "text_codec.h"

namespace fs = std::filesystem;
namespace layout = disk_layout;

namespace disk_split {

namespace {

// UTF-8 for everything written for the user to read on the host, the same
// choice and for the same reason as nastavenia.txt: a path or a file name with
// diacritics has to survive being written and read back, and the ANSI code
// page corrupts it *silently* (6.22).  A BOM is written, because these two
// files are opened in Notepad by hand; one is tolerated on the way in, because
// a SPOLU.txt the user typed themselves may or may not carry it.
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
                                         static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      result.data(), length);
  return result;
}

bool WriteTextFile(const fs::path& path, const std::wstring& text, bool bom,
                   std::wstring& error) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    error = L"Nedá sa zapísať súbor " + path.wstring() + L".";
    return false;
  }
  if (bom) stream.write(kBom, 3);
  const std::string bytes = ToUtf8(text);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    error = L"Pri zápise súboru " + path.wstring() + L" nastala chyba.";
    return false;
  }
  return true;
}

// An 8.3 name is ASCII by construction, so widening it cannot lose anything.
std::wstring Widen(const std::string& text) {
  return std::wstring(text.begin(), text.end());
}

// SPOLU.txt, spolu.txt and Spolu.TXT are one file on Windows, so the name has
// to be compared the way the filesystem compares it.  Only ASCII case is
// folded, which is all this name has.
bool SameName(const std::wstring& left, const std::wstring& right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](wchar_t a, wchar_t b) {
                      return std::towlower(a) == std::towlower(b);
                    });
}

const wchar_t* ModeName(layout::Mode mode) {
  switch (mode) {
    case layout::Mode::kByFolder: return L"podľa priečinkov";
    case layout::Mode::kTight: return L"natesno";
    default: return L"sekvenčne, abecedne";
  }
}

const wchar_t* RuleName(layout::UnitSource source) {
  switch (source) {
    case layout::UnitSource::kSpolu: return L"SPOLU.txt";
    case layout::UnitSource::kSameStem: return L"zhoda mena";
    case layout::UnitSource::kCompanion: return L"sprievodná prípona";
    default: return L"";
  }
}

std::wstring GroupName(const std::wstring& group) {
  return group.empty() ? L"koreň kolekcie" : group;
}

// The list Eureka herself reads, in her own encoding.  One line per file: the
// 8.3 name and the size, which is what a diskette's directory would show if
// the machine had a way of showing it.
std::wstring DisketteContents(const layout::Diskette& diskette,
                              const std::vector<layout::SourceFile>& files) {
  std::wstring text = L"OBSAH DISKETY " + diskette.label + L"\r\n\r\n";
  for (const layout::Item& item : diskette.items) {
    text += Widen(item.name) + L" " +
            std::to_wstring((files[item.file].size + 1023) / 1024) + L"K\r\n";
  }
  return text;
}

}  // namespace

bool Scan(const fs::path& root, Collection& out, std::wstring& error) {
  out = Collection();
  std::error_code ec;
  const fs::path absolute = fs::weakly_canonical(root, ec);
  if (ec || !fs::is_directory(absolute, ec)) {
    error = L"Priečinok s kolekciou neexistuje alebo nie je prístupný.";
    return false;
  }

  const fs::path spolu = absolute / kSpoluName;
  if (fs::is_regular_file(spolu, ec)) {
    std::ifstream stream(spolu, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(stream)), {});
    if (!stream && !stream.eof()) {
      error = L"Súbor " + spolu.wstring() + L" sa nedá prečítať.";
      return false;
    }
    if (bytes.rfind(kBom, 0) == 0) bytes.erase(0, 3);
    out.spolu = layout::ParseSpolu(FromUtf8(bytes));
    out.has_spolu = true;
  }

  // skip_permission_denied rather than a failure: a collection sitting beside
  // a folder this account cannot open is still a collection, and stopping on
  // it would mean the wizard could not be used at all.
  fs::recursive_directory_iterator walk(
      absolute, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    error = L"Priečinok " + absolute.wstring() + L" sa nedá prečítať.";
    return false;
  }
  for (const fs::directory_entry& entry : walk) {
    if (!entry.is_regular_file(ec)) continue;
    // SPOLU.txt is the collection's instructions, not part of it.  Only the
    // one in the root: a file of that name deeper down was put there by the
    // user and belongs on a diskette like anything else.
    if (entry.path().parent_path() == absolute &&
        SameName(entry.path().filename().wstring(), kSpoluName))
      continue;
    layout::SourceFile file;
    file.path = entry.path();
    file.group = entry.path().parent_path() == absolute
                     ? std::wstring()
                     : fs::relative(entry.path().parent_path(), absolute, ec).wstring();
    file.size = entry.file_size(ec);
    if (ec) {
      error = L"Veľkosť súboru " + entry.path().wstring() + L" sa nedá zistiť.";
      return false;
    }
    out.bytes += file.size;
    out.files.push_back(std::move(file));
  }
  if (out.files.empty()) {
    error = L"V priečinku " + absolute.wstring() + L" nie je ani jeden súbor.";
    return false;
  }
  return true;
}

std::wstring Describe(const layout::Plan& plan,
                      const std::vector<layout::SourceFile>& files,
                      const layout::Options& options, const fs::path& source,
                      const fs::path& target) {
  std::wstring text = L"Plán rozdelenia kolekcie\r\n\r\n";
  text += L"Zdroj: " + source.wstring() + L"\r\n";
  text += L"Cieľ: " + target.wstring() + L"\r\n";
  text += L"Delenie: " + std::wstring(ModeName(options.mode)) + L"\r\n";
  text += L"Zhoda mena: " +
          std::wstring(options.group_by_stem ? L"áno" : L"nie") + L"\r\n";
  text += L"Sprievodné prípony: " +
          std::wstring(options.group_by_companion ? L"áno" : L"nie") + L"\r\n";
  text += L"Zoznam OBSAH.TXT na diskete: " +
          std::wstring(options.catalogue_on_diskette ? L"áno" : L"nie") + L"\r\n";
  // Counted, never spelled with a plural: this whole text is read aloud by a
  // screen reader and a "Label: number" line reads the same for one as for
  // five.  Slovak counts in three shapes and none of them is worth risking
  // here for the sake of a nicer sentence.
  text += L"Súborov v kolekcii: " + std::to_wstring(files.size()) + L"\r\n";
  text += L"Diskiet: " + std::to_wstring(plan.diskettes.size()) + L"\r\n";
  text += L"Odmietnutých súborov: " + std::to_wstring(plan.rejected.size()) +
          L"\r\n\r\n";

  for (const layout::Diskette& diskette : plan.diskettes) {
    text += L"Disketa " + diskette.label + L"\r\n";
    text += L"  Súborov: " + std::to_wstring(diskette.items.size()) +
            L", blokov: " + std::to_wstring(diskette.blocks) + L" z " +
            std::to_wstring(cpm::kAvailableBlocks) + L", položiek adresára: " +
            std::to_wstring(diskette.entries) + L" z " +
            std::to_wstring(cpm::kDirectoryEntries) + L"\r\n";
    for (const layout::Item& item : diskette.items) {
      const std::wstring leaf = files[item.file].path.filename().wstring();
      text += L"  " + Widen(item.name);
      // The original name only when the diskette changed it.  Repeating a name
      // that came through untouched would double the length of the longest
      // part of this text for nothing.
      if (Widen(item.name) != leaf) text += L" — pôvodne " + leaf;
      if (item.renamed) text += L" (premenované kvôli zhode mien)";
      text += L"\r\n";
    }
    text += L"\r\n";
  }

  // The three lists the preview has to carry, because counting diskettes says
  // nothing about the risk: this is where the risk lives (HANDOFF 6.22).
  std::map<std::wstring, std::set<std::wstring>> spread;
  for (const layout::Diskette& diskette : plan.diskettes)
    for (const layout::Item& item : diskette.items)
      spread[files[item.file].group].insert(diskette.label);
  text += L"Priečinky rozdelené medzi viac diskiet\r\n";
  bool any = false;
  for (const auto& group : spread) {
    if (group.second.size() < 2) continue;
    any = true;
    text += L"  " + GroupName(group.first) + L" — diskety:";
    for (const std::wstring& label : group.second) text += L" " + label;
    text += L"\r\n";
  }
  if (!any) text += L"  Žiadny priečinok nie je rozdelený.\r\n";
  text += L"\r\n";

  text += L"Zlúčené jednotky\r\n";
  any = false;
  for (const layout::Unit& unit : plan.units) {
    if (unit.files.size() < 2) continue;
    any = true;
    text += L"  ";
    for (std::size_t i = 0; i < unit.files.size(); ++i) {
      if (i) text += L", ";
      text += files[unit.files[i]].path.filename().wstring();
    }
    text += L" — pravidlo: " + std::wstring(RuleName(unit.source)) +
            L", priečinok: " + GroupName(unit.group) + L"\r\n";
  }
  if (!any) text += L"  Nič nie je zlúčené, každý súbor stojí sám.\r\n";
  text += L"\r\n";

  text += L"Premenované súbory\r\n";
  any = false;
  for (const layout::Diskette& diskette : plan.diskettes)
    for (const layout::Item& item : diskette.items) {
      if (!item.renamed) continue;
      any = true;
      text += L"  " + files[item.file].path.filename().wstring() + L" → " +
              Widen(item.name) + L" (disketa " + diskette.label + L")\r\n";
    }
  if (!any) text += L"  Nič sa nepremenováva.\r\n";
  text += L"\r\n";

  text += L"Odmietnuté súbory\r\n";
  for (const layout::Rejected& rejected : plan.rejected)
    text += L"  " + files[rejected.file].path.filename().wstring() + L" — " +
            rejected.detail + L"\r\n";
  if (plan.rejected.empty())
    text += L"  Nič nebolo odmietnuté, celá kolekcia sa zmestila.\r\n";
  return text;
}

bool TargetIsUsable(const fs::path& source, const fs::path& target,
                    std::wstring& error) {
  std::error_code sourceEc;
  std::error_code ec;
  const fs::path from = fs::weakly_canonical(source, sourceEc);
  const fs::path to = fs::weakly_canonical(target, ec);
  if (sourceEc || ec) {
    error = L"Cesty k priečinkom sa nepodarilo overiť.";
    return false;
  }
  // Not into the collection itself and not below it.  Everything here only
  // ever creates files, so a target inside the source would leave the
  // diskettes mixed in among the files they were made from -- and the next
  // split of the same collection would take them for part of it.
  if (to == from || to.wstring().rfind(from.wstring() + L"\\", 0) == 0) {
    error = L"Cieľový priečinok nesmie ležať v priečinku s kolekciou.";
    return false;
  }
  if (!fs::exists(to, ec)) return true;
  if (!fs::is_directory(to, ec)) {
    error = L"Cieľ " + to.wstring() + L" nie je priečinok.";
    return false;
  }
  // Empty and nothing else.  Nothing here overwrites, so anything already in
  // the folder would end up mixed into the result -- and a file with a name a
  // diskette also wants would stop the copy half way.
  fs::directory_iterator entries(to, ec);
  if (ec) {
    error = L"Cieľový priečinok " + to.wstring() + L" sa nedá prečítať.";
    return false;
  }
  if (entries != fs::directory_iterator()) {
    error = L"Cieľový priečinok " + to.wstring() +
            L" nie je prázdny. Rozdelenie zapisuje len do prázdneho "
            L"priečinka, aby neprepísalo nič, čo v ňom je.";
    return false;
  }
  return true;
}

Outcome Execute(const layout::Plan& plan,
                const std::vector<layout::SourceFile>& files,
                const layout::Options& options, const fs::path& source,
                const fs::path& target) {
  Outcome outcome;
  std::error_code ec;

  if (plan.diskettes.empty()) {
    outcome.error = L"Plán je prázdny, nie je čo rozdeliť.";
    return outcome;
  }
  if (!TargetIsUsable(source, target, outcome.error)) return outcome;

  const fs::path from = fs::weakly_canonical(source, ec);
  const fs::path to = fs::weakly_canonical(target, ec);
  if (!fs::exists(to, ec) && !fs::create_directories(to, ec)) {
    outcome.error = L"Cieľový priečinok " + to.wstring() + L" sa nedá vytvoriť.";
    return outcome;
  }

  for (const layout::Diskette& diskette : plan.diskettes) {
    const fs::path folder = to / diskette.label;
    if (!fs::create_directory(folder, ec)) {
      outcome.error = L"Priečinok diskety " + folder.wstring() +
                      L" sa nedá vytvoriť.";
      return outcome;
    }
    ++outcome.diskettes;
    std::set<std::string> used;
    for (const layout::Item& item : diskette.items) {
      // copy_options::none, so an existing file is an error rather than a
      // silent overwrite.  On an empty target this cannot fire from our own
      // doing; it can fire when something else is writing into that folder at
      // the same time, and then it has to stop.
      fs::copy_file(files[item.file].path, folder / item.name,
                    fs::copy_options::none, ec);
      if (ec) {
        outcome.error = L"Súbor " + files[item.file].path.wstring() +
                        L" sa nepodarilo skopírovať do " + folder.wstring() +
                        L".\r\n\r\nČo už bolo skopírované, zostáva na mieste.";
        return outcome;
      }
      used.insert(item.name);
      ++outcome.copied;
    }
    if (!options.catalogue_on_diskette) continue;
    // The catalogue gives way to the user's own file of that name rather than
    // the other way round: the plan reserved a block and an entry for it, not
    // the name.  A collection carrying its own OBSAH.TXT is unusual, but
    // overwriting it would be the sort of quiet loss this whole layer exists
    // to prevent.
    const std::string name = cpm::UniqueName(kDisketteContentsName, used);
    const std::vector<uint8_t> bytes =
        EncodeKamenicky(DisketteContents(diskette, files));
    std::ofstream stream(folder / name, std::ios::binary | std::ios::trunc);
    if (stream) {
      stream.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
      // The CP/M end-of-text mark.  VirtualDisk::IsTextType counts TXT among
      // the text types and cuts an exported file at the first 1Ah, so a
      // catalogue without one would grow the diskette's padding every time it
      // travelled back out.
      const char eof = 0x1a;
      stream.write(&eof, 1);
    }
    if (!stream) {
      outcome.error = L"Zoznam " + (folder / name).wstring() +
                      L" sa nepodarilo zapísať.";
      return outcome;
    }
  }

  if (!WriteTextFile(to / kContentsName,
                     Describe(plan, files, options, from, to), true,
                     outcome.error))
    return outcome;
  outcome.ok = true;
  return outcome;
}

bool WriteSpolu(const fs::path& root,
                const std::vector<std::vector<std::wstring>>& groups,
                std::wstring& error) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    error = L"Priečinok s kolekciou už nie je prístupný, SPOLU.txt sa "
            L"nezapísal.";
    return false;
  }
  // A header, because this file is read by a person a year later and the
  // format is not obvious from three lines of names.  ParseSpolu skips it: a
  // line starting with # is a comment.
  const std::wstring text =
      L"# Ktoré súbory patria k sebe a nesmú sa rozdeliť na dve diskety.\r\n"
      L"# Jedna skupina na riadok, mená oddelené čiarkami.\r\n"
      L"# Riadok platí v každom priečinku kolekcie zvlášť.\r\n" +
      layout::FormatSpolu(groups);
  return WriteTextFile(root / kSpoluName, text, true, error);
}

}  // namespace disk_split
