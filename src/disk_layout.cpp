#include "disk_layout.h"

#include <algorithm>
#include <map>
#include <set>

namespace disk_layout {

namespace {

// Case and accents folded the way the diskette itself folds them, so that
// TURBO.COM and turbo.msg are one stem.  No length limit, though: cutting to
// eight characters first would make PRIBEHY-JAR and PRIBEHY-LETO look like
// one program and weld two unrelated files into an indivisible unit.
std::string Key(const std::wstring& text) { return cpm::NamePart(text, 255); }

// OBSAH.TXT on the diskette is one line per file -- at most twelve characters
// of 8.3 name, a space, the size in bytes and a CRLF -- plus a short heading.
// Both numbers are upper bounds: a reservation guessed low is a diskette that
// overflows on its last file, which is the failure this layer exists to stop.
constexpr unsigned kCatalogueHeaderBytes = 96;
constexpr unsigned kCatalogueBytesPerFile = 24;

std::uint64_t CatalogueBytes(std::size_t files) {
  return kCatalogueHeaderBytes + static_cast<std::uint64_t>(files) * kCatalogueBytesPerFile;
}

// Everything about one input file that the packer asks for more than once.
// Indexed exactly as the caller list is, so a unit can hold the indices the
// caller gave and still mean something to it afterwards.
struct Prepared {
  std::wstring group;
  std::wstring leaf;
  std::string stem_key;
  std::string type_key;
  std::string name;  // the 8.3 name it would carry if nothing collided
  unsigned blocks = 0;
  unsigned entries = 0;
};

// Files are merged into units by union-find rather than in one pass, because
// the three rules overlap: SPOLU.txt can name two files that the stem rule
// would have joined to a third anyway.
class Sets {
 public:
  explicit Sets(std::size_t count) : parent_(count), source_(count, UnitSource::kAlone) {
    for (std::size_t i = 0; i < count; ++i) parent_[i] = i;
  }

  std::size_t Root(std::size_t item) {
    while (parent_[item] != item) item = parent_[item] = parent_[parent_[item]];
    return item;
  }

  void Join(std::size_t left, std::size_t right, UnitSource rule) {
    const std::size_t a = Root(left);
    const std::size_t b = Root(right);
    // The weaker rule wins the label: a unit that SPOLU.txt started and a
    // guess then extended is worth showing as the guess it partly is.
    const UnitSource worst = std::max({source_[a], source_[b], rule});
    const std::size_t keep = std::min(a, b);
    const std::size_t drop = std::max(a, b);
    parent_[drop] = keep;
    source_[keep] = worst;
  }

  UnitSource Source(std::size_t item) { return source_[Root(item)]; }

