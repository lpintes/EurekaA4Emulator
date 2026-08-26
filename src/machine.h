#ifndef EUREKA_MACHINE_FULL_H
#define EUREKA_MACHINE_FULL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include "diagnostics.h"
#include "virtual_disk.h"

extern "C" {
#include "z80.h"
}

class EurekaMachine {
 public:
  static constexpr uint32_t kCpuHz = 6144000;
  static constexpr uint32_t kAudioHz = 48000;
  // The HD64180 drives 19 address lines, so physical addresses wrap at 80000h.
  // Every memory map in the Technical Manual (Appendix E) fits inside that
  // window, and the highest bank value this ROM ever loads is 70h.
  static constexpr uint32_t kPhysicalMask = 0x7ffff;
  static constexpr std::size_t kPhysicalSize = kPhysicalMask + 1;
  static constexpr std::size_t kRomSize = 0x40000;
  // Lowest physical address the machine treats as writable RAM.  Every memory
  // map in Appendix E puts RAM at 50000h, but this Czech image is its own
  // layout: it loads bank values up to 70h and --diag reports no dropped
  // writes with RAM here, so RAM occupies the top 64K of the 19-bit space.
  static constexpr uint32_t kRamBase = 0x70000;

  bool LoadRom(const std::filesystem::path& path, std::wstring& error);
  bool MountDisk(const std::filesystem::path& folder, std::wstring& error);
  void CreateRamDisk() { disk_.CreateRamDisk(); }
  bool FlushDisk(std::wstring& error) { return disk_.Flush(error); }
  bool ExportDisk(const std::filesystem::path& folder, std::wstring& error) {
    return disk_.ExportTo(folder, error);
  }
  // True once the guest has written and then left the disk alone long enough
  // that the image is a consistent filesystem again.  Writing it back on a
  // fixed timer instead would catch CP/M mid-update, and a directory entry
  // erased on the way to being rewritten reads as a deleted file.
  bool DiskSettled() const;
  void Reset();

  // False only once the machine has switched itself off.  The CPU is never
  // parked otherwise: the ROM waits for a key by spinning in its own event
  // dispatcher, exactly as the hardware does, so nothing here has to guess
  // when the machine is idle.
  bool Step();

  // True once the firmware has touched pwr_stb (port B8h) and the machine has
  // cut its own power.  Nothing runs after that until Reset(); on the real
  // Eureka the power supply is off and only RAM and the clock stay alive.
  bool powered_off() const { return poweredOff_; }
  // What the firmware left in C45Ah on the way out.  FFh means it powered down
  // deliberately (1D141), and the boot code reads it at 180CB to resume where
  // the user was instead of initialising from scratch.  A host that keeps the
  // RAM across runs has to keep this byte with it.
  uint8_t power_down_marker() const { return Peek(0xc45a); }
  void QueueKey(uint8_t key);
  // Optional counterpart of QueueKey for hosts that see key releases: it ends
  // the emulated press early.  Without it a key still comes up on its own.
  void ReleaseKey(uint8_t key);
  // Presses a braille chord on the dot keys: bit 0 is dot 3, bit 1 dot 2,
  // bit 2 dot 1, bit 3 dot 4, bit 4 dot 5, bit 5 dot 6, bit 7 the space bar.
  // Shift is the keyboard's own twentieth key on row 8Ch and is part of the
  // chord: it capitalises a letter and turns the bare space bar into Escape.
  // The machine turns the pattern into a character itself.
  void PressBraille(uint8_t dots, bool shift = false);
  // Hands one IBM PC scan code to the optional QWERTY keyboard on the clocked
  // serial port: XT set 1, so a make code is below 80h, a break code is the
  // make code with bit 7 set, and E0h prefixes the grey keys.  The ROM does
  // the whole translation itself, layout and modifiers included.
  void QueueScanCode(uint8_t code);
  // Types text on that same keyboard.  The characters are in the machine's own
  // charset (Kamenicky), and the keys they sit on come from the ROM's tables,
  // not from a layout of ours -- see BuildKeyboardLayout.  A character with no
  // key anywhere in them is refused: the call types *nothing*, returns false
  // and leaves the offending byte in *unmapped.  Typing the rest of the line
  // around it would leave the machine holding half a command and the caller
  // chasing the failure somewhere else entirely.
  bool QueueText(const std::string& text, uint8_t* unmapped = nullptr);
  std::vector<uint8_t> TakeConsoleOutput();
  std::vector<uint8_t> TakeSpeechInput();
  std::vector<int16_t> TakeAudio();
  // Renders the cycles a parked CPU did not run.  Step() returns false while
  // the BIOS waits for a key, which is most of the machine's life, but the
  // real Eureka is only spinning there: the DAC still holds its last value
  // and the filter behind it still runs.  Without this the audio stream dries
  // up between utterances, and starting it again costs about 19 ms measured
  // at the device -- paid at the front of every single thing the machine says.
  void RenderIdle(uint32_t cpuCycles) { RenderAudio(cpuCycles); }

