#include "machine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

namespace fs = std::filesystem;

namespace {
constexpr uint8_t kTimer0Vector = 0x04;
constexpr uint8_t kTimer1Vector = 0x06;
// The HD64180 lists its internal sources in a fixed order and the ROM's own
// table at C180 matches it: PRT0 at +4, PRT1 at +6, the clocked serial port
// at +12, where this image puts the IBM PC keyboard handler.
constexpr uint8_t kCsioVector = 0x0c;
constexpr uint8_t kTif0 = 0x40;
constexpr uint8_t kTif1 = 0x80;
constexpr uint8_t kTie0 = 0x10;
constexpr uint8_t kTie1 = 0x20;
constexpr uint8_t kTde0 = 0x01;
constexpr uint8_t kTde1 = 0x02;

uint8_t BcdOrBinary(int value) {
  return static_cast<uint8_t>(value);
}

// One section of the reconstruction filter that follows the DAC.  The machine
// really does have an analogue low pass there -- it is the thing B0h bit 4
// retunes -- so filtering here models hardware rather than prettifying it.
struct Biquad {
  double b0, b1, b2, a1, a2;
};

// One pole, which is what a 1989 portable plausibly had after its DAC: one
// resistor and one capacitor.  Steeper shapes were tried first and were wrong.
// A fourth-order Butterworth strips the 3.4-8 kHz band where /s/ and /ts/ live
// by 5 to 8 dB, and the owner of a real A4 heard it immediately as duller
// sibilants.  A gentle slope also matches the machines varying between units:
// analogue component tolerance moves the corner by tens of percent, which is
// why no two Eurekas sounded quite alike.
Biquad MakeOnePole(double cutoffHz, double sampleHz) {
  const double a = std::exp(-2.0 * 3.14159265358979323846 * cutoffHz / sampleHz);
  return {1.0 - a, 0.0, 0.0, -a, 0.0};
}

// The manual gives no cutoff, so these came from listening tests against the
// unfiltered output; HANDOFF records the measurements.  The open setting keeps
// the same 2:1 ratio as the sampling rates it serves: sound effects run at
// twice the speech rate (chapter 14).
const Biquad kSpeechFilter = MakeOnePole(5000.0, EurekaMachine::kAudioHz);
const Biquad kOpenFilter = MakeOnePole(10000.0, EurekaMachine::kAudioHz);

// The coupling capacitor every audio output stage has, and this one did not.
// The DAC holds its last written value for as long as nothing writes it again,
// and after an utterance that value is wherever the final sample happened to
// land -- measured 111 after the music editor, 131 after the calculator, 135
// after "kde som".  Without a high pass that becomes a permanent DC offset on
// the output, and every interruption of the stream steps between the offset
// and zero.  That step is the irregular clicking reported from use.  A real
// Eureka cannot pass DC to its speaker at all: no amplifier can, or the coil
// would sit with current through it.
//
// Written as a biquad so it shares the state handling above; the coefficients
// are the ordinary one pole y[n] = x[n] - x[n-1] + r*y[n-1], unity gain
// everywhere that matters.
Biquad MakeCoupling(double cutoffHz, double sampleHz) {
  const double r = std::exp(-2.0 * 3.14159265358979323846 * cutoffHz / sampleHz);
  return {1.0, -1.0, 0.0, -r, 0.0};
}

// 30 Hz.  The synthesiser's own fundamental sits near 174 Hz -- voiced runs
// hold a period of 41 to 45 samples at roughly 7.5 kHz -- so this is almost
// three octaves below anything the speech carries and takes 0.1 dB off it.
// Unlike the low pass above there is nothing here to calibrate by ear: the
// value only has to be low enough to leave the voice alone and high enough
// that a step decays promptly, the time constant being 1/(2*pi*f), 5.3 ms.
// Move it down if the speech ever sounds thin, not if something clicks.
const Biquad kCouplingFilter = MakeCoupling(30.0, EurekaMachine::kAudioHz);
}

bool EurekaMachine::LoadRom(const fs::path& path, std::wstring& error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = L"Nemožno otvoriť A4ROM.DMP.";
    return false;
  }
  stream.read(reinterpret_cast<char*>(memory_.data()), kRomSize);
  if (stream.gcount() != static_cast<std::streamsize>(kRomSize) || stream.peek() != EOF) {
    error = L"A4ROM.DMP musí mať presne 262144 bajtov.";
    return false;
  }
  romLoaded_ = true;
  return true;
}

bool EurekaMachine::MountDisk(const fs::path& folder, std::wstring& error) {
  return disk_.Mount(folder, error);
}

bool EurekaMachine::DiskSettled() const {
  if (!disk_.dirty()) return false;
  if (fdcWriting_) return false;  // a sector or track is still being fed in
  // One second of guest time with no write.  CP/M finishes a directory update
  // in far less; a host-visible export in the middle of one would be torn.
  return cycles_ - lastDiskWrite_ >= kCpuHz;
}

void EurekaMachine::Reset() {
  if (!romLoaded_) return;
  std::fill(memory_.begin() + kRomSize, memory_.end(), 0);
  io_.fill(0);
  rtcRam_.fill(0);
  cbar_ = 0xf0;
  cbr_ = 0;
  bbr_ = 0;
  outputLatch_ = 0;
  dac_ = 0x80;
  cycles_ = 0;
  instructions_ = 0;
  timerAccum_[0] = timerAccum_[1] = 0;
  timerCurrent_[0] = timerCurrent_[1] = 0xffff;
  timerControlRead_[0] = timerControlRead_[1] = false;
  timerPending_[0] = timerPending_[1] = false;
  audioPhase_ = 0;
  audioState_[0] = audioState_[1] = 0.0;
  couplingState_[0] = couplingState_[1] = 0.0;
  audio_.clear();
  keys_.clear();
  firmwareKeys_.clear();
  membraneFrames_.clear();
  membraneState_ = MembraneFrame();
  hardwareInputUntil_ = 0;
  membraneUntil_ = 0;
  membraneMinUntil_ = 0;
  membraneHeldKey_ = 0;
  keyboardInitialized_ = false;
  // A hard reset, which is what this is: RAM cleared above, so the firmware
  // finds no power-down marker in C45Ah and initialises from scratch.
  poweredOff_ = false;
  consoleOutput_.clear();
  speechInput_.clear();
  biosTrack_ = 0;
  biosSector_ = 0;
  biosDma_ = 0x80;
  biosReads_ = 0;
  fdcStatus_ = 0;
  fdcTrack_ = 0;
  fdcSector_ = 1;
  fdcCommand_ = 0;
  fdcBuffer_.clear();
  fdcPosition_ = 0;
  fdcWriting_ = false;
  fdcIntrq_ = false;
  dma1Armed_ = false;
  fdcStepDirection_ = 1;
  fdcFormattedCylinder_ = -1;
  fdcFormattedSide_ = -1;
  lastDiskWrite_ = 0;
  csioRx_.clear();
  csioData_ = 0;
  csioPending_ = false;
  csioReadyAt_ = 0;
  rtcLatched_ = false;
  rtcMask_ = 0;
  rtcCommand_ = 0;
  rtcStatus_ = 0;
  rtcAlarmMatched_ = false;
  rtcEventsPrimed_ = false;
  rtcPrevious_.fill(0);
  rtcNextPoll_ = 0;

  // Hardware reset values used by the ROM while probing serial devices.
  io_[0x04] = 0x02;
  io_[0x05] = 0x02;
  io_[0x0a] = 0;
  io_[0x10] = 0;
  io_[0x0e] = io_[0x0f] = io_[0x16] = io_[0x17] = 0xff;
  z80_init(&cpu_);
  cpu_.read_byte = ReadMemory;
  cpu_.write_byte = WriteMemory;
  cpu_.port_in = ReadPort;
  cpu_.port_out = WritePort;
  cpu_.userdata = this;
}