 private:
  std::vector<std::size_t> parent_;
  std::vector<UnitSource> source_;
};

// A diskette while it is being filled: the plan for it, plus the two things
// the packer needs and the finished plan does not carry.
struct Open {
  Diskette disk;
  std::set<std::string> used;
  std::map<std::wstring, unsigned> weight;  // blocks per group, for the label
};

bool Fits(const Open& open, const Unit& unit, const Options& options) {
  std::uint64_t blocks = open.disk.blocks + unit.blocks;
  std::uint64_t entries = open.disk.entries + unit.entries;
  if (options.catalogue_on_diskette) {
    const std::uint64_t bytes = CatalogueBytes(open.disk.items.size() + unit.files.size());
    blocks += cpm::BlocksFor(bytes);
    entries += cpm::EntriesFor(bytes);
  }
  return blocks <= cpm::kAvailableBlocks && entries <= cpm::kDirectoryEntries;
}

bool Clashes(const Open& open, const Unit& unit, const std::vector<Prepared>& prepared) {
  for (std::size_t file : unit.files)
    if (open.used.contains(prepared[file].name)) return true;
  return false;
}

void Add(Open& open, std::size_t unitIndex, const Unit& unit,
         const std::vector<Prepared>& prepared) {
  for (std::size_t file : unit.files) {
    Item item;
    item.file = file;
    item.unit = unitIndex;
    item.name = prepared[file].name;
    // Only ever reached for a unit of one file: Place refuses a diskette on
    // which a unit of several would have to be renamed, because a program
    // whose companion file was renamed fails in a way nobody can read.
    if (open.used.contains(item.name)) {
      item.name = cpm::UniqueName(item.name, open.used);
      item.renamed = true;
    }
    open.used.insert(item.name);
    open.disk.blocks += prepared[file].blocks;
    open.disk.entries += prepared[file].entries;
    open.weight[prepared[file].group] += prepared[file].blocks;
    open.disk.items.push_back(std::move(item));
  }
}

// `first` is where the search starts, and it is the whole difference between
// the modes: the last diskette for the sequential one, which therefore never
// goes back; diskette zero for the tight one; the diskette a folder was
// measured onto for the by-folder one.
void Place(std::vector<Open>& disks, std::size_t first, std::size_t unitIndex,
           const Unit& unit, const std::vector<Prepared>& prepared,
           const Options& options) {
  for (std::size_t i = first; i < disks.size(); ++i) {
    if (Fits(disks[i], unit, options) && !Clashes(disks[i], unit, prepared)) {
      Add(disks[i], unitIndex, unit, prepared);
      return;
    }
  }
  // A file that stands alone may be renamed instead, and only now: renaming
  // is the last resort, so it is worth avoiding a diskette of its own but not
  // worth trying before a diskette where the name is free.
  if (unit.files.size() == 1) {
    for (std::size_t i = first; i < disks.size(); ++i) {
      if (Fits(disks[i], unit, options)) {
        Add(disks[i], unitIndex, unit, prepared);
        return;
      }
    }
  }
  disks.emplace_back();
  Add(disks.back(), unitIndex, unit, prepared);
}

std::wstring UnitLabel(const Unit& unit, const std::vector<Prepared>& prepared) {
  if (unit.files.size() == 1) return L"Súbor " + prepared[unit.files[0]].leaf;
  std::wstring text = L"Skupina ";
  const std::size_t named = std::min<std::size_t>(3, unit.files.size());
  for (std::size_t i = 0; i < named; ++i) {
    if (i) text += L", ";
    text += prepared[unit.files[i]].leaf;
  }
  if (unit.files.size() > named) text += L" a ďalšie";
  return text;
}

// Why it is refused instead of being split.  Two sentences, because the
// reason for a lone file and the reason for a group are different facts.
std::wstring Undivided(const Unit& unit) {
  return unit.files.size() == 1
             ? L" Nezmestí sa preto na žiadnu disketu."
             : L" Rozdeliť ju nemožno, tie súbory patria k sebe.";
}

std::wstring TrailingName(const std::wstring& group) {
  const std::size_t cut = group.find_last_of(L"/\\");
  return cut == std::wstring::npos ? group : group.substr(cut + 1);
}

// An 8.3 name is ASCII by construction, so widening it cannot lose anything.
std::wstring Widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

}  // namespace

Plan Build(const std::vector<SourceFile>& files, const Options& options) {
  Plan plan;

  std::vector<Prepared> prepared(files.size());
  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::wstring extension = files[i].path.extension().wstring();
    prepared[i].group = files[i].group;
    prepared[i].leaf = files[i].path.filename().wstring();
    prepared[i].stem_key = Key(files[i].path.stem().wstring());
    prepared[i].type_key = extension.empty() ? std::string() : Key(extension.substr(1));
    prepared[i].name = cpm::MakeName(files[i].path);
    prepared[i].blocks = cpm::BlocksFor(files[i].size);
    prepared[i].entries = cpm::EntriesFor(files[i].size);
  }

