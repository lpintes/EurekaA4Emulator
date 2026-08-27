#include "host_console.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

}  // namespace

bool HasConsole() { return GetConsoleWindow() != nullptr; }

void AttachToParentConsole() {
  if (HasConsole()) return;
  if (AttachConsole(ATTACH_PARENT_PROCESS)) AdoptConsoleHandles();
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