uint32_t EurekaMachine::PhysicalAddress(uint16_t logical) const {
  const uint16_t bankStart = static_cast<uint16_t>(cbar_ & 0x0f) << 12;
  const uint16_t common1Start = static_cast<uint16_t>(cbar_ & 0xf0) << 8;
  uint32_t physical;
  if (logical < bankStart) physical = logical;
  else if (logical < common1Start) physical = logical + (static_cast<uint32_t>(bbr_) << 12);
  else physical = logical + (static_cast<uint32_t>(cbr_) << 12);
  return physical & kPhysicalMask;
}

uint8_t EurekaMachine::ReadMemory(void* context, uint16_t logical) {
  auto* machine = static_cast<EurekaMachine*>(context);
  return machine->memory_[machine->PhysicalAddress(logical)];
}

void EurekaMachine::WritePhysical(uint32_t physical, uint8_t value, uint16_t pc) {
  if (physical >= kRamBase) {
    memory_[physical] = value;
    return;
  }
  // Anything below kRamBase is ROM or an unmapped hole.  Dropping the write is
  // the safe choice, but dropping it silently hides both firmware bugs and a
  // wrongly placed RAM window, so it is counted when diagnostics are on.
  if (diag_.enabled()) diag_.NoteDroppedWrite(pc, physical, value);
}

void EurekaMachine::WriteMemory(void* context, uint16_t logical, uint8_t value) {
  auto* machine = static_cast<EurekaMachine*>(context);
  machine->WritePhysical(machine->PhysicalAddress(logical), value,
                         machine->cpu_.pc);
}

uint8_t EurekaMachine::Peek(uint16_t logical) const {
  return memory_[PhysicalAddress(logical)];
}

void EurekaMachine::Poke(uint16_t logical, uint8_t value) {
  WriteMemory(this, logical, value);
}

uint16_t EurekaMachine::PeekWord(uint16_t logical) const {
  return Peek(logical) | (static_cast<uint16_t>(Peek(logical + 1)) << 8);
}

// The eight clock registers as the RTC would show them at this instant.  Kept
// apart from SampleRtc because the event poller runs on its own schedule: if
// it went through the latch, a poll landing between the firmware's read of
// 90h and its read of 96h would re-latch the batch half way through a tick.
std::array<uint8_t, 8> EurekaMachine::CurrentRtcRegisters() const {
  const auto now = std::chrono::system_clock::now() +
                   std::chrono::seconds(rtcOffset_);
  std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_s(&local, &seconds);
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();
  // Port order is fixed by the hardware, not by the firmware's own layout:
  // 90h hundredths, 91h hour, 92h minute, 93h second, 94h month, 95h date,
  // 96h year, 97h day of week (IOPORT.LIB, Appendix H).  Reading the ROM
  // alone suggests hour and second are exchanged, because the read loop at
  // 0DFA4 stores the ports into a descending buffer and then swaps 91h with
  // 93h and 94h with 95h at 0DFBB, so its internal buffer runs year, month,
  // date, hour, minute, second, hundredths.  Do not "fix" this order.
  std::array<uint8_t, 8> registers{};
  registers[0] = BcdOrBinary(static_cast<int>((millis / 10) % 100));
  registers[1] = BcdOrBinary(local.tm_hour);
  registers[2] = BcdOrBinary(local.tm_min);
  registers[3] = BcdOrBinary(local.tm_sec);
  registers[4] = BcdOrBinary(local.tm_mon + 1);
  registers[5] = BcdOrBinary(local.tm_mday);
  registers[6] = BcdOrBinary(local.tm_year % 100);
  registers[7] = BcdOrBinary(local.tm_wday);
  return registers;
}

void EurekaMachine::SampleRtc() const {
  rtcRegisters_ = CurrentRtcRegisters();
  rtcLatched_ = true;
}

// True while every alarm register the firmware asked to be compared holds the
// value the clock shows.  A register with bit 7 set is a don't care: the alarm
// writer at 0DA5B stores 80h into the hundredths and the day of week always,
// and into the seconds unless C43Fh says the alarm wants them, so an alarm for
// 7:30 really means "any second of that minute".  The registers are binary and
// never exceed 99, so bit 7 cannot collide with a real value.
bool EurekaMachine::RtcAlarmMatches(const std::array<uint8_t, 8>& now) const {
  for (unsigned index = 0; index < 8; ++index)
    if ((rtcRam_[index] & 0x80) == 0 && rtcRam_[index] != now[index])
      return false;
  return true;
}

// Sets the bits the firmware will find in rtc_status.  Nothing here raises a
// CPU interrupt, and that is not an omission: the RTC's interrupt pin does not
// reach the processor.  The ROM never sets ITE1 or ITE2 -- its one write to
// the ITC at 196BE only clears the TRAP bit -- it contains no IM instruction
// at all, and the INT1 and INT2 entries of its vector table (the image at
// 1D000) both point at the bare EI/RET stub at CC37.  The pin goes to the
// power switch instead, which is why cold boot reads rtc_status at 18000,
// keeps it in 0040h and services an alarm from it at 180C2.  While the machine
// is on the alarm is found by polling: the PRT1 heartbeat falls into
// .service_alarm every tick (1D0FB), and that is the read at CF61 this feeds.
void EurekaMachine::UpdateRtcEvents() {
  if (cycles_ < rtcNextPoll_) return;
  // 5 ms of guest time.  The finest event the chip can raise is a hundredth of
  // a second, so this cannot miss one; the ROM enables none of the periodic
  // bits anyway, only bit 0.
  rtcNextPoll_ = cycles_ + kCpuHz / 200;

  const std::array<uint8_t, 8> now = CurrentRtcRegisters();
  uint8_t events = 0;
  if (rtcEventsPrimed_) {
    if (now[0] != rtcPrevious_[0]) events |= 0x02;            // 1/100 second
    if (now[0] / 10 != rtcPrevious_[0] / 10) events |= 0x04;  // 1/10 second
    if (now[3] != rtcPrevious_[3]) events |= 0x08;            // second
    if (now[2] != rtcPrevious_[2]) events |= 0x10;            // minute
    if (now[1] != rtcPrevious_[1]) events |= 0x20;            // hour
    if (now[5] != rtcPrevious_[5]) events |= 0x40;            // day
  }
  rtcPrevious_ = now;
  rtcEventsPrimed_ = true;

  // The alarm is an edge, not a level: the comparator stays true for the whole
  // minute an alarm without seconds names, so a level would put bit 0 back the
  // instant the heartbeat cleared it by reading the port, and the same alarm
  // would fire again and again until the minute ran out.
  const bool matched = RtcAlarmMatches(now);
  if (matched && !rtcAlarmMatched_) events |= 0x01;
  rtcAlarmMatched_ = matched;

  // Appendix H: a status bit stands for an event "in rtc_mask", and bit 7 says
  // an interrupt happened whatever rtc_command's enable bit says -- so the
  // mask gates the bits and the command register does not.
  const uint8_t fired = static_cast<uint8_t>(events & rtcMask_ & 0x7f);
  if (fired != 0) rtcStatus_ |= static_cast<uint8_t>(fired | 0x80);
}

