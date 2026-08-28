#ifndef EUREKA_MAIN_WINDOW_H
#define EUREKA_MAIN_WINDOW_H

#include <string>

#include "emulator_thread.h"
#include "win/window.h"

// The emulator's window.  It owns no machine state: every key and every menu
// command is posted to the emulator thread, and everything it displays comes
// back from there.  The client area is deliberately empty -- the machine's
// output is speech, and the host console keeps the text for diagnostics.
class MainWindow : public win::Window {
 public:
  MainWindow(EmulatorThread& emulator, std::wstring romPath,
             std::wstring diskDescription);

  bool Create();
  HACCEL accelerators() const { return accelerators_; }
  HACCEL hostAccelerators() const { return hostAccelerators_; }

  // True while the keyboard belongs to the host, so the Ctrl shortcuts apply.
  // The rest of the time they are Eureka's keys and the window never sees
  // them: that is the whole reason there are two tables.
  bool HostShortcutsActive() const { return released_ || passOnce_; }

 protected:
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

 private:
  void RegisterCommands();
  void RefreshTitle() const;
  // Puts the released state where another process can read it.  The NVDA
  // add-on is the only reader; see kKeyboardReleasedProp.
  void PublishKeyboardState() const;
  void RefreshMenu() const;
  // Puts the "F11, " prefix on the Ctrl shortcuts, or takes it off once the
  // keyboard is released and they no longer need it.
  void RefreshShortcutText(HMENU menu) const;
  void ForwardKey(bool down, WPARAM wParam, LPARAM lParam) const;
  void SendGuestKey(WORD virtualKey, bool alt) const;
  std::wstring AboutText() const;

  // True when this key event belongs to the host rather than to Eureka.  Also
  // spends the one-shot, which is why it is not const.
  bool HostKeepsKey(WPARAM virtualKey, bool down);
  void SetReleased(bool released);
  void SetPassOnce(bool armed);

  EmulatorThread& emulator_;
  std::wstring romPath_;
  std::wstring diskDescription_;
  HACCEL accelerators_ = nullptr;
  HACCEL hostAccelerators_ = nullptr;

  // Shift+F11: the keyboard is let go of altogether and the window behaves
  // like any other Windows window until it is taken back.
  bool released_ = false;
  // F11: the next key goes to Windows instead of to Eureka, once.
  bool passOnce_ = false;
  // Whether the press of that key has been seen yet.  See HostKeepsKey: the
  // release of F11 itself must not be mistaken for it.
  bool passOnceUsed_ = false;
};

#endif
