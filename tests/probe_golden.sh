#!/bin/sh
# Runs a fixed set of diag_probe sequences and writes each one's output to
# OUT/NN.txt, so that two runs -- before and after a change to the probe --
# can be compared with cmp (eureka.md, "Krok 1").  The set touches every
# family of seq tokens at least once.  None of them makes the machine tell the
# time: the clock comes from the host, so such output differs between minutes
# with no change to the code.
#
#   tests/probe_golden.sh OUT [ROM] [DISK]
#
# Run from bash, not PowerShell: "@" is PowerShell syntax (HANDOFF 6.46).
# Each sequence gets a fresh copy of DISK, so what one writes cannot change
# what the next one reads.

set -u
out=${1:?usage: probe_golden.sh OUT [ROM] [DISK]}
rom=${2:-${A4ROM:-C:/b/a4rom.dmp}}
disk=${3:-build/testdisk}
probe=./bin/diag_probe.exe

mkdir -p "$out"
n=0
run() {
  n=$((n + 1))
  name=$(printf '%02d' "$n")
  rm -rf "$out/disk"
  cp -r "$disk" "$out/disk"
  "$probe" "$rom" "$out/disk" "$@" > "$out/$name.txt" 2>&1
  echo "$name: $* (navrat $?)"
}

run boot
run seq 8000000 kC9 kD7 Y ?disk k1B
run seq 8000000 s3B . s01
run seq 8000000 +bs +b1 +b4 +b5 -b
run seq 8000000 +wp kD7 Y Y stav -wp nova kD7 Y . stav vysun kC8 .
run seq 8000000 slot2 stav slot1 stav slot2 stav
run seq 8000000 vypni zapni studeno
run seq 8000000 rychlost:0 hlasitost:20 kC9 zvuk dac:200000
run seq 8000000 kD6 . READ~ @Read_which
run seq 8000000 trace kD7 spin:200000
rm -rf "$out/disk"
