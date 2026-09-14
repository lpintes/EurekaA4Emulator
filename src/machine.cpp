#include "machine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>

#include "eureka_io.h"
#include "md5.h"

namespace fs = std::filesystem;

namespace {
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
  BuildKeyboardLayout();
  return true;
}

bool EurekaMachine::MountDisk(const fs::path& folder, std::wstring& error,
                              bool* tooBig) {
  if (!disk_.Mount(folder, error, tooBig)) return false;
  ForgetFormattedTrack();
  return true;
}

void EurekaMachine::EjectDisk() {
  disk_.Eject();
  ForgetFormattedTrack();
}

// The track the format routine last laid down is a property of the medium that
// was in the drive, so a new one must not inherit it: it makes reads succeed
// one cylinder past the image (see StartFdcCommand), and on a diskette that
// never was formatted here that would be a sector conjured out of nothing.
void EurekaMachine::ForgetFormattedTrack() {
  fdcFormattedCylinder_ = -1;
  fdcFormattedSide_ = -1;
}

bool EurekaMachine::DiskSettled() const {
  if (!disk_.dirty()) return false;
  if (fdcWriting_) return false;  // a sector or track is still being fed in
  // One second of guest time with no write.  CP/M finishes a directory update
  // in far less; a host-visible export in the middle of one would be torn.
  return cycles_ - lastDiskWrite_ >= kCpuHz;
}

// The host may take the diskette away only between two whole sector
// transfers, and only once anything the guest wrote has reached the folder.
// The guest itself needs no warning: EurekaDOS re-logs the drive from the
// directory checksums on its own (HANDOFF 6.17), which is why there is no
// "disk changed" line anywhere on this hardware to raise.
bool EurekaMachine::DiskSwappable() const {
  if (fdcWriting_) return false;
  return !disk_.dirty() || DiskSettled();
}

void EurekaMachine::Reset() {
  if (!romLoaded_) return;
  // A cold start, which on the hardware is the power cut-off switch: the RAM
  // goes, the clock chip's own eight bytes go with it -- INSTALL.2 promises
  // both, "all memory and the Real Time Clock will be cleared when the machine
  // is next switched on" -- and the cycle counter starts from zero, so the
  // firmware finds no magic 55AAh at C45Bh, wipes C43Ch-C508h at 1805E and
  // says "inicializace eureky" (18132).  Everything else the machine needs put
  // back is in PowerOn, which this shares with a warm start.
  std::fill(memory_.begin() + kRomSize, memory_.end(), 0);
  rtcRam_.fill(0);
  cycles_ = 0;
  instructions_ = 0;
  PowerOn();
}

void EurekaMachine::PowerOn() {
  if (!romLoaded_) return;
  io_.fill(0);
  cbar_ = hw::kCbarReset;
  cbr_ = 0;
  bbr_ = 0;
  outputLatch_ = 0;
  dac_ = hw::kDacMidScale;
  timerAccum_[0] = timerAccum_[1] = 0;
  timerCurrent_[0] = timerCurrent_[1] = 0xffff;
  timerControlRead_[0] = timerControlRead_[1] = false;
  timerPending_[0] = timerPending_[1] = false;
  audioPhase_ = 0;
  audioState_[0] = audioState_[1] = 0.0;
  couplingState_[0] = couplingState_[1] = 0.0;
  audio_.clear();
  membraneFrames_.clear();
  membraneState_ = MembraneFrame();
  membraneUntil_ = 0;
  membraneMinUntil_ = 0;
  membraneHeldKey_ = 0;
  membraneShift_ = false;
  holdQueue_.clear();
  holdState_ = MembraneHold();
  holdUntil_ = 0;
  poweredOff_ = false;
  consoleOutput_.clear();
  speechInput_.clear();
  biosTrack_ = 0;
  biosSector_ = 0;
  biosDma_ = 0x80;  // CP/M default DMA address, not a hardware constant.
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
  // rtc_mask and rtc_command are NOT reset here: they live in the clock chip,
  // on the supply that never cuts (GLOSSARY.TXT), same as rtcRam_.  A hand
  // switch-on never needed them cleared -- it finds rtc_status clear and calls
  // schedule_alarm at 180D2, which writes both.  A boot woken by the alarm
  // calls service_alarm1 at 180C8 first, and measured 11.9.2026, zeroing them
  // here left an alarm answered by a key disarmed for good (rtc_mask 00, date
  // not moved on).  integration_test budik pins it (HANDOFF 6.32).
  rtcStatus_ = 0;
  rtcAlarmMatched_ = false;
  rtcEventsPrimed_ = false;
  rtcPrevious_.fill(0);
  rtcNextPoll_ = 0;

  // Hardware reset values used by the ROM while probing serial devices.
  io_[hw::kStat0] = hw::kStatTdre;
  io_[hw::kStat1] = hw::kStatTdre;
  io_[hw::kCntr] = 0;
  io_[hw::kTcr] = 0;
  io_[hw::kRldr0l] = io_[hw::kRldr0h] = io_[hw::kRldr1l] = io_[hw::kRldr1h] = 0xff;
  z80_init(&cpu_);
  cpu_.read_byte = ReadMemory;
  cpu_.write_byte = WriteMemory;
  cpu_.port_in = CpuReadPort;
  cpu_.port_out = CpuWritePort;
  cpu_.userdata = this;
}

bool EurekaMachine::WakeOnAlarm() {
  if (!poweredOff_) return false;
  if ((rtcMask_ & hw::kRtcEventAlarm) == 0) return false;
  const std::array<uint8_t, 8> now = CurrentRtcRegisters();
  const bool matched = RtcAlarmMatches(now);
  const bool edge = matched && !rtcAlarmMatched_;
  rtcAlarmMatched_ = matched;
  if (!edge) return false;
  PowerOn();
  // PowerOn() just zeroed rtc_status so a hand-switched boot always takes the
  // "no alarm pending" branch at 180C2; this is the one caller that has to
  // put the bit back afterwards, so the boot this time finds it and serves
  // the alarm instead.
  rtcStatus_ = static_cast<uint8_t>(hw::kRtcEventAlarm | hw::kRtcInterrupted);
  rtcAlarmMatched_ = true;
  return true;
}

void EurekaMachine::CopyStateFrom(const EurekaMachine& other) {
  if (this == &other) return;
  *this = other;
  // The copy brought the other machine's userdata with it, and it points at
  // the other machine.  Left alone, every memory and port callback would run
  // against the machine we copied from.
  cpu_.userdata = this;
}

namespace {
// The magic carries the format version: a layout change bumps the digits and
// an older file simply reads back as kCorrupt.  Layout after it: 16 bytes ROM
// MD5, then the 8 clock bytes, then kRamSnapshotBytes of RAM.
constexpr char kSnapshotMagic[8] = {'E', 'A', '4', 'R', 'A', 'M', '0', '1'};
constexpr std::size_t kSnapshotHeaderSize =
    sizeof(kSnapshotMagic) + 16 + 8;
}  // namespace

bool EurekaMachine::SaveSnapshot(const fs::path& path,
                                 std::wstring& error) const {
  if (!romLoaded_) {
    error = L"Snímku RAM nemožno zapísať: nie je načítaná ROM.";
    return false;
  }
  const std::array<uint8_t, 16> romMd5 = Md5(memory_.data(), kRomSize);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (out) {
    out.write(kSnapshotMagic, sizeof(kSnapshotMagic));
    out.write(reinterpret_cast<const char*>(romMd5.data()), romMd5.size());
    out.write(reinterpret_cast<const char*>(rtcRam_.data()), rtcRam_.size());
    out.write(reinterpret_cast<const char*>(memory_.data() + kRamBase),
              kRamSnapshotBytes);
  }
  if (!out) {
    error = L"Snímku RAM sa nepodarilo zapísať do súboru\r\n" +
            path.wstring() +
            L"\r\n\r\nStav tohto behu sa pri ďalšom štarte neobnoví.";
    return false;
  }
  return true;
}