uint8_t EurekaMachine::ReadRtc(uint16_t port) const {
  const unsigned index = port & 7;
  // Reading rtc_100th latches all the other registers at that instant, which
  // is what stops a batch read from straddling a tick (Appendix H, "Real Time
  // Clock").  The ROM depends on it: 0DFA4 sweeps 90h..96h in one pass, and
  // without latching the seconds could advance halfway through the sweep.
  if (index == 0 || !rtcLatched_) SampleRtc();
  return rtcRegisters_[index];
}

uint8_t EurekaMachine::ReadInputBuffer() const {
  // Bits 0 and 1 are the two analogue comparators, each reporting the DAC
  // output against one measured input; the firmware binary-searches the DAC to
  // read a value.  Bit 6 of the output latch picks the pair: clear selects the
  // internal thermometer and the external voltmeter, set selects the speech
  // rate pot and the battery (Appendix H, vmsel_mask).
  const bool batteryPair = (outputLatch_ & 0x40) != 0;
  const uint8_t vm1Threshold = batteryPair ? 0x80 : 0x64;  // rate pot / thermometer
  const uint8_t vm2Threshold = batteryPair ? 0xdc : 0x80;  // battery / ext meter
  // A set comparator bit means the DAC has risen above the measured input, so
  // for the battery it means "below the reference" -- the disk path writes ADh
  // to the DAC at 19A01 and treats bit 1 as low battery at 19839 and 19A18.
  // Nothing else lives on bit 1: the FDC's INTRQ is not readable here, and
  // reporting it on this bit made every disk command fail as "slaba baterie".
  uint8_t value = 0x2c;  // CTS not asserted, no ring voltage, no modem carrier.
  if (dac_ >= vm1Threshold) value |= 0x01;
  if (dac_ >= vm2Threshold) value |= 0x02;
  return value;
}

namespace {
// How long one emulated press lasts, in milliseconds of guest time.  It has to
// outlive the debounce in KEYSCAN.MAC (two scans 20 ms apart) and stay well
// under the typematic delay the ROM arms at 1D4D6 (4Bh heartbeats = one
// second), or a single tap would repeat.
constexpr uint32_t kPressMs = 120;
// A modifier goes down before the key it modifies: pressed in the same scan,
// D4B0 decodes the braille row first and the cursor key is thrown away.
constexpr uint32_t kModifierMs = 80;
// Every key ends with the rows back at zero for long enough that the ROM sees
// the release; without it two keys in a row look like one held chord.
constexpr uint32_t kReleaseMs = 80;
// How much longer a key stays down each time the host repeats it.  Anything
// above the host's repeat interval (about 33 ms) is enough to keep the key
// continuously down for as long as the user holds it.
constexpr uint32_t kHoldMs = 100;
// Shortest press the ROM is guaranteed to notice: three ticks of the 75 Hz
// heartbeat that drives the scan at 1D1AE.  A host that reports key releases
// cuts a press back to this, so a tap answers without waiting out kPressMs.
constexpr uint32_t kMinPressMs = 40;

// Two codes are the same physical key if they differ only in their modifiers;
// the host may well have let go of Shift before the key it modified.
bool SameKey(uint8_t left, uint8_t right) {
  return ((left ^ right) & 0xcf) == 0;
}

constexpr uint32_t MsToCycles(uint32_t ms) {
  return EurekaMachine::kCpuHz / 1000 * ms;
}
}  // namespace

uint8_t EurekaMachine::ReadMembraneKeyboard(uint8_t port) {
  // The rows advance only on a read of the braille row.  Every full scan in
  // this ROM reads it first (00642, 0AA03, 18042, 1D1AE) and the one that does
  // not (1D208) reads it last, so no scan can see half of one key state and
  // half of the next.  Counting scans instead of guest time cannot work: the
  // tone generator's interrupt at 00642 scans the keyboard at the DAC rate,
  // several thousand times a second, and used to eat a keypress in 3 ms.
  if (port == 0x89 && cycles_ >= membraneUntil_ && !membraneFrames_.empty()) {
    membraneState_ = membraneFrames_.front();
    membraneFrames_.pop_front();
    membraneUntil_ = cycles_ + membraneState_.cycles;
    membraneMinUntil_ = cycles_ + MsToCycles(kMinPressMs);
    if (membraneState_.row0 == 0 && membraneState_.row1 == 0 &&
        membraneState_.row2 == 0)
      membraneHeldKey_ = 0;
  }
  switch (port) {
    case 0x89: return membraneState_.row0;
    case 0x8a: return membraneState_.row1;
    case 0x8c: return membraneState_.row2;
    default: return 0;
  }
}

bool EurekaMachine::MembraneBusy() const {
  return !membraneFrames_.empty() || membraneState_.row0 != 0 ||
         membraneState_.row1 != 0 || membraneState_.row2 != 0;
}

bool EurekaMachine::HardwareInputBusy() const {
  return cycles_ < hardwareInputUntil_ || MembraneBusy() || csioPending_ ||
         !csioRx_.empty();
}

// Keeps the CPU awake for a moment after a key that the machine's own hardware
// delivered.  The firmware answers such a key by starting to speak, but speech
// is driven by the 75 Hz heartbeat, so it needs at least one tick -- 13.3 ms --
// before anything comes out.  Measured: a scan code left the machine running
// 1.9 ms before the console read parked it, which is not one tick, and the
// announcement was silently dropped every single time.  A braille chord hid
// this by holding the key rows down for 200 ms.
//
// Only for keys the firmware itself now owns.  Text that the emulator holds in
// its own queue must NOT extend the window: there the firmware's own console
// read would spin forever waiting for a key it cannot see, and both
// application tests hang.
void EurekaMachine::NoteHardwareInput() {
  hardwareInputUntil_ = cycles_ + kCpuHz / 1000 * 100;
}

// Turns one Eureka key code into the physical presses that produce it.  The
// codes are KB.LIB's: bits 7-6 select the cursor keypad (10) or a function key
// (11), bit 5 is ALT -- which is physically the space bar on the braille
// keyboard -- bit 4 is shift, and the low nibble is the key itself.  For the
// keypad that nibble is the set of arrow keys held down at once, which is why
// Home is up plus left.
void EurekaMachine::PressMembraneKey(uint8_t key) {
  // Windows repeats a held key about thirty times a second, far faster than
  // the machine's own typematic (1D4D6 waits 4Bh heartbeats, then 1D1E9
  // repeats every ten).  Queueing a fresh tap per repeat would build a
  // backlog that keeps scrolling long after the key came up, so a repeat of
  // the key that is still down only keeps it down, and the ROM does the
  // repeating exactly as it would on the real machine.
  if (membraneHeldKey_ != 0 && SameKey(key, membraneHeldKey_)) {
    if (membraneState_.key == membraneHeldKey_) {
      const uint64_t until = cycles_ + MsToCycles(kHoldMs);
      if (until > membraneUntil_) membraneUntil_ = until;
    }
    return;
  }
  NoteHardwareInput();
  const uint8_t kind = key & 0xc0;
  const uint8_t number = key & 0x0f;
  const bool alt = (key & 0x20) != 0;
  const uint8_t shift = (key & 0x10) != 0 ? 0x40 : 0;
  MembraneFrame frame;
  if (kind == 0x80) {
    frame.row0 = alt ? 0x80 : 0;
    frame.row2 = static_cast<uint8_t>(number | shift);
  } else if (kind == 0xc0 && number < 8) {
    frame.row1 = static_cast<uint8_t>(1u << number);
    frame.row2 = shift;
  } else if (kind == 0xc0) {
    // There are only eight function keys.  F9 and F10 are chords of the space
    // bar and braille dots, decoded at 1D541; the shifted forms are chords of
    // their own, so they carry no shift bit.
    switch (key) {
      // The row bits run in Perkins key order, left to right, so bit 0 is dot
      // 3 and bit 2 is dot 1; the ROM indexes its braille tables (D7A0) with
      // the row byte itself, and index 04h there is the letter "a".
      case 0xc8: frame.row0 = 0x84; break;  // space + dot 1    = F9  (MODE)
      case 0xc9: frame.row0 = 0x88; break;  // space + dot 4    = F10 (WHERE)
      case 0xd8: frame.row0 = 0x86; break;  // space + dots 1,2 = Shift+F9
      case 0xd9: frame.row0 = 0x98; break;  // space + dots 4,5 = Shift+F10
      default: return;
    }
  } else {
    return;
  }
  if (alt && kind == 0x80) {
    MembraneFrame space;
    space.row0 = 0x80;
    space.cycles = MsToCycles(kModifierMs);
    membraneFrames_.push_back(space);
  }
  frame.cycles = MsToCycles(kPressMs);
  frame.key = key;
  membraneFrames_.push_back(frame);
  membraneHeldKey_ = key;
  MembraneFrame release;
  release.cycles = MsToCycles(kReleaseMs);
  membraneFrames_.push_back(release);
}