  // Sorted once, up front, and everything below works in this order.  That is
  // what makes the plan independent of the order the caller happened to read
  // the folder in -- and it is the order the diskette lists its files in, so
  // "diskette 3 is K to P" holds for the machine as well as for the plan.
  std::vector<std::size_t> order(files.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
    if (prepared[left].group != prepared[right].group)
      return prepared[left].group < prepared[right].group;
    if (prepared[left].leaf != prepared[right].leaf)
      return prepared[left].leaf < prepared[right].leaf;
    return files[left].path.wstring() < files[right].path.wstring();
  });

  // The rules join files only inside one folder.  TURBO.COM in one folder and
  // TURBO.MSG in another are two people's copies, not one program, and a rule
  // reaching across folders would drag them onto one diskette together.
  std::vector<std::vector<std::size_t>> byGroup;
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (i == 0 || prepared[order[i]].group != prepared[order[i - 1]].group)
      byGroup.emplace_back();
    byGroup.back().push_back(order[i]);
  }

  Sets sets(files.size());
  for (const std::vector<std::size_t>& group : byGroup) {
    // SPOLU.txt first, because it is the only source that is not guessing.
    // A line is a rule, not a list of particular files: it is applied inside
    // each folder separately, so one line covers a program sitting in several.
    for (const std::vector<std::wstring>& line : options.spolu) {
      // A name in the file is split into stem and type the same way a real
      // file is, because the fold turns a dot into an underscore: comparing
      // whole names would make WS.COM and WS_COM the same string and neither
      // of them the file on disk.
      std::vector<std::string> wanted;
      for (const std::wstring& name : line) {
        const std::filesystem::path written(name);
        const std::wstring type = written.extension().wstring();
        std::string key = Key(written.stem().wstring());
        if (type.size() > 1) key += "." + Key(type.substr(1));
        wanted.push_back(std::move(key));
      }
      std::size_t anchor = files.size();
      for (std::size_t file : group) {
        const std::string full = prepared[file].type_key.empty()
                                     ? prepared[file].stem_key
                                     : prepared[file].stem_key + "." + prepared[file].type_key;
        if (std::find(wanted.begin(), wanted.end(), full) == wanted.end()) continue;
        if (anchor == files.size()) anchor = file;
        else sets.Join(anchor, file, UnitSource::kSpolu);
      }
    }
    if (options.group_by_stem) {
      std::map<std::string, std::size_t> anchor;
      for (std::size_t file : group) {
        auto found = anchor.find(prepared[file].stem_key);
        if (found == anchor.end()) anchor.emplace(prepared[file].stem_key, file);
        else sets.Join(found->second, file, UnitSource::kSameStem);
      }
    }
    if (options.group_by_companion) {
      // Only when the folder holds exactly one program.  With two, an .OVR
      // belongs to one of them and this rule has no way of saying which.
      static const std::set<std::string> kCompanions = {"OVR", "OVL", "MSG",
                                                        "HLP", "CFG", "INS"};
      std::size_t program = files.size();
      unsigned programs = 0;
      for (std::size_t file : group) {
        if (prepared[file].type_key != "COM") continue;
        program = file;
        ++programs;
      }
      if (programs == 1) {
        for (std::size_t file : group)
          if (kCompanions.contains(prepared[file].type_key))
            sets.Join(program, file, UnitSource::kCompanion);
      }
    }
  }

  // Units in the order of their first file, so the sequential mode gets them
  // alphabetically without sorting them a second time.
  std::map<std::size_t, std::size_t> unitOfRoot;
  for (std::size_t file : order) {
    const std::size_t root = sets.Root(file);
    auto found = unitOfRoot.find(root);
    if (found == unitOfRoot.end()) {
      found = unitOfRoot.emplace(root, plan.units.size()).first;
      Unit unit;
      unit.source = sets.Source(file);
      unit.group = prepared[file].group;
      plan.units.push_back(std::move(unit));
    }
    Unit& unit = plan.units[found->second];
    unit.files.push_back(file);
    unit.blocks += prepared[file].blocks;
    unit.entries += prepared[file].entries;
  }

  // What can go nowhere is refused here, before anything is packed, so that
  // the packer never has to invent a diskette it knows will not hold.
  std::vector<std::size_t> packable;
  for (std::size_t index = 0; index < plan.units.size(); ++index) {
    const Unit& unit = plan.units[index];
    const Open empty;
    Reason reason = Reason::kTooManyBlocks;
    std::wstring detail;
    if (!Fits(empty, unit, options)) {
      const unsigned reserve = options.catalogue_on_diskette
                                   ? cpm::BlocksFor(CatalogueBytes(unit.files.size()))
                                   : 0;
      const bool blocks = unit.blocks + reserve > cpm::kAvailableBlocks;
      // Whichever count it is, it is past its limit by definition here --
      // over 396 or over 256 -- so the Slovak plural is the same one every
      // time and there is nothing for a counting helper to decide.
      reason = blocks ? Reason::kTooManyBlocks : Reason::kTooManyEntries;
      detail = UnitLabel(unit, prepared) +
               (blocks ? L" zaberie " + std::to_wstring(unit.blocks) +
                             L" blokov po 2 KiB a disketa má " +
                             std::to_wstring(cpm::kAvailableBlocks) + L"."
                       : L" potrebuje " + std::to_wstring(unit.entries) +
                             L" položiek adresára a disketa má " +
                             std::to_wstring(cpm::kDirectoryEntries) + L".") +
               Undivided(unit);
    } else {
      // Two files of one unit wanting one 8.3 name is the single collision no
      // diskette can settle: moving the unit does not help, and renaming
      // inside it is exactly what must never happen.
      std::map<std::string, std::size_t> seen;
      for (std::size_t file : unit.files) {
        auto found = seen.find(prepared[file].name);
        if (found == seen.end()) {
          seen.emplace(prepared[file].name, file);
          continue;
        }
        reason = Reason::kNameClashInsideUnit;
        detail = L"Súbory " + prepared[found->second].leaf + L" a " + prepared[file].leaf +
                 L" by na diskete mali obidva meno " + Widen(prepared[file].name) +
                 L" a patria k sebe, takže premenovať sa nesmie ani jeden.";
        break;
      }
    }
    if (detail.empty()) {
      packable.push_back(index);
      continue;
    }
    for (std::size_t file : unit.files) plan.rejected.push_back({file, reason, detail});
  }

  std::vector<Open> disks;
  if (options.mode == Mode::kTight) {
    // First fit decreasing: the big units first, each into the first diskette
    // that still has room for it.
    std::stable_sort(packable.begin(), packable.end(), [&](std::size_t left, std::size_t right) {
      return plan.units[left].blocks > plan.units[right].blocks;
    });
    for (std::size_t index : packable)
      Place(disks, 0, index, plan.units[index], prepared, options);
  } else if (options.mode == Mode::kByFolder) {
    // A folder is the thing being packed here, so it is measured whole and
    // put where it fits whole; one that fits nowhere is cut, and then along
    // its own subfolders first, because each of those is a group of its own.
    std::map<std::wstring, std::vector<std::size_t>> groups;
    for (std::size_t index : packable) groups[plan.units[index].group].push_back(index);
    std::vector<const std::pair<const std::wstring, std::vector<std::size_t>>*> byWeight;
    for (const auto& group : groups) byWeight.push_back(&group);
    std::sort(byWeight.begin(), byWeight.end(), [&](const auto* left, const auto* right) {
      unsigned leftBlocks = 0;
      unsigned rightBlocks = 0;
      for (std::size_t index : left->second) leftBlocks += plan.units[index].blocks;
      for (std::size_t index : right->second) rightBlocks += plan.units[index].blocks;
      if (leftBlocks != rightBlocks) return leftBlocks > rightBlocks;
      return left->first < right->first;
    });
    for (const auto* group : byWeight) {
      Unit whole;
      for (std::size_t index : group->second) {
        const Unit& unit = plan.units[index];
        whole.files.insert(whole.files.end(), unit.files.begin(), unit.files.end());
        whole.blocks += unit.blocks;
        whole.entries += unit.entries;
      }
      std::size_t target = disks.size();
      for (std::size_t i = 0; i < disks.size(); ++i) {
        if (Fits(disks[i], whole, options)) {
          target = i;
          break;
        }
      }
      for (std::size_t index : group->second)
        Place(disks, target, index, plan.units[index], prepared, options);
    }
  } else {
    for (std::size_t index : packable) {
      const std::size_t last = disks.empty() ? 0 : disks.size() - 1;
      Place(disks, last, index, plan.units[index], prepared, options);
    }
  }

  for (std::size_t i = 0; i < disks.size(); ++i) {
    // Named after the group that takes up most of it: the folder name is what
    // the title bar reads out once that diskette is in the drive.
    const std::wstring number = (i < 9 ? L"0" : L"") + std::to_wstring(i + 1);
    std::wstring dominant;
    unsigned best = 0;
    for (const auto& group : disks[i].weight) {
      if (group.second > best) {
        best = group.second;
        dominant = group.first;
      }
    }
    disks[i].disk.label =
        dominant.empty() ? number
                         : number + L"-" + Widen(cpm::NamePart(TrailingName(dominant), 8));
    plan.diskettes.push_back(std::move(disks[i].disk));
  }
  return plan;
}