EurekaMachine::SnapshotResult EurekaMachine::LoadSnapshot(const fs::path& path,
                                                          std::wstring& error) {
  if (!romLoaded_) {
    error = L"Snímku RAM nemožno načítať: nie je načítaná ROM.";
    return SnapshotResult::kCorrupt;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) return SnapshotResult::kMissing;

  std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  if (bytes.size() != kSnapshotHeaderSize + kRamSnapshotBytes ||
      std::memcmp(bytes.data(), kSnapshotMagic, sizeof(kSnapshotMagic)) != 0) {
    error = L"Snímka RAM je poškodená alebo je z inej verzie emulátora.";
    return SnapshotResult::kCorrupt;
  }

  const std::array<uint8_t, 16> romMd5 = Md5(memory_.data(), kRomSize);
  if (std::memcmp(bytes.data() + sizeof(kSnapshotMagic), romMd5.data(),
                  romMd5.size()) != 0) {
    error = L"Snímka RAM bola uložená pod inou ROM.";
    return SnapshotResult::kRomMismatch;
  }

  const char* clock = bytes.data() + sizeof(kSnapshotMagic) + 16;
  std::memcpy(rtcRam_.data(), clock, rtcRam_.size());
  std::memcpy(memory_.data() + kRamBase, clock + rtcRam_.size(),
              kRamSnapshotBytes);
  return SnapshotResult::kOk;
}

uint32_t EurekaMachine::PhysicalAddress(uint16_t logical) const {
  const uint16_t bankStart = static_cast<uint16_t>(cbar_ & hw::kCbarBankMask) << 12;
  const uint16_t common1Start =
      static_cast<uint16_t>(cbar_ & hw::kCbarCommon1Mask) << 8;
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
  for (unsigned index = 0; index < hw::kRtcRegisters; ++index)
    if ((rtcRam_[index] & hw::kRtcAlarmIgnore) == 0 && rtcRam_[index] != now[index])
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
    const unsigned hundredths = hw::kRtcHundredths;
    if (now[hundredths] != rtcPrevious_[hundredths])
      events |= hw::kRtcEventHundredth;
    if (now[hundredths] / 10 != rtcPrevious_[hundredths] / 10)
      events |= hw::kRtcEventTenth;
    if (now[hw::kRtcSecond] != rtcPrevious_[hw::kRtcSecond])
      events |= hw::kRtcEventSecond;
    if (now[hw::kRtcMinute] != rtcPrevious_[hw::kRtcMinute])
      events |= hw::kRtcEventMinute;
    if (now[hw::kRtcHour] != rtcPrevious_[hw::kRtcHour])
      events |= hw::kRtcEventHour;
    if (now[hw::kRtcDate] != rtcPrevious_[hw::kRtcDate])
      events |= hw::kRtcEventDay;
  }
  rtcPrevious_ = now;
  rtcEventsPrimed_ = true;

  // The alarm is an edge, not a level: the comparator stays true for the whole
  // minute an alarm without seconds names, so a level would put bit 0 back the
  // instant the heartbeat cleared it by reading the port, and the same alarm
  // would fire again and again until the minute ran out.
  const bool matched = RtcAlarmMatches(now);
  if (matched && !rtcAlarmMatched_) events |= hw::kRtcEventAlarm;
  rtcAlarmMatched_ = matched;

  // Appendix H: a status bit stands for an event "in rtc_mask", and bit 7 says
  // an interrupt happened whatever rtc_command's enable bit says -- so the
  // mask gates the bits and the command register does not.
  const uint8_t fired =
      static_cast<uint8_t>(events & rtcMask_ & hw::kRtcEventMask);
  if (fired != 0)
    rtcStatus_ |= static_cast<uint8_t>(fired | hw::kRtcInterrupted);
}

uint8_t EurekaMachine::ReadRtc(uint16_t port) const {
  const unsigned index = port & (hw::kRtcRegisters - 1);
  // Reading rtc_100th latches all the other registers at that instant, which
  // is what stops a batch read from straddling a tick (Appendix H, "Real Time
  // Clock").  The ROM depends on it: 0DFA4 sweeps 90h..96h in one pass, and
  // without latching the seconds could advance halfway through the sweep.
  if (index == hw::kRtcHundredths || !rtcLatched_) SampleRtc();
  return rtcRegisters_[index];
}