// Presses the six dot keys, and the space bar in bit 7, as one chord.  The
// bits are in the order the keys sit under the fingers, left to right, so
// bit 0 is dot 3 and bit 2 is dot 1: the ROM indexes its translation tables
// with this byte directly (1D79C), which is why nothing here converts dots to
// characters -- that is the machine's job and it does all three tables,
// literary, computer and numeric, on its own.
//
// Unlike PressMembraneKey this keeps no held-key state.  A chord is one
// deliberate act: the host collects the dots while the fingers are down and
// calls once, when they come up, so there is no host repeat to swallow.
//
// Shift is the twentieth key and belongs to the chord, not beside it: the
// decoder reads it off row 8Ch at 1D4F9 and 1D60C, so a shifted dot chord is
// a capital letter (D72D) and shift with the bare space bar is Escape (1D52F).
// Neither is reachable while row 2 stays empty.
void EurekaMachine::PressBraille(uint8_t dots, bool shift) {
  if (dots == 0) return;
  NoteHardwareInput();
  MembraneFrame frame;
  frame.row0 = dots;
  frame.row2 = shift ? 0x40 : 0;
  frame.cycles = MsToCycles(kPressMs);
  membraneFrames_.push_back(frame);
  MembraneFrame release;
  release.cycles = MsToCycles(kReleaseMs);
  membraneFrames_.push_back(release);
}

void EurekaMachine::InjectFirmwareKey() {
  if (!keyboardInitialized_ || firmwareKeys_.empty() || Peek(0xc100) != 0xc3) return;
  const uint8_t readIndex = Peek(0xc679) & 0x1f;
  const uint8_t writeIndex = Peek(0xc67a) & 0x1f;
  // Insert only when the ROM queue is empty. C678 is a key-type result flag,
  // not a busy flag, and legitimately remains set after function keys.
  if (readIndex != writeIndex) return;
  const uint8_t next = static_cast<uint8_t>((writeIndex + 1) & 0x1f);
  if (next == readIndex) return;
  const uint16_t entry = static_cast<uint16_t>(0xc67b + writeIndex * 2);
  Poke(entry, 0);  // ordinary key-down event
  Poke(static_cast<uint16_t>(entry + 1), firmwareKeys_.front());
  Poke(0xc67a, next);
  firmwareKeys_.pop_front();
}

uint8_t EurekaMachine::ReadPort(z80* cpu, uint16_t port) {
  auto* machine = static_cast<EurekaMachine*>(cpu->userdata);
  const uint8_t value = ReadPortInner(cpu, port);
  if (machine->diag_.enabled() &&
      machine->diag_.traces(static_cast<uint8_t>(port)))
    machine->diag_.TracePortRead(cpu->pc, port, value);
  return value;
}

uint8_t EurekaMachine::ReadPortInner(z80* cpu, uint16_t port) {
  auto* machine = static_cast<EurekaMachine*>(cpu->userdata);
  const uint8_t low = static_cast<uint8_t>(port);
  switch (port) {
    case 0x0190: case 0x0191: case 0x0192: case 0x0193:
    case 0x0194: case 0x0195: case 0x0196: case 0x0197:
      return machine->rtcRam_[port & 7];
    case 0x0290: {
      // rtc_status, and reading it clears it (Appendix H).  The firmware
      // depends on that: .service_alarm reads it every heartbeat at CF61 and
      // would otherwise service the same alarm on every tick that follows.
      const uint8_t status = machine->rtcStatus_;
      machine->rtcStatus_ = 0;
      return status;
    }
    // rtc_command is write only, so nothing reads this; answering with the
    // last value written is still better than the clock register that used to
    // sit here, because 291h and 91h are different registers.
    case 0x0291: return machine->rtcCommand_;
    default: break;
  }
  if (low >= 0x90 && low <= 0x97) return machine->ReadRtc(low);
  switch (low) {
    case 0x04: return static_cast<uint8_t>(machine->io_[0x04] | 0x02);
    case 0x05: return static_cast<uint8_t>(machine->io_[0x05] | 0x02);
    case 0x08: return 0;
    case 0x09: return 0;
    case 0x0a: return static_cast<uint8_t>(machine->io_[0x0a] |
                                           (machine->csioPending_ ? 0x80 : 0));
    case 0x0b:
      // Reading the data register takes the byte and clears EF, which is what
      // both the reset handshake at 18847 and the scan code interrupt at
      // 1DD2E rely on to know another byte may come.
      machine->csioPending_ = false;
      return machine->csioData_;
    case 0x0c: return machine->ReadTimerData(0, false);
    case 0x0d: return machine->ReadTimerData(0, true);
    case 0x10:
      machine->timerControlRead_[0] = machine->timerControlRead_[1] = true;
      return machine->io_[0x10];
    case 0x14: return machine->ReadTimerData(1, false);
    case 0x15: return machine->ReadTimerData(1, true);
    case 0x18: return static_cast<uint8_t>((machine->cycles_ / 20) & 0xff);
    case 0x30: return machine->io_[0x30];
    case 0x38: return machine->cbr_;
    case 0x39: return machine->bbr_;
    case 0x3a: return machine->cbar_;
    // The membrane keyboard uses true logic: a set bit means a pressed key.
    case 0x89: case 0x8a: case 0x8c:
      return machine->ReadMembraneKeyboard(low);
    case 0x98: {
      const uint8_t status = machine->fdcStatus_;
      machine->fdcIntrq_ = false;
      return status;
    }
    case 0x99: return machine->fdcTrack_;
    case 0x9a: return machine->fdcSector_;
    case 0x9b: return machine->ReadFdcData();
    case 0xa8: return machine->ReadInputBuffer();
    // pwr_stb: the strobe that cuts the main supply.  IOPORT.LIB calls it R/W
    // and says any access switches the machine off, so the value returned here
    // never matters -- the firmware never looks at it.  It reads the port at
    // 1D144 and then spins on JR $-2 waiting for the power to go, which is why
    // ignoring this used to leave the emulator in a two-instruction loop with
    // interrupts disabled: silent, deaf and burning a core.
    case 0xb8:
      machine->PowerDown();
      return 0xff;
    default:
      if (machine->diag_.enabled())
        machine->diag_.NotePortRead(cpu->pc, port, machine->io_[low]);
      return machine->io_[low];
  }
}