std::vector<std::vector<std::wstring>> ParseSpolu(const std::wstring& text) {
  std::vector<std::vector<std::wstring>> groups;
  std::wstring line;
  std::size_t position = 0;
  while (position <= text.size()) {
    if (position < text.size() && text[position] != L'\n') {
      line += text[position++];
      continue;
    }
    while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
    const std::size_t start = line.find_first_not_of(L" \t");
    if (start != std::wstring::npos && line[start] != L'#') {
      std::vector<std::wstring> names;
      std::wstring name;
      for (std::size_t i = start; i <= line.size(); ++i) {
        if (i < line.size() && line[i] != L',') {
          name += line[i];
          continue;
        }
        const std::size_t from = name.find_first_not_of(L" \t");
        if (from != std::wstring::npos) {
          const std::size_t to = name.find_last_not_of(L" \t");
          names.push_back(name.substr(from, to - from + 1));
        }
        name.clear();
      }
      if (!names.empty()) groups.push_back(std::move(names));
    }
    line.clear();
    ++position;
  }
  return groups;
}

std::wstring FormatSpolu(const std::vector<std::vector<std::wstring>>& groups) {
  // CRLF: this is a file the user opens in Notepad, not one the diskette
  // carries.  Anything written onto a diskette goes through text_codec.
  std::wstring text;
  for (const std::vector<std::wstring>& group : groups) {
    for (std::size_t i = 0; i < group.size(); ++i) {
      if (i) text += L", ";
      text += group[i];
    }
    text += L"\r\n";
  }
  return text;
}

std::vector<std::vector<std::wstring>> UnitsAsGroups(const Plan& plan,
                                                     const std::vector<SourceFile>& files) {
  std::vector<std::vector<std::wstring>> groups;
  for (const Unit& unit : plan.units) {
    if (unit.files.size() < 2) continue;
    std::vector<std::wstring> names;
    for (std::size_t file : unit.files) names.push_back(files[file].path.filename().wstring());
    // One line is one rule and holds in every folder, so the same line
    // arriving from two folders is still one line.
    if (std::find(groups.begin(), groups.end(), names) == groups.end())
      groups.push_back(std::move(names));
  }
  return groups;
}

}  // namespace disk_layout
