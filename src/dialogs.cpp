#include "dialogs.h"

#include <filesystem>

#include "res/resource.h"

bool SettingsDialog::OnInit() {
  SetChecked(IDC_MODE_BRAILLE, mode_ == InputMode::kBraille);
  SetChecked(IDC_MODE_PC, mode_ == InputMode::kPc);
  SetChecked(IDC_DIAGNOSTICS, diagnostics_);
  // false: let the dialog manager focus the first tab stop, which is the radio
  // group.  It then announces the whole group, not just one button.
  return false;
}

bool SettingsDialog::OnOk() {
  mode_ = IsChecked(IDC_MODE_BRAILLE) ? InputMode::kBraille : InputMode::kPc;
  diagnostics_ = IsChecked(IDC_DIAGNOSTICS);
  return true;
}

namespace {

// Whether this slot's lock outlives the emulator.  It is written into
// nastavenia.txt against the folder's path, so a diskette with no path cannot
// have one written down -- Settings::SetDiskLocked drops it.
//
// This used to be called SlotCanLock and then SlotLockIsRemembered, and its
// answer was used for whether the box could be *ticked at all*.  It cannot be:
// the notch is a member of VirtualDisk and works on every diskette, saved or
// not.  Not being able to remember something is not the same as not being able
// to do it (6.26) -- whether there is a diskette to do it to is
// SlotHasDiskette, which is a different question again.
bool SlotLockPersists(const std::wstring& slot) {
  return !slot.empty() && !SlotIsUnsaved(slot);
}

// "3: Slovník — zamknutá — C:\Diskety\Slovnik", or "3: (prázdny)".  The number
// leads because it is the shortcut: a screen reader reading the line has
// already said which key puts that diskette in.  The path is appended only
// when there is one -- an unsaved diskette has no path to show.
std::wstring SlotLine(int number, const std::wstring& slot, bool locked) {
  std::wstring line =
      std::to_wstring(number) + L": " + SlotDisplayName(slot);
  // Deliberately nothing here about a slot whose diskette does not exist yet.
  // It said "(vznikne pri vložení)" for a few hours, to explain why the lock
  // below is greyed, and it was wrong twice over: a diskette comes into being
  // when it is made, not when it is put in -- that the worker defers making it
  // to the first insert is our bookkeeping, not something the user is doing --
  // and every word in this line is read aloud on every arrow key.  The greying
  // stands on its own; the reason belongs in README, not in nine list rows.
  // Reported by the owner 31. 8. 2026, and it is the 6.25 mistake again:
  // mechanism leaking into speech.
  //
  // Said in the line itself and not only in the check box below it: the list
  // is what a screen reader reads when arrowing through the slots, and a lock
  // that showed up only after moving the focus elsewhere would be a property
  // of the slot that cannot be found by reading it.
  //
  // "dočasne" for a diskette that has no folder to remember it by: the lock is
  // as real as any other and lasts exactly as long as the diskette does, which
  // is until the emulator closes.  Saying only "zamknutá" there would promise
  // it back at the next start.
  if (locked) line += SlotLockPersists(slot) ? L" — zamknutá"
                                             : L" — zamknutá dočasne";
  if (!slot.empty() && !SlotIsUnsaved(slot)) line += L" — " + slot;
  return line;
}

}  // namespace

bool NewDiskDialog::OnInit() {
  SetChecked(IDC_NEW_EMPTYFOLDER, true);
  const HWND slots = Item(IDC_NEW_SLOT);
  SendMessageW(slots, CB_ADDSTRING, 0,
               reinterpret_cast<LPARAM>(L"nenastavený"));
  // Every slot says what is in it now, so choosing one that is taken is a
  // decision and not a surprise.  The overwrite is confirmed in OnOk anyway,
  // but a picker that reads "3 — Slovník" has already said it.
  for (int number = 1; number <= Settings::kSlots; ++number) {
    const std::wstring line =
        std::to_wstring(number) + L" — " +
        SlotDisplayName(slots_[static_cast<std::size_t>(number - 1)]);
    SendMessageW(slots, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(line.c_str()));
  }
  SendMessageW(slots, CB_SETCURSEL, 0, 0);
  RefreshEnabled();
  // false: let the dialog manager focus the first tab stop, which is the radio
  // group.  It then announces the whole group, not just one button.
  return false;
}

NewDiskDialog::Kind NewDiskDialog::SelectedKind() const {
  if (IsChecked(IDC_NEW_UNSAVED)) return Kind::kUnsaved;
  if (IsChecked(IDC_NEW_UNFORMATTED)) return Kind::kUnformatted;
  return Kind::kNewFolder;
}

void NewDiskDialog::RefreshEnabled() {
  const Kind kind = SelectedKind();
  const bool needsFolder = kind == Kind::kNewFolder;
  SetEnabled(IDC_NEW_PATH, needsFolder);
  SetEnabled(IDC_NEW_BROWSE, needsFolder);
  // An unformatted diskette cannot go in a slot: unformatted lasts until the
  // first Shift+F8, so the slot would keep offering a state this diskette has
  // long left behind.  Greying it says that where a screen reader reads it.
  SetEnabled(IDC_NEW_SLOT, kind != Kind::kUnformatted);
  if (kind == Kind::kUnformatted)
    SendMessageW(Item(IDC_NEW_SLOT), CB_SETCURSEL, 0, 0);
}

