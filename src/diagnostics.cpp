#include "diagnostics.h"

#include <algorithm>

namespace {

std::wstring Hex(uint64_t value, int digits) {
  static const wchar_t kDigits[] = L"0123456789ABCDEF";
  std::wstring text(static_cast<std::size_t>(digits), L'0');
  for (int index = digits - 1; index >= 0; --index) {
    text[static_cast<std::size_t>(index)] = kDigits[value & 0xf];
    value >>= 4;
  }
  return text;
}

std::wstring Dec(uint64_t value) {
  if (value == 0) return L"0";
  std::wstring text;
  while (value) {
    text.push_back(static_cast<wchar_t>(L'0' + value % 10));
    value /= 10;
  }
  std::reverse(text.begin(), text.end());
  return text;
}

// The latches are write-only in hardware, so the ROM keeps a mirror of each
// one in RAM.  Naming them here makes the report readable without forcing the
// reader back to the hardware map.
const wchar_t* LatchName(uint8_t port) {
  switch (port) {
    case 0x80: return L"80h (zvuk, strobe, sériové parametre; tieň C439h)";
    case 0xa0: return L"A0h (motor diskety, výstup, reč; tieň C43Ah)";
    case 0xb0: return L"B0h (disk, CSI/O linky, RTS; tieň C438h)";
    default: return L"neznámy latch";
  }
}

}  // namespace

void Diagnostics::Push(Kind kind, uint16_t pc, uint32_t address, uint8_t value,
                       uint8_t extra) {
  ring_[ringNext_] = Event{kind, pc, address, value, extra};
  ringNext_ = (ringNext_ + 1) % kRingSize;
  ++ringTotal_;
}

void Diagnostics::TracePortRead(uint16_t pc, uint16_t port, uint8_t value) {
  Push(Kind::kPortRead, pc, port, value, 0);
}

void Diagnostics::TracePortWrite(uint16_t pc, uint16_t port, uint8_t value) {
  Push(Kind::kPortWrite, pc, port, value, 0);
}

void Diagnostics::NoteDroppedWrite(uint16_t pc, uint32_t physical, uint8_t value) {
  PageStats& page = pages_[physical >> 12];
  if (page.count == 0) page.firstPc = pc;
  ++page.count;
  page.lowest = std::min(page.lowest, physical);
  page.highest = std::max(page.highest, physical);
  page.lastValue = value;
  Push(Kind::kDroppedWrite, pc, physical, value, 0);
}

void Diagnostics::NotePortRead(uint16_t pc, uint16_t port, uint8_t value) {
  PortStats& stats = ports_[port];
  if (stats.reads == 0 && stats.writes == 0) stats.firstPc = pc;
  ++stats.reads;
  stats.lastValue = value;
  Push(Kind::kPortRead, pc, port, value, 0);
}

void Diagnostics::NotePortWrite(uint16_t pc, uint16_t port, uint8_t value) {
  PortStats& stats = ports_[port];
  if (stats.reads == 0 && stats.writes == 0) stats.firstPc = pc;
  ++stats.writes;
  stats.lastValue = value;
  Push(Kind::kPortWrite, pc, port, value, 0);
}

void Diagnostics::NoteLatch(uint16_t pc, uint8_t port, uint8_t before,
                            uint8_t after) {
  const uint8_t changed = static_cast<uint8_t>(before ^ after);
  if (changed == 0) return;
  LatchStats& stats = latches_[port];
  ++stats.changes;
  for (unsigned bit = 0; bit < 8; ++bit) {
    if ((changed >> bit & 1) == 0) continue;
    if (stats.setCount[bit] == 0 && stats.clearCount[bit] == 0)
      stats.firstPc[bit] = pc;
    if ((after >> bit & 1) != 0) ++stats.setCount[bit];
    else ++stats.clearCount[bit];
  }
  Push(Kind::kLatch, pc, port, after, changed);
}

void Diagnostics::Clear() {
  pages_.clear();
  ports_.clear();
  latches_.clear();
  trace_.fill(false);
  ring_.fill(Event{});
  ringNext_ = 0;
  ringTotal_ = 0;
}

