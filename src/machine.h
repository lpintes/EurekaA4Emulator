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
  static constexpr std::size_t kPhysicalSize = 1u << 20;
  static constexpr std::size_t kRomSize = 0x40000;
  // Lowest physical address the machine treats as writable RAM.  This is a
  // working assumption: the ROM only ever loads immediate bank values up to
  // 70h, which is consistent with RAM sitting at the top of the address
  // space, but the firmware also programs CBR/BBR from variables.  Run with
  // --diag and watch the dropped-write report to confirm or move it.
  static constexpr uint32_t kRamBase = 0x70000;

  bool LoadRom(const std::filesystem::path& path, std::wstring& error);
  bool MountDisk(const std::filesystem::path& folder, std::wstring& error);
  bool FlushDisk(std::wstring& error) { return disk_.Flush(error); }
  void Reset();

  // Returns false only while BIOS console input is waiting for a host key.
  bool Step();
  void QueueKey(uint8_t key);
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
  std::size_t queued_keys() const { return keys_.size() + firmwareKeys_.size(); }
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

  void StartFdcCommand(uint8_t command);
  uint8_t ReadFdcData();
  void WriteFdcData(uint8_t value);
  uint8_t ReadRtc(uint16_t port) const;
  uint8_t ReadInputBuffer() const;
  uint8_t ReadMembraneKeyboard(uint8_t port);
  void InjectFirmwareKey();

  z80 cpu_{};
  std::array<uint8_t, kPhysicalSize> memory_{};
  std::array<uint8_t, 256> io_{};
  std::array<uint8_t, 8> rtcRam_{};
  VirtualDisk disk_;
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
  std::deque<uint8_t> membraneKeys_;
  uint8_t membraneKey_ = 0;
  unsigned membraneScansRemaining_ = 0;
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
  // The controller's INTRQ line, reported on port A8h bit 1.  The ROM
  // waits on it after every command (19837h) and calls the timeout
  // "v jednotce neni disk", so without it no disk command can finish.
  bool fdcIntrq_ = false;
  bool csioReady_ = false;
};

#endif