void EurekaMachine::WritePort(z80* cpu, uint16_t port, uint8_t value) {
  auto* machine = static_cast<EurekaMachine*>(cpu->userdata);
  const uint8_t low = static_cast<uint8_t>(port);
  const uint8_t previous = machine->io_[low];
  if (machine->diag_.enabled() && machine->diag_.traces(low))
    machine->diag_.TracePortWrite(cpu->pc, port, value);
  switch (port) {
    case 0x0190: case 0x0191: case 0x0192: case 0x0193:
    case 0x0194: case 0x0195: case 0x0196: case 0x0197:
      machine->rtcRam_[port & 7] = value;
      // Re-read the comparator without firing anything.  The firmware writes
      // the eight alarm registers one at a time (0DA5B), and a stale field
      // half way through that sequence can match by accident; letting that
      // count as the match would eat the rising edge the real alarm needs.
      machine->rtcAlarmMatched_ =
          machine->RtcAlarmMatches(machine->CurrentRtcRegisters());
      return;
    // Neither of these is a clock register.  They used to land in io_[90h] and
    // io_[91h], the same cells the hour and minute counters use, which stayed
    // harmless only for as long as nothing read the mask back.
    case 0x0290: machine->rtcMask_ = value; return;
    case 0x0291: machine->rtcCommand_ = value; return;
    default: break;
  }
  if (low != 0x10) machine->io_[low] = value;
  switch (low) {
    case 0x0a: break;
    case 0x0b:
      // FFh is the Reset command every PC keyboard answers with AAh, "self
      // test passed"; the ROM waits for exactly that at 18858 and gives up
      // after about 180 ms.  A real keyboard takes its time, and answering
      // instantly would be worse than useless: the ROM does a throwaway read
      // of TRDR at 18841 after enabling the receiver, and an answer that
      // early would be swallowed by it.
      if (value == 0xff) {
        // Reset also empties the keyboard's own output buffer, and leaving
        // that out cost a whole evening: the Enter that launched the emulator
        // arrives as a break code before the machine has executed a single
        // instruction, so the queue held 9Ch ahead of the AAh.  The ROM read
        // the 9Ch at 18858, decided no keyboard was attached and switched the
        // receiver off for good -- every key after that fell into silence.
        machine->csioRx_.clear();
        machine->csioPending_ = false;
        machine->csioRx_.push_back(0xaa);
        machine->csioReadyAt_ = machine->cycles_ + kCpuHz / 1000 * 30;
      }
      break;
    case 0x0c: machine->WriteTimerData(0, false, value); break;
    case 0x0d: machine->WriteTimerData(0, true, value); break;
    case 0x10:
      // TIF1/TIF0 are read-only; only the low six control bits are writable.
      machine->io_[0x10] = static_cast<uint8_t>((machine->io_[0x10] & 0xc0) |
                                                (value & 0x3f));
      break;
    case 0x14: machine->WriteTimerData(1, false, value); break;
    case 0x15: machine->WriteTimerData(1, true, value); break;
    case 0x30: {
      uint8_t status = previous;
      if ((value & 0x20) == 0)
        status = static_cast<uint8_t>((status & ~0x80) | (value & 0x80));
      if ((value & 0x10) == 0)
        status = static_cast<uint8_t>((status & ~0x40) | (value & 0x40));
      status = static_cast<uint8_t>((status & 0xc0) | (value & 0x0d));
      machine->io_[0x30] = status;
      if ((status & 0x40) != 0) machine->RunDma0();
      if ((status & 0x80) != 0) machine->MaybeRunDma1();
      break;
    }
    case 0x38: machine->cbr_ = value; break;
    case 0x39: machine->bbr_ = value; break;
    case 0x3a: machine->cbar_ = value; break;
    case 0x80: case 0xa0:
      // Stored in io_ above; the machine does not act on these yet, so record
      // the bit transitions instead of letting them disappear.
      if (machine->diag_.enabled())
        machine->diag_.NoteLatch(cpu->pc, low, previous, value);
      break;
    case 0x88: machine->dac_ = value; break;
    // The strobe answers to a write just as it does to a read.  This ROM only
    // ever reads it (1D144 is the single access in the whole image), but the
    // port is documented R/W and a program loaded from disk may well write it.
    case 0xb8: machine->PowerDown(); break;
    case 0x98: machine->StartFdcCommand(value); break;
    case 0x99: machine->fdcTrack_ = value; break;
    case 0x9a: machine->fdcSector_ = value; break;
    case 0x9b: machine->WriteFdcData(value); break;
    case 0xb0:
      if (machine->diag_.enabled())
        machine->diag_.NoteLatch(cpu->pc, low, machine->outputLatch_, value);
      machine->outputLatch_ = value;
      break;
    default:
      if (machine->diag_.enabled())
        machine->diag_.NotePortWrite(cpu->pc, port, value);
      break;
  }
}

void EurekaMachine::RunDma0() {
  uint32_t source = io_[0x20] | (static_cast<uint32_t>(io_[0x21]) << 8) |
                    (static_cast<uint32_t>(io_[0x22] & 0x0f) << 16);
  uint32_t destination = io_[0x23] | (static_cast<uint32_t>(io_[0x24]) << 8) |
                         (static_cast<uint32_t>(io_[0x25] & 0x0f) << 16);
  uint32_t count = io_[0x26] | (static_cast<uint32_t>(io_[0x27]) << 8);
  if (count == 0) count = 65536;
  for (uint32_t index = 0; index < count; ++index) {
    // Routed through the guarded write so a stray descriptor cannot corrupt
    // the ROM image, which Reset() does not restore.
    WritePhysical((destination + index) & kPhysicalMask,
                  memory_[(source + index) & kPhysicalMask], cpu_.pc);
  }
  io_[0x26] = io_[0x27] = 0;
  io_[0x30] &= ~0x40;
}

bool EurekaMachine::FdcWantsDma() const {
  if (fdcBuffer_.empty() || fdcPosition_ >= fdcBuffer_.size()) return false;
  // Direction has to match, or a read buffer the firmware never drained (it
  // verifies a formatted track by status alone) would look like a controller
  // asking to be fed, and the next Write Track would lose its data.
  const bool toMemory = (io_[0x32] & 0x02) != 0;
  return toMemory != fdcWriting_;
}

void EurekaMachine::MaybeRunDma1() {
  // On real hardware channel 1 is paced by the controller's DRQ, so enabling
  // DE1 before the command has been issued simply parks the channel.  The ROM
  // relies on exactly that order when formatting: 19ED1 enables DMA, 19EE8
  // writes the Write Track command, and 19EF3 then spins on BUSY.  Running the
  // burst at the DSTAT write would push the whole track into a controller that
  // is not yet transferring, and BUSY would never clear.
  if (FdcWantsDma()) {
    RunDma1();
    return;
  }
  dma1Armed_ = true;
}

