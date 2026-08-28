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

// "3: Slovník — C:\Diskety\Slovnik", or "3: (prázdny)".  The number leads
// because it is the shortcut: a screen reader reading the line has already
// said which key puts that diskette in.  The path is appended only when there
// is one -- a slot holding a diskette in memory has no path to show.
std::wstring SlotLine(int number, const std::wstring& slot) {
  std::wstring line =
      std::to_wstring(number) + L": " + SlotDisplayName(slot);
  if (!slot.empty() && !SlotIsRam(slot) && !SlotIsUnformattedRam(slot))
    line += L" — " + slot;
  return line;
}

}  // namespace

bool NewDiskDialog::OnInit() {
  SetChecked(IDC_NEW_FOLDER, true);
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
  if (IsChecked(IDC_NEW_EMPTYFOLDER)) return Kind::kEmptyFolder;
  if (IsChecked(IDC_NEW_RAM)) return Kind::kRam;
  if (IsChecked(IDC_NEW_UNFORMATTED)) return Kind::kUnformattedRam;
  return Kind::kFolder;
}

void NewDiskDialog::RefreshEnabled() {
  const Kind kind = SelectedKind();
  const bool needsFolder =
      kind == Kind::kFolder || kind == Kind::kEmptyFolder;
  SetEnabled(IDC_NEW_PATH, needsFolder);
  SetEnabled(IDC_NEW_BROWSE, needsFolder);
}

bool NewDiskDialog::OnCommand(int id, int notification) {
  if (id == IDC_NEW_FOLDER || id == IDC_NEW_EMPTYFOLDER ||
      id == IDC_NEW_RAM || id == IDC_NEW_UNFORMATTED) {
    RefreshEnabled();
    return true;
  }
  if (id == IDC_NEW_BROWSE) {
    const Kind kind = SelectedKind();
    const std::wstring folder = win::PickFolder(
        hwnd_, kind == Kind::kEmptyFolder
                   ? L"Vyberte priečinok, v ktorom sa nová disketa vytvorí"
                   : L"Vyberte priečinok, ktorý bude disketou");
    if (folder.empty()) return true;
    // For a new diskette the picker chooses the parent, so a name is
    // suggested and left editable: the field then holds the exact path that
    // will be created, and nothing is invented behind the user's back.
    SetText(IDC_NEW_PATH, kind == Kind::kEmptyFolder
                              ? folder + L"\\Disketa"
                              : folder);
    if (kind == Kind::kEmptyFolder) SetFocus(Item(IDC_NEW_PATH));
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
  if (kind_ != Kind::kFolder && kind_ != Kind::kEmptyFolder) return true;

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
  if (kind_ == Kind::kFolder) {
    if (!std::filesystem::is_directory(path, ec)) {
      MessageBoxW(hwnd_, (L"Priečinok neexistuje:\r\n\r\n" + folder_).c_str(),
                  L"Nová disketa", MB_OK | MB_ICONWARNING);
      SetFocus(Item(IDC_NEW_PATH));
      return false;
    }
    return true;
  }
  // A new diskette must not land on top of something that is already there.
  // Creating it into an existing folder would quietly adopt whatever files
  // it holds, and "new and empty" would be neither.
  if (std::filesystem::exists(path, ec)) {
    MessageBoxW(hwnd_,
                (L"Toto už existuje:\r\n\r\n" + folder_ +
                 L"\r\n\r\nNová disketa musí byť nový priečinok. Zvoľte iné "
                 L"meno, alebo taký priečinok vložte ako existujúcu disketu.")
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
  for (int index = 0; index < Settings::kSlots; ++index)
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(
                     SlotLine(index + 1, slots_[static_cast<std::size_t>(index)])
                         .c_str()));
  SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
  // Nothing to put in a slot when no folder-backed diskette is in the drive,
  // and a button that answers with silence is worse than one that is greyed.
  SetEnabled(IDC_SLOT_CURRENT, !currentDisk_.empty());
}

int SlotsDialog::Selected() const {
  const LRESULT selected =
      SendMessageW(Item(IDC_SLOT_LIST), LB_GETCURSEL, 0, 0);
  return selected == LB_ERR ? -1 : static_cast<int>(selected);
}

void SlotsDialog::SetSlotAndRefresh(int index, std::wstring path) {
  slots_[static_cast<std::size_t>(index)] = std::move(path);
  FillList(index);
  SetFocus(Item(IDC_SLOT_LIST));
}

bool SlotsDialog::OnCommand(int id, int notification) {
  const int index = Selected();
  if (index < 0) return false;
  switch (id) {
    case IDC_SLOT_ASSIGN: {
      const std::wstring folder = win::PickFolder(
          hwnd_, L"Vyberte priečinok, ktorý bude v tomto slote");
      if (!folder.empty()) SetSlotAndRefresh(index, folder);
      return true;
    }
    case IDC_SLOT_CURRENT:
      if (!currentDisk_.empty()) SetSlotAndRefresh(index, currentDisk_);
      return true;
    case IDC_SLOT_CLEAR:
      SetSlotAndRefresh(index, L"");
      return true;
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
