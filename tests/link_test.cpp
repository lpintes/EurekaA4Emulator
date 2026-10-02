// The serial cable between two emulators over TCP (src/tcp_link.*, ea4-7zw.4).
//
// Two links on loopback, no ROM and no machine.  What this holds is what the
// machine on either end cannot tell apart from a broken cable: every byte
// arrives once and in order, FFh included, which the wire uses as its escape;
// RTS reaches the other side as CTS and never overtakes a byte sent before it;
// and a cable that goes away says so, and leaves CTS dropped rather than stuck.
//
// Nothing here goes beyond 127.0.0.1, and every port comes from Listen(0).

#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "serial_socket.h"
#include "tcp_link.h"

namespace {

using namespace std::chrono_literals;
using Event = TcpLink::Event;
using State = TcpLink::State;

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

// What a link reported, collected from its own thread.
class Recorder {
 public:
  explicit Recorder(TcpLink& link) {
    link.SetListener([this](Event event, const std::wstring& detail) {
      std::lock_guard guard(lock_);
      events_.push_back(event);
      if (!detail.empty()) detail_ = detail;
    });
  }
  int Count(Event event) {
    std::lock_guard guard(lock_);
    int count = 0;
    for (const Event seen : events_) count += seen == event ? 1 : 0;
    return count;
  }
  std::wstring Detail() {
    std::lock_guard guard(lock_);
    return detail_;
  }

