# Tests

Build them with `build-tests.bat` (mingw64 from msys2, same toolchain as
the emulator). The executables land in `bin\` next to the emulator.

`codec_test.cpp` verifies Unicode ↔ Kamenicky conversion.

`integration_test.cpp` boots the real ROM and has three modes:

- `com` starts `READ.COM` through Shift+F7 and verifies its prompt;
- `bas` opens Eureka BASIC, loads `BEEP.BAS`, issues `RUN`, and verifies that
  the program keeps producing emulated output/audio;
- `kbd` presses all 38 key codes a host keyboard can produce, one at a time,
  and checks each against what the ROM's own decoder wrote to C638h.  Nothing
  on the machine's ports carries a key code -- it scans a twenty-key braille
  keyboard and works the code out at D4B0 -- so a row taken from the wrong
  port silently turns a cursor key into a braille letter, and the application
  then does whatever that letter means.  It then types "ahoj" on the six dot
  keys in the word processor and has the machine read the line back, which is
  the only way to catch a wrong bit order: the row bits run in key order, so
  bit 0 is dot 3 and bit 2 is dot 1.  Needs no files on the disk.

The test disk folder must contain a native Eureka `READ.COM` and `BEEP.BAS`.
A genuine `READ.COM` ships with the Technical Manual's development disk and is
in the tree at `eurekatech/TECHMAN1/READ.COM`; copy it into the disk folder.
It is third-party material and not covered by the emulator's MIT licence, so
`.gitattributes` keeps it out of line-ending conversion -- the test depends on
it being byte for byte what came off the original diskette.

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
| F2 | hodiny a kalendar | Shift+F2 | (silent) |
| F3 | kalkulator | Shift+F3 | teplomer (TIC/TIF) |
| F4 | komunikace | Shift+F4 | voltmeter (DVM) |
| F5 | telefonni seznam | Shift+F5 | databaze |
| F6 | prekladac bejsiku | Shift+F6 | diskove funkce |
| F7 | hudebni editor | Shift+F7 | spustit program z disku |
| F8 | adresar disku | Shift+F8 | formatovat disk |
| F9 | rezim | Shift+F9 | stav baterie |
| F10 | kde jsem | Shift+F10 | sebekontrola |

F2 announces nothing on entry; F10 is what says where you are.  F9 and F10 are
not keys of their own but chords of the space bar and braille dots -- see the
keyboard section of `hardware-map.md`.

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

All four bits are now solved, and the first reading of them was wrong in a way
worth recording, because the probe is what exposed it.

Bit 0 is a timeout: the poll loop at physical 19837h counts down and returns
01h if nothing arrives. The first fix asserted the controller's INTRQ on port
A8h bit 1, which did stop the timeout -- but for the wrong reason. Appendix H
of the Technical Manual gives A8h bit 1 as `vm2_mask`, the battery comparator,
and the loop treats a set bit as an *abort*: it returns 02h, which is bit 1,
"slaba baterie". So the disk stopped timing out and started reporting a flat
battery instead. INTRQ is not readable on any external port.

What the loop actually waits for is INDEX in the controller's own status
register on port 98h, which `TypeOneStatus()` now supplies.

Bit 1 follows from the same appendix. The `OUT (88h),ADh` at 19A01h before
every controller command sets the DAC as the comparator reference, and `OR 42h`
on the output latch selects the drive and switches the comparators to the
battery pair at the same time: the machine watches the battery for the whole
disk operation. Nothing was wrong with the thresholds in `ReadInputBuffer`,
which is why raising them to E0h changed nothing -- `fdcIntrq_` was overriding
the result.

Mode `trace` records every access to a chosen set of ports even when the model
implements them, and stops the moment the ROM speaks a given phrase, so the
ring buffer ends on the decision being investigated:

```text
diag_probe A4ROM.DMP disk-folder trace 20000000 formatovaci
```

Note that `trace` answers the format prompt only once. The firmware now gets
far enough to ask a second question ("already formatted, reformat?"), so the
mode stops one gate short of the format itself; drive it with `seq` instead:

```text
diag_probe A4ROM.DMP disk-folder seq 15000000 kD7 Y Y
```

Formatting now runs to completion and the machine says "formatovani skonceno":
162 track writes, 1620 verify reads, 81 steps. That was the first time DMA
channel 1 and Write Track were ever executed, and getting there took four more
model fixes -- DMA arming order and direction, BUSY on reads, and the type I
step commands. `HANDOFF.md` section 5 lists them.

The moral, and the reason this is written down: every wrong step above came
from reading the ROM alone and mistaking adjacency for causation. The probe
showed the machine misbehaving; only Appendix H said why.