void EurekaMachine::RunDma1() {
  // Channel 1 is always memory <-> I/O.  28h-2Ah hold the 20-bit memory
  // address, 2Bh-2Ch the I/O address, 2Eh-2Fh the byte count.  DCNTL bit 1
  // selects the direction and bit 0 whether the memory address counts down.
  //
  // The ROM uses this for Write Track: 7000 bytes -- one double-density
  // track -- streamed from a RAM buffer into the floppy data register at 9Bh.
  const uint8_t mode = io_[0x32] & 0x03;
  const bool toMemory = (mode & 0x02) != 0;
  const int32_t step = (mode & 0x01) != 0 ? -1 : 1;
  uint32_t address = io_[0x28] | (static_cast<uint32_t>(io_[0x29]) << 8) |
                     (static_cast<uint32_t>(io_[0x2a] & 0x0f) << 16);
  const uint16_t port = static_cast<uint16_t>(
      io_[0x2b] | (static_cast<uint16_t>(io_[0x2c]) << 8));
  uint32_t count = io_[0x2e] | (static_cast<uint32_t>(io_[0x2f]) << 8);
  if (count == 0) count = 65536;

  // Real transfers are paced by DREQ1; running the whole block at once keeps
  // the model simple and matches how channel 0 already behaves.  Nothing in
  // this ROM observes the intermediate state.
  for (uint32_t index = 0; index < count; ++index) {
    if (toMemory)
      WritePhysical(address & kPhysicalMask, ReadPort(&cpu_, port), cpu_.pc);
    else
      WritePort(&cpu_, port, memory_[address & kPhysicalMask]);
    address = static_cast<uint32_t>(address + step) & kPhysicalMask;
  }

  io_[0x28] = static_cast<uint8_t>(address);
  io_[0x29] = static_cast<uint8_t>(address >> 8);
  io_[0x2a] = static_cast<uint8_t>((io_[0x2a] & 0xf0) | ((address >> 16) & 0x0f));
  io_[0x2e] = io_[0x2f] = 0;
  dma1Armed_ = false;
  io_[0x30] = static_cast<uint8_t>(io_[0x30] & ~0x80);  // DE1 clears at BCR1 = 0
}

uint8_t EurekaMachine::TypeOneStatus() const {
  // After a Type I command the firmware waits for the index pulse to decide
  // whether a disk is actually in the drive: 19828 issues a seek, then polls
  // 98h with TST 02h and reports "neni disk" if it never arrives (19848).
  // A mounted disk is always present, spinning and unprotected here, so the
  // pulse is held asserted rather than timed to a 200ms revolution -- the
  // firmware only ever asks whether it appears, never how often.  With no
  // disk it never appears, which is how the machine says "neni disk".
  uint8_t status = 0;
  if (disk_.present()) status |= 0x02;  // INDEX comes from the hole in the medium
  if (fdcTrack_ == 0) status |= 0x04;   // TRACK 00 is a sensor on the mechanism
  return status;                        // bit 6 (write protect) stays clear
}

void EurekaMachine::StartFdcCommand(uint8_t command) {
  fdcCommand_ = command;
  fdcBuffer_.clear();
  fdcPosition_ = 0;
  fdcWriting_ = false;
  fdcIntrq_ = false;
  const uint8_t type = command & 0xf0;
  if (type == 0x00) {
    fdcTrack_ = 0;
    fdcStepDirection_ = -1;
    fdcStatus_ = TypeOneStatus();
  } else if (type == 0x10) {
    const uint8_t target = io_[0x9b];
    fdcStepDirection_ = target >= fdcTrack_ ? 1 : -1;
    fdcTrack_ = target;
    fdcStatus_ = TypeOneStatus();
  } else if (type >= 0x20 && type <= 0x70) {
    // Step, Step In and Step Out.  The format routine walks the disk with
    // Step In (50h at 19EE8) rather than seeking, so without the update flag
    // being honoured every track is written on top of track 0.
    if (type == 0x40 || type == 0x50) fdcStepDirection_ = 1;
    else if (type == 0x60 || type == 0x70) fdcStepDirection_ = -1;
    if ((command & 0x10) != 0) {  // U: update the track register
      const int stepped = static_cast<int>(fdcTrack_) + fdcStepDirection_;
      fdcTrack_ = static_cast<uint8_t>(std::clamp(stepped, 0, 255));
    }
    fdcStatus_ = TypeOneStatus();
  } else if ((command & 0xe0) == 0x80) {
    fdcBuffer_.resize(512);
    const unsigned side = outputLatch_ & 1;
    // A read finishes on its own: the controller walks the whole sector and
    // drops BUSY even when nobody services DRQ, merely flagging lost data.
    // Holding BUSY until the buffer drains hangs the verify read the format
    // routine issues at 19EF0 with no DMA armed, which spins on BUSY at 19EF3.
    if (disk_.ReadPhysicalSector(fdcTrack_, side, fdcSector_, fdcBuffer_.data())) {
      fdcStatus_ = 0x02;
    } else if (static_cast<int>(fdcTrack_) == fdcFormattedCylinder_ &&
               static_cast<int>(side) == fdcFormattedSide_) {
      // The format routine writes 161 logical tracks, one past the 800K image,
      // and verifies each one it wrote.  A real mechanism steps to cylinder 80
      // and reads back the track it has just laid down, so a read of wherever
      // Write Track last ran has to succeed even outside the image.
      std::fill(fdcBuffer_.begin(), fdcBuffer_.end(), 0xe5);
      fdcStatus_ = 0x02;
    } else {
      // Record Not Found.  The buffer has to go with it: a DMA channel armed
      // for this read would otherwise drain 512 bytes of nothing into RAM.
      fdcBuffer_.clear();
      fdcStatus_ = 0x10;
      fdcIntrq_ = true;
    }
  } else if ((command & 0xe0) == 0xa0) {
    if (!disk_.present()) {
      fdcStatus_ = 0x10;
      fdcIntrq_ = true;
    } else {
      fdcBuffer_.assign(512, 0);
      fdcWriting_ = true;
      fdcStatus_ = 0x03;
    }
  } else if (type == 0xc0) {
    fdcBuffer_ = {fdcTrack_, static_cast<uint8_t>(outputLatch_ & 1),
                  fdcSector_, 2, 0, 0};
    fdcStatus_ = 0x02;  // Read Address completes on its own, as above.
  } else if (type == 0xd0) {
    fdcStatus_ = 0;
  } else if (type == 0xe0) {
    if (!disk_.present()) {
      fdcStatus_ = 0x10;
      fdcIntrq_ = true;
      return;
    }
    fdcBuffer_.resize(5120);
    for (unsigned sector = 1; sector <= 10; ++sector) {
      disk_.ReadPhysicalSector(fdcTrack_, outputLatch_ & 1, sector,
                               fdcBuffer_.data() + (sector - 1) * 512);
    }
    fdcStatus_ = 0x02;  // Read Track completes on its own, as above.
  } else if (type == 0xf0) {
    if (!disk_.present()) {
      fdcStatus_ = 0x10;
      fdcIntrq_ = true;
      return;
    }
    fdcBuffer_.assign(6250, 0);
    fdcWriting_ = true;
    fdcFormattedCylinder_ = fdcTrack_;
    fdcFormattedSide_ = static_cast<int>(outputLatch_ & 1);
    // The track image itself is discarded: the disk is a host folder, and
    // erasing the user's files because the emulated machine formatted is not
    // this model's call to make.  The operation still has to report success.
    fdcStatus_ = 0x03;
  } else {
    fdcStatus_ = 0;
  }
  // Type I commands and Force Interrupt finish inside this model, so they
  // raise INTRQ straight away.  Type II and III commands raise it when their
  // data transfer runs out, in ReadFdcData and WriteFdcData below.
  if ((command & 0x80) == 0 || type == 0xd0) fdcIntrq_ = true;
  // A channel parked by an earlier DE1 write starts moving now.
  if (dma1Armed_ && FdcWantsDma()) {
    dma1Armed_ = false;
    RunDma1();
  }
}

uint8_t EurekaMachine::ReadFdcData() {
  if (fdcPosition_ >= fdcBuffer_.size()) {
    fdcStatus_ = 0;
    return 0;
  }
  const uint8_t value = fdcBuffer_[fdcPosition_++];
  if (fdcPosition_ == fdcBuffer_.size()) {
    fdcStatus_ = 0;
    fdcIntrq_ = true;
  }
  return value;
}