uint8_t EurekaMachine::ReadInputBuffer() const {
  // Bits 0 and 1 are the two analogue comparators, each reporting the DAC
  // output against one measured input; the firmware binary-searches the DAC to
  // read a value.  Bit 6 of the output latch picks the pair: clear selects the
  // internal thermometer and the external voltmeter, set selects the speech
  // rate pot and the battery (Appendix H, vmsel_mask).
  // What the other three inputs sit at, on the DAC's own scale.  These are
  // not documented anywhere: they are the values that make the firmware report
  // a sane room temperature and a charged battery.  The fourth is the rate
  // slider, and that one is the user's (SetRatePot).
  constexpr uint8_t kThermometerLevel = 0x64;
  constexpr uint8_t kExternalMeterLevel = 0x80;
  constexpr uint8_t kBatteryLevel = 0xdc;
  const bool batteryPair = (outputLatch_ & hw::kVmselMask) != 0;
  const uint8_t vm1Threshold = batteryPair ? ratePot_ : kThermometerLevel;
  const uint8_t vm2Threshold = batteryPair ? kBatteryLevel : kExternalMeterLevel;
  // A set comparator bit means the DAC has risen above the measured input, so
  // for the battery it means "below the reference" -- the disk path writes ADh
  // to the DAC at 19A01 and treats bit 1 as low battery at 19839 and 19A18.
  // Nothing else lives on bit 1: the FDC's INTRQ is not readable here, and
  // reporting it on this bit made every disk command fail as "slaba baterie".
  // Those three are active low, so leaving them set is the idle state.
  uint8_t value = hw::kCts1Mask | hw::kRingMask | hw::kDcd0Mask;
  if (dac_ >= vm1Threshold) value |= hw::kVm1Mask;
  if (dac_ >= vm2Threshold) value |= hw::kVm2Mask;
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
// Minimum dwell time for one state of HoldMembrane's live layer, so two
// finger movements inside one scan cannot pass the ROM a state it has no
// chance of seeing -- the same debounce kMinPressMs gives a scripted tap.
// TODO(ea4-v1j step 3): measured, not guessed yet; starts at kMinPressMs on
// the strength of the same three-heartbeat-tick argument, but the owner's
// partial-chord measurements (dot 1 -> "a", +2 -> "b", +4 -> "f") are what
// this has to survive, and that has not been run against the model yet.
constexpr uint32_t kHoldStepMs = kMinPressMs;

// Two codes are the same physical key if they differ only in their modifiers;
// the host may well have let go of Shift before the key it modified.
bool SameKey(uint8_t left, uint8_t right) {
  return ((left ^ right) & hw::kKeyIdentityMask) == 0;
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
  if (port == hw::kBkbDots && cycles_ >= membraneUntil_ &&
      !membraneFrames_.empty()) {
    membraneState_ = membraneFrames_.front();
    membraneFrames_.pop_front();
    membraneUntil_ = cycles_ + membraneState_.cycles;
    membraneMinUntil_ = cycles_ + MsToCycles(kMinPressMs);
    if (membraneState_.row0 == 0 && membraneState_.row1 == 0 &&
        membraneState_.row2 == 0)
      membraneHeldKey_ = 0;
  }
  // The live layer advances through the same gate, on the same read, but on
  // its own clock: HoldMembrane's states are not a sequence being played out,
  // they are fingers landing and lifting whenever they please, so nothing
  // here waits for the frame queue and nothing in the frame queue waits for
  // this.  The last state queued is left on the ports -- unlike a frame nothing
  // ever pops it off on its own, because nobody presses "release" on a key
  // that is simply not held any more; the next HoldMembrane call says so.
  if (port == hw::kBkbDots && cycles_ >= holdUntil_ && !holdQueue_.empty()) {
    holdState_ = holdQueue_.front();
    holdQueue_.pop_front();
    holdUntil_ = cycles_ + MsToCycles(kHoldStepMs);
  }
  switch (port) {
    case hw::kBkbDots:
      return static_cast<uint8_t>(membraneState_.row0 | holdState_.row0);
    case hw::kBkbFunction:
      return static_cast<uint8_t>(membraneState_.row1 | holdState_.row1);
    // Shift is ORed in rather than framed: it is held across whatever the
    // frame queue happens to be playing, exactly as a finger holds it.  A
    // frame that carries the bit itself -- a capital letter's chord -- still
    // reads the same, which is why nothing had to change in PressBraille.
    case hw::kBkbCursor:
      return static_cast<uint8_t>(membraneState_.row2 | holdState_.row2 |
                                  (membraneShift_ ? hw::kBkbShift : 0));
    default: return 0;
  }
}

bool EurekaMachine::MembraneBusy() const {
  return !membraneFrames_.empty() || !holdQueue_.empty() ||
         membraneState_.row0 != 0 || membraneState_.row1 != 0 ||
         membraneState_.row2 != 0;
}

// Sets the live matrix state for the nineteen non-shift membrane keys.  Queued
// rather than written straight to holdState_, and through the same
// hw::kBkbDots read that steps the frame queue, for the reason kHoldStepMs
// documents: a state that came and went between two reads would otherwise be
// invisible to the ROM's scan.  A call that repeats the state already at the
// back of the queue (or, when the queue is empty, already on the ports) is
// dropped rather than queued, so a key that keeps being reported down for
// every host repeat does not grow the queue without bound.
void EurekaMachine::HoldMembrane(uint8_t row0, uint8_t row1, uint8_t row2) {
  const MembraneHold requested{row0, row1,
                                static_cast<uint8_t>(row2 & ~hw::kBkbShift)};
  const MembraneHold& last = holdQueue_.empty() ? holdState_ : holdQueue_.back();
  if (requested == last) return;
  holdQueue_.push_back(requested);
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
  const uint8_t kind = key & hw::kKeyKindMask;
  const uint8_t number = key & hw::kKeyNumberMask;
  const bool alt = (key & hw::kKeyAlt) != 0;
  const uint8_t shift = (key & hw::kKeyShift) != 0 ? hw::kBkbShift : 0;
  MembraneFrame frame;
  if (kind == hw::kKeyKeypad) {
    frame.row0 = alt ? hw::kBkbSpace : 0;
    frame.row2 = static_cast<uint8_t>(number | shift);
  } else if (kind == hw::kKeyFunction && number < hw::kFunctionKeysWithRowBit) {
    frame.row1 = static_cast<uint8_t>(1u << number);
    frame.row2 = shift;
  } else if (kind == hw::kKeyFunction) {
    // There are only eight function keys.  F9 and F10 are chords of the space
    // bar and braille dots, decoded at 1D541; the shifted forms are chords of
    // their own, so they carry no shift bit.
    switch (key) {
      // The row bits run in Perkins key order, left to right, so bit 0 is dot
      // 3 and bit 2 is dot 1; the ROM indexes its braille tables (D7A0) with
      // the row byte itself, and index 04h there is the letter "a".
      case hw::kKeyF9:  // MODE
        frame.row0 = hw::kBkbSpace | hw::kBkbDot1;
        break;
      case hw::kKeyF10:  // WHERE
        frame.row0 = hw::kBkbSpace | hw::kBkbDot4;
        break;
      case hw::kKeySF9:
        frame.row0 = hw::kBkbSpace | hw::kBkbDot1 | hw::kBkbDot2;
        break;
      case hw::kKeySF10:
        frame.row0 = hw::kBkbSpace | hw::kBkbDot4 | hw::kBkbDot5;
        break;
      // F11 is a chord too -- the "d" chord, which the keyboard section of
      // hardware-map.md has listed all along and this switch did not.  It was
      // reachable by typing the dots and unreachable by its key code, and
      // the difference was silent.  Measured: chord 9Ch says "ROM
      // operacniho systemu", the same as scan code 57h on the PC keyboard.
      case hw::kKeyF11:
        frame.row0 = hw::kBkbSpace | hw::kBkbDot1 | hw::kBkbDot4 | hw::kBkbDot5;
        break;
      default: return;
    }
  } else {
    return;
  }
  if (alt && kind == hw::kKeyKeypad) {
    MembraneFrame space;
    space.row0 = hw::kBkbSpace;
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
// Unlike PressMembraneKey and HoldMembrane this keeps no held-key state of its
// own.  A chord sent through this call is one deliberate act: the caller
// collects the dots while the fingers are down and calls once, when they come
// up, so there is no host repeat to swallow.  That is still the right call for
// a fixed sequence -- a probe or test script that wants "press this chord" and
// nothing else -- but a host that wants the ROM to see the chord being built,
// dot by dot, and to fall silent again when a dot is let go without the whole
// pattern coming up, uses HoldMembrane instead: that is the live matrix state,
// this is a scripted press queued after whatever it is playing.
//
// Shift is the twentieth key and belongs to the chord, not beside it: the
// decoder reads it off row 8Ch at 1D4F9 and 1D60C, so a shifted dot chord is
// a capital letter (D72D) and shift with the bare space bar is Escape (1D52F).
// Neither is reachable while row 2 stays empty.
void EurekaMachine::PressBraille(uint8_t dots, bool shift) {
  if (dots == 0) return;
  MembraneFrame frame;
  frame.row0 = dots;
  frame.row2 = shift ? hw::kBkbShift : 0;
  frame.cycles = MsToCycles(kPressMs);
  membraneFrames_.push_back(frame);
  MembraneFrame release;
  release.cycles = MsToCycles(kReleaseMs);
  membraneFrames_.push_back(release);
}

// The instruction tables in z80.c leave out the wait states of an I/O cycle.
// UM005004 Table 4: an external port takes 1 to 4 of them as DCNTL says, the
// on-chip registers none -- except the PRT, ASCI and CSI/O data registers,
// which take 0 to 4 "as a function of internal synchronization".  Those are
// charged 0, the one count here no source pins down, and the tone generator's
// interrupt reads TMDR0L every time it runs (HANDOFF 6.10).
void EurekaMachine::ChargeIoWaits(z80* cpu, uint16_t port) {
  if (static_cast<uint8_t>(port) < hw::kInternalIoEnd) return;
  auto* machine = static_cast<EurekaMachine*>(cpu->userdata);
  cpu->cyc +=
      1 + ((machine->io_[hw::kDcntl] & hw::kDcntlIwi) >> hw::kDcntlIwiShift);
}

uint8_t EurekaMachine::CpuReadPort(z80* cpu, uint16_t port) {
  ChargeIoWaits(cpu, port);
  return ReadPort(cpu, port);
}

void EurekaMachine::CpuWritePort(z80* cpu, uint16_t port, uint8_t value) {
  ChargeIoWaits(cpu, port);
  WritePort(cpu, port, value);
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
    case hw::kRtcRamBase + 0: case hw::kRtcRamBase + 1:
    case hw::kRtcRamBase + 2: case hw::kRtcRamBase + 3:
    case hw::kRtcRamBase + 4: case hw::kRtcRamBase + 5:
    case hw::kRtcRamBase + 6: case hw::kRtcRamBase + 7:
      return machine->rtcRam_[port & (hw::kRtcRegisters - 1)];
    case hw::kRtcStatus: {
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
    case hw::kRtcCommand: return machine->rtcCommand_;
    default: break;
  }
  if (low >= hw::kRtcBase && low <= hw::kRtcLast) return machine->ReadRtc(low);
  switch (low) {
    case hw::kStat0:
      return static_cast<uint8_t>(machine->io_[hw::kStat0] | hw::kStatTdre);
    case hw::kStat1:
      return static_cast<uint8_t>(machine->io_[hw::kStat1] | hw::kStatTdre);
    case hw::kRdr0: return 0;
    case hw::kRdr1: return 0;
    case hw::kCntr:
      return static_cast<uint8_t>(
          machine->io_[hw::kCntr] | (machine->csioPending_ ? hw::kCntrEf : 0));
    case hw::kTrdr:
      // Reading the data register takes the byte and clears EF, which is what
      // both the reset handshake at 18847 and the scan code interrupt at
      // 1DD2E rely on to know another byte may come.
      machine->csioPending_ = false;
      return machine->csioData_;
    case hw::kTmdr0l: return machine->ReadTimerData(0, false);
    case hw::kTmdr0h: return machine->ReadTimerData(0, true);
    case hw::kTcr:
      machine->timerControlRead_[0] = machine->timerControlRead_[1] = true;
      return machine->io_[hw::kTcr];
    case hw::kTmdr1l: return machine->ReadTimerData(1, false);
    case hw::kTmdr1h: return machine->ReadTimerData(1, true);
    case hw::kFrc:
      return static_cast<uint8_t>((machine->cycles_ / hw::kTimerPrescale) & 0xff);
    case hw::kDstat: return machine->io_[hw::kDstat];
    case hw::kCbr: return machine->cbr_;
    case hw::kBbr: return machine->bbr_;
    case hw::kCbar: return machine->cbar_;
    // The membrane keyboard uses true logic: a set bit means a pressed key.
    case hw::kBkbDots: case hw::kBkbFunction: case hw::kBkbCursor:
      return machine->ReadMembraneKeyboard(low);
    case hw::kFdcStatus: {
      const uint8_t status = machine->fdcStatus_;
      machine->fdcIntrq_ = false;
      return status;
    }
    case hw::kFdcTrack: return machine->fdcTrack_;
    case hw::kFdcSector: return machine->fdcSector_;
    case hw::kFdcData: return machine->ReadFdcData();
    case hw::kInputBuffer: return machine->ReadInputBuffer();
    // pwr_stb: the strobe that cuts the main supply.  IOPORT.LIB calls it R/W
    // and says any access switches the machine off, so the value returned here
    // never matters -- the firmware never looks at it.  It reads the port at
    // 1D144 and then spins on JR $-2 waiting for the power to go, which is why
    // ignoring this used to leave the emulator in a two-instruction loop with
    // interrupts disabled: silent, deaf and burning a core.
    case hw::kPwrStb:
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
    case hw::kRtcRamBase + 0: case hw::kRtcRamBase + 1:
    case hw::kRtcRamBase + 2: case hw::kRtcRamBase + 3:
    case hw::kRtcRamBase + 4: case hw::kRtcRamBase + 5:
    case hw::kRtcRamBase + 6: case hw::kRtcRamBase + 7:
      machine->rtcRam_[port & (hw::kRtcRegisters - 1)] = value;
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
    case hw::kRtcMask: machine->rtcMask_ = value; return;
    case hw::kRtcCommand: machine->rtcCommand_ = value; return;
    default: break;
  }
  if (low != hw::kTcr) machine->io_[low] = value;
  switch (low) {
    case hw::kCntr: break;
    case hw::kTrdr:
      // FFh is the Reset command every PC keyboard answers with AAh, "self
      // test passed"; the ROM waits for exactly that at 18858 and gives up
      // after about 180 ms.  A real keyboard takes its time, and answering
      // instantly would be worse than useless: the ROM does a throwaway read
      // of TRDR at 18841 after enabling the receiver, and an answer that
      // early would be swallowed by it.
      if (value == hw::kKbReset) {
        // Reset also empties the keyboard's own output buffer, and leaving
        // that out cost a whole evening: the Enter that launched the emulator
        // arrives as a break code before the machine has executed a single
        // instruction, so the queue held 9Ch ahead of the AAh.  The ROM read
        // the 9Ch at 18858, decided no keyboard was attached and switched the
        // receiver off for good -- every key after that fell into silence.
        machine->csioRx_.clear();
        machine->csioPending_ = false;
        machine->csioRx_.push_back(hw::kKbSelfTestOk);
        machine->csioReadyAt_ = machine->cycles_ + kCpuHz / 1000 * 30;
      }
      break;
    case hw::kTmdr0l: machine->WriteTimerData(0, false, value); break;
    case hw::kTmdr0h: machine->WriteTimerData(0, true, value); break;
    case hw::kTcr:
      // TIF1/TIF0 are read-only; only the low six control bits are writable.
      machine->io_[hw::kTcr] = static_cast<uint8_t>(
          (machine->io_[hw::kTcr] & (hw::kTcrTif0 | hw::kTcrTif1)) |
          (value & hw::kTcrWritable));
      break;
    case hw::kTmdr1l: machine->WriteTimerData(1, false, value); break;
    case hw::kTmdr1h: machine->WriteTimerData(1, true, value); break;
    case hw::kDstat: {
      uint8_t status = previous;
      if ((value & hw::kDstatDwe1) == 0)
        status = static_cast<uint8_t>((status & ~hw::kDstatDe1) |
                                      (value & hw::kDstatDe1));
      if ((value & hw::kDstatDwe0) == 0)
        status = static_cast<uint8_t>((status & ~hw::kDstatDe0) |
                                      (value & hw::kDstatDe0));
      status = static_cast<uint8_t>((status & (hw::kDstatDe0 | hw::kDstatDe1)) |
                                    (value & hw::kDstatPlain));
      machine->io_[hw::kDstat] = status;
      if ((status & hw::kDstatDe0) != 0) machine->RunDma0();
      if ((status & hw::kDstatDe1) != 0) machine->MaybeRunDma1();
      break;
    }
    case hw::kCbr: machine->cbr_ = value; break;
    case hw::kBbr: machine->bbr_ = value; break;
    case hw::kCbar: machine->cbar_ = value; break;
    case hw::kModemLatch: case hw::kPowerLatch:
      // Stored in io_ above; the machine does not act on these yet, so record
      // the bit transitions instead of letting them disappear.
      if (machine->diag_.enabled())
        machine->diag_.NoteLatch(cpu->pc, low, previous, value);
      break;
    case hw::kDacPort: machine->dac_ = value; break;
    // The strobe answers to a write just as it does to a read.  This ROM only
    // ever reads it (1D144 is the single access in the whole image), but the
    // port is documented R/W and a program loaded from disk may well write it.
    case hw::kPwrStb: machine->PowerDown(); break;
    case hw::kFdcCommand: machine->StartFdcCommand(value); break;
    case hw::kFdcTrack: machine->fdcTrack_ = value; break;
    case hw::kFdcSector: machine->fdcSector_ = value; break;
    case hw::kFdcData: machine->WriteFdcData(value); break;
    case hw::kOutputLatch:
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
  uint32_t source =
      io_[hw::kSar0l] | (static_cast<uint32_t>(io_[hw::kSar0h]) << 8) |
      (static_cast<uint32_t>(io_[hw::kSar0b] & hw::kDmaBankMask) << 16);
  uint32_t destination =
      io_[hw::kDar0l] | (static_cast<uint32_t>(io_[hw::kDar0h]) << 8) |
      (static_cast<uint32_t>(io_[hw::kDar0b] & hw::kDmaBankMask) << 16);
  uint32_t count = io_[hw::kBcr0l] | (static_cast<uint32_t>(io_[hw::kBcr0h]) << 8);
  if (count == 0) count = hw::kDmaFullCount;
  for (uint32_t index = 0; index < count; ++index) {
    // Routed through the guarded write so a stray descriptor cannot corrupt
    // the ROM image, which Reset() does not restore.
    WritePhysical((destination + index) & kPhysicalMask,
                  memory_[(source + index) & kPhysicalMask], cpu_.pc);
  }
  io_[hw::kBcr0l] = io_[hw::kBcr0h] = 0;
  io_[hw::kDstat] &= static_cast<uint8_t>(~hw::kDstatDe0);
}

bool EurekaMachine::FdcWantsDma() const {
  if (fdcBuffer_.empty() || fdcPosition_ >= fdcBuffer_.size()) return false;
  // Direction has to match, or a read buffer the firmware never drained (it
  // verifies a formatted track by status alone) would look like a controller
  // asking to be fed, and the next Write Track would lose its data.
  const bool toMemory = (io_[hw::kDcntl] & hw::kDcntlDim1) != 0;
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
  // Channel 1 is always memory <-> I/O: MAR1 holds the 20-bit memory address,
  // IAR1 the I/O address and BCR1 the byte count, and DCNTL says which way the
  // bytes go and whether the memory address counts down.
  //
  // The ROM uses this for Write Track: 7000 bytes -- one double-density
  // track -- streamed from a RAM buffer into the floppy data register.
  const uint8_t mode = io_[hw::kDcntl] & hw::kDcntlDim;
  const bool toMemory = (mode & hw::kDcntlDim1) != 0;
  const int32_t step = (mode & hw::kDcntlDim0) != 0 ? -1 : 1;
  uint32_t address =
      io_[hw::kMar1l] | (static_cast<uint32_t>(io_[hw::kMar1h]) << 8) |
      (static_cast<uint32_t>(io_[hw::kMar1b] & hw::kDmaBankMask) << 16);
  const uint16_t port = static_cast<uint16_t>(
      io_[hw::kIar1l] | (static_cast<uint16_t>(io_[hw::kIar1h]) << 8));
  uint32_t count = io_[hw::kBcr1l] | (static_cast<uint32_t>(io_[hw::kBcr1h]) << 8);
  if (count == 0) count = hw::kDmaFullCount;

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

  io_[hw::kMar1l] = static_cast<uint8_t>(address);
  io_[hw::kMar1h] = static_cast<uint8_t>(address >> 8);
  io_[hw::kMar1b] = static_cast<uint8_t>((io_[hw::kMar1b] & ~hw::kDmaBankMask) |
                                         ((address >> 16) & hw::kDmaBankMask));
  io_[hw::kBcr1l] = io_[hw::kBcr1h] = 0;
  dma1Armed_ = false;
  // DE1 clears at BCR1 = 0.
  io_[hw::kDstat] = static_cast<uint8_t>(io_[hw::kDstat] & ~hw::kDstatDe1);
}

uint8_t EurekaMachine::TypeOneStatus(uint8_t command) const {
  // After a Type I command the firmware waits for the index pulse to decide
  // whether a disk is actually in the drive: 19828 issues a seek, then polls
  // 98h with TST 02h and reports "neni disk" if it never arrives (19848).
  // A mounted disk is always present and spinning here, so the pulse is held
  // asserted rather than timed to a 200ms revolution -- the firmware only ever
  // asks whether it appears, never how often.  With no disk it never appears,
  // which is how the machine says "neni disk".
  uint8_t status = 0;
  // INDEX comes from the hole in the medium; TRACK 00 is a sensor on the
  // mechanism.
  if (disk_.present()) status |= hw::kFdcStatusIndex;
  if (fdcTrack_ == 0) status |= hw::kFdcStatusTrack00;
  // Bit 6 is the WPRT input of the 1770, driven by the notch in the medium.
  // The firmware reads it here: DEVICES.10 reports it as bit 6 of both
  // fdc_ctl_chkdsk and fdc_ctl_diskin, and SYSEQU.LIB has it in
  // write_error_mask but not in read_error_mask, so it stops writes only.
  if (disk_.write_protected()) status |= hw::kFdcStatusWriteProtect;
  // The verify flag makes the controller read an ID header off the track it
  // landed on, and that one read is the only thing in the machine that tells
  // three media apart.  fdc_ctl_disk_test (197FB) issues the machine's only
  // verify -- 14h at 1980A -- and reads the answer three ways: a formatted
  // track answers, an unformatted one comes back Seek Error and becomes
  // "unreadable" (19825), and an empty drive answers nothing at all.  With no
  // index holes the controller never leaves BUSY, so the firmware's own
  // timeout at 19A16 runs out, EA00 hands back 80h and 19812 reads that as
  // "no disk".  Answering every verify with success is why both of them used
  // to end up at "vadny disk" instead.
  if ((command & hw::kFdcVerify) != 0 &&
      !disk_.TrackFormatted(fdcTrack_, outputLatch_ & hw::kFdcSide))
    status |= disk_.present() ? hw::kFdcStatusSeekError : hw::kFdcStatusBusy;
  return status;
}

void EurekaMachine::StartFdcCommand(uint8_t command) {
  fdcCommand_ = command;
  fdcBuffer_.clear();
  fdcPosition_ = 0;
  fdcWriting_ = false;
  fdcIntrq_ = false;
  const uint8_t type = command & hw::kFdcCommandType;
  if (type == hw::kFdcCmdRestore) {
    fdcTrack_ = 0;
    fdcStepDirection_ = -1;
    fdcStatus_ = TypeOneStatus(command);
  } else if (type == hw::kFdcCmdSeek) {
    const uint8_t target = io_[hw::kFdcData];
    fdcStepDirection_ = target >= fdcTrack_ ? 1 : -1;
    fdcTrack_ = target;
    fdcStatus_ = TypeOneStatus(command);
  } else if ((command & hw::kFdcTypeTwoOrThree) == 0) {
    // The rest of Type I: Step, Step In and Step Out, each of which occupies
    // two nibbles.  The format routine walks the disk with Step In (50h at
    // 19EE8) rather than seeking, so without the update flag being honoured
    // every track is written on top of track 0.
    const uint8_t group = command & hw::kFdcCommandGroup;
    if (group == hw::kFdcCmdStepIn) fdcStepDirection_ = 1;
    else if (group == hw::kFdcCmdStepOut) fdcStepDirection_ = -1;
    if ((command & hw::kFdcFlagUpdateTrack) != 0) {
      const int stepped = static_cast<int>(fdcTrack_) + fdcStepDirection_;
      fdcTrack_ = static_cast<uint8_t>(std::clamp(stepped, 0, 255));
    }
    fdcStatus_ = TypeOneStatus(command);
  } else if ((command & hw::kFdcCommandGroup) == hw::kFdcCmdReadSector) {
    fdcBuffer_.resize(hw::kSectorBytes);
    const unsigned side = outputLatch_ & hw::kFdcSide;
    // A read finishes on its own: the controller walks the whole sector and
    // drops BUSY even when nobody services DRQ, merely flagging lost data.
    // Holding BUSY until the buffer drains hangs the verify read the format
    // routine issues at 19EF0 with no DMA armed, which spins on BUSY at 19EF3.
    if (disk_.ReadPhysicalSector(fdcTrack_, side, fdcSector_, fdcBuffer_.data())) {
      fdcStatus_ = hw::kFdcStatusDrq;
    } else if (static_cast<int>(fdcTrack_) == fdcFormattedCylinder_ &&
               static_cast<int>(side) == fdcFormattedSide_) {
      // The format routine writes 161 logical tracks, one past the 800K image,
      // and verifies each one it wrote.  A real mechanism steps to cylinder 80
      // and reads back the track it has just laid down, so a read of wherever
      // Write Track last ran has to succeed even outside the image.
      std::fill(fdcBuffer_.begin(), fdcBuffer_.end(), hw::kFormatFill);
      fdcStatus_ = hw::kFdcStatusDrq;
    } else {
      // Record Not Found.  The buffer has to go with it: a DMA channel armed
      // for this read would otherwise drain 512 bytes of nothing into RAM.
      //
      // An empty drive gets the same answer, and that is a deliberate
      // departure from the controller: with no index holes a 1770 would never
      // stop looking, so a real one stays BUSY here.  Measured that way the
      // firmware's own driver does reach "disk neni zalozen" -- but the wait
      // it spins in at 19EF3 has no timeout at all, so any path that does get
      // that far would hang the emulator outright rather than say anything.
      // A wrong status the firmware recovers from beats a machine that stops.
      // The distinction the user hears is made in DiskFailure instead.
      fdcBuffer_.clear();
      fdcStatus_ = hw::kFdcStatusNotFound;
      fdcIntrq_ = true;
    }
  } else if ((command & hw::kFdcCommandGroup) == hw::kFdcCmdWriteSector) {
    if (!disk_.present()) {
      fdcStatus_ = hw::kFdcStatusNotFound;
      fdcIntrq_ = true;
    } else if (disk_.write_protected()) {
      // A 1770 refuses a write on a protected medium before it touches the
      // surface: it raises bit 6 and drops BUSY at once, with no data request
      // at all.  Leaving fdcWriting_ false is the point -- the guest's DMA
      // then finds nothing to feed and the image is never opened for writing.
      fdcStatus_ = hw::kFdcStatusWriteProtect;
      fdcIntrq_ = true;
    } else {
      fdcBuffer_.assign(hw::kSectorBytes, 0);
      fdcWriting_ = true;
      fdcStatus_ = hw::kFdcStatusBusy | hw::kFdcStatusDrq;
    }
  } else if (type == hw::kFdcCmdReadAddress) {
    // Read Address hands back the first sector ID header it finds, so it is
    // exactly how anything asks "is there a format on this track" -- and it is
    // how the ROM's own format routine decides whether to warn that the disk
    // is already formatted.  An unformatted track has no ID headers at all,
    // so it has to come back Record Not Found; answering with a made-up
    // header made a blank diskette claim to be formatted.
    if (!disk_.present() ||
        !disk_.TrackFormatted(fdcTrack_, outputLatch_ & hw::kFdcSide)) {
      fdcStatus_ = hw::kFdcStatusNotFound;
      fdcIntrq_ = true;
      return;
    }
    // The six bytes of an ID field: track, side, sector, size code and the two
    // CRC bytes, which nothing here checks.
    fdcBuffer_ = {fdcTrack_, static_cast<uint8_t>(outputLatch_ & hw::kFdcSide),
                  fdcSector_, hw::kSectorSizeCode, 0, 0};
    fdcStatus_ = hw::kFdcStatusDrq;  // Completes on its own, as above.
  } else if (type == hw::kFdcCmdForceInterrupt) {
    fdcStatus_ = 0;
  } else if (type == hw::kFdcCmdReadTrack) {
    // Read Track reads the raw surface, so the same applies: nothing written
    // means nothing to read.
    if (!disk_.present() ||
        !disk_.TrackFormatted(fdcTrack_, outputLatch_ & hw::kFdcSide)) {
      fdcStatus_ = hw::kFdcStatusNotFound;
      fdcIntrq_ = true;
      return;
    }
    fdcBuffer_.resize(hw::kSectorsPerTrack * hw::kSectorBytes);
    for (unsigned sector = 1; sector <= hw::kSectorsPerTrack; ++sector) {
      disk_.ReadPhysicalSector(
          fdcTrack_, outputLatch_ & hw::kFdcSide, sector,
          fdcBuffer_.data() + (sector - 1) * hw::kSectorBytes);
    }
    fdcStatus_ = hw::kFdcStatusDrq;  // Completes on its own, as above.
  } else if (type == hw::kFdcCmdWriteTrack) {
    if (!disk_.present()) {
      fdcStatus_ = hw::kFdcStatusNotFound;
      fdcIntrq_ = true;
      return;
    }
    if (disk_.write_protected()) {
      // Write Track is a write like any other, and DEVICES.10 says so from
      // the other end: fdc_ctl_write_track returns status 2, "Disk Write
      // Protected".  So a protected diskette cannot be formatted either.
      fdcStatus_ = hw::kFdcStatusWriteProtect;
      fdcIntrq_ = true;
      return;
    }
    // One raw double-density track, gaps and address marks included.
    fdcBuffer_.assign(6250, 0);
    fdcWriting_ = true;
    fdcFormattedCylinder_ = fdcTrack_;
    fdcFormattedSide_ = static_cast<int>(outputLatch_ & hw::kFdcSide);
    // The track the guest is laying down.  Behind a host folder this changes
    // nothing -- the track image is discarded, because erasing the user's
    // files because the emulated machine formatted is not this model's call
    // to make (6.5) -- but on a diskette with no home it is the whole
    // point: an unformatted one has no track answering until this runs.
    disk_.FormatTrack(fdcTrack_, outputLatch_ & hw::kFdcSide);
    // The operation reports success either way.
    fdcStatus_ = hw::kFdcStatusBusy | hw::kFdcStatusDrq;
  } else {
    fdcStatus_ = 0;
  }
  // Type I commands and Force Interrupt finish inside this model, so they
  // raise INTRQ straight away.  Type II and III commands raise it when their
  // data transfer runs out, in ReadFdcData and WriteFdcData below.  The one
  // Type I that does not finish is a verify with no medium under the head:
  // it is still BUSY, so it has not completed and must not signal that it
  // has -- the firmware is meant to time it out and call that an empty drive.
  // Force Interrupt is what gets the controller back, and it clears BUSY.
  if (((command & hw::kFdcTypeTwoOrThree) == 0 &&
       (fdcStatus_ & hw::kFdcStatusBusy) == 0) ||
      type == hw::kFdcCmdForceInterrupt)
    fdcIntrq_ = true;
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
  io_[hw::kFdcData] = value;
  if (!fdcWriting_ || fdcPosition_ >= fdcBuffer_.size()) return;
  fdcBuffer_[fdcPosition_++] = value;
  if (fdcPosition_ == fdcBuffer_.size()) {
    if ((fdcCommand_ & hw::kFdcCommandGroup) == hw::kFdcCmdWriteSector) {
      disk_.WritePhysicalSector(fdcTrack_, outputLatch_ & hw::kFdcSide,
                                fdcSector_, fdcBuffer_.data());
      lastDiskWrite_ = cycles_;
    }
    fdcStatus_ = 0;
    fdcWriting_ = false;
    fdcIntrq_ = true;
  }
}

uint16_t EurekaMachine::TimerReload(unsigned channel) const {
  const unsigned base = channel == 0 ? hw::kRldr0l : hw::kRldr1l;
  return io_[base] | (static_cast<uint16_t>(io_[base + 1]) << 8);
}

uint8_t EurekaMachine::ReadTimerData(unsigned channel, bool high) {
  if (timerControlRead_[channel]) {
    const uint8_t flag = channel == 0 ? hw::kTcrTif0 : hw::kTcrTif1;
    io_[hw::kTcr] &= static_cast<uint8_t>(~flag);
    timerPending_[channel] = false;
    timerControlRead_[channel] = false;
  }
  return static_cast<uint8_t>(high ? timerCurrent_[channel] >> 8
                                   : timerCurrent_[channel]);
}

void EurekaMachine::WriteTimerData(unsigned channel, bool high, uint8_t value) {
  const uint8_t enable = channel == 0 ? hw::kTcrTde0 : hw::kTcrTde1;
  if ((io_[hw::kTcr] & enable) != 0) return;
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
  const Biquad& b =
      (outputLatch_ & hw::kFilterselMask) != 0 ? kSpeechFilter : kOpenFilter;
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
    // Last the volume slider: a pot between the output stage and the speaker,
    // so it scales the result and leaves the filters' state alone.
    audio_.push_back(static_cast<int16_t>(
        std::clamp(coupled * volume_, -32768.0, 32767.0)));
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
  if ((io_[hw::kCntr] & hw::kCntrRe) == 0) return;  // the receiver is off
  csioData_ = csioRx_.front();
  csioRx_.pop_front();
  csioPending_ = true;
  // A completed receive clears RE on the 64180; the firmware has to arm the
  // port again for every single byte, which is exactly what it does at 1E012.
  // Leaving RE set costs the arrows: their scan codes are two bytes, E0 then
  // the key, and the interrupt handler's throwaway read of TRDR at 1DFFE would
  // swallow the second one before the handler ever came round to it.  Single
  // byte keys never noticed, so letters worked and navigation did not.
  io_[hw::kCntr] &= static_cast<uint8_t>(~hw::kCntrRe);
}

void EurekaMachine::Advance(uint32_t cpuCycles) {
  cycles_ += cpuCycles;
  RenderAudio(cpuCycles);
  PumpCsio();
  UpdateRtcEvents();
  const uint8_t control = io_[hw::kTcr];
  for (unsigned channel = 0; channel < 2; ++channel) {
    const uint8_t enable = channel == 0 ? hw::kTcrTde0 : hw::kTcrTde1;
    if ((control & enable) == 0) {
      timerAccum_[channel] = 0;
      continue;
    }
    timerAccum_[channel] += cpuCycles;
    while (timerAccum_[channel] >= hw::kTimerPrescale) {
      timerAccum_[channel] -= hw::kTimerPrescale;
      if (timerCurrent_[channel] == 0) timerCurrent_[channel] = TimerReload(channel);
      else --timerCurrent_[channel];
      if (timerCurrent_[channel] == 0) {
        timerPending_[channel] = true;
        io_[hw::kTcr] |= channel == 0 ? hw::kTcrTif0 : hw::kTcrTif1;
      }
    }
  }
}

// Decided from scratch after every instruction: the request is a level, not a
// latch.  Kept from one instruction to the next, it was taken even when that
// instruction had just cleared or disabled its source (HANDOFF 6.33).
void EurekaMachine::ScheduleInterrupt() {
  cpu_.int_pending = 0;
  if (!cpu_.iff1) return;
  const uint8_t control = io_[hw::kTcr];
  const uint8_t base = io_[hw::kIl] & hw::kIlVectorBase;
  if (timerPending_[0] && (control & hw::kTcrTie0)) {
    // HD64180 internal interrupts always use I/IL vectored acquisition,
    // independently of the Z80 IM setting.
    cpu_.interrupt_mode = 2;
    z80_gen_int(&cpu_, static_cast<uint8_t>(base | hw::kVectorTimer0));
  } else if (timerPending_[1] && (control & hw::kTcrTie1)) {
    cpu_.interrupt_mode = 2;
    z80_gen_int(&cpu_, static_cast<uint8_t>(base | hw::kVectorTimer1));
  } else if (csioPending_ && (io_[hw::kCntr] & hw::kCntrEie)) {
    // Lowest of the three, which is the HD64180's own order.  The flag stays
    // up until the handler reads TRDR, so this re-arms by itself.
    cpu_.interrupt_mode = 2;
    z80_gen_int(&cpu_, static_cast<uint8_t>(base | hw::kVectorCsio));
  }
}

void EurekaMachine::ReturnFromCall() {
  cpu_.pc = PeekWord(cpu_.sp);
  cpu_.sp += 2;
}

// Why a BIOS disk transfer failed, in the code BDOS branches on at 1BB45.
// The distinction is not bookkeeping: it is the sentence the user hears, and
// the three cases sound nothing alike.  A read that fails on an empty drive
// is "disk neni zalozen"; a write refused by the notch is "disk je chranen
// proti zapisu"; a sector that will not come back off a diskette that is
// there is "vadny disk", and that last one is the only one that ever meant a
// broken diskette.  Answering all of them with 1 said "vadny disk" to a user
// who had simply not put a diskette in.
//
// These are not codes this model invents.  Measured with the intercept turned
// off and the controller left BUSY on an empty drive, the ROM's own driver
// reaches "disk neni zalozen" by itself, which is the sentence this reproduces
// -- and reproduces without the several thousand status polls that answer
// costs the firmware.
uint8_t EurekaMachine::DiskFailure(bool writing) const {
  if (!disk_.present()) return hw::kDiskResultNoDisk;
  if (writing && disk_.write_protected()) return hw::kDiskResultWriteProtected;
  return hw::kDiskResultFaulty;
}

bool EurekaMachine::InterceptBios() {
  // Eureka's extended CP/M BIOS jump table lives at C100. Page zero points
  // at BOOT/WBOOT targets, not at the beginning of this table.
  constexpr uint16_t base = 0xc100;
  // Every entry is a JP, so a table that does not start with one is not the
  // table -- the ROM has not been mapped in yet.
  constexpr uint8_t kOpcodeJp = 0xc3;
  if (Peek(base) != kOpcodeJp) return true;
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
    // Console status (2) and console input (3) are deliberately NOT answered
    // here.  The ROM waits for a key by spinning in its own event dispatcher
    // at 19B41, exactly as the hardware does, and that is the only way it ever
    // reads the input it produces for itself: a function key types its text
    // into the ROM's own queue (fk_table, SYSRAM.A), and answering the read
    // from here starved every one of them.
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
      cpu_.a = ok ? hw::kDiskResultOk : DiskFailure(false);
      ReturnFromCall();
      return true;
    }
    case 14: {
      std::array<uint8_t, VirtualDisk::kRecordSize> record{};
      for (unsigned i = 0; i < record.size(); ++i) record[i] = Peek(biosDma_ + i);
      const bool ok = disk_.WriteRecord(biosTrack_, biosSector_, record.data());
      cpu_.a = ok ? hw::kDiskResultOk : DiskFailure(true);
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
  dac_ = hw::kDacMidScale;
  membraneFrames_.clear();
  membraneState_ = MembraneFrame{};
  membraneHeldKey_ = 0;
  membraneShift_ = false;
  holdQueue_.clear();
  holdState_ = MembraneHold{};
  holdUntil_ = 0;
}

bool EurekaMachine::Step() {
  if (poweredOff_) return false;
  if (!InterceptBios()) return false;
  // The speech module's own jump table at 0100h, which the SYSJUMPS stubs
  // enter as 0100h + L (1D2CF).  Both the logical and the physical address
  // have to match, or an unrelated routine that happens to sit there in some
  // other bank would be captured too.
  //
  // .speak (0103h) takes one character in A.  .spconv (010Fh) speaks the
  // conversion buffer up to its zero (copy loop at 002E3), and that is the road
  // the clock's announcement takes: with .speak alone the transcript stayed
  // empty while the machine told the time.  The zero is kept, so the sentence
  // keeps its end.  .spchar (0106h) is left out on purpose -- it is a key's
  // echo or an index into nine fixed click commands (00369), nothing the
  // caller does not know already (HANDOFF section 3).
  constexpr uint16_t kSpeak = 0x0103;
  constexpr uint16_t kSpconv = 0x010f;
  constexpr uint16_t kConversionBuffer = 0xc7e2;  // 002DD
  // A buffer without its zero is not a sentence.  C835h is the next SYSRAM
  // variable the firmware is known to use (braille table choice, 1D784).
  constexpr uint16_t kConversionEnd = 0xc835;
  if ((cpu_.pc == kSpeak || cpu_.pc == kSpconv) &&
      PhysicalAddress(cpu_.pc) == cpu_.pc) {
    if (cpu_.pc == kSpeak) {
      speechInput_.push_back(cpu_.a);
    } else {
      for (uint16_t address = kConversionBuffer; address < kConversionEnd;
           ++address) {
        const uint8_t byte = Peek(address);
        speechInput_.push_back(byte);
        if (byte == 0) break;
      }
    }
  }
  // The instruction first and the interrupt after it, with the clock brought
  // up to date in between: a Z180 samples its interrupt inputs at the end of
  // an instruction (UM005004 Table 47, note 7).  Deciding the request before
  // the instruction let OUT0 (TCR) at 0027C, which switches timer 0's
  // interrupt off, still take one -- three times in a sweep (HANDOFF 6.33).
  unsigned long before = cpu_.cyc;
  z80_execute(&cpu_);
  Advance(static_cast<uint32_t>(cpu_.cyc - before));
  ScheduleInterrupt();
  before = cpu_.cyc;
  z80_process_interrupts(&cpu_);
  if (cpu_.cyc != before) Advance(static_cast<uint32_t>(cpu_.cyc - before));
  ++instructions_;
  return true;
}

void EurekaMachine::QueueKey(uint8_t key) {
  // Only the twenty keys the machine actually has.  They always go on the real
  // keyboard ports, because the ROM does far more with them than hand them to
  // the program: it decodes them in its heartbeat, and MODE, WHERE and the
  // application keys act wherever they are pressed.  Text is not a key code
  // and has no business here -- it is typed with QueueText, on the PC keyboard
  // the ROM knows how to read.
  if ((key & hw::kKeyIsCode) == 0) return;
  PressMembraneKey(key);
}

void EurekaMachine::ReleaseKey(uint8_t key) {
  if ((key & hw::kKeyIsCode) == 0 || membraneHeldKey_ == 0 ||
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
  csioRx_.push_back(code);
}

// The three tables the scan code decoder picks between at 1DDCB, by what
// C670h says is held down: nothing, either shift (bits 0-1) or the right Alt
// (bit 4).  These are physical addresses into the loaded image, not a copy of
// it kept here: the layout is Czech QWERTZ and the ROM is the only thing that
// says so, so a copy would be a second opinion nobody asked for.
constexpr uint32_t kScanTablePlain = 0x1df05;
constexpr uint32_t kScanTableShift = 0x1df5e;
constexpr uint32_t kScanTableAltGr = 0x1df98;
// Pairs of (scan code, result) for the codes that arrive behind an E0h
// prefix, searched at 1DD51.  The list starts one pair in and ends at a zero.
constexpr uint32_t kScanTableExtended = 0x1dfd6;
// What the tables put where a character would go for the modifiers themselves:
// F0h and up is a modifier (1DDE6), F1h specifically is the left shift flag
// tested at 1DDD5, and 10h in the extended list marks the right Alt (1DD51).
constexpr uint8_t kModifierEntry = 0xf0;
constexpr uint8_t kLeftShiftFlag = 0xf1;
constexpr uint8_t kAltGrFlag = 0x10;
// The other extended modifier the plain table carries (1DD79), and caps lock
// (1DD7E).  kCtrlFlag shares its value with kFirstCharacter below by accident,
// not by meaning: one is a flag in a table, the other a bound on characters.
constexpr uint8_t kCtrlFlag = 0x04;
constexpr uint8_t kCapsLockFlag = 0x01;
// 01h to 03h go to the dead-key handler and put nothing in the queue (1DDED).
constexpr uint8_t kFirstCharacter = 0x04;
// Bounds the decoder itself keeps: nothing from 60h up reaches a table at all
// (1DD4C), and 3Ah is where the letters end and the modifier tables stop being
// consulted (1DD68).
constexpr unsigned kScanCodeLimit = 0x60;
constexpr unsigned kScanCodeLetters = 0x3a;

uint8_t EurekaMachine::TranslatedScanCode(uint8_t code, ScanModifier modifier)
    const {
  if (code == 0 || code >= kScanCodeLimit) return 0;
  if (code >= kScanCodeLetters) {
    // 1DD68 sends these straight to the plain table: above the letters the
    // decoder never looks at a modifier table at all, which is why the numeric
    // keypad types the same character shifted or not.
    if (modifier == ScanModifier::kAltGr) return 0;  // dropped at 1DD85
    const uint8_t value = memory_[kScanTablePlain + code];
    // The two extended modifiers (1DD74, 1DD79), caps lock (1DD7E), and from
    // 80h up an Eureka key code rather than a character (1DD88).
    if (value == 0 || value == kAltGrFlag || value == kCtrlFlag ||
        value == kCapsLockFlag || value >= hw::kKeyIsCode)
      return 0;
    // The one key whose shifted character the decoder computes instead of
    // looking it up (1DD8C), and so the only place '<' and '>' live.
    if (value == '<') return modifier == ScanModifier::kShift ? '>' : '<';
    return value;
  }
  const uint32_t table = modifier == ScanModifier::kAltGr   ? kScanTableAltGr
                         : modifier == ScanModifier::kShift ? kScanTableShift
                                                            : kScanTablePlain;
  const uint8_t value = memory_[table + code];
  if (value == 0 || value >= kModifierEntry || value < kFirstCharacter)
    return 0;
  return value;
}

void EurekaMachine::BuildKeyboardLayout() {
  typedKeys_.fill(TypedKey{});
  // Where the modifiers sit is read out of the tables too, by what they do
  // rather than by where a PC keyboard usually keeps them: F1h is the left
  // shift flag (1DDE6 hands F0h and up to C670h, and bit 0 is what 1DDD5
  // tests), and the extended list is the only thing that says E0 38 is the
  // right Alt that selects the third table (1DD51 -> 10h -> bit 4).
  shiftScanCode_ = 0;
  for (unsigned code = 1; code < kScanCodeLetters; ++code)
    if (memory_[kScanTablePlain + code] == kLeftShiftFlag) {
      shiftScanCode_ = static_cast<uint8_t>(code);
      break;
    }
  altGrScanCode_ = 0;
  for (uint32_t at = kScanTableExtended + 2; memory_[at] != 0; at += 2)
    if (memory_[at + 1] == kAltGrFlag && memory_[at] < hw::kScanBreak) {
      altGrScanCode_ = memory_[at];
      break;
    }

  // Strongest modifier first and highest scan code first, so the weakest and
  // lowest one wins the character in the end: '+' comes out as the unshifted
  // 1 key (02h) rather than the keypad (4Eh), which is what a host typing a
  // line of text means by it.
  const ScanModifier order[] = {ScanModifier::kAltGr, ScanModifier::kShift,
                                ScanModifier::kNone};
  for (ScanModifier modifier : order) {
    if (modifier == ScanModifier::kShift && shiftScanCode_ == 0) continue;
    if (modifier == ScanModifier::kAltGr && altGrScanCode_ == 0) continue;
    for (unsigned code = kScanCodeLimit - 1; code >= 1; --code) {
      const uint8_t ch = TranslatedScanCode(static_cast<uint8_t>(code), modifier);
      if (ch == 0) continue;
      typedKeys_[ch] = TypedKey{static_cast<uint8_t>(code), modifier};
    }
  }
}

bool EurekaMachine::QueueText(const std::string& text, uint8_t* unmapped) {
  for (unsigned char ch : text) {
    if (typedKeys_[ch].code != 0) continue;
    if (unmapped != nullptr) *unmapped = ch;
    return false;
  }
  for (unsigned char ch : text) {
    const TypedKey key = typedKeys_[ch];
    switch (key.modifier) {
      case ScanModifier::kShift:
        QueueScanCode(shiftScanCode_);
        break;
      case ScanModifier::kAltGr:
        QueueScanCode(hw::kScanExtended);
        QueueScanCode(altGrScanCode_);
        break;
      case ScanModifier::kNone:
        break;
    }
    QueueScanCode(key.code);
    QueueScanCode(static_cast<uint8_t>(key.code | hw::kScanBreak));
    switch (key.modifier) {
      case ScanModifier::kShift:
        QueueScanCode(static_cast<uint8_t>(shiftScanCode_ | hw::kScanBreak));
        break;
      case ScanModifier::kAltGr:
        QueueScanCode(hw::kScanExtended);
        QueueScanCode(static_cast<uint8_t>(altGrScanCode_ | hw::kScanBreak));
        break;
      case ScanModifier::kNone:
        break;
    }
  }
  return true;
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
