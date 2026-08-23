# Tests

`codec_test.cpp` verifies Unicode ↔ Kamenicky conversion.

`integration_test.cpp` boots the real ROM and has two modes:

- `com` starts `READ.COM` through Shift+F7 and verifies its prompt;
- `bas` opens Eureka BASIC, loads `BEEP.BAS`, issues `RUN`, and verifies that
  the program keeps producing emulated output/audio.

The test disk folder must contain a native Eureka `READ.COM` and `BEEP.BAS`.
These firmware/application files are deliberately not licensed as part of the
emulator source tree.

`diag_probe.cpp` boots the ROM with diagnostics enabled and prints the report.
It needs no application files, only the ROM and any folder to act as a disk:

```text
diag_probe A4ROM.DMP some-empty-folder [instructions]
```

It answers two questions that the ROM image alone cannot: whether
`EurekaMachine::kRamBase` is placed correctly (any dropped write below it is
listed with the physical page and the program counter that produced it), and
which external I/O ports the machine still does not implement. It also prints
per-bit activity for the three write-only control latches, which is the
cheapest way to find out what a still-unnamed bit is wired to.

Mode `sweep` resets the machine once per function key, boots to the first
console prompt, presses the key and runs on, so each ROM application is
exercised in isolation. The function keys map as follows:

| key | application | key | application |
|---|---|---|---|
| F1 | zaznamnik | Shift+F1 | textovy procesor |
| F3 | kalkulator | Shift+F3 | teplomer (TIC/TIF) |
| F4 | komunikace | Shift+F4 | voltmeter (DVM) |
| F5 | telefonni seznam | Shift+F5 | databaze |
| F6 | prekladac bejsiku | Shift+F6 | diskove funkce |
| F7 | hudebni editor | Shift+F7 | spustit program z disku |
| F8 | adresar disku | Shift+F8 | formatovat disk |

Results of the current sweep:

- no unmodelled external ports at all;
- `kRamBase = 0x70000` holds throughout. The only dropped writes are three
  bytes at physical 1C1FAh, where the ROM runs its in-place uppercase routine
  (1CAF0h) over its own `"*.*"` string literal. Real hardware discards those
  writes too, so this is firmware sloppiness rather than a mapping error;
- RLDR0 is rewritten 13041 times in one sweep, which is the firmware changing
  the speech sample rate at runtime — the emulated audio rate must therefore
  follow the timer rather than be assumed constant;
- latch bits still never exercised: B0 bit 0 (disk density), B0 bit 7 (RTS),
  A0 bit 2 (output device select). Reaching those needs an actual format, an
  actual serial session and an actual print.

The temperature and voltage the sweep reports come from the synthetic
comparator thresholds in `EurekaMachine::ReadInputBuffer`, not from anything
in the ROM.

Mode `seq` drives the machine with a scripted sequence. A token is either
`kXX` (one key code in hex) or a literal string typed as text; `~` stands for
Enter. The machine is run until the BIOS blocks on console input before each
token, so the script follows the ROM's own pacing:

```text
diag_probe A4ROM.DMP disk-folder seq 15000000 kD7 Y
```

Note that the yes/no prompt answers to `Y`, not to `a`: the check at physical
19FD1h is `CP 59h`, twice over, which looks like a leftover from the English
build even though the ROM speaks "ano".

Formatting cannot currently be driven to completion. The ROM answers "v
jednotce neni disk", from the error dispatch at physical 13D08h, which decodes
a firmware-composed status byte:

| bit | message |
|---|---|
| 0 | v jednotce neni disk |
| 1 | slaba baterie |
| 2 | formatovaci chyba |
| 6 | disk je chranen proti zapisu |

Only bit 6 lines up with a WD177x status bit, so the byte is assembled by the
firmware from more than the controller.

Bit 0 is now solved. It is not a hardware line at all but a timeout: the poll
loop at physical 19837h waits for INTRQ on port A8h bit 1 or for DRQ on the
controller status, counts down, and returns 01h when neither arrives. The
model never asserted INTRQ, so every disk command timed out. `fdcIntrq_` in
`EurekaMachine` now provides it, with WD177x semantics: raised when a type I
command or Force Interrupt completes and when a data transfer runs out,
cleared by reading the status register or by writing a new command.

Mode `trace` records every access to a chosen set of ports even when the model
implements them, and stops the moment the ROM speaks a given phrase, so the
ring buffer ends on the decision being investigated:

```text
diag_probe A4ROM.DMP disk-folder trace 20000000 baterie
```

With INTRQ in place the disk sequence runs to completion -- motor on via A0
bit 0, drive select via B0 bit 1, `IN (99h)`, `OUT (9Bh)`, Seek command 10h,
INTRQ observed, status read, motor off. Formatting now stops one gate later,
on "slaba baterie", which is bit 1 of the same status byte. That bit does not
come from the DAC comparators in `ReadInputBuffer`: raising both thresholds to
E0h changes nothing. Its source is still unknown, and it is what now stands
between the probe and the first exercise of DMA channel 1 and Write Track.

One incidental find: the `OUT (88h),ADh` that this review flagged as
unexplained is issued at 19A01h before every controller command. The routine
called immediately after it (19A3Eh) turns out to be a BUSY check that issues
Force Interrupt, so the ADh write is part of the pre-command sequence rather
than anything to do with audio. What it actually drives is still open.