 private:
  std::mutex lock_;
  std::vector<Event> events_;
  std::wstring detail_;
};

// Everything that arrives until `count` bytes are in, or five seconds pass.
std::vector<uint8_t> Collect(TcpLink& link, std::size_t count) {
  std::vector<uint8_t> got;
  WaitUntil([&] {
    while (auto character = link.Receive()) got.push_back(*character);
    return got.size() >= count;
  });
  return got;
}

// All 256 values, then runs of FFh of every length up to 8 (FF FF FF on the
// wire is an escaped FFh followed by an escape that has to wait for its pair),
// then a long stretch so the stream spans many recv calls.
std::vector<uint8_t> Payload(uint32_t seed) {
  std::vector<uint8_t> bytes;
  for (int value = 0; value < 256; ++value) bytes.push_back(static_cast<uint8_t>(value));
  for (int run = 1; run <= 8; ++run) {
    bytes.insert(bytes.end(), run, 0xff);
    bytes.push_back(static_cast<uint8_t>(run));
  }
  for (int i = 0; i < 50000; ++i) {
    seed = seed * 1103515245u + 12345u;
    bytes.push_back(static_cast<uint8_t>(seed >> 24));
  }
  return bytes;
}

bool Parses(const std::wstring& text, const std::wstring& host, uint16_t port) {
  std::wstring gotHost;
  uint16_t gotPort = 0;
  return ParseTcpAddress(text, TcpLink::kDefaultPort, gotHost, gotPort) && gotHost == host &&
         gotPort == port;
}

bool Rejected(const std::wstring& text) {
  std::wstring host;
  uint16_t port = 0;
  return !ParseTcpAddress(text, TcpLink::kDefaultPort, host, port);
}

void Addresses() {
  constexpr uint16_t kDefault = TcpLink::kDefaultPort;
  Check(Parses(L"localhost", L"localhost", kDefault), "meno bez portu dostane predvoleny");
  Check(Parses(L"192.168.1.5:5000", L"192.168.1.5", 5000), "IPv4 s portom");
  Check(Parses(L"  pocitac.local:80 ", L"pocitac.local", 80), "medzery okolo sa odhodia");
  Check(Parses(L"[fd7a:115c::1]:4200", L"fd7a:115c::1", 4200), "IPv6 v zatvorkach s portom");
  Check(Parses(L"[::1]", L"::1", kDefault), "IPv6 v zatvorkach bez portu");
  Check(Parses(L"fd7a:115c::1", L"fd7a:115c::1", kDefault), "hola IPv6 bez portu");
  Check(Rejected(L""), "prazdne sa odmietne");
  Check(Rejected(L"   "), "same medzery sa odmietnu");
  Check(Rejected(L"pocitac:"), "dvojbodka bez portu sa odmietne");
  Check(Rejected(L"pocitac:0"), "port 0 sa odmietne");
  Check(Rejected(L"pocitac:65536"), "port nad 65535 sa odmietne");
  Check(Rejected(L"pocitac:12a"), "port s pismenom sa odmietne");
  Check(Rejected(L":4161"), "port bez mena sa odmietne");
  Check(Rejected(L"[::1"), "neuzavreta zatvorka sa odmietne");
  Check(Rejected(L"[::1]4161"), "za zatvorkou musi byt dvojbodka");
  Check(Rejected(L"moj pocitac"), "medzera v mene sa odmietne");
  uint16_t port = 0;
  Check(ParseTcpPort(L"65535", port) && port == 65535, "port 65535 prejde");
  Check(!ParseTcpPort(L"-1", port) && !ParseTcpPort(L"100000", port), "zly port sa odmietne");
}

void Cable() {
  TcpLink a;
  TcpLink b;
  Recorder heardA(a);
  Recorder heardB(b);
  std::wstring error;
  Check(a.Listen(0, error) && a.port() != 0 && a.state() == State::kListening,
        "cakanie na volnom porte");

  TcpLink squatter;
  std::wstring squatterError;
  Check(!squatter.Listen(a.port(), squatterError) && !squatterError.empty(),
        "obsadeny port sa odmietne hned a s dovodom");

  // Nobody connected yet: CTS down and a transmitted byte simply lost.
  a.SetRequestToSend(true);
  a.Transmit(0x41);
  Check(!a.ClearToSend(), "bez spojenia CTS nie je");

  b.Connect(L"127.0.0.1", a.port());
  Check(WaitUntil([&] {
          return heardA.Count(Event::kConnected) == 1 && heardB.Count(Event::kConnected) == 1;
        }),
        "obe strany ohlasia spojenie");
  Check(a.state() == State::kConnected && b.state() == State::kConnected, "obe su spojene");

  // A asserted RTS before the cable existed; B must learn it on connecting.
  Check(WaitUntil([&] { return b.ClearToSend(); }), "RTS spred spojenia dojde ako CTS");
  Check(!a.ClearToSend(), "B RTS nema, takze A nema CTS");
  b.SetRequestToSend(true);
  Check(WaitUntil([&] { return a.ClearToSend(); }), "RTS z B dojde do A");
  Check(!b.Receive().has_value(), "bajt poslany bez spojenia neprisiel");

  // Both directions at once.
  const std::vector<uint8_t> fromA = Payload(1);
  const std::vector<uint8_t> fromB = Payload(2);
  std::thread sendA([&] {
    for (const uint8_t byte : fromA) a.Transmit(byte);
  });
  for (const uint8_t byte : fromB) b.Transmit(byte);
  sendA.join();
  Check(Collect(b, fromA.size()) == fromA, "vsetky bajty z A do B, aj FFh, v poradi");
  Check(Collect(a, fromB.size()) == fromB, "vsetky bajty z B do A, aj FFh, v poradi");

  // RTS dropped after a burst must not arrive before the burst does.
  for (int i = 0; i < 2000; ++i) a.Transmit(0xff);
  a.SetRequestToSend(false);
  Check(WaitUntil([&] { return !b.ClearToSend(); }), "spadnute RTS dojde");
  std::size_t waiting = 0;
  while (b.Receive()) ++waiting;
  Check(waiting == 2000, "RTS nepredbehne bajty poslane pred nim (" +
                             std::to_string(waiting) + " z 2000)");

  // A third emulator while the cable is in use is turned away, and the two
  // ends keep their cable.
  TcpLink c;
  Recorder heardC(c);
  c.Connect(L"127.0.0.1", a.port());
  Check(WaitUntil([&] { return heardC.Count(Event::kLost) == 1; }),
        "treti emulator je odmietnuty");
  c.Close();
  b.Transmit(0x33);
  Check(Collect(a, 1) == std::vector<uint8_t>{0x33}, "povodny kabel po odmietnuti funguje");
  Check(heardA.Count(Event::kLost) == 0, "odmietnutie nie je strata kabla");

  // B unplugs: A hears it, waits again and has no CTS; B hears nothing,
  // because it did it itself.
  b.Close();
  Check(WaitUntil([&] { return heardA.Count(Event::kLost) == 1; }), "A ohlasi stratu");
  Check(WaitUntil([&] { return a.state() == State::kListening; }), "A znova caka");
  Check(!a.ClearToSend(), "po strate CTS nevisi");
  Check(heardB.Count(Event::kLost) == 0, "vlastne odpojenie sa nehlasi ako strata");
  Check(b.state() == State::kIdle, "B je odpojeny");

  // Plugged in again: bytes from the old connection do not leak into it.
  a.Transmit(0x55);  // nobody there
  b.Connect(L"127.0.0.1", a.port());
  Check(WaitUntil([&] { return heardB.Count(Event::kConnected) == 2; }),
        "B sa pripoji znova");
  b.Transmit(0x66);
  Check(Collect(a, 1) == std::vector<uint8_t>{0x66}, "po novom spojeni idu bajty");
  Check(!b.Receive().has_value(), "nic zo stareho spojenia neprislo");

  // The listening end going away is the same loss from the other side.
  a.Close();
  Check(WaitUntil([&] { return heardB.Count(Event::kLost) == 1; }), "B ohlasi stratu");
  Check(b.state() == State::kIdle, "pripajajuci sa po strate nepripaja znova");
  Check(!b.ClearToSend(), "ani B nema po strate CTS");
}

void Failures() {
  uint16_t closed = 0;
  {
    TcpLink probe;
    std::wstring error;
    probe.Listen(0, error);
    closed = probe.port();
  }
  TcpLink link;
  Recorder heard(link);
  link.Connect(L"127.0.0.1", closed);
  Check(WaitUntil([&] { return heard.Count(Event::kFailed) == 1; }, 15s),
        "port, kde nikto necaka, je zlyhanie");
  Check(!heard.Detail().empty(), "zlyhanie ma dovod");
  Check(link.state() == State::kIdle, "po zlyhani je kabel odpojeny");

  TcpLink nameless;
  Recorder heardNameless(nameless);
  nameless.Connect(L"neexistuje.invalid", TcpLink::kDefaultPort);
  Check(WaitUntil([&] { return heardNameless.Count(Event::kFailed) == 1; }, 15s),
        "neexistujuce meno je zlyhanie");

  // An address where nothing answers at all: the attempt would run for ten
  // seconds, and unplugging must not wait for them -- the window does it.
  TcpLink hanging;
  Recorder heardHanging(hanging);
  hanging.Connect(L"10.255.255.1", TcpLink::kDefaultPort);
  std::this_thread::sleep_for(200ms);
  const auto start = std::chrono::steady_clock::now();
  hanging.Close();
  const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  Check(took < 1s, "odpojenie pocas pripajania necaka (" + std::to_string(took.count()) +
                       " ms)");
  Check(heardHanging.Count(Event::kFailed) == 0, "zrusene pripajanie sa nehlasi ako zlyhanie");
}

// A cable that only remembers what it was told, for the socket below.
class Probe : public SerialLink {
 public:
  bool cts = false;
  bool rts = false;
  std::optional<LineFormat> format;
  std::vector<uint8_t> sent;
  std::vector<uint8_t> waiting;

