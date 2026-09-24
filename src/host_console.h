#ifndef EUREKA_HOST_CONSOLE_H
#define EUREKA_HOST_CONSOLE_H

// The console is the diagnostic surface and nothing else: --diag, the guest's
// own console device, --help.  The emulator is a GUI subsystem program, so it
// has no console of its own unless one is asked for -- an unasked-for console
// window takes the focus on start-up and then has to be found again with
// Alt+Tab, which is noise, not information.
//
// Anything the *user* needs to be told goes in a message box instead.  A GUI
// program that reports through a window nobody opened is reporting nowhere.

#include <functional>
#include <string>

namespace host {

// Runs when the console window is closed, in the few seconds Windows grants
// before it kills the process.  Closing a console ends every process attached
// to it and that cannot be refused, so this is damage control, not a defence:
// the caller gets one chance to put the machine's state somewhere permanent.
//
// Measured 24. 9. 2026: a close gives about five seconds (4921 ms) and the
// other threads keep running throughout.  That is enough to stop the worker,
// flush the diskette and write the RAM snapshot, and nowhere near enough for
// anything that needs a dialog -- and the handler runs on a thread of the
// system's making, which must not open windows anyway.
//
// Pass nullptr to unregister before the objects it touches go out of scope.
void SetCloseRescue(std::function<void()> rescue);

// Attaches to the console this process was launched from, when there is one.
// No window appears and nothing takes the focus, so this is free: it only
// means that a --help or a --diag run from a shell prints where it should.
void AttachToParentConsole();

// Makes sure there is somewhere to print, creating a console if the process
// still has none.  This one does open a window, so it is only for output the
// user asked for by name.
//
// Closing a console window ends every process attached to it, so a close of
// the diagnostic window is a hidden battery cut-off for the emulator: exit
// never runs, and with it no question about powering down, no offer to save a
// diskette and no RAM snapshot.  A console created here therefore swallows
// Ctrl+C and loses the Close item from its system menu -- but only the first
// of those actually holds; Alt+F4 still gets through.  A console inherited
// from a parent shell is left as it is.  See host_console.cpp and HANDOFF
// 6.47 for what was measured.
void OpenConsole();

bool HasConsole();

// Wide through WriteConsoleW when there is a console, UTF-8 bytes when the
// output is redirected, and nothing at all when there is neither.  std::wcout
// narrows through the "C" locale and stops dead at the first character it
// cannot represent, which used to cut the help text off mid-word at "Pou".
void Print(const std::wstring& text);

// A console this process created dies with it, taking the last message with
// it.  Hold it until a key so there is a chance to read it.  Does nothing when
// the console belongs to a shell that will outlive us.
void HoldConsole();

}  // namespace host

#endif