  uint64_t cycles() const { return cycles_; }
  uint64_t instructions() const { return instructions_; }
  uint16_t pc() const { return cpu_.pc; }
  uint8_t cbar() const { return cbar_; }
  uint8_t cbr() const { return cbr_; }
  uint8_t bbr() const { return bbr_; }
  uint32_t physical_pc() const { return PhysicalAddress(cpu_.pc); }
  uint16_t sp() const { return cpu_.sp; }
  uint8_t a() const { return cpu_.a; }
  bool zero_flag() const { return cpu_.zf; }
  std::size_t queued_keys() const {
    return keys_.size() + firmwareKeys_.size() + membraneFrames_.size() +
           csioRx_.size();
  }
  // Moves the clock the RTC reports, in seconds, without touching the host's.
  // The Eureka's clock is the host clock, which is what a user wants and what
  // makes anything waiting for a time untestable: a test for the alarm would
  // have to sit out the wait in real time.  Nothing but tests sets this.
  void SetRtcOffset(int64_t seconds) { rtcOffset_ = seconds; }

  uint8_t debug_peek(uint16_t address) const { return Peek(address); }
  // The alarm the firmware last armed: registers 190h-197h in port order, the
  // interrupt mask at 290h, and the events waiting to be read from it.
  uint8_t debug_rtc_ram(unsigned index) const { return rtcRam_[index & 7]; }
  uint8_t debug_rtc_mask() const { return rtcMask_; }
  uint8_t debug_rtc_status() const { return rtcStatus_; }
  uint8_t debug_io(uint8_t port) const { return io_[port]; }
  uint64_t debug_bios_reads() const { return biosReads_; }
  uint16_t debug_bios_track() const { return biosTrack_; }
  uint16_t debug_bios_sector() const { return biosSector_; }
  const VirtualDisk& disk() const { return disk_; }
  Diagnostics& diagnostics() { return diag_; }

 private:
  void PowerDown();

  static uint8_t ReadMemory(void* context, uint16_t logical);
  static void WriteMemory(void* context, uint16_t logical, uint8_t value);
  static uint8_t ReadPort(z80* cpu, uint16_t port);
  static uint8_t ReadPortInner(z80* cpu, uint16_t port);
  static void WritePort(z80* cpu, uint16_t port, uint8_t value);

  uint32_t PhysicalAddress(uint16_t logical) const;
  void WritePhysical(uint32_t physical, uint8_t value, uint16_t pc);
  uint8_t Peek(uint16_t logical) const;
  void Poke(uint16_t logical, uint8_t value);
  uint16_t PeekWord(uint16_t logical) const;
  void ReturnFromCall();
  bool InterceptBios();
  void Advance(uint32_t cpuCycles);
  void ScheduleInterrupt();
  void RenderAudio(uint32_t cpuCycles);
  uint16_t TimerReload(unsigned channel) const;
  uint8_t ReadTimerData(unsigned channel, bool high);
  void WriteTimerData(unsigned channel, bool high, uint8_t value);
  void RunDma0();
  void RunDma1();
  bool FdcWantsDma() const;
  void MaybeRunDma1();

