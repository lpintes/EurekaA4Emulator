#ifndef EUREKA_DIAGNOSTICS_H
#define EUREKA_DIAGNOSTICS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

// Records hardware accesses the machine model does not implement.
//
// Two open questions about the Eureka A4 cannot be settled from the ROM image
// alone: where writable RAM actually begins, and what device hangs off the
// Z180 clocked serial port.  Both are answered by watching a running machine,
// so dropped memory writes, accesses to unmodelled ports and every change of
// the three write-only control latches are counted here instead of being
// discarded silently.
//
// Everything is aggregated rather than streamed: the ROM touches these paths
// thousands of times per second, and a per-access log would bury the signal.
class Diagnostics {
 public:
  enum class Kind : uint8_t { kDroppedWrite, kPortRead, kPortWrite, kLatch };

  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }

  // Tracing records a port even when the machine does model it.  It is
  // deliberately opt-in per port: the DAC and the timer reload are written
  // thousands of times a second and would fill the ring on their own.
  void set_trace(uint8_t port, bool on) { trace_[port] = on; }
  bool traces(uint8_t port) const { return trace_[port]; }
  void TracePortRead(uint16_t pc, uint16_t port, uint8_t value);
  void TracePortWrite(uint16_t pc, uint16_t port, uint8_t value);

  void NoteDroppedWrite(uint16_t pc, uint32_t physical, uint8_t value);
  void NotePortRead(uint16_t pc, uint16_t port, uint8_t value);
  void NotePortWrite(uint16_t pc, uint16_t port, uint8_t value);
  void NoteLatch(uint16_t pc, uint8_t port, uint8_t before, uint8_t after);

  void Clear();
  std::wstring Report() const;

 private:
  struct PageStats {
    uint64_t count = 0;
    uint32_t lowest = 0xffffffffu;
    uint32_t highest = 0;
    uint16_t firstPc = 0;
    uint8_t lastValue = 0;
  };
  struct PortStats {
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint16_t firstPc = 0;
    uint8_t lastValue = 0;
  };
  struct LatchStats {
    uint64_t changes = 0;
    // One entry per bit: how often the ROM drove it to 1 and to 0, and the
    // program counter that first did so.  A bit that never changes is either
    // unused or wired to something the emulator has not exercised yet.
    std::array<uint64_t, 8> setCount{};
    std::array<uint64_t, 8> clearCount{};
    std::array<uint16_t, 8> firstPc{};
  };
  struct Event {
    Kind kind = Kind::kPortRead;
    uint16_t pc = 0;
    uint32_t address = 0;
    uint8_t value = 0;
    uint8_t extra = 0;
  };

  static constexpr std::size_t kRingSize = 512;

  void Push(Kind kind, uint16_t pc, uint32_t address, uint8_t value, uint8_t extra);

  bool enabled_ = false;
  std::array<bool, 256> trace_{};
  std::map<uint32_t, PageStats> pages_;
  std::map<uint16_t, PortStats> ports_;
  std::map<uint8_t, LatchStats> latches_;
  std::array<Event, kRingSize> ring_{};
  std::size_t ringNext_ = 0;
  uint64_t ringTotal_ = 0;
};

#endif