  void SetLineFormat(const LineFormat& line) override { format = line; }
  bool ClearToSend() override { return cts; }
  void SetRequestToSend(bool asserted) override { rts = asserted; }
  void Transmit(uint8_t character) override { sent.push_back(character); }
  std::optional<uint8_t> Receive() override {
    if (waiting.empty()) return std::nullopt;
    const uint8_t character = waiting.front();
    waiting.erase(waiting.begin());
    return character;
  }
};

// The socket the machine stays wired to while cables go in and out of it
// (src/serial_socket.h, ea4-7zw.3).  The machine says RTS and the format only
// when they change, so a cable plugged in after that would otherwise run with
// RTS dropped -- and the far end would never send -- and a real port at
// whatever rate it had before.  Both are quiet.
void Socket() {
  std::cout << "zasuvka\n";
  const LineFormat line{9600, 8, LineFormat::Parity::kNone, 1};
  SerialSocket socket;
  socket.SetRequestToSend(true);
  socket.SetLineFormat(line);
  socket.Transmit('a');
  Check(!socket.ClearToSend() && !socket.Receive(), "prazdna zasuvka: bez CTS a bez znakov");

  Probe first;
  first.cts = true;
  first.waiting = {'z'};
  socket.Plug(&first);
  Check(first.rts, "zastrceny kabel dostane RTS, ktore stroj nastavil pred nim");
  Check(first.format == line, "zastrceny kabel dostane format, ktory stroj nastavil pred nim");
  Check(first.sent.empty(), "co islo do prazdnej zasuvky, sa do kabla nedostane");
  socket.Transmit('b');
  Check(socket.ClearToSend() && socket.Receive() == uint8_t{'z'} && first.sent.size() == 1,
        "zasuvka prepusta CTS, prijem aj vysielanie");

  Probe second;
  socket.SetRequestToSend(false);
  socket.Plug(&second);
  socket.Transmit('c');
  Check(first.sent.size() == 1 && second.sent.size() == 1,
        "po vymene kabla ide vysielanie len do noveho");
  Check(!second.rts && second.format == line, "novy kabel dostane posledne RTS aj format");
  socket.Plug(nullptr);
  Check(!socket.ClearToSend(), "vytiahnuty kabel: CTS nie je");
}

}  // namespace

int main() {
  Addresses();
  Cable();
  Failures();
  Socket();

  std::cout << (failures == 0 ? "PASS" : "FAIL") << " mode=LINK kontrol=" << checks
            << " chyb=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}