bool NewDiskDialog::OnCommand(int id, int notification) {
  if (id == IDC_NEW_EMPTYFOLDER || id == IDC_NEW_UNSAVED ||
      id == IDC_NEW_UNFORMATTED) {
    RefreshEnabled();
    return true;
  }
  if (id == IDC_NEW_BROWSE) {
    // The naming picker, not the choosing one: a diskette being made does not
    // require the folder to be there yet, and having to go and create it first
    // would turn one act into two.  Choosing a folder that exists is Ctrl+I.
    const std::wstring folder = win::PickFolderToCreate(
        hwnd_, L"Kde sa má nová disketa vytvoriť", L"Disketa");
    if (!folder.empty()) SetText(IDC_NEW_PATH, folder);
    return true;
  }
  return false;
}

bool NewDiskDialog::OnOk() {
  kind_ = SelectedKind();
  folder_.clear();
  const LRESULT chosen = SendMessageW(Item(IDC_NEW_SLOT), CB_GETCURSEL, 0, 0);
  slot_ = chosen == CB_ERR ? 0 : static_cast<int>(chosen);
  if (slot_ > 0 && !slots_[static_cast<std::size_t>(slot_ - 1)].empty()) {
    // Overwriting a slot the user set up earlier is worth one question.  It
    // is the only thing in this dialog that destroys something.
    const std::wstring question =
        L"V slote " + std::to_wstring(slot_) + L" už je:\r\n\r\n" +
        SlotDisplayName(slots_[static_cast<std::size_t>(slot_ - 1)]) +
        L"\r\n\r\nMá ho nová disketa nahradiť?";
    if (MessageBoxW(hwnd_, question.c_str(), L"Nová disketa",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
      SetFocus(Item(IDC_NEW_SLOT));
      return false;
    }
  }
  if (kind_ != Kind::kNewFolder) return true;

  folder_ = GetText(IDC_NEW_PATH);
  if (folder_.empty()) {
    // Said here rather than let the mount fail later: an empty field is the
    // user not having finished, not an error about a diskette.
    MessageBoxW(hwnd_, L"Zadajte priečinok alebo ho vyberte tlačidlom "
                       L"Prehľadávať.",
                L"Nová disketa", MB_OK | MB_ICONINFORMATION);
    SetFocus(Item(IDC_NEW_PATH));
    return false;
  }
  std::error_code ec;
  const std::filesystem::path path(folder_);
  // A new diskette must not land on top of something that is already there.
  // Creating it into an existing folder would quietly adopt whatever files
  // it holds, and "new and empty" would be neither.
  if (std::filesystem::exists(path, ec)) {
    MessageBoxW(hwnd_,
                (L"Toto už existuje:\r\n\r\n" + folder_ +
                 L"\r\n\r\nNová disketa musí byť nový priečinok. Zvoľte iné "
                 L"meno, alebo tento priečinok vložte ako hotovú disketu "
                 L"cez Disketa → Vložiť disketu z priečinka (F11, Ctrl+I).")
                    .c_str(),
                L"Nová disketa", MB_OK | MB_ICONWARNING);
    SetFocus(Item(IDC_NEW_PATH));
    return false;
  }
  if (!std::filesystem::is_directory(path.parent_path(), ec)) {
    MessageBoxW(hwnd_,
                (L"Nadradený priečinok neexistuje:\r\n\r\n" +
                 path.parent_path().wstring())
                    .c_str(),
                L"Nová disketa", MB_OK | MB_ICONWARNING);
    SetFocus(Item(IDC_NEW_PATH));
    return false;
  }
  return true;
}

bool SlotsDialog::OnInit() {
  FillList(0);
  return false;
}

void SlotsDialog::FillList(int select) {
  const HWND list = Item(IDC_SLOT_LIST);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (int index = 0; index < Settings::kSlots; ++index) {
    const auto slot = static_cast<std::size_t>(index);
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(
                     SlotLine(index + 1, slots_[slot], locks_[slot]).c_str()));
  }
  SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
  // Nothing to put in a slot when the drive is empty,
  // and a button that answers with silence is worse than one that is greyed.
  SetEnabled(IDC_SLOT_CURRENT, !currentDisk_.empty());
  RefreshLock(select);
}

void SlotsDialog::RefreshLock(int index) {
  if (index < 0 || index >= Settings::kSlots) return;
  const auto slot = static_cast<std::size_t>(index);
  SetChecked(IDC_SLOT_LOCK, locks_[slot]);
  // Whether there is a diskette to lock, and nothing else.  It used to ask
  // whether the lock could be written down, so an unsaved slot came up greyed
  // and the dialog said "this diskette cannot be locked" about a diskette that
  // locks perfectly well -- see SlotLockPersists (6.26).
  SetEnabled(IDC_SLOT_LOCK, present_[slot]);
}

