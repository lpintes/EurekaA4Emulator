#ifndef EUREKA_DISK_SPLIT_H
#define EUREKA_DISK_SPLIT_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "disk_layout.h"

// The two layers disk_layout deliberately does without: reading the collection
// off the disk, and carrying the plan out.
//
// disk_layout takes names and sizes and gives back a plan; it opens nothing.
// That is what lets it be tested without a diskette folder, and it is also
// what leaves this file with a job.  Everything that touches a file is here,
// and the dialog above it does nothing but ask and show.
//
// The rules about what must not happen to the user's data live here as code,
// not in the dialog, because the dialog is not the only thing that could ever
// call this: the target has to be an empty folder, nothing is overwritten, and
// the source is opened for writing in exactly one place -- WriteSpolu, and
// only when the user says so in as many words (HANDOFF 6.22).
namespace disk_split {

// The file in the root of the collection that says which files belong
// together.  One group per line, names separated by commas, # for a comment.
// It is read here and written back by WriteSpolu, so a unit the user corrects
// in the wizard survives the next split -- which is the whole reason it is
// first in the order of trust.
inline constexpr wchar_t kSpoluName[] = L"SPOLU.txt";
// The plan, written into the root of the target folder: outside the diskettes,
// so it costs no block on any of them.
inline constexpr wchar_t kContentsName[] = L"OBSAH.txt";
// The optional list on each diskette, in the machine's own encoding so that
// Eureka can read it herself.  An 8.3 name, because that is what the diskette
// carries.
inline constexpr char kDisketteContentsName[] = "OBSAH.TXT";

// What Scan found.  The files are in the shape disk_layout::Build wants, with
// `group` filled in from the subfolder each one came from.
struct Collection {
  std::vector<disk_layout::SourceFile> files;
  // Whatever SPOLU.txt held, already parsed.  Empty when there is no such
  // file, which is not an error -- most collections have none.
  std::vector<std::vector<std::wstring>> spolu;
  bool has_spolu = false;
  std::uint64_t bytes = 0;
};

// Reads the collection: every file under `root`, with its size and the
// subfolder it came from, plus SPOLU.txt if there is one.
//
// Recursive, unlike VirtualDisk::Mount, which ignores subfolders (6.19).  The
// difference is not an inconsistency: a diskette is one flat CP/M directory,
// but a *collection* is exactly the thing that is too big to be one, and its
// subfolders are what the "podla priecinkov" mode packs by.
bool Scan(const std::filesystem::path& root, Collection& out, std::wstring& error);

// The plan in words: which diskette gets what, which folders ended up split,
// which units were merged and by which rule, what was renamed and what was
// refused and why.
//
// One function for both the preview and OBSAH.txt on purpose.  The preview is
// what the user decides on and OBSAH.txt is what they will read in a month;
// two texts would be two chances to disagree about what happened.
std::wstring Describe(const disk_layout::Plan& plan,
                      const std::vector<disk_layout::SourceFile>& files,
                      const disk_layout::Options& options,
                      const std::filesystem::path& source,
                      const std::filesystem::path& target);

// Whether the plan may be written into `target`: an empty folder, or one that
// does not exist yet, and never inside `source`.
//
// Its own function because two callers need the same answer at two different
// moments -- Execute before it creates anything, and the wizard's first page
// so that a target it cannot use is refused before the plan is built rather
// than after the user has read it.  Two copies of this rule would be two
// answers, and the one the user heard would not be the one that decides.
bool TargetIsUsable(const std::filesystem::path& source,
                    const std::filesystem::path& target, std::wstring& error);

struct Outcome {
  bool ok = false;
  // Why it stopped, or empty.  A failure leaves behind whatever was already
  // copied: nothing is taken back, because half a collection on disk with a
  // message naming the file that failed is worth more than a clean slate.
  std::wstring error;
  std::size_t diskettes = 0;
  std::size_t copied = 0;
};

// Creates one folder per diskette under `target` and copies the files into it
// under the 8.3 names the plan gave them, then writes OBSAH.txt into the root
// of the target and, if asked for, OBSAH.TXT onto every diskette.
//
// `target` must be an empty folder or one that does not exist yet, and must
// not be inside `source`: this only ever creates files, so anything already
// there would be mixed into the result without a word.
Outcome Execute(const disk_layout::Plan& plan,
                const std::vector<disk_layout::SourceFile>& files,
                const disk_layout::Options& options,
                const std::filesystem::path& source,
                const std::filesystem::path& target);

// The one write into the source folder, and only on the user's word.
bool WriteSpolu(const std::filesystem::path& root,
                const std::vector<std::vector<std::wstring>>& groups,
                std::wstring& error);

}  // namespace disk_split

#endif