void EurekaMachine::WriteFdcData(uint8_t value) {
  io_[0x9b] = value;
  if (!fdcWriting_ || fdcPosition_ >= fdcBuffer_.size()) return;
  fdcBuffer_[fdcPosition_++] = value;
  if (fdcPosition_ == fdcBuffer_.size()) {
    if ((fdcCommand_ & 0xe0) == 0xa0) {
      disk_.WritePhysicalSector(fdcTrack_, outputLatch_ & 1, fdcSector_,
                                fdcBuffer_.data());
      lastDiskWrite_ = cycles_;
    }
    fdcStatus_ = 0;
    fdcWriting_ = false;
    fdcIntrq_ = true;
  }
}

uint16_t EurekaMachine::TimerReload(unsigned channel) const {
  const unsigned base = channel == 0 ? 0x0e : 0x16;
  return io_[base] | (static_cast<uint16_t>(io_[base + 1]) << 8);
}

uint8_t EurekaMachine::ReadTimerData(unsigned channel, bool high) {
  if (timerControlRead_[channel]) {
    const uint8_t flag = channel == 0 ? kTif0 : kTif1;
    io_[0x10] &= static_cast<uint8_t>(~flag);
    timerPending_[channel] = false;
    timerControlRead_[channel] = false;
  }
  return static_cast<uint8_t>(high ? timerCurrent_[channel] >> 8
                                   : timerCurrent_[channel]);
}

void EurekaMachine::WriteTimerData(unsigned channel, bool high, uint8_t value) {
  const uint8_t enable = channel == 0 ? kTde0 : kTde1;
  if ((io_[0x10] & enable) != 0) return;
  if (high)
    timerCurrent_[channel] = static_cast<uint16_t>((timerCurrent_[channel] & 0x00ff) |
                                                   (value << 8));
  else
    timerCurrent_[channel] = static_cast<uint16_t>((timerCurrent_[channel] & 0xff00) | value);
}

void EurekaMachine::RenderAudio(uint32_t cpuCycles) {
  audioPhase_ += static_cast<uint64_t>(cpuCycles) * kAudioHz;
  if (audioPhase_ < kCpuHz) return;

  // The DAC holds its value between writes, so sampling it is a zero order
  // hold and the images that produces are real -- they exist on the hardware
  // too.  What the hardware also has, and this did not, is the analogue low
  // pass that removes them.  filtersel_mask (B0h bit 4) picks its cutoff:
  // set is the normal one the speech engine wants, clear opens it up.
  const Biquad& b = (outputLatch_ & 0x10) != 0 ? kSpeechFilter : kOpenFilter;
  const double input = (static_cast<double>(dac_) - 128.0) * 256.0;
  while (audioPhase_ >= kCpuHz) {
    // Direct form II transposed: the state carries over when the cutoff
    // switches, so retuning the filter mid-utterance does not click.
    const double out = b.b0 * input + audioState_[0];
    audioState_[0] = b.b1 * input - b.a1 * out + audioState_[1];
    audioState_[1] = b.b2 * input - b.a2 * out;
    // Then the coupling capacitor, which is what keeps the resting value of
    // the DAC from becoming a standing offset on the output.
    const Biquad& c = kCouplingFilter;
    const double coupled = c.b0 * out + couplingState_[0];
    couplingState_[0] = c.b1 * out - c.a1 * coupled + couplingState_[1];
    couplingState_[1] = c.b2 * out - c.a2 * coupled;
    audio_.push_back(
        static_cast<int16_t>(std::clamp(coupled, -32768.0, 32767.0)));
    audioPhase_ -= kCpuHz;
  }
}

// Hands the next byte from the keyboard to the serial port when the receiver
// is enabled and the previous one has been collected.  Doing it here rather
// than at the moment a key is queued keeps the port's own timing: the ROM
// polls EF during the reset handshake (18847) but takes scan codes on the
// interrupt (vector C18C -> CD62 -> DD2E), and both need the byte to appear
// while the receiver is on, not before.
void EurekaMachine::PumpCsio() {
  if (csioPending_ || csioRx_.empty() || cycles_ < csioReadyAt_) return;
  if ((io_[0x0a] & 0x20) == 0) return;  // RE clear: the receiver is off
  csioData_ = csioRx_.front();
  csioRx_.pop_front();
  csioPending_ = true;
  // A completed receive clears RE on the 64180; the firmware has to arm the
  // port again for every single byte, which is exactly what it does at 1E012.
  // Leaving RE set costs the arrows: their scan codes are two bytes, E0 then
  // the key, and the interrupt handler's throwaway read of TRDR at 1DFFE would
  // swallow the second one before the handler ever came round to it.  Single
  // byte keys never noticed, so letters worked and navigation did not.
  io_[0x0a] &= static_cast<uint8_t>(~0x20);
}

void EurekaMachine::Advance(uint32_t cpuCycles) {
  cycles_ += cpuCycles;
  RenderAudio(cpuCycles);
  PumpCsio();
  UpdateRtcEvents();
  const uint8_t control = io_[0x10];
  for (unsigned channel = 0; channel < 2; ++channel) {
    const uint8_t enable = channel == 0 ? kTde0 : kTde1;
    if ((control & enable) == 0) {
      timerAccum_[channel] = 0;
      continue;
    }
    timerAccum_[channel] += cpuCycles;
    while (timerAccum_[channel] >= 20) {
      timerAccum_[channel] -= 20;
      if (timerCurrent_[channel] == 0) timerCurrent_[channel] = TimerReload(channel);
      else --timerCurrent_[channel];
      if (timerCurrent_[channel] == 0) {
        timerPending_[channel] = true;
        io_[0x10] |= channel == 0 ? kTif0 : kTif1;
      }
    }
  }
}

void EurekaMachine::ScheduleInterrupt() {
  if (cpu_.int_pending || !cpu_.iff1) return;
  const uint8_t control = io_[0x10];
  if (timerPending_[0] && (control & kTie0)) {
    // HD64180 internal interrupts always use I/IL vectored acquisition,
    // independently of the Z80 IM setting.
    cpu_.interrupt_mode = 2;
    z80_gen_int(&cpu_, static_cast<uint8_t>((io_[0x33] & 0xe0) | kTimer0Vector));
  } else if (timerPending_[1] && (control & kTie1)) {
    cpu_.interrupt_mode = 2;
    z80_gen_int(&cpu_, static_cast<uint8_t>((io_[0x33] & 0xe0) | kTimer1Vector));
  } else if (csioPending_ && (io_[0x0a] & 0x40)) {
    // Lowest of the three, which is the HD64180's own order.  The flag stays
    // up until the handler reads TRDR, so this re-arms by itself.
    cpu_.interrupt_mode = 2;
    z80_gen_int(&cpu_, static_cast<uint8_t>((io_[0x33] & 0xe0) | kCsioVector));
  }
}

void EurekaMachine::ReturnFromCall() {
  cpu_.pc = PeekWord(cpu_.sp);
  cpu_.sp += 2;
}

