#!/bin/bash
# Builds the drive sound set in src/res/zvuky-mechaniky from Luboš Krejča's
# recordings of a real Eureka A4 drive (HANDOFF 6.54, PUVOD.md there).  The
# recordings themselves are not in the tree; this is how the set was cut from
# them, so that it can be cut again.
#
# Usage: tools/sada-mechaniky.sh RECORDINGS_DIR OUTPUT_DIR [GAIN]
#
# The recordings are already WAV, PCM, 48 kHz, mono, 16-bit.  One gain for
# the whole set keeps them in their own proportions; 1.4 is as far as the
# loudest of them goes without clipping.
set -e
S="$1"
O="$2"
G="${3:-1.4}"
if [ -z "$S" ] || [ -z "$O" ]; then
  echo "Použitie: $0 PRIEČINOK_NAHRÁVOK CIEĽ [ZISK]" >&2
  exit 1
fi
mkdir -p "$O"
OUT=(-ac 1 -ar 48000 -c:a pcm_s16le -bitexact -fflags +bitexact -map_metadata -1 -y -loglevel error)
# The motor on its own for a long while: a 1.44 MB PC diskette in the
# Eureka, which spins it trying to read it (25. 10. 2026).  It was made at
# the same level as the others -- its first second is as loud as the motor
# in "Diskove funkce" -- so the one gain fits all.
M="$S/jednotka jiná verze.wav"

# Spin-up: from the switch-on to 3.5 s.  The motor is up to speed at once
# but some 7 dB louder at first and settles over about three seconds; its
# pitch stays.  The steady motor starts exactly where this ends.
ffmpeg -i "$M" -af "atrim=0:3.5,asetpts=PTS-STARTPTS,afade=t=in:d=0.02,volume=$G" \
  "${OUT[@]}" "$O/motor-rozbeh.wav"

# The steady motor, 3.5-12.2 s.  Not a loop: the emulator plays it in random
# runs of whole revolutions (drive_sound.cpp), so its ends need no seam.
ffmpeg -i "$M" -af "atrim=3.5:12.2,asetpts=PTS-STARTPTS,volume=$G" \
  "${OUT[@]}" "$O/motor-slucka.wav"

# The stop: the motor switched off at 12.7 s -- a drop to a third within
# 50 ms, a faint click and a quiet coast; the other long recording ends the
# same way.  The recording ends during the coast, hence the short fade.
ffmpeg -i "$M" -af "atrim=12.45,asetpts=PTS-STARTPTS,areverse,afade=t=in:d=0.03,areverse,volume=$G" \
  "${OUT[@]}" "$O/motor-dobeh.wav"

# The step: Pohyb7 (pohyb8 is the same take).  The step in "Diskove funkce"
# is two strikes 50 ms apart -- the first move after the spin-up, not an
# ordinary step -- and played in turn with this one it made every other step
# of a format sound doubled, so it is left out.
ffmpeg -i "$S/Pohyb7.wav" -af "afade=t=in:d=0.002,afade=t=out:st=0.06:d=0.038,volume=$G" \
  "${OUT[@]}" "$O/krok-1.wav"

# Rake: pohyb1, the one that clicks every 6 ms -- the ROM's step rate.  The
# others were made at other rates (12 ms in pohyb5 and pohyb6, 2-3 ms in
# pohyb2 to pohyb4), most likely by DiskEdit.
ffmpeg -i "$S/pohyb1.wav" -af "afade=t=in:d=0.005,afade=t=out:st=0.52:d=0.04,volume=$G" \
  "${OUT[@]}" "$O/presun.wav"