  void StartFdcCommand(uint8_t command);
  uint8_t TypeOneStatus() const;
  uint8_t ReadFdcData();
  void WriteFdcData(uint8_t value);
  uint8_t ReadRtc(uint16_t port) const;
  void SampleRtc() const;
  std::array<uint8_t, 8> CurrentRtcRegisters() const;
  bool RtcAlarmMatches(const std::array<uint8_t, 8>& now) const;
  void UpdateRtcEvents();
  uint8_t ReadInputBuffer() const;
  void PumpCsio();
  uint8_t ReadMembraneKeyboard(uint8_t port);
  void PressMembraneKey(uint8_t key);
  bool MembraneBusy() const;
  // True while a keypress is still on its way in through real hardware -- the
  // keyboard rows or the serial port.  The machine has to keep running for it
  // to arrive, so the BIOS console read must not park the CPU meanwhile.
  bool HardwareInputBusy() const;
  void NoteHardwareInput();
  void InjectFirmwareKey();
  // Which of the ROM's three translation tables a scan code is looked up in.
  enum class ScanModifier { kNone, kShift, kAltGr };
  // What the ROM would make of one scan code held with one modifier, or 0 if
  // that combination produces no character at all.  It follows the decoder at
  // 1DD63 branch for branch rather than just indexing a table, because the
  // branches are where the layout really lives.
  uint8_t TranslatedScanCode(uint8_t code, ScanModifier modifier) const;
  // Runs the decoder backwards over every scan code and every modifier, so
  // typing a character means pressing the key the ROM itself would have read
  // it from.  Called once the image is loaded; the tables are in it.
  void BuildKeyboardLayout();

  z80 cpu_{};
  std::array<uint8_t, kPhysicalSize> memory_{};
  std::array<uint8_t, 256> io_{};
  // Alarm registers, ports 190h-197h: the time the RTC compares the clock
  // against, in the same order as the clock registers.  80h in a field means
  // "do not compare this one".
  std::array<uint8_t, 8> rtcRam_{};
  // Snapshot of the clock taken when the firmware reads rtc_100th; see ReadRtc.
  mutable std::array<uint8_t, 8> rtcRegisters_{};
  mutable bool rtcLatched_ = false;
  uint8_t rtcMask_ = 0;      // 290h written: which events reach rtc_status
  uint8_t rtcCommand_ = 0;   // 291h: crystal, 12/24h, run, interrupt pin
  uint8_t rtcStatus_ = 0;    // 290h read: events since the last read
  bool rtcAlarmMatched_ = false;
  bool rtcEventsPrimed_ = false;
  std::array<uint8_t, 8> rtcPrevious_{};
  uint64_t rtcNextPoll_ = 0;
  int64_t rtcOffset_ = 0;
  VirtualDisk disk_;
  uint64_t lastDiskWrite_ = 0;
  Diagnostics diag_;

  uint8_t cbar_ = 0xf0;
  uint8_t cbr_ = 0;
  uint8_t bbr_ = 0;
  uint8_t outputLatch_ = 0;
  uint8_t dac_ = 0x80;
  bool romLoaded_ = false;

  uint64_t cycles_ = 0;
  uint64_t instructions_ = 0;
  uint64_t timerAccum_[2]{};
  uint16_t timerCurrent_[2]{0xffff, 0xffff};
  bool timerControlRead_[2]{};
  bool timerPending_[2]{};
  uint64_t audioPhase_ = 0;
  std::vector<int16_t> audio_;