int SlotsDialog::Selected() const {
  const LRESULT selected =
      SendMessageW(Item(IDC_SLOT_LIST), LB_GETCURSEL, 0, 0);
  return selected == LB_ERR ? -1 : static_cast<int>(selected);
}

void SlotsDialog::SetSlotAndRefresh(int index, std::wstring path) {
  const auto slot = static_cast<std::size_t>(index);
  // Pointing this slot anywhere new takes back the promise that the diskette
  // in the drive would go into it.  Without this the settings and the stash
  // disagree: the settings say the slot is a folder while the worker parks the
  // memory diskette under that number -- and kInsertSlot takes from the stash
  // *before* it looks at the path, so Ctrl+3 would hand back the memory
  // diskette while the menu named the folder.  Silent and wrong, not loud.
  // "Sem vloženú disketu" sets it again right after calling this.
  if (assignedCurrent_ == index + 1) assignedCurrent_ = 0;
  slots_[slot] = std::move(path);
  // Any slot that names something has a diskette: a folder is one, and a slot
  // marked unsaved gets one made for it when OK is pressed.  Only an emptied
  // slot has none.  This used to say "a folder, and nothing else", from when
  // the marker was a promise rather than a diskette.
  present_[slot] = !slots_[slot].empty();
  // Pointed somewhere else, the box has to follow the new diskette rather than
  // stay ticked from the old one -- otherwise OK would lock a diskette nobody
  // asked about.  For a folder the lock lives in the settings, so one that is
  // already locked arrives ticked; a slot with no diskette has no lock to show.
  locks_[slot] = present_[slot] && settings_->disk_locked(slots_[slot]);
  FillList(index);
  SetFocus(Item(IDC_SLOT_LIST));
}

bool SlotsDialog::OnCommand(int id, int notification) {
  const int index = Selected();
  if (index < 0) return false;
  switch (id) {
    case IDC_SLOT_ASSIGN: {
      // Worded like the menu's own Vložiť disketu z priečinka, because it is
      // the same act asked in a different place: pick the folder a diskette
      // already lives in.
      const std::wstring folder = win::PickFolder(
          hwnd_, L"Vyberte priečinok s disketou pre tento slot");
      if (!folder.empty()) SetSlotAndRefresh(index, folder);
      return true;
    }
    case IDC_SLOT_CURRENT:
      if (!currentDisk_.empty()) {
        SetSlotAndRefresh(index, currentDisk_);
        assignedCurrent_ = index + 1;
        // This diskette exists -- it is in the drive -- so its lock can be set
        // here whether or not it has a folder, and it arrives showing the
        // notch it actually has rather than what the settings could recall.
        const auto slot = static_cast<std::size_t>(index);
        present_[slot] = true;
        locks_[slot] = currentLocked_;
        FillList(index);
        SetFocus(Item(IDC_SLOT_LIST));
      }
      return true;
    case IDC_SLOT_NEWUNSAVED:
      // The marker, not a diskette: nothing is made here and nothing could be
      // -- an unsaved diskette is the worker's to create, and it does so on
      // the first insert of a slot that has nothing put away yet.  Until then
      // this slot is a promise of an empty one, which is what the list line
      // "Disketa v pamäti" says.
      //
      // The only other road to this value was "Sem vloženú disketu", so a
      // memory slot could not be set up unless one was already in the drive.
      SetSlotAndRefresh(index, kSlotUnsaved);
      return true;
    case IDC_SLOT_CLEAR:
      SetSlotAndRefresh(index, L"");
      return true;
    case IDC_SLOT_LOCK: {
      const bool locked = IsChecked(IDC_SLOT_LOCK);
      const auto chosen = static_cast<std::size_t>(index);
      locks_[chosen] = locked;
      // Every slot holding this same folder, not just the selected one: the
      // lock belongs to the diskette, so two slots pointing at it are one
      // diskette with one lock.  Ticking one and leaving the other unticked
      // would be a dialog disagreeing with itself, and the settings would
      // then take whichever slot happened to be written last.
      //
      // Only for slots that name a folder.  Two unsaved slots both read
      // "*pamat" and SameDisk would call them one diskette, but they are two
      // different diskettes on two different shelves -- the marker is not a
      // name, it is the absence of one.
      if (SlotLockPersists(slots_[chosen]))
        for (std::size_t other = 0; other < locks_.size(); ++other)
          if (other != chosen && SlotLockPersists(slots_[other]) &&
              Settings::SameDisk(slots_[other], slots_[chosen]))
            locks_[other] = locked;
      // The list line carries the lock too, so it has to be rebuilt -- but the
      // focus stays on the box the user has just ticked.
      FillList(index);
      return true;
    }
    case IDC_SLOT_LIST:
      if (notification == LBN_SELCHANGE) RefreshLock(index);
      return false;
    default:
      return false;
  }
}

bool AboutDialog::OnInit() {
  SetText(IDC_ABOUT_TEXT, body_);
  // The edit control starts with everything selected, which a screen reader
  // reads out as a selection rather than as text.  Put the caret at the top
  // instead and leave the focus where the dialog manager wants it.
  SendMessageW(Item(IDC_ABOUT_TEXT), EM_SETSEL, 0, 0);
  return false;
}
