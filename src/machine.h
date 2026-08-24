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

  // Returns false only while BIOS console input is waiting for a host key.
  bool Step();
  void QueueKey(uint8_t key);
  // Optional counterpart of QueueKey for hosts that see key releases: it ends
  // the emulated press early.  Without it a key still comes up on its own.
  void ReleaseKey(uint8_t key);
  // Presses a braille chord on the dot keys: bit 0 is dot 3, bit 1 dot 2,
  // bit 2 dot 1, bit 3 dot 4, bit 4 dot 5, bit 5 dot 6, bit 7 the space bar.
  // The machine turns the pattern into a character itself.
  void PressBraille(uint8_t dots);
  // Hands one IBM PC scan code to the optional QWERTY keyboard on the clocked
  // serial port: XT set 1, so a make code is below 80h, a break code is the
  // make code with bit 7 set, and E0h prefixes the grey keys.  The ROM does
  // the whole translation itself, layout and modifiers included.
  void QueueScanCode(uint8_t code);
  void QueueText(const std::string& ascii);
  std::vector<uint8_t> TakeConsoleOutput();
  std::vector<uint8_t> TakeSpeechInput();
  std::vector<int16_t> TakeAudio();

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
  uint8_t debug_peek(uint16_t address) const { return Peek(address); }
  uint8_t debug_io(uint8_t port) const { return io_[port]; }
  uint64_t debug_bios_reads() const { return biosReads_; }
  uint16_t debug_bios_track() const { return biosTrack_; }
  uint16_t debug_bios_sector() const { return biosSector_; }
  const VirtualDisk& disk() const { return disk_; }
  Diagnostics& diagnostics() { return diag_; }

 private:
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

  z80 cpu_{};
  std::array<uint8_t, kPhysicalSize> memory_{};
  std::array<uint8_t, 256> io_{};
  std::array<uint8_t, 8> rtcRam_{};
  // Snapshot of the clock taken when the firmware reads rtc_100th; see ReadRtc.
  mutable std::array<uint8_t, 8> rtcRegisters_{};
  mutable bool rtcLatched_ = false;
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
  bool biosWaiting_ = false;
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
  // Reconstruction filter state; see RenderAudio.
  double audioState_[2] = {};
};

#endif