bool EurekaMachine::InterceptBios() {
  // Eureka's extended CP/M BIOS jump table lives at C100. Page zero points
  // at BOOT/WBOOT targets, not at the beginning of this table.
  constexpr uint16_t base = 0xc100;
  if (Peek(base) != 0xc3) return true;
  unsigned function = 0;
  // Every C100 entry is a JP into the matching C03A stub, so a call that comes
  // through the table passes both points.  Functions answered with
  // ReturnFromCall never reach the stub, but one that lets the ROM run does.
  bool viaJumpTable = false;
  if (cpu_.pc >= base && cpu_.pc < static_cast<uint16_t>(base + 23 * 3) &&
      (cpu_.pc - base) % 3 == 0) {
    function = (cpu_.pc - base) / 3;
    viaJumpTable = true;
  } else {
    // ROM applications also call the first-level BIOS stubs directly.
    constexpr uint16_t targets = 0xc03a;
    if (cpu_.pc < targets || cpu_.pc >= static_cast<uint16_t>(targets + 23 * 3) ||
        (cpu_.pc - targets) % 3 != 0) return true;
    function = (cpu_.pc - targets) / 3;
  }
  const uint16_t bc = (static_cast<uint16_t>(cpu_.b) << 8) | cpu_.c;
  switch (function) {
    case 2:  // console status
      // While a key is on its way through the keyboard ports the ROM's own
      // queue is the one that knows about it, so let the ROM answer.
      if (HardwareInputBusy()) return true;
      cpu_.a = keys_.empty() ? 0 : 0xff;
      ReturnFromCall();
      return true;
    case 3:  // console input
      if (HardwareInputBusy()) {
              return true;
      }
      // Answer only what the host actually typed.  While a key is on its way
      // in through real hardware, or when there is nothing queued at all, the
      // ROM is left to wait for the key itself.  It does that by spinning in
      // its own event dispatcher (19B41), which is what the real machine does,
      // and it is the only way it ever gets to read the input it produces for
      // itself: a function key types its text into the ROM's own queue
      // (fk_table, SYSRAM.A), and answering the read from here starved every
      // one of them.  The same trap already caught scan codes once -- see
      // HardwareInputBusy.
      if (keys_.empty()) return true;
      cpu_.a = keys_.front();
      keys_.pop_front();
      // H bit 0 distinguishes Eureka function/cursor key codes from ordinary
      // text in the ROM console ABI.
      cpu_.h = (cpu_.a & 0x80) != 0 ? 1 : 0;
      keyboardInitialized_ = true;
          ReturnFromCall();
      return true;
    case 4:  // console output: capture, then let the ROM feed screen and speech.
      // Captured at the stub only.  Taking it here as well doubled every
      // character on the host console -- "hotovo" arrived as "hhoottoovvoo".
      if (!viaJumpTable) consoleOutput_.push_back(cpu_.c);
      return true;
    case 8:
      biosTrack_ = 0;
      ReturnFromCall();
      return true;
    case 10:
      biosTrack_ = bc;
      ReturnFromCall();
      return true;
    case 11:
      biosSector_ = bc;
      ReturnFromCall();
      return true;
    case 12:
      biosDma_ = bc;
      ReturnFromCall();
      return true;
    case 13: {
      ++biosReads_;
      std::array<uint8_t, VirtualDisk::kRecordSize> record{};
      const bool ok = disk_.ReadRecord(biosTrack_, biosSector_, record.data());
      if (ok) for (unsigned i = 0; i < record.size(); ++i) Poke(biosDma_ + i, record[i]);
      cpu_.a = ok ? 0 : 1;
      ReturnFromCall();
      return true;
    }
    case 14: {
      std::array<uint8_t, VirtualDisk::kRecordSize> record{};
      for (unsigned i = 0; i < record.size(); ++i) record[i] = Peek(biosDma_ + i);
      cpu_.a = disk_.WriteRecord(biosTrack_, biosSector_, record.data()) ? 0 : 1;
      lastDiskWrite_ = cycles_;
      ReturnFromCall();
      return true;
    }
    case 16:
      cpu_.h = cpu_.b;
      cpu_.l = cpu_.c;
      ReturnFromCall();
      return true;
    default:
      return true;
  }
}

// Switches the machine off the way the strobe does on the hardware: the CPU
// stops mid-instruction and stays stopped.  RAM and the real time clock are
// deliberately left alone -- on the real Eureka their supply is separate and
// never cut (GLOSSARY.TXT), which is the whole reason the firmware can resume
// where the user was.  Everything the host has to do about it -- draining the
// sound, writing the disk back, one day saving the RAM -- belongs to the
// caller, because only it knows whether the machine is being emulated
// interactively or driven by a test.
void EurekaMachine::PowerDown() {
  if (poweredOff_) return;
  poweredOff_ = true;
  // The DAC holds whatever the last sample was, and with the CPU stopped
  // nothing will ever move it again.  Parking it at mid-scale keeps the tail
  // of the announcement from ending on a step to a DC offset.
  dac_ = 0x80;
  membraneFrames_.clear();
  membraneState_ = MembraneFrame{};
  membraneHeldKey_ = 0;
}

bool EurekaMachine::Step() {
  if (poweredOff_) return false;
  InjectFirmwareKey();
  if (!InterceptBios()) return false;
  if (cpu_.pc == 0x0103 && PhysicalAddress(cpu_.pc) == 0x0103)
    speechInput_.push_back(cpu_.a);
  ScheduleInterrupt();
  const unsigned long before = cpu_.cyc;
  z80_step(&cpu_);
  const uint32_t elapsed = static_cast<uint32_t>(cpu_.cyc - before);
  Advance(elapsed);
  ++instructions_;
  return true;
}

void EurekaMachine::QueueKey(uint8_t key) {
  // Eureka key codes with bit 7 set describe chords on the built-in 20-key
  // keyboard, and the ROM does far more with them than hand them to the
  // program: it decodes them in its heartbeat, and MODE, WHERE and the
  // application keys act wherever they are pressed.  Handing them to a
  // blocked BIOS console read instead made them dead inside BASIC and the
  // music editor, so they always go on the real keyboard ports.
  if ((key & 0x80) != 0) {
    PressMembraneKey(key);
  } else if (keyboardInitialized_) {
    // Printable keys from a Windows keyboard enter the same ROM-owned queue
    // as characters decoded from the optional IBM PC/XT keyboard interface.
    firmwareKeys_.push_back(key);
  } else {
    keys_.push_back(key);
  }
}

void EurekaMachine::ReleaseKey(uint8_t key) {
  if ((key & 0x80) == 0 || membraneHeldKey_ == 0 ||
      !SameKey(key, membraneHeldKey_))
    return;
  if (membraneState_.key == membraneHeldKey_) {
    // The press is on the ports now: let it stand until the ROM has certainly
    // scanned it, then let the release frame follow.
    const uint64_t earliest = std::max(cycles_, membraneMinUntil_);
    if (earliest < membraneUntil_) membraneUntil_ = earliest;
  } else {
    // Released before the first scan even reached it, which happens with a
    // fast tap; shorten the press instead of dropping it.
    for (MembraneFrame& frame : membraneFrames_)
      if (frame.key == membraneHeldKey_) frame.cycles = MsToCycles(kMinPressMs);
  }
  membraneHeldKey_ = 0;
}

void EurekaMachine::QueueScanCode(uint8_t code) {
  NoteHardwareInput();
  csioRx_.push_back(code);
}

void EurekaMachine::QueueText(const std::string& ascii) {
  for (unsigned char ch : ascii) QueueKey(ch);
}

std::vector<uint8_t> EurekaMachine::TakeConsoleOutput() {
  std::vector<uint8_t> output;
  output.swap(consoleOutput_);
  return output;
}

std::vector<uint8_t> EurekaMachine::TakeSpeechInput() {
  std::vector<uint8_t> output;
  output.swap(speechInput_);
  return output;
}

std::vector<int16_t> EurekaMachine::TakeAudio() {
  std::vector<int16_t> output;
  output.swap(audio_);
  return output;
}
