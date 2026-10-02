// The RS-232 socket wired to a serial port of the host (src/com_link.*,
// ea4-7zw.3).
//
// It needs a pair of ports joined as a null-modem cable -- com0com on the
// developer's machine -- named in EA4_COM_PAIR, for instance "COM8,COM9".
// Neither CI nor a fresh checkout has one, so this is not among the checks of
// run-tests.bat; without the variable it says it skipped and ends with 2, so
// that nobody reads silence as a pass.
//
// What it holds is what the machine cannot tell from a broken cable: every
// byte arrives once and in order in both directions; our RTS reaches the far
// end as its CTS, including an RTS set before the port opened; a port that is
// missing or already taken is refused with a reason; and the list of ports
// knows both ends of the pair.  Losing a port and a format the driver refuses
// are not here -- com0com takes any rate, and pulling it out needs an
// administrator.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "com_link.h"

namespace {

using namespace std::chrono_literals;

int checks = 0;
int failures = 0;

void Check(bool passed, const std::string& name) {
  ++checks;
  if (!passed) ++failures;
  std::cout << (passed ? "  ok   " : "  CHYBA ") << name << "\n";
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds limit = 5s) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

// Everything that arrives until `count` bytes are in, or five seconds pass.
std::vector<uint8_t> Collect(ComLink& link, std::size_t count) {
  std::vector<uint8_t> got;
  WaitUntil([&] {
    while (auto character = link.Receive()) got.push_back(*character);
    return got.size() >= count;
  });
  return got;
}

// The console of a test is not the user's, so ASCII is enough: the system's
// reason is in the user's language and keeps its letters, the rest is "?".
std::string Narrow(const std::wstring& text) {
  std::string narrow;
  for (const wchar_t ch : text) narrow += ch < 0x80 ? static_cast<char>(ch) : '?';
  return narrow;
}

// Every byte value both ways, FFh and 00h among them, sent one by one as the
// machine sends them.
void CheckBytes(ComLink& from, ComLink& to, const std::string& direction) {
  std::vector<uint8_t> sent;
  for (int value = 0; value < 256; ++value) sent.push_back(static_cast<uint8_t>(value));
  for (const uint8_t value : sent) from.Transmit(value);
  Check(Collect(to, sent.size()) == sent, direction + ": 256 bajtov prislo bajt na bajt");
}

}  // namespace

int main() {
  const char* pair = std::getenv("EA4_COM_PAIR");
  const std::string text = pair != nullptr ? pair : "";
  const std::size_t comma = text.find(',');
  if (comma == std::string::npos) {
    std::cout << "preskocene: EA4_COM_PAIR nie je zadane (napriklad COM8,COM9)\n";
    return 2;
  }
  const std::wstring nameA(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(comma));
  const std::wstring nameB(text.begin() + static_cast<std::ptrdiff_t>(comma) + 1, text.end());

  const std::vector<std::wstring> ports = ListComPorts();
  const auto listed = [&](const std::wstring& name) {
    return std::find(ports.begin(), ports.end(), name) != ports.end();
  };
  Check(listed(nameA) && listed(nameB), "zoznam portov pozna oba konce dvojice");

  std::wstring error;
  {
    ComLink nowhere;
    // Opened first: the order of a call's arguments is not specified, and the
    // message would otherwise be built from an empty reason.
    const bool refused = !nowhere.Open(L"COM250", error);
    Check(refused && !error.empty(),
          "neexistujuci port sa odmietne s dovodom: " + Narrow(error));
    Check(!nowhere.ClearToSend(), "bez portu CTS nie je");
  }

  ComLink a;
  ComLink b;
  // RTS before the port opens, as SetSerialLink does at Start: it must reach
  // the wire when the port opens, not wait for the next change.
  a.SetRequestToSend(true);
  b.SetRequestToSend(false);
  if (!a.Open(nameA, error) || !b.Open(nameB, error)) {
    Check(false, "dvojica sa otvorila: " + Narrow(error));
    std::cout << "FAIL com_test\n";
    return 1;
  }
  {
    ComLink taken;
    error.clear();
    const bool refused = !taken.Open(nameA, error);
    Check(refused && !error.empty(),
          "port drzany inym sa odmietne s dovodom: " + Narrow(error));
  }
  Check(WaitUntil([&] { return b.ClearToSend(); }), "RTS nastavene pred otvorenim prislo ako CTS");
  Check(!a.ClearToSend(), "druha strana RTS nema, CTS nie je");

  b.SetRequestToSend(true);
  Check(WaitUntil([&] { return a.ClearToSend(); }), "RTS zapnute prislo ako CTS");
  a.SetRequestToSend(false);
  Check(WaitUntil([&] { return !b.ClearToSend(); }), "RTS vypnute zhodilo CTS");
  a.SetRequestToSend(true);

  CheckBytes(a, b, Narrow(nameA) + "->" + Narrow(nameB));
  CheckBytes(b, a, Narrow(nameB) + "->" + Narrow(nameA));

  // Two open ports doing nothing for a second must cost next to nothing --
  // after traffic, so that every one of the worker's events has fired.  They
  // are reset by hand, and one left signalled would spin the worker at a full
  // core with every byte still arriving correctly.
  const auto cpu = [] {
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const auto ticks = [](const FILETIME& time) {
      return (uint64_t{time.dwHighDateTime} << 32) | time.dwLowDateTime;
    };
    return ticks(kernel) + ticks(user);
  };
  const uint64_t before = cpu();
  std::this_thread::sleep_for(1s);
  const uint64_t spent = (cpu() - before) / 10000;
  Check(spent < 100, "necinne porty nezatazuju procesor (" + std::to_string(spent) + " ms za 1 s)");

  // A format change while the port runs -- the worker calls SetCommState with
  // reads and the CTS watch still pending -- and the port keeps carrying.
  // Whether the driver really uses the format is not visible here: com0com
  // passes bytes as written unless told to emulate the rate.
  a.SetLineFormat({4800, 7, LineFormat::Parity::kEven, 2});
  b.SetLineFormat({4800, 7, LineFormat::Parity::kEven, 2});
  std::this_thread::sleep_for(100ms);
  a.Transmit('E');
  Check(Collect(b, 1) == std::vector<uint8_t>{'E'}, "po zmene formatu sa da dalej posielat");

  a.Close();
  Check(!a.ClearToSend() && !a.is_open(), "po Close nie je CTS ani port");
  b.Transmit('x');
  Check(!a.Receive(), "zavrety koniec nic neprijme");
  error.clear();
  const bool reopened = a.Open(nameA, error);
  Check(reopened, "port sa da po Close otvorit znovu" + (reopened ? "" : ": " + Narrow(error)));

  std::cout << checks - failures << "/" << checks << "\n";
  std::cout << (failures == 0 ? "PASS" : "FAIL") << " com_test\n";
  return failures == 0 ? 0 : 1;
}
