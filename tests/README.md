# Tests

Build them with `build-tests.bat` (mingw64 from msys2, same toolchain as
the emulator). The executables land in `bin\` next to the emulator.

`codec_test.cpp` verifies Unicode ↔ Kamenicky conversion.

`settings_test.cpp` covers the settings file: where it goes (a `config` folder
beside the EXE if there is one, `%APPDATA%\EurekaA4` otherwise), and that a
hand-edited file cannot cost the user the slots in it. The round trip is
checked with Slovak diacritics rather than ASCII, because ASCII survives every
encoding this could accidentally use -- the failure being guarded against is
the quiet one, where a saved path comes back mangled and the slot simply points
somewhere else. Runs without the ROM, in the system temp folder.

`disk_test.cpp` covers the diskette model: capacity, naming, swapping, the
unformatted state, and what a file looks like on its way back to the host.
Everything it needs it makes for itself in the system temp folder, because a
real disk folder is a moving target and a test that reads one measures whatever
happened to be there. The capacity numbers come from the Disk Parameter Block
in the technical manual -- 396 free blocks of 2 KiB and 256 directory entries
-- so a change in the disk model has to break this test before it breaks a
diskette. Runs without the ROM.

Two of its groups are worth knowing about before touching `VirtualDisk`:

- the checks around a diskette in memory write the image the only way a guest
  can, through `WritePhysicalSector`, laying down the directory entry and the
  data blocks by hand. That is not ceremony: a folder-backed diskette has
  `imported_` full, so it knows every file's exact length and the export never
  has to work one out. The paths that do have to work it out are reachable only
  from a diskette the host folder knows nothing about, and `Mount` cannot get
  there;

- `klasifikacia_typov_je_pribita` pins, one type at a time, which extensions
  the export may cut at the 01Ah end-of-file marker. `IsTextType` is an
  allowlist whose two mistakes cost differently: a type wrongly called text
  loses data for good, a type wrongly called binary keeps a few bytes of
  padding. `BAS` sat on that list for two years -- a saved BASIC program is
  binary, and 01Ah is an ordinary byte in it -- and only surfaced as a backup
  that would no longer load. Measured over 723 real files, cutting at the first
  01Ah would have destroyed 61 of 108 `.BAS`, 78 of 81 `.COM`, 95 of 240 `.MEL`
  and all 65 archives, two of them down to zero bytes. The roster in this test
  is the barrier; moving a type across it has to fail here rather than turn up
  as a damaged file months later. `HANDOFF.md` section 6.23 has the whole
  measurement.

`integration_test.cpp` boots the real ROM and has several modes:

- `format` boots with an unformatted RAM diskette -- the only medium on which
  the firmware's format routine has anything to do, since over a host folder it
  is a deliberate no-op (6.5) -- presses Shift+F8 and answers the two questions
  it asks. Then every one of the 160 tracks has to carry a format and the
  diskette has to be readable through the controller and the BIOS stub alike.
  Two things this measured, both in 6.5: the answer is `y` and not `a`, and the
  ROM asks "disk je uz naformatovan, preformatovat?" even about a blank one --
  it never reads the medium at all;

- `com` starts `READ.COM` through Shift+F7 and verifies its prompt;
- `bas` opens Eureka BASIC, loads `BEEP.BAS`, issues `RUN`, and verifies that
  the program keeps producing emulated output/audio;
- `kbd` presses all 38 key codes a host keyboard can produce, one at a time,
  and checks each against what the ROM's own decoder wrote to C638h.  Nothing
  on the machine's ports carries a key code -- it scans a twenty-key braille
  keyboard and works the code out at D4B0 -- so a row taken from the wrong
  port silently turns a cursor key into a braille letter, and the application
  then does whatever that letter means.  It then types "ahoj" on the six dot
  keys in the word processor and has the machine read it back with Home, which
  says the word the cursor is in and not the line (the line is space plus arrow
  up, A1h) -- one word is all this test types, so Home is enough.  Reading it
  back is the only way to catch a wrong bit order: the row bits run in key
  order, so bit 0 is dot 3 and bit 2 is dot 1.  The first chord is shifted and the word
  must come back as "Ahoj": shift is the keyboard's twentieth key, on row 8Ch,
  and the decoder sends a shifted chord through D72D (1D60C).  Then shift with
  the bare space bar, which is Escape and not a space (1D52F) -- the only key
  code on this machine that needs two rows held at once, so it is the sharpest
  check that the host presses shift as a key and not as a flag it keeps to
  itself.  Then it holds shift on its own in the middle of an utterance, which
  has to stop it: C621h is FFh only while speech is playing, and a key going
  down where the shadow has no bit copies it into spabrt (C620h) -- the way
  continuous reading in the word processor has always been paused, named as
  such in SYSJUMPS.11.  Measured, "hlavni menu" runs 783k cycles and ends after
  203k with shift, so the check asks only that it stopped before half.  A chord
  cannot fake this: a chord's shift arrives together with dots the shadow is
  about to learn anyway.  Last it types the same word again as IBM
  PC scan codes down the serial port, which exercises the reset handshake, the
  CSI/O interrupt and the ROM's own Czech QWERTZ tables.  Last it presses AltGr
  and lets it go, then types the same key on its own: the right Alt is the one
  modifier that lives behind an E0 prefix, and a break sent without that prefix
  clears the left Alt bit instead and leaves the right one set for good, after
  which every key is read through the AltGr table.  Expects 40h 88h -- '@' then
  'ě'; a stuck AltGr shows up as a second '@'.  Needs no files on the disk.
- `power` presses all four cursor keys and checks the machine switches itself
  off through pwr_stb;
- `dc` checks the output settles to silence after speech, whatever the DAC is
  left holding;
- `rtc` sets an alarm in the clock and calendar application, checks it reached
  the RTC alarm registers, then moves the clock into that minute and checks the
  firmware services it.  The RTC raises no interrupt on this machine, so an
  alarm is found only by polling rtc_status; while that port answered zero, the
  alarm, the chime and the diary were dead together.  Needs no files on the
  disk.  Typing here goes down the serial port, and the ROM's keyboard table is
  Czech QWERTZ -- the digits need shift.
- `hudba` opens the music composer, which plays its jingle on entry, and checks
  that the space bar stops it.  The player's own stop test reads the keyboard
  rows directly (8Ch at 10F13, 89h at 10F1C) and never looks at the ROM's key
  queue, so only a key that reaches the rows silences the tune.  The same
  window is measured twice, once with the space bar pressed and once with
  nothing pressed at all: without the second run the check would pass just as
  happily on a tune that had ended by itself.  Needs no files on the disk.

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

F2 announces nothing on entry; F10 is what says where you are.  F9, F10 and
F11 are not keys of their own but chords of the space bar and braille dots --
see the keyboard section of `hardware-map.md`.

The sweep stops at F10 because that is where the membrane's own codes stop
being interesting, but the row does not: **F11 is "ROM operacniho systemu"**,
the dates of every ROM module, and F12 is unused.  On the PC keyboard they are
scan codes 57h and 58h (CAh and CBh at 1DF05); on the braille keyboard F11 is
the "d" chord, 9Ch.

**Alt+Fn names the function instead of entering it**, which is the row a user
walks along to find out what is where.  Measured, because the speech alone
does not show it -- F4 and Alt+F4 both say "komunikace", and the difference
only appears on what comes next: after F4 an Escape asks "ukoncit?, ano nebo
ne?", so the machine is inside communications, while after Alt+F4 it is silent
because the machine never left the main menu.  Alt+F11 says "data ROMu" and
Alt+F12 "nepouzito", which is how the row was found in the first place.

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
Enter.  Text goes in on the emulated PC keyboard, on the keys the ROM's own
tables put those characters on, so a character that is on none of them is
refused out loud instead of being typed as something near it.  Between tokens
the machine is run until it has been quiet for half a second, so the script
follows the ROM's own pacing rather than a guessed instruction count -- the CPU
is not parked at console input any more, so there is no block to wait for:

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

Formatting runs to completion and the machine says "formatovani skonceno":
162 track writes, 1620 verify reads, 81 steps.  The sequence above no longer
reaches that on its own, though: `RunUntilPrompt` returns half a second after
the console falls quiet, and a format is a long quiet, so `seq` prints its last
answer and exits within four million instructions.  Driven by hand and then
simply left running, the same machine still says it after 206 million, so what
is missing is a way to tell the sequence "now just run" -- not anything in the
model.

That was the first time DMA channel 1 and Write Track were ever executed, and
getting there took four more model fixes -- DMA arming order and direction,
BUSY on reads, and the type I step commands. `HANDOFF.md` section 5 lists them.

The moral, and the reason this is written down: every wrong step above came
from reading the ROM alone and mistaking adjacency for causation. The probe
showed the machine misbehaving; only Appendix H said why.
