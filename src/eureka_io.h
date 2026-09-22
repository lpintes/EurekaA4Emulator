#pragma once

#include <cstdint>

// Port numbers, bit masks and controller command codes for the hardware this
// emulator models.  Nothing here is a decision: every name is the one its
// source document uses, transcribed into the project's kPascalCase.  Keeping
// the original spelling is the point -- a name in this file can be grepped in
// the manual's own sources and the question of what a bit means is then
// settled by the manufacturer rather than by whoever wrote the code.  The
// manual is third-party material and therefore not in this repository: set
// EUREKATECH to your copy of it (see ROM-NOTICE.txt), and read the file names
// below as relative to that.
//
// Four sources, in descending order of authority for this machine:
//
//   IOREG.LIB    HD64180 internal registers, ports 00h-3Fh.  Appendix H says
//                to consult Hitachi for their bits, so the bit names below
//                come from the HD64180 data sheet and are marked as such.
//   IOPORT.LIB   external ports 80h-BFh with their bit masks (Appendix H).
//   KB.H         Eureka key codes as an application sees them.
//   WD177x       the floppy controller's own data sheet -- not in eurekatech/,
//                so those names carry a comment saying where the ROM confirms
//                them.
namespace hw {

// ---------------------------------------------------------------------------
// HD64180 internal registers (IOREG.LIB).  These sit at fixed port numbers
// below 40h; the ICR at 3Fh could move the whole block, and this ROM never
// writes it.

// ASCI, the two asynchronous serial channels.
constexpr uint8_t kCntla0 = 0x00;  // cntla0
constexpr uint8_t kCntla1 = 0x01;  // cntla1
constexpr uint8_t kCntlb0 = 0x02;  // cntlb0
constexpr uint8_t kCntlb1 = 0x03;  // cntlb1
constexpr uint8_t kStat0 = 0x04;   // stat0
constexpr uint8_t kStat1 = 0x05;   // stat1
constexpr uint8_t kTdr0 = 0x06;    // tdr0
constexpr uint8_t kTdr1 = 0x07;    // tdr1
constexpr uint8_t kRdr0 = 0x08;    // rdr0
constexpr uint8_t kRdr1 = 0x09;    // rdr1

// Data sheet: STAT bit 1 is TDRE and reads high whenever the transmit data
// register is free.  The ROM probes both channels expecting that.
constexpr uint8_t kStatTdre = 0x02;

// CSI/O, the clocked serial port.  This machine wires the PC keyboard to it.
constexpr uint8_t kCntr = 0x0a;  // cntr
constexpr uint8_t kTrdr = 0x0b;  // trdr

// Data sheet: CNTR bit 5 enables the receiver, bit 6 the end-of-transfer
// interrupt, and bit 7 is the end flag the handler clears by reading TRDR.
constexpr uint8_t kCntrRe = 0x20;
constexpr uint8_t kCntrEie = 0x40;
constexpr uint8_t kCntrEf = 0x80;

// Programmable reload timers.
constexpr uint8_t kTmdr0l = 0x0c;  // tmdr0l
constexpr uint8_t kTmdr0h = 0x0d;  // tmdr0h
constexpr uint8_t kRldr0l = 0x0e;  // rldr0l
constexpr uint8_t kRldr0h = 0x0f;  // rldr0h
constexpr uint8_t kTcr = 0x10;     // tcr
constexpr uint8_t kTmdr1l = 0x14;  // tmdr1l
constexpr uint8_t kTmdr1h = 0x15;  // tmdr1h
constexpr uint8_t kRldr1l = 0x16;  // rldr1l
constexpr uint8_t kRldr1h = 0x17;  // rldr1h
constexpr uint8_t kFrc = 0x18;     // frc

// Data sheet: TCR holds two down-count enables, two interrupt enables and two
// flags.  The flags are read-only -- they clear by reading TCR and then the
// matching TMDR -- which is why kTcrWritable exists at all.
constexpr uint8_t kTcrTde0 = 0x01;
constexpr uint8_t kTcrTde1 = 0x02;
constexpr uint8_t kTcrTie0 = 0x10;
constexpr uint8_t kTcrTie1 = 0x20;
constexpr uint8_t kTcrTif0 = 0x40;
constexpr uint8_t kTcrTif1 = 0x80;
constexpr uint8_t kTcrWritable = 0x3f;
// The 64180 clocks the timers at PHI/20.
constexpr uint32_t kTimerPrescale = 20;

// DMA.
constexpr uint8_t kSar0l = 0x20;  // sar0l
constexpr uint8_t kSar0h = 0x21;  // sar0h
constexpr uint8_t kSar0b = 0x22;  // sar0b
constexpr uint8_t kDar0l = 0x23;  // dar0l
constexpr uint8_t kDar0h = 0x24;  // dar0h
constexpr uint8_t kDar0b = 0x25;  // dar0b
constexpr uint8_t kBcr0l = 0x26;  // bcr0l
constexpr uint8_t kBcr0h = 0x27;  // bcr0h
constexpr uint8_t kMar1l = 0x28;  // mar1l
constexpr uint8_t kMar1h = 0x29;  // mar1h
constexpr uint8_t kMar1b = 0x2a;  // mar1b
constexpr uint8_t kIar1l = 0x2b;  // iar1l
constexpr uint8_t kIar1h = 0x2c;  // iar1h
constexpr uint8_t kBcr1l = 0x2e;  // bcr1l
constexpr uint8_t kBcr1h = 0x2f;  // bcr1h
constexpr uint8_t kDstat = 0x30;  // dstat
constexpr uint8_t kDmode = 0x31;  // dmode
constexpr uint8_t kDcntl = 0x32;  // dcntl

// The bank field of a 20-bit DMA address is four bits wide.
constexpr uint8_t kDmaBankMask = 0x0f;
// A byte count of zero means a full 64K transfer.
constexpr uint32_t kDmaFullCount = 65536;

// Data sheet: DE0 and DE1 arm the two channels, and each may only be written
// while its own write-enable bit is clear in the same byte.  That is what
// stops an unrelated write to DSTAT from starting a transfer.
constexpr uint8_t kDstatDme = 0x01;
constexpr uint8_t kDstatDie0 = 0x04;
constexpr uint8_t kDstatDie1 = 0x08;
constexpr uint8_t kDstatDwe0 = 0x10;
constexpr uint8_t kDstatDwe1 = 0x20;
constexpr uint8_t kDstatDe0 = 0x40;
constexpr uint8_t kDstatDe1 = 0x80;
// Everything in DSTAT that is neither an enable nor one of their gates, and so
// is taken from a write as it stands.
constexpr uint8_t kDstatPlain = 0x0d;

// Data sheet: DCNTL bits 1-0 select what channel 1 does.  Bit 1 set means
// I/O to memory, and bit 0 set means the memory address counts down.
constexpr uint8_t kDcntlDim0 = 0x01;
constexpr uint8_t kDcntlDim1 = 0x02;
constexpr uint8_t kDcntlDim = 0x03;

// UM005004 Table 4: DCNTL bits 5-4 (IWI1, IWI0) give an external I/O cycle 1,
// 2, 3 or 4 wait states.  The on-chip registers below kInternalIoEnd ignore
// them.  The firmware writes 38h at 00012, four wait states, and SYSEQU.LIB
// says so: "slowio ... for 4 wait states on IO".
constexpr uint8_t kDcntlIwi = 0x30;
constexpr unsigned kDcntlIwiShift = 4;
constexpr uint8_t kInternalIoEnd = 0x40;

// The I/O address is sixteen bits wide and the on-chip registers answer only
// when the top eight of them are zero.  TECHMAN1/64180.4 says so outright and
// warns that Turbo Pascal's 8-bit port[] array cannot reach them for exactly
// that reason; IN0 and OUT0 exist because they force those bits to zero.
constexpr bool IsInternalRegister(uint16_t port) {
  return port < kInternalIoEnd;
}

// An address whose low byte names an on-chip register but whose high byte is
// not zero belongs to nobody: the 64180 does not decode it, and no Eureka
// peripheral has an I/O address with a low byte below 40h (TECHMAN1/64180.4).
//
// This is reachable from the machine, not a theoretical case.  The built-in
// BASIC compiles OUT to OUT (C),A with the whole 16-bit port in BC (0B5C1)
// and INP to IN A,(C) the same way (0B5B7), so OUT 306,240 addresses 0132h.
// Measured 22 September 2026; see ea4-dfd.
constexpr bool IsUndecodedIo(uint16_t port) {
  return static_cast<uint8_t>(port) < kInternalIoEnd && port >= 0x100;
}

// What a read of such an address gives back.  FFh is a **choice, not a
// measurement**: it is what an undriven bus with pull-ups reads as, and
// nothing in this project has measured what the real Eureka puts there.
// Do not cite this number as evidence of anything.
constexpr uint8_t kUndecodedIoRead = 0xff;

// The one exception among the on-chip registers.  UM005004 Table 4 gives the
// PRT data registers 0 to 4 wait states "as a function of internal
// synchronization" and does not say which; the manual bounds this number, it
// does not fix it.  Measured against a recording of the real machine: the
// tone generator's interrupt reads TMDR0L once per run, so this is the one
// free cost in the 540 T-states a note-timing loop has to share, and 3 is
// what makes the jingle land within 0,2 % (HANDOFF 6.10).
constexpr unsigned kTmdr0lWaits = 3;

// Interrupts, refresh and the MMU.
constexpr uint8_t kIl = 0x33;    // il
constexpr uint8_t kItc = 0x34;   // itc
constexpr uint8_t kRcr = 0x36;   // rcr
constexpr uint8_t kCbr = 0x38;   // cbr
constexpr uint8_t kBbr = 0x39;   // bbr
constexpr uint8_t kCbar = 0x3a;  // cbar
constexpr uint8_t kIcr = 0x3f;   // icr

// HD64180Z manual, ITC: TRAP is set by an undefined op code and software can
// only write it to 0; UFO says whether the third op code byte was the
// undefined one and is read-only.  WBOOT at E6B3h (196B3) tests TRAP.
constexpr uint8_t kItcTrap = 0x80;
constexpr uint8_t kItcUfo = 0x40;

// Data sheet: IL supplies bits 7-5 of the vector for every internal source.
constexpr uint8_t kIlVectorBase = 0xe0;

// Data sheet: the internal sources sit at fixed offsets from that base, and
// the ROM vector table at C180 is laid out to match.
constexpr uint8_t kVectorTimer0 = 0x04;  // PRT channel 0
constexpr uint8_t kVectorTimer1 = 0x06;  // PRT channel 1
// The clocked serial port, which is where this image puts the handler for the
// IBM PC keyboard.
constexpr uint8_t kVectorCsio = 0x0c;

// Data sheet: CBAR splits the 64K logical space.  The low nibble scaled by 4K
// starts the bank area, the high nibble scaled by 4K starts common area 1.
constexpr uint8_t kCbarBankMask = 0x0f;
constexpr uint8_t kCbarCommon1Mask = 0xf0;
// What the MMU holds out of reset: bank and common area 1 both at F000.
constexpr uint8_t kCbarReset = 0xf0;

// ---------------------------------------------------------------------------
// Modem latch (IOPORT.LIB).  Write only; SYSRAM keeps modem_copy.

constexpr uint8_t kModemLatch = 0x80;   // modem_latch
constexpr uint8_t kDtrMask = 0x01;      // dtr_mask
constexpr uint8_t kMdmModeMask = 0x3e;  // mdm_mode_mask
constexpr uint8_t kMantxMask = 0x40;    // mantx_mask
constexpr uint8_t kLoopMask = 0x80;     // loop_mask

// ---------------------------------------------------------------------------
// Membrane keyboard (IOPORT.LIB) and the DAC, which share 88h-8Fh: the
// keyboard answers reads, the DAC takes writes.
//
// These three ports are named for what they carry rather than bkb_row0/1/2,
// because the manual contradicts itself about which row number goes with which
// port.  IOPORT.LIB computes bkb_row0 = 89h, bkb_row1 = 8Ah, bkb_row2 = 8Ch,
// while the prose of Appendix H prints the same three descriptions against
// $8C, $8A and $89 -- rows 0 and 2 swapped.
//
// IOPORT.LIB is the one that is right, and the ROM settles it: 1D41F masks 89h
// with 3Fh to get the six dots, and 1D206 masks 8Ch with 0Fh to ignore the
// shift bit above the cursor keys.  hardware-map.md carries the full argument.
// A row number would only send the reader back to the half of the manual that
// is wrong, so it is dropped and the contents name the port instead.
constexpr uint8_t kKeyboardBase = 0x88;  // keyboard
constexpr uint8_t kBkbDots = 0x89;       // dots 1-6 in bits 0-5, space in bit 7
constexpr uint8_t kBkbFunction = 0x8a;   // function keys 1-8, one per bit
constexpr uint8_t kBkbCursor = 0x8c;     // cursor keys in bits 0-3, shift in 6

// Bits within those rows.  Dot order is the order the keys sit under the
// fingers, left to right, so bit 0 is dot 3 and bit 2 is dot 1.
constexpr uint8_t kBkbDot3 = 0x01;
constexpr uint8_t kBkbDot2 = 0x02;
constexpr uint8_t kBkbDot1 = 0x04;
constexpr uint8_t kBkbDot4 = 0x08;
constexpr uint8_t kBkbDot5 = 0x10;
constexpr uint8_t kBkbDot6 = 0x20;
constexpr uint8_t kBkbSpace = 0x80;
constexpr uint8_t kBkbShift = 0x40;

constexpr uint8_t kDacPort = 0x88;  // dac_port
// The DAC is unipolar, so silence is half scale rather than zero.
constexpr uint8_t kDacMidScale = 0x80;

// ---------------------------------------------------------------------------
// Real time clock (IOPORT.LIB).  The counters are at 90h-97h, a parallel bank
// of alarm RAM at 190h-197h, and the interrupt registers at 290h-291h.  Those
// are 16-bit port numbers: the high byte is address, not a mirror.

constexpr uint16_t kRtcBase = 0x0090;     // rtc_100th
constexpr uint16_t kRtcLast = 0x0097;     // rtc_dow
constexpr uint16_t kRtcRamBase = 0x0190;  // rtc_ram_100th
constexpr uint16_t kRtcRamLast = 0x0197;  // rtc_ram_dow
constexpr uint16_t kRtcStatus = 0x0290;   // rtc_status (read)
constexpr uint16_t kRtcMask = 0x0290;     // rtc_mask (write)
constexpr uint16_t kRtcCommand = 0x0291;  // rtc_command

// Index of each counter within the eight-register bank, in the order
// IOPORT.LIB lists them.
constexpr unsigned kRtcHundredths = 0;
constexpr unsigned kRtcHour = 1;
constexpr unsigned kRtcMinute = 2;
constexpr unsigned kRtcSecond = 3;
constexpr unsigned kRtcMonth = 4;
constexpr unsigned kRtcDate = 5;
constexpr unsigned kRtcYear = 6;
constexpr unsigned kRtcDow = 7;
constexpr unsigned kRtcRegisters = 8;

// Appendix H: bits 0-6 of rtc_status mirror the events named in rtc_mask, and
// bit 7 says an interrupt happened whatever the enable bit in rtc_command
// says.  Which event is which bit comes from the clock data sheet; the ROM
// confirms bit 0, the only one it enables, by polling it from the heartbeat
// (CF61).
constexpr uint8_t kRtcEventAlarm = 0x01;
constexpr uint8_t kRtcEventHundredth = 0x02;
constexpr uint8_t kRtcEventTenth = 0x04;
constexpr uint8_t kRtcEventSecond = 0x08;
constexpr uint8_t kRtcEventMinute = 0x10;
constexpr uint8_t kRtcEventHour = 0x20;
constexpr uint8_t kRtcEventDay = 0x40;
constexpr uint8_t kRtcEventMask = 0x7f;
constexpr uint8_t kRtcInterrupted = 0x80;
// An alarm register with bit 7 set is a don't care: the writer at 0DA5B puts
// 80h into every field the alarm does not name.  The counters never exceed 99,
// so the bit cannot collide with a real value.
constexpr uint8_t kRtcAlarmIgnore = 0x80;

// ---------------------------------------------------------------------------
// Floppy disk controller (IOPORT.LIB).  A WD177x behind four ports.

constexpr uint8_t kFdcBase = 0x98;     // fdc_base
constexpr uint8_t kFdcStatus = 0x98;   // fdc_status (read)
constexpr uint8_t kFdcCommand = 0x98;  // fdc_command (write)
constexpr uint8_t kFdcTrack = 0x99;    // fdc_track
constexpr uint8_t kFdcSector = 0x9a;   // fdc_sector
constexpr uint8_t kFdcData = 0x9b;     // fdc_data

// WD177x command codes.  The top nibble selects the command and the low
// nibble carries its flags, except that Read Sector and Write Sector each
// occupy two nibbles and so are matched on the top three bits.
constexpr uint8_t kFdcCommandType = 0xf0;
constexpr uint8_t kFdcCommandGroup = 0xe0;
constexpr uint8_t kFdcCmdRestore = 0x00;
constexpr uint8_t kFdcCmdSeek = 0x10;
constexpr uint8_t kFdcCmdStep = 0x20;          // 20h and 30h
constexpr uint8_t kFdcCmdStepIn = 0x40;        // 40h and 50h
constexpr uint8_t kFdcCmdStepOut = 0x60;       // 60h and 70h
constexpr uint8_t kFdcCmdReadSector = 0x80;    // 80h and 90h
constexpr uint8_t kFdcCmdWriteSector = 0xa0;   // A0h and B0h
constexpr uint8_t kFdcCmdReadAddress = 0xc0;
constexpr uint8_t kFdcCmdForceInterrupt = 0xd0;
constexpr uint8_t kFdcCmdReadTrack = 0xe0;
constexpr uint8_t kFdcCmdWriteTrack = 0xf0;
// Type I flag: update the track register as the head steps.  The format
// routine relies on it (50h at 19EE8) instead of seeking.
constexpr uint8_t kFdcFlagUpdateTrack = 0x10;
// Type I flag: verify the head landed where it was sent by reading an ID
// header off the track.  The ROM issues it exactly once -- 14h at 1980A, in
// fdc_ctl_disk_test -- and that one command is how the machine tells an empty
// drive from an unformatted diskette from a good one.
constexpr uint8_t kFdcVerify = 0x04;  // fdc_verify
// Type I commands leave bit 7 clear, and they finish inside the controller.
constexpr uint8_t kFdcTypeTwoOrThree = 0x80;

// WD177x status bits.  Bits 1, 2 and 4 mean different things after a Type I
// command than after a data transfer, which is why each carries two names.
constexpr uint8_t kFdcStatusBusy = 0x01;
constexpr uint8_t kFdcStatusIndex = 0x02;       // Type I
constexpr uint8_t kFdcStatusDrq = 0x02;         // Type II and III
constexpr uint8_t kFdcStatusTrack00 = 0x04;     // Type I
constexpr uint8_t kFdcStatusLostData = 0x04;    // Type II and III
constexpr uint8_t kFdcStatusCrcError = 0x08;
constexpr uint8_t kFdcStatusSeekError = 0x10;   // Type I
constexpr uint8_t kFdcStatusNotFound = 0x10;    // Type II and III
constexpr uint8_t kFdcStatusWriteProtect = 0x40;
constexpr uint8_t kFdcStatusMotorOn = 0x80;

// What the BIOS disk entries hand back in A.  DEVICES.10 lists them for
// fdc_ctl_read_track and BDOS branches on the same set at 1BB45: it speaks
// "disk je chranen proti zapisu" for 2, "disk neni zalozen" for 3, "slaba
// baterie" for 4 and "vadny disk" for everything else.  So the code chosen
// here is the sentence the user hears -- answering every failure with 1 made
// an empty drive report a faulty diskette.
constexpr uint8_t kDiskResultOk = 0x00;
constexpr uint8_t kDiskResultFaulty = 0x01;
constexpr uint8_t kDiskResultWriteProtected = 0x02;
constexpr uint8_t kDiskResultNoDisk = 0x03;
constexpr uint8_t kDiskResultLowBattery = 0x04;

// Geometry the controller and the ROM agree on.
constexpr unsigned kSectorBytes = 512;
constexpr unsigned kSectorsPerTrack = 10;
// Sector size code 2 in an ID header means 512 bytes.
constexpr uint8_t kSectorSizeCode = 2;
// What a freshly formatted surface reads back as.
constexpr uint8_t kFormatFill = 0xe5;

// ---------------------------------------------------------------------------
// Power latch (IOPORT.LIB).  Write only; SYSRAM keeps power_copy.

constexpr uint8_t kPowerLatch = 0xa0;  // power_latch
constexpr uint8_t kPolDisk = 0x01;     // pol_disk
constexpr uint8_t kPolModem = 0x02;    // pol_modem
constexpr uint8_t kPolRelay = 0x04;    // pol_relay
constexpr uint8_t kPolVoice = 0x08;    // pol_voice

// ---------------------------------------------------------------------------
// Input buffer (IOPORT.LIB).  Two analogue comparators and three modem lines;
// everything but the comparators is active low.

constexpr uint8_t kInputBuffer = 0xa8;  // input_buffer
constexpr uint8_t kVm1Mask = 0x01;      // vm1_mask
constexpr uint8_t kVm2Mask = 0x02;      // vm2_mask
constexpr uint8_t kCts1Mask = 0x04;     // cts1_mask
constexpr uint8_t kRingMask = 0x08;     // ring_mask
constexpr uint8_t kDcd0Mask = 0x20;     // dcd0_mask

// ---------------------------------------------------------------------------
// General purpose output latch (IOPORT.LIB).  Write only; SYSRAM keeps
// output_copy.

constexpr uint8_t kOutputLatch = 0xb0;    // output_latch
constexpr uint8_t kFdcSide = 0x01;        // fdc_side
constexpr uint8_t kFdcDrv0 = 0x02;        // fdc_drv0
constexpr uint8_t kKbRstMask = 0x04;      // kb_rst_mask
constexpr uint8_t kKbPrimeMask = 0x08;    // kb_prime_mask
constexpr uint8_t kFilterselMask = 0x10;  // filtersel_mask
constexpr uint8_t kDtmfMask = 0x20;       // dtmf_mask
constexpr uint8_t kVmselMask = 0x40;      // vmsel_mask
constexpr uint8_t kRts1Mask = 0x80;       // rts1_mask

// ---------------------------------------------------------------------------
// Power down strobe (IOPORT.LIB).  Any access cuts the main supply.

constexpr uint8_t kPwrStb = 0xb8;  // pwr_stb

// ---------------------------------------------------------------------------
// The external decoder only looks at the top five bits.  IOPORT.LIB hands out
// the 80h-BFh space in blocks of eight -- "80-87 Modem latch", "88-8F Digital
// to analog converter", "A0-A7 Power latch" and so on -- and each of the
// single register blocks answers on all eight addresses.  The ROM only ever
// uses the first one, but a program loaded from disk need not: the EUROU demo
// plays its samples with OUT (8Ch),A, and a model that decoded 88h alone
// dropped every one of them in silence.
//
// The blocks that do use the low bits keep them: the clock (90h-97h), the
// floppy controller (98h-9Fh) and the keyboard rows, which are one-hot in
// 88h-8Fh on reads.  Writes to that same block go to the DAC.
constexpr uint8_t kExternalBlockMask = 0xf8;  // top five bits select a block

constexpr uint8_t DecodedPort(uint8_t port, bool write) {
  const uint8_t block = port & kExternalBlockMask;
  switch (block) {
    case kModemLatch: case kPowerLatch: case kInputBuffer:
    case kOutputLatch: case kPwrStb:
      return block;
    case kDacPort: return write ? block : port;
    default: return port;
  }
}

// ---------------------------------------------------------------------------
// Eureka key codes (KB.H).  This is what an application receives, and what
// PressMembraneKey turns back into the rows above.

constexpr uint8_t kKeyKeypad = 0x80;    // K_KEYPAD
constexpr uint8_t kKeyFunction = 0xc0;  // K_FUNCTION
constexpr uint8_t kKeyShift = 0x10;     // K_SHIFT
constexpr uint8_t kKeyAlt = 0x20;       // K_ALT
// Both kinds carry bit 7, so it is what separates a key code from a character.
// KB.H does not name it, but every K_ constant in it is built this way.
constexpr uint8_t kKeyIsCode = 0x80;
// The two selector bits, and the key number under them.
constexpr uint8_t kKeyKindMask = 0xc0;
constexpr uint8_t kKeyNumberMask = 0x0f;
// Everything except the modifiers: two codes matching here are the same
// physical key, however it was modified.
constexpr uint8_t kKeyIdentityMask = 0xcf;

constexpr uint8_t kKeyUp = 0x81;        // K_UP
constexpr uint8_t kKeyDown = 0x82;      // K_DOWN
constexpr uint8_t kKeyMiddle = 0x83;    // K_MIDDLE
constexpr uint8_t kKeyLeft = 0x84;      // K_LEFT
constexpr uint8_t kKeyHome = 0x85;      // K_HOME
constexpr uint8_t kKeyEnd = 0x86;       // K_END
constexpr uint8_t kKeySpare = 0x87;     // K_SPARE
constexpr uint8_t kKeyRight = 0x88;     // K_RIGHT
constexpr uint8_t kKeyPgUp = 0x89;      // K_PGUP
constexpr uint8_t kKeyPgDn = 0x8a;      // K_PGDN
constexpr uint8_t kKeyIns = 0x8b;       // K_INS
constexpr uint8_t kKeyDel = 0x8c;       // K_DEL
// KB.H and KB.LIB disagree about the last two.  The ROM settles it in KB.LIB's
// favour: its PC keyboard table maps scan code 52h, Insert, to 8Dh and 53h,
// Delete, to 8Eh (DF05).  So these two are Insert and Delete in behaviour, and
// the KB.H names below are kept only because that is where the values are.
constexpr uint8_t kKeyCtrlUp = 0x8d;    // K_CTRLUP, Insert on the PC keyboard
constexpr uint8_t kKeyCtrlDown = 0x8e;  // K_CTRLDOWN, Delete
constexpr uint8_t kKeyPrtSc = 0x8f;     // K_PRTSC

constexpr uint8_t kKeyF1 = 0xc0;    // K_F1
constexpr uint8_t kKeyF9 = 0xc8;    // K_F9,  K_MODE
constexpr uint8_t kKeyF10 = 0xc9;   // K_F10, K_WHERE
constexpr uint8_t kKeySF9 = 0xd8;   // K_SF9
constexpr uint8_t kKeySF10 = 0xd9;  // K_SF10
// F11 is not in KB.H, which stops at K_F10.  The header is not the machine:
// the PC scan table in ROM (1DF05, indexed by scan code) maps 57h to CAh, and
// pressing it says "ROM operacniho systemu".  hardware-map.md carries the
// measurement and the matching braille chord.
constexpr uint8_t kKeyF11 = 0xca;
// Only eight function keys have a bit of their own in kBkbFunction; F9 and
// above are chords of the space bar and braille dots, decoded at 1D541.
constexpr uint8_t kFunctionKeysWithRowBit = 8;

// ---------------------------------------------------------------------------
// PC keyboard scan codes, as the CSI/O delivers them.

// A break code is the make code with bit 7 set.
constexpr uint8_t kScanBreak = 0x80;
// The prefix that marks the extended half of the keyboard.
constexpr uint8_t kScanExtended = 0xe0;
// The Reset command, and the "self test passed" answer every PC keyboard
// gives.  The ROM waits for exactly that at 18858.
constexpr uint8_t kKbReset = 0xff;
constexpr uint8_t kKbSelfTestOk = 0xaa;

}  // namespace hw
