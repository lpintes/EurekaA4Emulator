#include "host_console.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mutex>

namespace host {
namespace {

void WriteAllBytes(HANDLE output, const std::string& bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD written = 0;
    if (!WriteFile(output, bytes.data() + offset,
                   static_cast<DWORD>(bytes.size() - offset), &written,
                   nullptr) ||
        written == 0)
      return;
    offset += written;
  }
}

bool OwnsConsole() {
  DWORD pids[4]{};
  return GetConsoleProcessList(pids, 4) == 1;
}

// A GUI subsystem process starts with no standard handles at all, and
// AttachConsole only fills them in under conditions not worth relying on.  So
// they are opened explicitly -- but only when there is nothing usable there
// already, because "EurekaA4Emulator.exe --help > subor.txt" hands us a file
// handle and taking that over would send the help to the screen instead.
void AdoptConsoleHandles() {
  SetConsoleOutputCP(CP_UTF8);
  const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (output == nullptr || output == INVALID_HANDLE_VALUE) {
    const HANDLE console =
        CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    0, nullptr);
    if (console != INVALID_HANDLE_VALUE) SetStdHandle(STD_OUTPUT_HANDLE, console);
  }
  const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (input == nullptr || input == INVALID_HANDLE_VALUE) {
    const HANDLE console =
        CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    0, nullptr);
    if (console != INVALID_HANDLE_VALUE) SetStdHandle(STD_INPUT_HANDLE, console);
  }
}

// A console window is not a window of ours, and closing it does not close a
// window -- it ends every process attached to that console, ours included.
// For this program that is a battery cut-off: the exit path never runs, so
// there is no "close without powering down?" question, no offer to save the
// unsaved diskette or the ones in the slots, and no RAM snapshot.  The next
// start then says "inicializace eureky" and the machine is back to nothing.
// The user need not even mean it: the diagnostic window sits next to the main
// one and Alt+F4 lands wherever the focus happens to be.
//
// What this does and does not achieve, measured 24. 9. 2026 on a -mwindows
// stub with an AllocConsole console (classic conhost, class
// ConsoleWindowClass, launched through the Windows shell the way the emulator
// is):
//
//   - CTRL_C_EVENT kills the process by default -- the stub's log stops dead
//     at the line that sent it.  With this handler it survives and runs on.
//     This half works, and it matters: the console takes the focus when it
//     opens, so it is the window a Ctrl+C lands in.
//
//   - DeleteMenu(SC_CLOSE) returns 1 and GetMenuState then returns -1, so the
//     item is really gone -- and it does NOT stop a close.  Posting
//     WM_SYSCOMMAND/SC_CLOSE to that window ends the process exactly as it
//     does without the DeleteMenu: six log lines either way.  That is Alt+F4's
//     own path, because DefWindowProc turns Alt+F4 into SC_CLOSE without ever
//     consulting the system menu.  The menu governs what can be *clicked*, so
//     this greys the X button and nothing more.
//
// Alt+F4 on the diagnostic window therefore still ends the emulator.  Stopping
// it is not possible from here: that window belongs to conhost in another
// process, so it cannot be given a window procedure of ours.  The routes that
// remain are a diagnostic window of our own instead of a console, or damage
// control in CTRL_CLOSE_EVENT -- a close grants about five seconds before the
// kill (measured: 4921 ms, and the other threads keep running through it).
// See HANDOFF 6.47.
//
// A console inherited from a parent shell is that shell's window, not ours, so
// it is left alone -- see OpenConsole, where this is called only in the
// AllocConsole branch.
// Guarded because the handler runs on a thread the system injects, which can
// arrive at any moment -- including while main is tearing the same objects
// down.  std::function is not safe to call while another thread reassigns it.
std::mutex g_rescueMutex;
std::function<void()> g_rescue;

// Whether the console is one we made.  Ctrl+C is swallowed only then: in a
// shell the user started us from, Ctrl+C ending the program is what everybody
// expects, and taking that away would be the surprise.  The close rescue, by
// contrast, applies to both -- closing the shell's window kills us just as
// dead, and the data is worth just as much.
bool g_ownConsole = false;

BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
  if (type == CTRL_CLOSE_EVENT) {
    // Cannot be refused: the system kills us when this returns, or after about
    // five seconds, whichever comes first.  So the only thing worth doing is
    // saving what would otherwise be lost.
    std::function<void()> rescue;
    {
      std::lock_guard<std::mutex> lock(g_rescueMutex);
      rescue = g_rescue;
    }
    if (rescue) rescue();
    return TRUE;
  }
  // TRUE means handled, which is what stops the default handler from ending
  // the process.  Logoff and shutdown are left to the system.
  return g_ownConsole && (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT);
}

void EnsureCtrlHandler() {
  static std::once_flag once;
  std::call_once(once, [] { SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE); });
}

void MakeOwnConsoleUnclosable() {
  g_ownConsole = true;
  EnsureCtrlHandler();
  const HWND window = GetConsoleWindow();
  if (window == nullptr) return;
  HMENU menu = GetSystemMenu(window, FALSE);
  if (menu == nullptr) return;
  DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
  DrawMenuBar(window);
}

}  // namespace

bool HasConsole() { return GetConsoleWindow() != nullptr; }

void SetCloseRescue(std::function<void()> rescue) {
  std::lock_guard<std::mutex> lock(g_rescueMutex);
  g_rescue = std::move(rescue);
}

void AttachToParentConsole() {
  if (HasConsole()) return;
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    AdoptConsoleHandles();
    // Not ours, so Ctrl+C keeps its usual meaning -- but closing that window
    // ends us too, and the rescue is worth having either way.
    EnsureCtrlHandler();
  }
}

void OpenConsole() {
  if (HasConsole()) {
    AdoptConsoleHandles();
    return;
  }
  AttachToParentConsole();
  if (HasConsole()) return;
  if (AllocConsole()) {
    SetConsoleTitleW(L"Eureka A4 — diagnostika");
    AdoptConsoleHandles();
    // Only here, never after AttachConsole above: the branch is the whole
    // guard.  A console we were handed belongs to the shell that started us,
    // and taking Close off somebody else's window is not ours to do.
    MakeOwnConsoleUnclosable();
  }
}

void Print(const std::wstring& text) {
  if (text.empty()) return;
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (output == nullptr || output == INVALID_HANDLE_VALUE) return;

  DWORD mode = 0;
  if (GetConsoleMode(output, &mode)) {
    size_t offset = 0;
    while (offset < text.size()) {
      DWORD written = 0;
      if (!WriteConsoleW(output, text.data() + offset,
                         static_cast<DWORD>(text.size() - offset), &written,
                         nullptr) ||
          written == 0)
        return;
      offset += written;
    }
    return;
  }

  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return;
  std::string bytes(static_cast<size_t>(size), 0);
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      bytes.data(), size, nullptr, nullptr);
  WriteAllBytes(output, bytes);
}

void HoldConsole() {
  if (!HasConsole() || !OwnsConsole()) return;
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode)) return;
  Print(L"\r\nStlačte ľubovoľný kláves...\r\n");
  SetConsoleMode(input, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
  FlushConsoleInputBuffer(input);
  for (;;) {
    INPUT_RECORD record{};
    DWORD read = 0;
    if (!ReadConsoleInputW(input, &record, 1, &read) || !read) break;
    if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) break;
  }
  SetConsoleMode(input, mode);
}

}  // namespace host
