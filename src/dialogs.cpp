#include "dialogs.h"

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

bool AboutDialog::OnInit() {
  SetText(IDC_ABOUT_TEXT, body_);
  // The edit control starts with everything selected, which a screen reader
  // reads out as a selection rather than as text.  Put the caret at the top
  // instead and leave the focus where the dialog manager wants it.
  SendMessageW(Item(IDC_ABOUT_TEXT), EM_SETSEL, 0, 0);
  return false;
}
