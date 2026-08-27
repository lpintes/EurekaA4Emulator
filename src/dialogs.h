#ifndef EUREKA_DIALOGS_H
#define EUREKA_DIALOGS_H

// The emulator's own dialogs.  They are real dialogs from resource templates
// -- see win/dialog.h for why that matters and not merely how it looks.

#include <string>

#include "emulator_thread.h"
#include "win/dialog.h"

class SettingsDialog : public win::Dialog {
 public:
  SettingsDialog(InputMode mode, bool diagnostics)
      : mode_(mode), diagnostics_(diagnostics) {}

  InputMode mode() const { return mode_; }
  bool diagnostics() const { return diagnostics_; }

 protected:
  bool OnInit() override;
  bool OnOk() override;

 private:
  InputMode mode_;
  bool diagnostics_;
};

class AboutDialog : public win::Dialog {
 public:
  explicit AboutDialog(std::wstring body) : body_(std::move(body)) {}

 protected:
  bool OnInit() override;

 private:
  std::wstring body_;
};

#endif
