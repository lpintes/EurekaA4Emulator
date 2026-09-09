#ifndef EUREKA_DISK_LAYOUT_H
#define EUREKA_DISK_LAYOUT_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cpm_disk.h"

// Splitting a collection too big for one diskette into diskettes.
//
// The input is a list of files with their sizes, the output is a *plan*:
// which file goes on which diskette and under what 8.3 name, plus what was
// refused and why.  Nothing here opens a file, touches Win32 or knows the
// machine exists -- reading the collection and copying it out are the two
// layers above.  That is what lets disk_test pin the three limits down
// (HANDOFF 6.12, 6.22) without a diskette folder and without the ROM.
namespace disk_layout {

// One file offered to the splitter.  `group` is the subfolder it came from,
// relative to the root of the collection and empty for the root itself; the
// caller fills it in, because this layer never looks at a disk.  It is what
// "podla priecinkov" packs by and what gives the diskette folder its name.
//
// A subfolder deeper down is its own group, so a group too big for one
// diskette is cut along its own subfolders before it is cut alphabetically.
struct SourceFile {
  std::filesystem::path path;
  std::wstring group;
  std::uint64_t size = 0;
};

enum class Mode {
  // Alphabetical, fill up and never skip.  It looks unambitious and it is the
  // strongest mode for a user who cannot see the screen: "diskette 3 is K to
  // P" is a fact worth remembering, and adding one file does not reshuffle
  // the rest.
  kSequential,
  // A subfolder is a group and groups are packed first-fit-decreasing, so a
  // folder stays together as long as it fits anywhere at all.
  kByFolder,
  // Fewest diskettes, first-fit-decreasing over the units themselves.  For
  // archiving rather than for working, because it scatters folders.
  kTight,
};

// Which rule merged the files of a unit.  Ordered by how much it is a guess,
// and a unit built by two rules reports the *weaker* one: the preview has to
// show what is worth overruling, and SPOLU.txt never is (HANDOFF 6.22).
enum class UnitSource {
  kAlone = 0,      // one file, no rule involved
  kSpolu = 1,      // SPOLU.txt said so -- the only source that is not a guess
  kSameStem = 2,   // TURBO.COM + TURBO.MSG + TURBO.OVR
  kCompanion = 3,  // WS.COM + WSMSGS.OVR, by companion extension
};

// What the packer moves: an indivisible unit, never a file.  TP.COM without
// TURBO.MSG is a broken program, so this is a first-class notion here and not
// a patch bolted on afterwards -- all three modes pack units.
//
// Held as indices into the input, so the wizard can show a unit, let the user
// correct it and write it back into SPOLU.txt.  A correction that does not
// survive the next split is a correction the user makes again every time.
struct Unit {
  std::vector<std::size_t> files;
  UnitSource source = UnitSource::kAlone;
  std::wstring group;
  unsigned blocks = 0;
  unsigned entries = 0;
};

struct Item {
  std::size_t file = 0;  // index into the input
  std::size_t unit = 0;  // index into Plan::units
  std::string name;      // the 8.3 name this file will carry on the diskette
  // A renamed file is one its program can no longer open, so it happens only
  // to a file that stands alone and the plan says so by name.
  bool renamed = false;
};

struct Diskette {
  // The folder the wizard will create: 01-HUDBA, named after the group that
  // takes up most of it.  The window title reads this, so it is worth having.
  std::wstring label;
  std::vector<Item> items;
  unsigned blocks = 0;
  unsigned entries = 0;
};

enum class Reason {
  kTooManyBlocks,       // more than 792 KiB once blocks are counted whole
  kTooManyEntries,      // more than 256 directory entries
  kNameClashInsideUnit, // two files of one unit want one 8.3 name
};

// Nothing is dropped quietly: every file the plan does not carry is here with
// a sentence saying why.  The wizard reads these out and writes them into
// OBSAH.txt, so a file missing from the diskettes can always be accounted for.
struct Rejected {
  std::size_t file = 0;
  Reason reason = Reason::kTooManyBlocks;
  std::wstring detail;
};

struct Options {
  Mode mode = Mode::kSequential;
  // TURBO.COM + TURBO.MSG.  Safe enough to be on by default.
  bool group_by_stem = true;
  // WS.COM + WSMSGS.OVR + WSOVLY1.OVR: the companion extensions of the one
  // .COM in a folder.  This one really is guessing, so it is off by default.
  bool group_by_companion = false;
  // Groups read out of SPOLU.txt: one group per line, names as the user wrote
  // them.  A line is a rule and is applied inside each folder separately, so
  // one line covers a program that appears in several of them.
  std::vector<std::vector<std::wstring>> spolu;
  // An OBSAH.TXT on each diskette so the machine can read the list itself.
  // It costs a block and an entry like any other file and has to be counted
  // before the split, not after -- afterwards there is no room left for it.
  bool catalogue_on_diskette = false;
};

struct Plan {
  std::vector<Unit> units;
  std::vector<Diskette> diskettes;
  std::vector<Rejected> rejected;
};

// The plan.  Deterministic: the same files with the same options give the
// same plan, and the order they arrive in does not matter, because the first
// thing this does is sort them by folder and name.
Plan Build(const std::vector<SourceFile>& files, const Options& options);

// SPOLU.txt: one group per line, names separated by commas, # for a comment.
// Read and written by one pair of functions so that what the wizard saves is
// read back as the same units -- the whole reason the file is first in the
// order of trust.
std::vector<std::vector<std::wstring>> ParseSpolu(const std::wstring& text);
std::wstring FormatSpolu(const std::vector<std::vector<std::wstring>>& groups);
// The units of a plan in the shape ParseSpolu produces, ready to be handed to
// FormatSpolu after the user has edited them.  Units of one file are left out:
// a line naming one file states nothing.
std::vector<std::vector<std::wstring>> UnitsAsGroups(const Plan& plan,
                                                     const std::vector<SourceFile>& files);

}  // namespace disk_layout

#endif
