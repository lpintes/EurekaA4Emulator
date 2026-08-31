#ifndef EUREKA_DISK_STASH_H
#define EUREKA_DISK_STASH_H

// Where diskettes are when they are not in the drive.
//
// A diskette is a thing, not a recipe for making one.  Taking it out and
// putting it back in has to give the same diskette back, with whatever was
// written on it -- the machine's own bulk copy depends on exactly that: it
// fills the target, sends the user back for the source, and then asks for the
// target again to finish the file it started.  A target that came back blank
// makes the ROM say "soubor nelze najít" and cancel the job, having written
// nothing (measured 29. 8. 2026).
//
// Only diskettes with no home are held here.  One that has a home folder needs
// no copy: it is written back before it leaves the drive, and reading it again
// is both cheaper and truer -- a folder the user changed meanwhile comes back
// changed, which is what the same diskette would do.
//
// Slots are numbered the way the menu numbers them, 1..kSlots.  There is
// deliberately no shelf for a diskette belonging to no slot: a shelf holds one
// diskette, so the second one put on it would destroy the first -- the same
// silent loss in a smaller box.  An unsaved diskette with nothing pointing at
// it is a decision for the user, not a place for this class to invent; the
// window asks before anything pushes it out of the drive.

#include <array>
#include <memory>

#include "virtual_disk.h"

class DiskStash {
 public:
  // The quick-choice slots, numbered as the menu numbers them.
  static constexpr int kSlots = 9;

  // Puts a diskette away.  One with a home is declined and says so by
  // returning false: nothing is lost, because the folder holds it.
  bool Put(int slot, const VirtualDisk& disk);
  // Takes it back out, leaving the slot empty.  Null when the slot never had
  // one, which is how the caller knows to make a fresh diskette instead.
  //
  // A pointer and not a value: a VirtualDisk is 800 KiB, so handing one back
  // by value puts that much on the caller's stack, and this is called from a
  // worker thread.  Measured -- it overflowed (0xC00000FD).
  std::unique_ptr<VirtualDisk> Take(int slot);
  bool holds(int slot) const;
  // Moves the notch on a diskette that is on the shelf.  False when the slot
  // holds none, which is the caller's cue that there is nothing to lock: an
  // unsaved slot that has never been inserted has no diskette yet, and a
  // property cannot be set on a thing that does not exist.
  //
  // Here and not only on the way in, because the lock belongs to the diskette
  // and the diskette is here.  Locking it on the way back into the drive would
  // be a lock belonging to the drive again, which is the mistake VirtualDisk's
  // notch comment warns about.
  bool SetWriteProtected(int slot, bool protect);
  bool WriteProtected(int slot) const;
  // Whether any slot holds a diskette that still has files on it.  Asked at
  // exit, where those diskettes are about to stop existing.
  bool HoldsAnythingWritten() const;
  bool empty() const;

 private:
  static bool Valid(int slot) { return slot >= 1 && slot <= kSlots; }

  // On the heap, one at a time.  A VirtualDisk carries its whole 800 KiB
  // image, so nine of them inline would be seven megabytes -- and this object
  // lives on the worker's stack, where that is not a size, it is a crash.
  // Measured: as a plain array it overflowed before main() got anywhere
  // (0xC00000FD).  Index 0 is unused so that slot numbers read as they do
  // everywhere else.
  std::array<std::unique_ptr<VirtualDisk>, kSlots + 1> disks_;
};

#endif
