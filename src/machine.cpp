#include "machine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

namespace fs = std::filesystem;

namespace {
constexpr uint8_t kTimer0Vector = 0x04;
constexpr uint8_t kTimer1Vector = 0x06;
constexpr uint8_t kTif0 = 0x40;
constexpr uint8_t kTif1 = 0x80;
constexpr uint8_t kTie0 = 0x10;
constexpr uint8_t kTie1 = 0x20;
constexpr uint8_t kTde0 = 0x01;
constexpr uint8_t kTde1 = 0x02;

uint8_t BcdOrBinary(int value) {
  return static_cast<uint8_t>(value);
}
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
  audio_.clear();
  keys_.clear();
  firmwareKeys_.clear();
  membraneKeys_.clear();
  membraneKey_ = 0;
  membraneScansRemaining_ = 0;
  keyboardInitialized_ = false;
  biosWaiting_ = false;
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
  csioReady_ = false;
  rtcLatched_ = false;

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

void EurekaMachine::SampleRtc() const {
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  // Port order is fixed by the hardware, not by the firmware's own layout:
  // 90h hundredths, 91h hour, 92h minute, 93h second, 94h month, 95h date,
  // 96h year, 97h day of week (IOPORT.LIB, Appendix H).  Reading the ROM
  // alone suggests hour and second are exchanged, because the read loop at
  // 0DFA4 stores the ports into a descending buffer and then swaps 91h with
  // 93h and 94h with 95h at 0DFBB, so its internal buffer runs year, month,
  // date, hour, minute, second, hundredths.  Do not "fix" this order.
  rtcRegisters_[0] = BcdOrBinary(static_cast<int>((millis / 10) % 100));
  rtcRegisters_[1] = BcdOrBinary(local.tm_hour);
  rtcRegisters_[2] = BcdOrBinary(local.tm_min);
  rtcRegisters_[3] = BcdOrBinary(local.tm_sec);
  rtcRegisters_[4] = BcdOrBinary(local.tm_mon + 1);
  rtcRegisters_[5] = BcdOrBinary(local.tm_mday);
  rtcRegisters_[6] = BcdOrBinary(local.tm_year % 100);
  rtcRegisters_[7] = BcdOrBinary(local.tm_wday);
  rtcLatched_ = true;
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

uint8_t EurekaMachine::ReadMembraneKeyboard(uint8_t port) {
  if (membraneKey_ == 0 && !membraneKeys_.empty()) {
    membraneKey_ = membraneKeys_.front();
    membraneKeys_.pop_front();
    // A Windows key-down is represented as a short physical press.  The ROM
    // observes three stable scans before accepting a chord and scans this path
    // through its heartbeat scheduler, so keep it asserted for a little over
    // 300 ms at the documented 75 Hz scan rate.
    membraneScansRemaining_ = 25;
  }
  uint8_t value = 0;
  if (membraneKey_ != 0) {
    const uint8_t kind = membraneKey_ & 0xc0;
    const uint8_t number = membraneKey_ & 0x0f;
    if (port == 0x8a && kind == 0xc0 && number < 8) value = 1u << number;
    if (port == 0x89) {
      if (kind == 0x80) value |= number & 0x0f;
      // The production A4 ROM decodes the second modifier from row 2 bit 7.
      if ((membraneKey_ & 0x20) != 0) value |= 0x80;
      if (membraneScansRemaining_ > 0 && --membraneScansRemaining_ == 0)
        membraneKey_ = 0;
    }
    // Despite an early manual's row labels, this ROM image derives K_SHIFT
    // from row 0 bit 6 (the otherwise unused matrix position).
    if (port == 0x8c && (membraneKey_ & 0x10) != 0) value |= 0x40;
  }
  return value;
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
    case 0x0290: return 0;
    case 0x0291: return machine->io_[0x91];
    default: break;
  }
  if (low >= 0x90 && low <= 0x97) return machine->ReadRtc(low);
  switch (low) {
    case 0x04: return static_cast<uint8_t>(machine->io_[0x04] | 0x02);
    case 0x05: return static_cast<uint8_t>(machine->io_[0x05] | 0x02);
    case 0x08: return 0;
    case 0x09: return 0;
    case 0x0a: return static_cast<uint8_t>(machine->io_[0x0a] |
                                           (machine->csioReady_ ? 0x80 : 0));
    case 0x0b:
      machine->csioReady_ = false;
      return 0xaa;
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
    case 0xb8: return 0xff;
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
      return;
    case 0x0290: case 0x0291:
      machine->io_[static_cast<uint8_t>(port)] = value;
      return;
    default: break;
  }
  if (low != 0x10) machine->io_[low] = value;
  switch (low) {
    case 0x0a: machine->csioReady_ = true; break;
    case 0x0b: break;
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
  // A mounted folder is always a present, spinning, unprotected disk here, so
  // the pulse is held asserted rather than timed to a 200ms revolution -- the
  // firmware only ever asks whether it appears, never how often.
  uint8_t status = 0x02;               // INDEX
  if (fdcTrack_ == 0) status |= 0x04;  // TRACK 00
  return status;                       // bit 6 (write protect) stays clear
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
      fdcStatus_ = 0x10;
    }
  } else if ((command & 0xe0) == 0xa0) {
    fdcBuffer_.assign(512, 0);
    fdcWriting_ = true;
    fdcStatus_ = 0x03;
  } else if (type == 0xc0) {
    fdcBuffer_ = {fdcTrack_, static_cast<uint8_t>(outputLatch_ & 1),
                  fdcSector_, 2, 0, 0};
    fdcStatus_ = 0x02;  // Read Address completes on its own, as above.
  } else if (type == 0xd0) {
    fdcStatus_ = 0;
  } else if (type == 0xe0) {
    fdcBuffer_.resize(5120);
    for (unsigned sector = 1; sector <= 10; ++sector) {
      disk_.ReadPhysicalSector(fdcTrack_, outputLatch_ & 1, sector,
                               fdcBuffer_.data() + (sector - 1) * 512);
    }
    fdcStatus_ = 0x02;  // Read Track completes on its own, as above.
  } else if (type == 0xf0) {
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
  while (audioPhase_ >= kCpuHz) {
    audio_.push_back(static_cast<int16_t>((static_cast<int>(dac_) - 128) << 8));
    audioPhase_ -= kCpuHz;
  }
}

void EurekaMachine::Advance(uint32_t cpuCycles) {
  cycles_ += cpuCycles;
  RenderAudio(cpuCycles);
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
  if (cpu_.pc >= base && cpu_.pc < static_cast<uint16_t>(base + 23 * 3) &&
      (cpu_.pc - base) % 3 == 0) {
    function = (cpu_.pc - base) / 3;
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
      cpu_.a = keys_.empty() ? 0 : 0xff;
      ReturnFromCall();
      return true;
    case 3:  // console input
      if (keys_.empty()) {
        biosWaiting_ = true;
        return false;
      }
      cpu_.a = keys_.front();
      keys_.pop_front();
      // H bit 0 distinguishes Eureka function/cursor key codes from ordinary
      // text in the ROM console ABI.
      cpu_.h = (cpu_.a & 0x80) != 0 ? 1 : 0;
      keyboardInitialized_ = true;
      biosWaiting_ = false;
      ReturnFromCall();
      return true;
    case 4:  // console output: capture, then let the ROM feed screen and speech.
      consoleOutput_.push_back(cpu_.c);
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

bool EurekaMachine::Step() {
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
  if (biosWaiting_) {
    keys_.push_back(key);
    return;
  }
  // Eureka key codes with bit 7 set describe function/cursor chords on the
  // built-in 20-key keyboard.  Present them on the real keyboard ports so the
  // ROM's 75 Hz scanner and debounce logic see exactly the original hardware.
  if ((key & 0x80) != 0) {
    membraneKeys_.push_back(key);
  } else if (keyboardInitialized_) {
    // Printable keys from a Windows keyboard enter the same ROM-owned queue
    // as characters decoded from the optional IBM PC/XT keyboard interface.
    firmwareKeys_.push_back(key);
  } else {
    keys_.push_back(key);
  }
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