  std::deque<uint8_t> keys_;
  std::deque<uint8_t> firmwareKeys_;
  // One scanned state of the 20-key braille keyboard.  The rows are the three
  // read-only ports of Appendix H; the ROM decides which is which, and it
  // disagrees with the manual's prose: 1D41F masks 89h with 3Fh to get the six
  // braille dots and 1D206 masks 8Ch with 0Fh to ignore the shift key, which
  // is the row order KEYSCAN.MAC and the IOPORT.LIB equates describe.
  struct MembraneFrame {
    uint8_t row0 = 0;  // 89h: braille dots 1-6, bit 7 = space bar (ALT)
    uint8_t row1 = 0;  // 8Ah: function keys 1-8
    uint8_t row2 = 0;  // 8Ch: the four cursor keys, bit 6 = shift
    uint32_t cycles = 0;
    uint8_t key = 0;  // the Eureka code this frame presses; 0 = release
  };
  std::deque<MembraneFrame> membraneFrames_;
  MembraneFrame membraneState_;
  uint64_t hardwareInputUntil_ = 0;
  uint64_t membraneUntil_ = 0;
  uint64_t membraneMinUntil_ = 0;
  uint8_t membraneHeldKey_ = 0;
  bool keyboardInitialized_ = false;
  bool poweredOff_ = false;
  std::vector<uint8_t> consoleOutput_;
  std::vector<uint8_t> speechInput_;
  uint16_t biosTrack_ = 0;
  uint16_t biosSector_ = 0;
  uint16_t biosDma_ = 0x80;
  uint64_t biosReads_ = 0;

  uint8_t fdcStatus_ = 0;
  uint8_t fdcTrack_ = 0;
  uint8_t fdcSector_ = 1;
  uint8_t fdcCommand_ = 0;
  std::vector<uint8_t> fdcBuffer_;
  std::size_t fdcPosition_ = 0;
  bool fdcWriting_ = false;
  // The controller's INTRQ line.  It is tracked but deliberately not
  // readable anywhere: no external port carries it (Appendix H), and A8h
  // bit 1 in particular is the battery comparator, where reporting INTRQ
  // made every disk command fail as "slaba baterie".  Kept because it
  // records when the controller would actually raise the line, which is
  // what a future wiring to the Z180 interrupt inputs would need.
  bool fdcIntrq_ = false;
  // DE1 was set while the controller had no data to move; see MaybeRunDma1.
  bool dma1Armed_ = false;
  // Last direction a Type I step moved the head; a bare Step repeats it.
  int fdcStepDirection_ = 1;
  // Position of the last Write Track, so a verify read of it always succeeds.
  int fdcFormattedCylinder_ = -1;
  int fdcFormattedSide_ = -1;
  // The clocked serial port, which is where the optional IBM PC keyboard
  // hangs.  CNTR bit 7 is EF (a byte has arrived and waits in TRDR), bit 6
  // EIE, bit 5 RE, bit 4 TE.
  std::deque<uint8_t> csioRx_;
  uint8_t csioData_ = 0;
  bool csioPending_ = false;
  uint64_t csioReadyAt_ = 0;
  // The keyboard the ROM expects, worked out from its own tables: for each
  // character of the machine's charset, the scan code that types it and the
  // modifier that has to be held down.  code 0 means the character is not on
  // this keyboard at all.
  struct TypedKey {
    uint8_t code = 0;
    ScanModifier modifier = ScanModifier::kNone;
  };
  std::array<TypedKey, 256> typedKeys_{};
  // Positions of the two modifiers themselves, also taken from the tables and
  // not assumed; 0 means the image has no such key and the characters that
  // need it stay unreachable.
  uint8_t shiftScanCode_ = 0;
  uint8_t altGrScanCode_ = 0;
  // Reconstruction filter state; see RenderAudio.
  double audioState_[2] = {};
  double couplingState_[2] = {};
};

#endif
