// Keys for EurekaSession, one type per way into the machine (eureka.md).
//
// The three are not interchangeable, and the difference is quiet: Escape
// dropped into the key queue does nothing in the clock application, while the
// same key over the PC keyboard's wire says "ahoj" and goes back to the Main
// Menu (CLAUDE.md, the sXX token).  So each path has its own type and
// EurekaSession its own method, and sending a key down the wrong one is a
// compile error rather than a silent keystroke.
//
// Values come from src/eureka_io.h, names from KB.H without the K_ -- this
// header adds no codes of its own apart from the ones KB.H builds by the same
// rule (K_F2 is 1 | K_FUNCTION).

#ifndef EUREKA_KEYS_H
#define EUREKA_KEYS_H

#include <cstdint>

#include "eureka_io.h"

namespace eureka {

// ---------------------------------------------------------------------------
// Key codes as the firmware receives them (QueueKey).

namespace keys {

struct Key {
  uint8_t code;
  // Any code at all, for the probe's kXX token.  Not for tests: a name is
  // what makes a scenario readable and a typo a compile error.
  static constexpr Key Raw(uint8_t code) { return Key{code}; }
};

struct Modifier {
  uint8_t bits;
};

inline constexpr Modifier Shift{hw::kKeyShift};  // K_SHIFT
inline constexpr Modifier Alt{hw::kKeyAlt};      // K_ALT

// KB.H builds K_SF8 as K_F8 | K_SHIFT and K_ALEFT as K_LEFT | K_ALT; this is
// the same composition.  Key | Key is deliberately missing -- F8 | F9 is not
// a key.
constexpr Modifier operator|(Modifier a, Modifier b) {
  return Modifier{static_cast<uint8_t>(a.bits | b.bits)};
}
constexpr Key operator|(Modifier m, Key k) {
  return Key{static_cast<uint8_t>(k.code | m.bits)};
}

inline constexpr Key F1{hw::kKeyF1};
inline constexpr Key F2{hw::kKeyF1 + 1};
inline constexpr Key F3{hw::kKeyF1 + 2};
inline constexpr Key F4{hw::kKeyF1 + 3};
inline constexpr Key F5{hw::kKeyF1 + 4};
inline constexpr Key F6{hw::kKeyF1 + 5};
inline constexpr Key F7{hw::kKeyF1 + 6};
inline constexpr Key F8{hw::kKeyF1 + 7};
inline constexpr Key F9{hw::kKeyF9};
inline constexpr Key F10{hw::kKeyF10};
inline constexpr Key F11{hw::kKeyF11};

inline constexpr Key Up{hw::kKeyUp};
inline constexpr Key Down{hw::kKeyDown};
inline constexpr Key Middle{hw::kKeyMiddle};
inline constexpr Key Left{hw::kKeyLeft};
inline constexpr Key Home{hw::kKeyHome};
inline constexpr Key End{hw::kKeyEnd};
inline constexpr Key Spare{hw::kKeySpare};
inline constexpr Key Right{hw::kKeyRight};
inline constexpr Key PgUp{hw::kKeyPgUp};
inline constexpr Key PgDn{hw::kKeyPgDn};
inline constexpr Key Ins{hw::kKeyIns};
inline constexpr Key Del{hw::kKeyDel};
inline constexpr Key CtrlUp{hw::kKeyCtrlUp};
inline constexpr Key CtrlDown{hw::kKeyCtrlDown};
inline constexpr Key PrtSc{hw::kKeyPrtSc};

}  // namespace keys

// ---------------------------------------------------------------------------
// The optional PC keyboard (QueueScanCode).  KB.H does not name these; the
// values are XT set 1 make codes, and what the firmware makes of them is its
// own table at 1DF05, indexed by scan code.

namespace pc {

struct Key {
  uint8_t make;
  // Any scan code, for the probe's sXX token.
  static constexpr Key Raw(uint8_t make) { return Key{make}; }
};

inline constexpr Key Esc{0x01};
inline constexpr Key Backspace{0x0e};
inline constexpr Key Tab{0x0f};
inline constexpr Key Enter{0x1c};
inline constexpr Key Space{0x39};
inline constexpr Key F1{0x3b};
inline constexpr Key F2{0x3c};
inline constexpr Key F3{0x3d};
inline constexpr Key F4{0x3e};
inline constexpr Key F5{0x3f};
inline constexpr Key F6{0x40};
inline constexpr Key F7{0x41};
inline constexpr Key F8{0x42};
inline constexpr Key F9{0x43};
inline constexpr Key F10{0x44};
inline constexpr Key F11{0x57};  // CAh at 1DF05, "ROM operacniho systemu"
inline constexpr Key F12{0x58};  // CBh, "nepouzito"

}  // namespace pc

// ---------------------------------------------------------------------------
// The membrane keyboard, as the port rows it reads as (HoldMembrane): the six
// dots and the space bar on 89h, eight function keys on 8Ah, four cursor bits
// on 8Ch.  Shift is a key of its own there and goes through HoldShift, see
// machine.h.

namespace membrane {

struct Keys {
  uint8_t dots = 0;    // row 89h
  uint8_t fn = 0;      // row 8Ah
  uint8_t cursor = 0;  // row 8Ch, bits 0-3
  bool shift = false;
};

constexpr Keys operator|(Keys a, Keys b) {
  return Keys{static_cast<uint8_t>(a.dots | b.dots),
              static_cast<uint8_t>(a.fn | b.fn),
              static_cast<uint8_t>(a.cursor | b.cursor), a.shift || b.shift};
}

inline constexpr Keys Dot1{hw::kBkbDot1};
inline constexpr Keys Dot2{hw::kBkbDot2};
inline constexpr Keys Dot3{hw::kBkbDot3};
inline constexpr Keys Dot4{hw::kBkbDot4};
inline constexpr Keys Dot5{hw::kBkbDot5};
inline constexpr Keys Dot6{hw::kBkbDot6};
inline constexpr Keys Space{hw::kBkbSpace};
inline constexpr Keys Shift{0, 0, 0, true};

// One bit per key on 8Ah; F9 and above are chords (1D541).
inline constexpr Keys F1{0, 0x01};
inline constexpr Keys F2{0, 0x02};
inline constexpr Keys F3{0, 0x04};
inline constexpr Keys F4{0, 0x08};
inline constexpr Keys F5{0, 0x10};
inline constexpr Keys F6{0, 0x20};
inline constexpr Keys F7{0, 0x40};
inline constexpr Keys F8{0, 0x80};

// The key number of K_UP and friends is their bit on 8Ch.
inline constexpr Keys Up{0, 0, hw::kKeyUp & hw::kKeyNumberMask};
inline constexpr Keys Down{0, 0, hw::kKeyDown & hw::kKeyNumberMask};
inline constexpr Keys Left{0, 0, hw::kKeyLeft & hw::kKeyNumberMask};
inline constexpr Keys Right{0, 0, hw::kKeyRight & hw::kKeyNumberMask};

}  // namespace membrane

}  // namespace eureka

#endif
