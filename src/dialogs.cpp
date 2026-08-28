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

// "3: Slovník — C:\Diskety\Slovnik", or "3: prázdny".  The number leads
// because it is the shortcut: a screen reader reading the line has already
// said which key puts that diskette in.
std::wstring SlotLine(int number, const std::wstring& path) {
  std::wstring line = std::to_wstring(number) + L": ";
  if (path.empty()) return line + L"prázdny";
  const std::filesystem::path folder(path);
  std::filesystem::path leaf = folder.filename();
  if (leaf.empty()) leaf = folder.parent_path().filename();
  return line + (leaf.empty() ? path : leaf.wstring()) + L" — " + path;
}

}  // namespace

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
