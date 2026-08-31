#include "disk_stash.h"

bool DiskStash::Put(int slot, const VirtualDisk& disk) {
  if (!Valid(slot)) return false;
  // An empty drive is not a diskette, and one with a home keeps itself.
  if (!disk.present() || disk.has_home()) return false;
  disks_[static_cast<std::size_t>(slot)] = std::make_unique<VirtualDisk>(disk);
  return true;
}

std::unique_ptr<VirtualDisk> DiskStash::Take(int slot) {
  if (!Valid(slot)) return nullptr;
  return std::move(disks_[static_cast<std::size_t>(slot)]);
}

bool DiskStash::holds(int slot) const {
  return Valid(slot) && disks_[static_cast<std::size_t>(slot)] != nullptr;
}

bool DiskStash::HoldsAnythingWritten() const {
  for (const auto& disk : disks_)
    if (disk && disk->StoredFiles() > 0) return true;
  return false;
}

bool DiskStash::empty() const {
  for (const auto& disk : disks_)
    if (disk) return false;
  return true;
}