std::wstring Diagnostics::Report() const {
  std::wstring text = L"\r\n=== Diagnostika hardvéru ===\r\n";

  text += L"\r\n-- Zahodené zápisy do pamäte (mimo okna RAM) --\r\n";
  if (pages_.empty()) {
    text += L"   žiadne\r\n";
  } else {
    for (const auto& [page, stats] : pages_) {
      text += L"   stránka " + Hex(page << 12, 5) + L"h: " + Dec(stats.count) +
              L"x, rozsah " + Hex(stats.lowest, 5) + L"h-" +
              Hex(stats.highest, 5) + L"h, prvý PC " + Hex(stats.firstPc, 4) +
              L"h, posledná hodnota " + Hex(stats.lastValue, 2) + L"h\r\n";
    }
    text += L"   Ak sem firmvér zapisuje opakovane, okno RAM v WriteMemory je\r\n"
            L"   nastavené zle a treba ho posunúť nadol.\r\n";
  }

  // A port below 40h with no high address byte is a Z180 on-chip register.
  // Those are stored in io_ and read back by the timer and MMU code, so they
  // are only "unmodelled" in the sense of having no side effect; keeping them
  // apart stops them from burying the external ports that matter.
  const auto internal = [](uint16_t port) { return port < 0x40; };

  text += L"\r\n-- Externé porty bez modelu --\r\n";
  bool anyExternal = false;
  for (const auto& [port, stats] : ports_) {
    if (internal(port)) continue;
    anyExternal = true;
    text += L"   port " + Hex(port, 4) + L"h: " + Dec(stats.reads) +
            L" čítaní, " + Dec(stats.writes) + L" zápisov, prvý PC " +
            Hex(stats.firstPc, 4) + L"h, posledná hodnota " +
            Hex(stats.lastValue, 2) + L"h\r\n";
  }
  if (!anyExternal) text += L"   žiadne\r\n";

  text += L"\r\n-- Interné registre Z180 bez správania (len uložené) --\r\n";
  bool anyInternal = false;
  for (const auto& [port, stats] : ports_) {
    if (!internal(port)) continue;
    anyInternal = true;
    text += L"   reg " + Hex(port, 2) + L"h: " + Dec(stats.reads) +
            L" čítaní, " + Dec(stats.writes) + L" zápisov, posledná hodnota " +
            Hex(stats.lastValue, 2) + L"h\r\n";
  }
  if (!anyInternal) text += L"   žiadne\r\n";

  text += L"\r\n-- Riadiace latche po bitoch --\r\n";
  if (latches_.empty()) {
    text += L"   žiadne zmeny\r\n";
  } else {
    for (const auto& [port, stats] : latches_) {
      text += L"   " + std::wstring(LatchName(port)) + L", zmien: " +
              Dec(stats.changes) + L"\r\n";
      for (int bit = 7; bit >= 0; --bit) {
        const auto index = static_cast<std::size_t>(bit);
        if (stats.setCount[index] == 0 && stats.clearCount[index] == 0) {
          text += L"      bit " + Dec(static_cast<uint64_t>(bit)) +
                  L": nikdy sa nezmenil\r\n";
          continue;
        }
        text += L"      bit " + Dec(static_cast<uint64_t>(bit)) + L": na 1 " +
                Dec(stats.setCount[index]) + L"x, na 0 " +
                Dec(stats.clearCount[index]) + L"x, prvý PC " +
                Hex(stats.firstPc[index], 4) + L"h\r\n";
      }
    }
  }

  text += L"\r\n-- Posledné udalosti (najnovšia dole) --\r\n";
  if (ringTotal_ == 0) {
    text += L"   žiadne\r\n";
  } else {
    const uint64_t shown = std::min<uint64_t>(ringTotal_, kRingSize);
    const std::size_t start = (ringNext_ + kRingSize -
                               static_cast<std::size_t>(shown)) % kRingSize;
    for (uint64_t offset = 0; offset < shown; ++offset) {
      const Event& event = ring_[(start + static_cast<std::size_t>(offset)) %
                                 kRingSize];
      text += L"   PC " + Hex(event.pc, 4) + L"h  ";
      switch (event.kind) {
        case Kind::kDroppedWrite:
          text += L"zahodený zápis " + Hex(event.address, 5) + L"h <- " +
                  Hex(event.value, 2) + L"h";
          break;
        case Kind::kPortRead:
          text += L"čítanie portu " + Hex(event.address, 4) + L"h -> " +
                  Hex(event.value, 2) + L"h";
          break;
        case Kind::kPortWrite:
          text += L"zápis portu " + Hex(event.address, 4) + L"h <- " +
                  Hex(event.value, 2) + L"h";
          break;
        case Kind::kLatch:
          text += L"latch " + Hex(event.address, 2) + L"h = " +
                  Hex(event.value, 2) + L"h (zmenené bity " +
                  Hex(event.extra, 2) + L"h)";
          break;
      }
      text += L"\r\n";
    }
    if (ringTotal_ > kRingSize)
      text += L"   (starších " + Dec(ringTotal_ - kRingSize) +
              L" udalostí sa už nezmestilo)\r\n";
  }
  text += L"\r\n";
  return text;
}
