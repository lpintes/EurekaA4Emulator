# Tests

Build them with `build-tests.bat` (mingw64 from msys2, same toolchain as
the emulator), or with `build-tests.sh` on Linux and WSL, which uses the
mingw-w64 cross compiler and produces the same Windows executables. Either
way they land in `bin\` next to the emulator.

`codec_test.cpp` verifies Unicode ↔ Kamenicky conversion.

`zex_test.cpp` runs ZEXDOC, the Z80 instruction exerciser, on the bare core
with no machine around it. The manual check in `HANDOFF.md` 6.33 covered
timing and the Z180 extensions; ZEXDOC covers what that could not, the
results and documented flags of the ordinary Z80 instructions, as CRCs
against a real Z80. The program is not in the tree (GPL, someone else's
work) and is not part of `run-tests.bat`; pass its path:

```text
zex_test C:\b\z80-tests\zexdoc.com
```

It serves BDOS functions 2 and 9, stops on a jump to 0000h and prints
`PASS` when the program reached "Tests complete" without an "ERROR". A run
is 5.8 billion instructions, about two minutes. ZEXALL runs too, but it
also checks the undocumented X and Y flags, which nothing here depends on.

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

Four of its groups are worth knowing about before touching `VirtualDisk`:

- the checks around an unsaved diskette write the image the only way a guest
  can, through `WritePhysicalSector`, laying down the directory entry and the
  data blocks by hand. That is not ceremony: a diskette with a home folder has
  `imported_` full, so it knows every file's exact length and the export never
  has to work one out. The paths that do have to work it out are reachable only
  from a diskette no host folder knows anything about, and `Mount` cannot get
  there;

- `StashKeepsTheLock` pins that the write-protect notch travels onto the shelf
  and back, that one slot's notch is not another's, and that it can be moved
  while the diskette is *on* the shelf. It was written before the slots dialog
  was allowed to offer that (6.26), where the box had been greyed for unsaved
  slots -- which read as "this diskette cannot be locked" when the truth is
  only that the lock cannot be written into `nastavenia.txt`. The last three
  checks pin the one honest reason to grey it: a slot that holds no diskette
  yet has no notch to move;

- `SavingAdoptsTheFolder` pins that saving is Save As. The half that matters is
  the second one: after saving, a file the guest then deletes has to leave the
  new folder. Copying the image out and leaving `imported_` empty would look
  identical on the day -- every file there, contents right -- and the diskette
  would quietly stop honouring deletions from then on, because the write-back
  only trashes host files it knows about;

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

The last group in that file is not about `VirtualDisk` at all: it covers
`disk_layout`, the splitter that cuts a collection too big for one diskette
into diskettes. It shares this test because it shares the numbers -- both ask
`cpm_disk.h` what a block costs and what a file will be called -- and because
it needs neither a folder nor the ROM: the input is a list of names and sizes,
the output is a plan. Each of the three limits gets its own check (396 blocks,
256 directory entries, unique 8.3 names within one diskette); `PlanProblem`
re-derives every diskette from the input and refuses a plan in which a file
turns up twice, in neither list, or on a diskette where its name is already
taken. Two more pin the rules that make a plan usable rather than merely
valid: `LayoutNeverSplitsAUnit`, because `TP.COM` without `TURBO.MSG` is a
broken program, and `LayoutNeverRenamesInsideAUnit`, because a program whose
companion file was renamed fails in a way nobody can read -- even with both of
them on the same diskette.

`integration_test.cpp` boots the real ROM and has several modes:

- `format` boots with an unformatted unsaved diskette -- the only medium on which
  the firmware's format routine has anything to do, since over a host folder it
  is a deliberate no-op (6.5) -- presses Shift+F8 and answers the two questions
  it asks. Then every one of the 160 tracks has to carry a format and the
  diskette has to be readable through the controller and the BIOS stub alike.
  Two things this measured, both in 6.5: the answer is `y` and not `a`, and the
  ROM asks "disk je uz naformatovan, preformatovat?" as a second confirmation.
  That second question used to arrive even about a blank diskette, and 6.5
  recorded it as unconditional; it is not. The ROM asks fdc_ctl_disk_test
  first and skips it when the medium has no format at all -- what made it look
  unconditional was the model answering that test with "formatted" whatever was
  in the drive (6.30). Measured: on an unformatted diskette the format now runs
  straight through on one `y`;

- `hlaseni` asks the machine about drives it cannot read, entry point by
  entry point, and checks it says what the real machine said when the owner
  measured it (HANDOFF 6.30, 7. 9. 2026). An empty drive is "disk neni
  zalozen" through F8, the word processor (reading and saving) and BASIC
  (`LOAD` and `SAVE`, both with "chyba 21"), but "v jednotce neni disk"
  through the disk functions (Shift+F6) -- and through formatting, where it
  must come only after "mam formatovat disk, ano nebo ne?" has been answered;
  the question arriving first, with no such sentence in the 30M instructions
  before the answer, is checked. A diskette that never was formatted is
  "vadny disk" through F8 and "disk neni naformatovan, chces jej
  naformatovat?" through the disk functions. Every other case checks that
  "vadny disk" did *not* come out, which is the whole point: that sentence
  means a damaged diskette, and it used to be the answer to all of them.
  Mutation-tested: BIOS answering an empty drive with 1 instead of 3 fails
  the five cases that go through it, every verify succeeding fails the two
  disk-function ones. See 6.30 for where each sentence is composed in the ROM;

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
  about to learn anyway.  Then it taps shift instead -- `TapShift`, pressed for
  a moment and let go on its own, which is what a tap on Ctrl does in the
  window (HANDOFF 6.35) -- in the same utterance, asked once on the membrane
  and once as scan code 44h on the PC keyboard, and both have to stop.  Half a
  second after a tap a bare space bar must not come out as Escape, which is
  what a shift left down would make of it.  A bare space bar leaves C638h at
  00 there, so only 1Bh is asked about; verified by mutation, a two-second tap
  fails it, and a zero-length one fails both speech checks.  Last it types the
  same word again as IBM
  PC scan codes down the serial port, which exercises the reset handshake, the
  CSI/O interrupt and the ROM's own Czech QWERTZ tables.  Last it presses AltGr
  and lets it go, then types the same key on its own: the right Alt is the one
  modifier that lives behind an E0 prefix, and a break sent without that prefix
  clears the left Alt bit instead and leaves the right one set for good, after
  which every key is read through the AltGr table.  Expects 40h 88h -- '@' then
  'ě'; a stuck AltGr shows up as a second '@'.  Needs no files on the disk.
- `power` presses all four cursor keys and checks the machine switches itself
  off through pwr_stb;
- `snimka` covers the RAM snapshot (HANDOFF 6.15): it boots, arms an alarm so
  `rtcRam_` carries something -- the one part of the snapshot that is not in
  `memory_` -- and calls `SaveSnapshot` to a temp file.  A fresh machine
  `LoadSnapshot`s it, and the check is threefold: the eight clock bytes come
  back; a second save from the restored machine reproduces the file byte for
  byte, which pins the RAM and the ROM MD5 without depending on the MMU state;
  and `PowerOn` on the restored RAM comes up without "inicializace eureky"
  while a cold `Reset` on the same machine still says it.  Then the three
  refusals: a missing file is `kMissing`, a short/wrong-magic file is
  `kCorrupt`, and a good file with one byte of the ROM MD5 flipped is
  `kRomMismatch` -- none of them touch the machine.  It also checks `md5.cpp`
  against two RFC 1321 vectors, so a broken hash fails here rather than
  silently letting every mismatched snapshot load.  Needs no files on the
  disk;
- `dc` checks the output settles to silence after speech, whatever the DAC is
  left holding;
- `rtc` first checks that F2 announces the time: the hours, then the minutes
  once the hours have been said.  Both are spoken from the conversion buffer
  by `.spconv`, not by `.speak`, so a speech capture that loses that entry
  fails here and nowhere else.  The clock is set to seven minutes past the
  hour for it, because at a full hour the minutes sentence is empty.  Then it
  sets an alarm in
  the clock and calendar application, checks it reached
  the RTC alarm registers, then moves the clock into that minute and checks the
  firmware services it.  The RTC raises no interrupt on this machine, so an
  alarm is found only by polling rtc_status; while that port answered zero, the
  alarm, the chime and the diary were dead together.  Needs no files on the
  disk.  Typing here goes down the serial port, and the ROM's keyboard table is
  Czech QWERTZ -- the digits need shift.
- `akord` pins the membrane keyboard's live-matrix layer (ea4-91z, HANDOFF
  6.18): held through `HoldMembrane` dot by dot rather than sent as one
  finished `PressBraille` chord, dot 1 makes a sound, adding dot 2 makes
  another, adding dot 4 a third -- proof the ROM saw each new state, not
  which letter, since the echo goes through `.spchar` and never reaches the
  speech transcript. Letting go of dot 4 alone while 1 and 2 stay down has to
  be silent: the matrix falls back to a state already seen, not a new one.
  What lands in the text once 1 and 2 also let go is checked by reading the
  line back with Home: "f", the union of every dot the fingers reached during
  the one continuous press. Needs no files on the disk.
- `budik` pins waking a switched-off machine on its alarm (HANDOFF 6.32).  It
  arms an alarm through F2 and Shift+F3, leaves to the Main Menu with Escape
  and switches off with the cursor chord, then moves the clock ten seconds
  into the alarm's minute and calls `WakeOnAlarm()`.  It checks that the
  alarm registers and `rtc_mask` survive switching off, that the machine
  wakes and `rtc_mask` is still armed afterwards -- the bug it exists for,
  `PowerOn()` used to zero it -- that the loudspeaker moves, and that the
  alarm nobody answers re-arms itself for its next occurrence and puts the
  machine back to sleep with `C45Ah` still `FFh`.  Audio is collected while
  it runs, because `Grind` throws samples away.  Verified by mutation: a
  `WakeOnAlarm()` that never wakes and a `PowerOn()` that zeroes the mask
  each fail it.  Needs no files on the disk.
- `hudba` opens the music composer, which plays its jingle on entry, and checks
  that the space bar stops it.  The player's own stop test reads the keyboard
  rows directly (8Ch at 10F13, 89h at 10F1C) and never looks at the ROM's key
  queue, so only a key that reaches the rows silences the tune.  The same
  window is measured with the space bar pressed, with nothing pressed at all
  -- without that run the check would pass just as happily on a tune that had
  ended by itself -- and with a tap on shift (`TapShift`).  The player stops on
  row 8Ch only for a bit its previous read lacked (shadow refreshed at 10F1B)
  and reads it every 13.7 to 20.0 ms, so a tap too short to span one read
  would miss; a zero-length tap fails it (verified by mutation).  Needs no
  files on the disk.
- `zvuk` pins what the output stage does to the DAC, and it is the only thing
  that does.  The firmware never runs: `debug_feed_dac` puts a 440 Hz sine on
  the pin at the tune's own DAC rate (540 cycles a step, `RLDR0` = 26) and a
  Goertzel measures how much energy lands away from the fundamental and its
  harmonics.  The threshold is 55 dB with ten to spare either way.  Reading
  the DAC at the instant of each output sample instead of integrating it over
  the sample's interval -- 540 cycles is 4.22 samples at 48 kHz, so the edge
  of every step jitters -- drops that to 43 dB, and it was heard as the music
  crackling (HANDOFF 6.38).  Verified by mutation: that change fails this mode
  while `hudba` passes regardless.  Needs no files on the disk.
- `trap` pins the Z180 trap on an undefined op code (ea4-cyq, HANDOFF 6.33
  point 5).  It writes the 33 bytes of TRAP.COM into a folder of its own in
  `%TEMP%` -- the shared test diskette is mounted by every mode at once --
  and runs it with Shift+F7.  The program says "pred", executes `ED 77` and
  says "po".  A real Eureka said "pred", then "ahoj" and went back to the Main
  Menu, so "ahoj" has to follow "pred" with no "po" between them.  Verified
  by mutation: with `z180_traps` off the machine says "pred po ahoj" and the
  mode fails.

The test disk folder must contain a native Eureka `READ.COM` and `BEEP.BAS`.
A genuine `READ.COM` ships with the Technical Manual's development disk, which
is third-party material and therefore **not in this repository** -- see
`ROM-NOTICE.txt`.  Point `EUREKATECH` at your copy of that folder (the default
is `C:\b\eurekatech`) and `run-tests.bat` copies the file in for you.

Without it the `com` and `wp` modes are skipped and the suite reports sixteen
`PASS` lines instead of eighteen, saying so as it goes.  Both modes run that
same `READ.COM`: `com` to start it, `wp` to show a write-protected diskette
still reads (`CheckProtectedDiskStillReads`).  Nothing else needs the manual.
Copy the file yourself only if you are running `integration_test.exe` by hand
-- and keep it byte for byte as it came off the original diskette, because
both modes depend on that.

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
| F2 | hodiny a kalendar | Shift+F2 | diar |
| F3 | kalkulator | Shift+F3 | teplomer (TIC/TIF) |
| F4 | komunikace | Shift+F4 | voltmeter (DVM) |
| F5 | telefonni seznam | Shift+F5 | databaze |
| F6 | prekladac bejsiku | Shift+F6 | diskove funkce |
| F7 | hudebni editor | Shift+F7 | spustit program z disku |
| F8 | adresar disku | Shift+F8 | formatovat disk |
| F9 | rezim | Shift+F9 | stav baterie |
| F10 | kde jsem | Shift+F10 | sebekontrola |

**"F2 announces nothing on entry" stood here until 10. 9. 2026 and was
wrong**, and so was "(silent)" for Shift+F2.  F2 announces the time, in two
sentences -- "9 hodin", then "45 minut" once the first has been said -- and
Shift+F2 says "diar".  What was missing was the report, not the speech: the
capture took only the byte the speech engine gets at `0103h` (`.speak`), and
all three sentences are spoken from the conversion buffer by `.spconv`
(`010Fh`).  It was found with the probe's `zvuk` token -- after F2 the
loudspeaker delivered 48 000 samples swinging the whole range while the
transcript stayed empty, which is exactly what a hole in the capture looks
like and exactly what "the application is silent" looked like from here.  The
capture takes `.spconv` too now, and mode `rtc` fails if it stops.  F9, F10 and
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
`kXX` (one key code in hex), `sXX` (one PC scan code in hex, make and break)
or a literal string typed as text; `~` stands for Enter.  There are also tokens that change the medium -- `nova` puts in a
diskette that never was formatted and `vysun` empties the drive, the two the
quick choice cannot produce and the two the firmware talks about differently
(6.30).  Three tokens work the power switch: `vypni` presses the four cursor
keys from the Main Menu and runs on until the machine cuts its own power,
`zapni` switches it back on warm -- RAM, the clock chip's eight bytes and the
cycle counter survive, as they do on the hardware -- and `studeno` is the cold
start, `Reset()`, which wipes them.  Each prints whether the machine is running
and what is in `C45Ah`, so the difference between the two ways back on is in
the report and not in the reader's head (6.15, 6.31).  Two tokens are about
the clock: `cas:+7d` moves the time the RTC answers with (`+2h`, `-30m`, a
bare number is seconds) and `budik` prints that time beside the alarm the
firmware armed, the mask and the status.  Between them they are the only way
to ask what a week-old alarm does, and the answer is measured in 6.15: sitting
out a real week is not a measurement.  A switched-off machine wakes on its
alarm (6.32): `cas:` says so on the token that landed in the alarm's minute,
and every token polls for it before it runs.  Land inside that minute, not
past it -- the alarm is an edge.  An alarm left unanswered rings and goes back
to sleep, but `.` stops on the gaps between the rings, so wait it out with
`spin:`.  And `?text` is case-sensitive while the transcript is upper case:
`?DOBR` matches, `?dobr` never does and runs the whole budget.  Text goes in on the
emulated PC keyboard, on the keys the ROM's own
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

**`kXX` and `sXX` are not two spellings of the same key.** `kXX` drops a
finished key code into the queue; `sXX` sends the PC scan code and lets the
ROM's own delivery routine (`1DDB0` → `1DE47`) make the code out of it.  For
most keys the two agree, and for Escape they do not: `k1B` inside the clock
and calendar does nothing at all -- the machine stays in the read loop at
`0DFB3`-`0DFB9` -- while `s01` says "ahoj" and lands back in the Main Menu,
which is what the owner's machine does.  It matters because the power switch
is only answered from the Main Menu (`18154`), so a sequence that leaves an
application with `k1B` finds `vypni` does nothing and the machine still
running.  When a key seems not to arrive, send it the other way before
concluding anything about the firmware.

**Half a second of quiet is not a pace at all where nothing writes to the
console.**  "Quiet" is measured on console output, and the clock and calendar
writes none -- it talks -- so `.` returns almost at once, the alarm dialogue
never gets far enough to take the digits, the alarm registers stay at zero and
nothing says why.  `spin:8000000` per step is what the sequence in 6.15 uses
instead.  `zvuk` prints how many samples the loudspeaker got and how far they
swung, which is the one report no hole in the speech capture can fool.
`wav:FILE` writes those samples out instead of counting them: the swing tells
sound from silence and nothing else, while a crackle is a shape that has to be
looked at.  What comes out is the model alone -- no device, no queue, no
steered clock -- so a defect that survives into the file is in the model and
one that does not is in the real time path.  `dac:N` asks the other half of
the question, how the DAC is being driven: the gaps between **writes** (always
540 cycles while a tune plays; twice that would be a lost PRT0 interrupt) and
how far the value moves when they come (a jump near 256 would be the
firmware's sum of four voices wrapping through the byte).  It counts writes
and not changes of value on purpose -- the tone generator hands over a sample
on every interrupt whether the value moved or not, so watching for changes
invents gaps that are not there (HANDOFF 6.38).
`rychlost:N` and `hlasitost:N` move the two sliders, by the positions the
window uses (`src/sliders.h`: rate 0-31, volume 0-20).  The firmware looks at
the rate pot only while it speaks (`001BB`), and `zvuk` counts samples over a
fixed stretch of guest time rather than the length of a sentence, so the rate
shows in the firmware's writes to `RLDR0L` (port `0Eh` from `0264h`) in the
probe's report, not there (HANDOFF 6.34).

**`@TEXT` times a program's answer.**  It runs until the console shows TEXT
(`_` stands for a space) and prints the cycles since the typed line before
it, in seconds too, and how much of that the CPU spent in the program itself
-- RAM below `C000h`.  The rest is the ROM echoing the line aloud and BDOS;
the hardware does the same, so the whole figure is what compares with a
stopwatch, but only the program's share grows with the work.  A line followed
by `@` goes in without the usual wait for quiet, since that wait would eat
the answer or be counted in it.  The console and not the speech, because the
speech trails the work by seconds.  Run it from bash: PowerShell reads `@` as
its own syntax.  This is how CHESS.COM was timed against the owner's memory
of the real machine (HANDOFF 6.46):

```text
diag_probe A4ROM.DMP disk-folder seq 2000000000 kD6 CHESS~ ?AHEAD 3~ @VARIATION N~ @WHITE Y~ @MOVE "a2 a3~" @MY_MOVE
```

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
diag_probe A4ROM.DMP disk-folder seq 300000000 kD7 Y Y ?skonceno
```

Formatting runs to completion and the machine says "formatovani skonceno":
162 track writes, 1620 verify reads, 81 steps.  The last token is what carries
the sequence through it.  Waiting by silence does not: `RunUntilPrompt` returns
half a second after the console falls quiet, a format is a long quiet, and
`seq 15000000 kD7 Y Y` therefore printed its last answer and exited within four
million instructions -- before the format finished.  `?text` waits for the
machine to say something instead of for it to stop talking, so it spans the
whole job; the budget has to be large enough to hold it, hence 300 million.

That was the first time DMA channel 1 and Write Track were ever executed, and
getting there took four more model fixes -- DMA arming order and direction,
BUSY on reads, and the type I step commands. `HANDOFF.md` section 5 lists them.

The moral, and the reason this is written down: every wrong step above came
from reading the ROM alone and mistaking adjacency for causation. The probe
showed the machine misbehaving; only Appendix H said why.
