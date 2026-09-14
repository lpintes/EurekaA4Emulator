#ifndef EUREKA_SLIDERS_H
#define EUREKA_SLIDERS_H

// The Eureka's two slider controls, as positions the user moves them between.
// In one place because the machine, the settings file, the window and the
// probe all have to agree on what position 16 means, and a second copy of the
// range would let a saved setting land on a step the slider does not have.

#include <cmath>
#include <cstdint>

namespace sliders {

// The left slider: the speech rate pot, which comparator vm1 reports while
// vmsel is set (Appendix H).  The firmware tracks it against the DAC every
// eighth sample of speech (001BB) and uses five bits of what it found: RLDR0 =
// pitch - estimate/8 (00258).  Thirty-two positions are therefore everything
// the machine can tell apart; a finer slider would only add steps that do
// nothing.
inline constexpr int kRatePositions = 32;
// The middle, which gives the same reload as the fixed 80h the emulator held
// before this slider existed.
inline constexpr int kRateDefault = 16;

// Where a position sits on the DAC's scale: the middle of its firmware step,
// never the edge between two.  That also keeps the default off 80h exactly,
// the DAC's resting level in a pause, where the firmware's tracking finds the
// two equal and restarts timer 0 every eighth sample of silence.
//
// Full travel over 00h..FFh is an assumption.  Nobody has measured the voltage
// at the ends of a real slider; a recording of one sentence at both ends would
// settle it, and this is the function to change then.  A higher level is
// faster speech.
constexpr uint8_t RatePotLevel(int position) {
  if (position < 0) position = 0;
  if (position >= kRatePositions) position = kRatePositions - 1;
  return static_cast<uint8_t>(position * 8 + 4);
}

// The right slider: volume.  Purely analogue, between the output stage and the
// speaker, and the firmware never learns where it is.
//
// The top is the loudest the output can honestly go: speech there already
// peaks near full scale (measured 27880 of 32767), so a position above it
// could only clip, and on this machine distorted speech is a broken user
// interface.  The default is therefore the middle, 10 dB under the top --
// roughly half as loud to the ear -- so the slider can go both ways from where
// it starts.  Steps are in decibels because that is how loudness is heard:
// 1 dB above the middle, fine enough to settle a level, and 2 dB below it, so
// the slider still reaches a whisper before position zero, which is silence.
// The real slider went all the way down too.
inline constexpr int kVolumePositions = 21;
inline constexpr int kVolumeDefault = 10;
inline constexpr double kVolumeFineDb = 1.0;
inline constexpr double kVolumeCoarseDb = 2.0;

inline double VolumeGain(int position) {
  const int top = kVolumePositions - 1;
  if (position <= 0) return 0.0;
  if (position >= top) return 1.0;
  const double db =
      position >= kVolumeDefault
          ? (position - top) * kVolumeFineDb
          : (kVolumeDefault - top) * kVolumeFineDb -
                (kVolumeDefault - position) * kVolumeCoarseDb;
  return std::pow(10.0, db / 20.0);
}

}  // namespace sliders

#endif
