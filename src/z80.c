#include "z80.h"

// MARK: timings
// Z180 T-states from Zilog UM005004, Tables 38-47, one row per high nibble.
// They leave out every wait state: the one on each opcode fetch is m1_wait
// below, and the ones on external I/O are added by the machine, which knows
// DCNTL.  A conditional instruction is listed at its not-taken cost; the rest
// is added where the condition is decided.  Undocumented opcodes, which a real
// Z180 traps, are charged as their unprefixed form plus the prefix fetch.
static const uint8_t cyc_00[256] = {
    3, 9, 7, 4, 4, 4, 6, 3, 4, 7, 6, 4, 4, 4, 6, 3,          // 0x
    7, 9, 7, 4, 4, 4, 6, 3, 8, 7, 6, 4, 4, 4, 6, 3,          // 1x
    6, 9, 16, 4, 4, 4, 6, 4, 6, 7, 15, 4, 4, 4, 6, 3,        // 2x
    6, 9, 13, 4, 10, 10, 9, 3, 6, 7, 12, 4, 4, 4, 6, 3,      // 3x
    4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,          // 4x
    4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,          // 5x
    4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,          // 6x
    7, 7, 7, 7, 7, 7, 3, 7, 4, 4, 4, 4, 4, 4, 6, 4,          // 7x
    4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,          // 8x
    4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,          // 9x
    4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,          // Ax
    4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 6, 4,          // Bx
    5, 9, 6, 9, 6, 11, 6, 11, 5, 9, 6, 0, 6, 16, 6, 11,      // Cx
    5, 9, 6, 10, 6, 11, 6, 11, 5, 3, 6, 9, 6, 0, 6, 11,      // Dx
    5, 9, 6, 16, 6, 11, 6, 11, 5, 3, 6, 3, 6, 0, 6, 11,      // Ex
    5, 9, 6, 3, 6, 11, 6, 11, 5, 4, 6, 3, 6, 0, 6, 11};      // Fx

static const uint8_t cyc_ed[256] = {
    12, 13, 6, 6, 7, 6, 6, 6, 12, 13, 6, 6, 7, 6, 6, 6,      // 0x
    12, 13, 6, 6, 7, 6, 6, 6, 12, 13, 6, 6, 7, 6, 6, 6,      // 1x
    12, 13, 6, 6, 7, 6, 6, 6, 12, 13, 6, 6, 7, 6, 6, 6,      // 2x
    12, 13, 6, 6, 10, 6, 6, 6, 12, 13, 6, 6, 7, 6, 6, 6,     // 3x
    9, 10, 10, 19, 6, 12, 6, 6, 9, 10, 10, 18, 17, 12, 6, 6, // 4x
    9, 10, 10, 19, 6, 12, 6, 6, 9, 10, 10, 18, 17, 12, 6, 6, // 5x
    9, 10, 10, 19, 9, 12, 6, 16, 9, 10, 10, 18, 17, 12, 6, 16, // 6x
    9, 10, 10, 19, 12, 12, 8, 6, 9, 10, 10, 18, 17, 12, 6, 6, // 7x
    6, 6, 6, 14, 6, 6, 6, 6, 6, 6, 6, 14, 6, 6, 6, 6,        // 8x
    6, 6, 6, 14, 6, 6, 6, 6, 6, 6, 6, 14, 6, 6, 6, 6,        // 9x
    12, 12, 12, 12, 6, 6, 6, 6, 12, 12, 12, 12, 6, 6, 6, 6,  // Ax
    12, 12, 12, 12, 6, 6, 6, 6, 12, 12, 12, 12, 6, 6, 6, 6,  // Bx
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,          // Cx
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,          // Dx
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,          // Ex
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6};         // Fx

// What a DD/FD opcode costs on its own.  Anything the handler below passes on
// to exec_opcode is charged 3 here, the prefix fetch, and its unprefixed cost
// there.  DDCB is charged in exec_opcode_dcb.
static const uint8_t cyc_ddfd[256] = {
    3, 3, 3, 3, 3, 3, 3, 3, 3, 10, 3, 3, 3, 3, 3, 3,         // 0x
    3, 3, 3, 3, 3, 3, 3, 3, 3, 10, 3, 3, 3, 3, 3, 3,         // 1x
    3, 12, 19, 7, 7, 7, 9, 3, 3, 10, 18, 7, 7, 7, 9, 3,      // 2x
    3, 3, 3, 3, 18, 18, 15, 3, 3, 10, 3, 3, 3, 3, 3, 3,      // 3x
    3, 3, 3, 3, 7, 7, 14, 3, 3, 3, 3, 3, 7, 7, 14, 3,        // 4x
    3, 3, 3, 3, 7, 7, 14, 3, 3, 3, 3, 3, 7, 7, 14, 3,        // 5x
    7, 7, 7, 7, 7, 7, 14, 7, 7, 7, 7, 7, 7, 7, 14, 7,        // 6x
    15, 15, 15, 15, 15, 15, 3, 15, 3, 3, 3, 3, 7, 7, 14, 3,  // 7x
    3, 3, 3, 3, 7, 7, 14, 3, 3, 3, 3, 3, 7, 7, 14, 3,        // 8x
    3, 3, 3, 3, 7, 7, 14, 3, 3, 3, 3, 3, 7, 7, 14, 3,        // 9x
    3, 3, 3, 3, 7, 7, 14, 3, 3, 3, 3, 3, 7, 7, 14, 3,        // Ax
    3, 3, 3, 3, 7, 7, 14, 3, 3, 3, 3, 3, 7, 7, 14, 3,        // Bx
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 3, 3,          // Cx
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,          // Dx
    3, 12, 3, 19, 3, 14, 3, 3, 3, 6, 3, 3, 3, 3, 3, 3,       // Ex
    3, 3, 3, 3, 3, 3, 3, 3, 3, 7, 3, 3, 3, 3, 3, 3};         // Fx

// One wait state on every opcode fetch, prefix bytes included -- each of them
// is an M1 cycle (UM005004 Table 51).  The manual's own count of the delay loop
// DEC HL / LD A,H / OR L / JR NZ is 5+5+5+9 = 24 T-states
// (eurekatech/TECHMAN1/KEYSCAN.MAC), one more per instruction than the table
// above, and the ROM's millisecond wait at 19CD9 runs that loop 256 times:
// 256 x 24 = 6144, one millisecond at 6.144 MHz.  What adds it on the board is
// not documented.  With it the tone generator's interrupt leaves the ROM's note
// timer the share the recording of a real machine asks for (HANDOFF 6.10).
static const unsigned m1_wait = 1;

// MARK: helpers

// get bit "n" of number "val"
#define GET_BIT(n, val) (((val) >> (n)) & 1)

static inline uint8_t rb(z80* const z, uint16_t addr) {
  return z->read_byte(z->userdata, addr);
}

static inline void wb(z80* const z, uint16_t addr, uint8_t val) {
  z->write_byte(z->userdata, addr, val);
}

static inline uint16_t rw(z80* const z, uint16_t addr) {
  return (z->read_byte(z->userdata, addr + 1) << 8) |
         z->read_byte(z->userdata, addr);
}

static inline void ww(z80* const z, uint16_t addr, uint16_t val) {
  z->write_byte(z->userdata, addr, val & 0xFF);
  z->write_byte(z->userdata, addr + 1, val >> 8);
}

static inline void pushw(z80* const z, uint16_t val) {
  z->sp -= 2;
  ww(z, z->sp, val);
}

static inline uint16_t popw(z80* const z) {
  z->sp += 2;
  return rw(z, z->sp - 2);
}

static inline uint8_t nextb(z80* const z) {
  return rb(z, z->pc++);
}

static inline uint16_t nextw(z80* const z) {
  z->pc += 2;
  return rw(z, z->pc - 2);
}

static inline uint16_t get_bc(z80* const z) {
  return (z->b << 8) | z->c;
}

static inline uint16_t get_de(z80* const z) {
  return (z->d << 8) | z->e;
}

static inline uint16_t get_hl(z80* const z) {
  return (z->h << 8) | z->l;
}

static inline void set_bc(z80* const z, uint16_t val) {
  z->b = val >> 8;
  z->c = val & 0xFF;
}

static inline void set_de(z80* const z, uint16_t val) {
  z->d = val >> 8;
  z->e = val & 0xFF;
}

static inline void set_hl(z80* const z, uint16_t val) {
  z->h = val >> 8;
  z->l = val & 0xFF;
}

static inline uint8_t get_f(z80* const z) {
  uint8_t val = 0;
  val |= z->cf << 0;
  val |= z->nf << 1;
  val |= z->pf << 2;
  val |= z->xf << 3;
  val |= z->hf << 4;
  val |= z->yf << 5;
  val |= z->zf << 6;
  val |= z->sf << 7;
  return val;
}

static inline void set_f(z80* const z, uint8_t val) {
  z->cf = (val >> 0) & 1;
  z->nf = (val >> 1) & 1;
  z->pf = (val >> 2) & 1;
  z->xf = (val >> 3) & 1;
  z->hf = (val >> 4) & 1;
  z->yf = (val >> 5) & 1;
  z->zf = (val >> 6) & 1;
  z->sf = (val >> 7) & 1;
}

// increments R, keeping the highest byte intact
static inline void inc_r(z80* const z) {
  z->r = (z->r & 0x80) | ((z->r + 1) & 0x7f);
}

// returns if there was a carry between bit "bit_no" and "bit_no - 1" when
// executing "a + b + cy"
static inline bool carry(int bit_no, uint16_t a, uint16_t b, bool cy) {
  int32_t result = a + b + cy;
  int32_t carry = result ^ a ^ b;
  return carry & (1 << bit_no);
}

// returns the parity of byte: 0 if number of 1 bits in `val` is odd, else 1
static inline bool parity(uint8_t val) {
  uint8_t nb_one_bits = 0;
  for (int i = 0; i < 8; i++) {
    nb_one_bits += ((val >> i) & 1);
  }

  return (nb_one_bits & 1) == 0;
}

static void exec_opcode(z80* const z, uint8_t opcode);
static void exec_opcode_cb(z80* const z, uint8_t opcode);
static void exec_opcode_dcb(
    z80* const z, const uint8_t opcode, const uint16_t addr);
static void exec_opcode_ed(z80* const z, uint8_t opcode);
static void exec_opcode_ddfd(z80* const z, uint8_t opcode, uint16_t* const iz);

// MARK: HD64180 / Z180 helpers
//
// Eureka's CPU is an HD64180.  It is Z80 compatible, but the firmware also
// uses the Z180 IN0/OUT0, MLT, TST and block-I/O instructions.  Keeping these
// operations in the CPU core means that the original ROM code, rather than a
// reimplementation of its linguistic rules, remains the source of the audio.

static inline uint8_t* z180_reg(z80* const z, uint8_t code) {
  switch (code & 7) {
  case 0: return &z->b;
  case 1: return &z->c;
  case 2: return &z->d;
  case 3: return &z->e;
  case 4: return &z->h;
  case 5: return &z->l;
  case 7: return &z->a;
  default: return NULL; // code 6 denotes no register / (HL)
  }
}

static inline void z180_test(z80* const z, uint8_t operand) {
  const uint8_t result = z->a & operand;
  z->sf = result >> 7;
  z->zf = result == 0;
  z->yf = GET_BIT(5, result);
  z->hf = 1;
  z->xf = GET_BIT(3, result);
  z->pf = parity(result);
  z->nf = 0;
  z->cf = 0;
}

static inline void z180_in0(z80* const z, uint8_t opcode) {
  const uint8_t port = nextb(z);
  const uint8_t value = z->port_in(z, port);
  uint8_t* const reg = z180_reg(z, opcode >> 3);
  if (reg) *reg = value;
  z->sf = value >> 7;
  z->zf = value == 0;
  z->yf = GET_BIT(5, value);
  z->hf = 0;
  z->xf = GET_BIT(3, value);
  z->pf = parity(value);
  z->nf = 0;
  // Carry is not affected.
}

static inline void z180_out0(z80* const z, uint8_t opcode) {
  const uint8_t port = nextb(z);
  uint8_t* const reg = z180_reg(z, opcode >> 3);
  z->port_out(z, port, reg ? *reg : 0);
}

static inline void z180_mlt(z80* const z, uint8_t opcode) {
  uint16_t value;
  switch ((opcode >> 4) & 3) {
  case 0:
    value = z->b * z->c;
    set_bc(z, value);
    break;
  case 1:
    value = z->d * z->e;
    set_de(z, value);
    break;
  case 2:
    value = z->h * z->l;
    set_hl(z, value);
    break;
  default:
    value = (z->sp >> 8) * (z->sp & 0xff);
    z->sp = value;
    break;
  }
  // MLT does not affect flags.
}

static inline void z180_otim(z80* const z, bool decrement, bool repeat) {
  const uint16_t address = get_hl(z);
  const uint8_t value = rb(z, address);
  // Unlike OUTI, the Z180 block-I/O instructions place 00h on A8-A15 rather
  // than B.  This machine decodes the upper address byte (the clock sits at
  // 0190h and 0290h), so the difference is observable.
  z->port_out(z, z->c, value);
  set_hl(z, decrement ? address - 1 : address + 1);
  z->c += decrement ? -1 : 1;
  z->b -= 1;

  // The firmware relies on B and the repeat condition; these documented flag
  // results also make single-step diagnostics deterministic.
  z->zf = z->b == 0;
  z->nf = 1;
  if (repeat && z->b != 0) {
    z->pc -= 2;
    z->cyc += 2;
  }
}

// Z180 TRAP (UM005004 p. 70-71, HD64180Z manual on ITC): the stacked PC
// points one byte past the start of the instruction when the second op code
// byte was undefined (UFO=0) and two bytes past it when the third was (UFO=1).
// Execution restarts at logical 0000h.  Measured on a real Eureka with ED 77
// (HANDOFF 6.33, point 5): the program stops there and nothing after it runs.
static bool z180_trap(z80* const z, uint16_t start, bool ufo) {
  if (!z->z180_traps) return false;
  z->trap = ufo ? 2 : 1;
  pushw(z, start + (ufo ? 2 : 1));
  z->pc = 0;
  return true;
}

// UM005004 Table 50 plus the g=110 forms Table 46 documents for IN and IN0.
static bool z180_ed_defined(uint8_t op) {
  if (op < 0x40) {
    switch (op & 7) {
    case 0: return true;             // in0
    case 1: return op != 0x31;       // out0
    case 4: return true;             // tst
    default: return false;
    }
  }
  if (op < 0x80) {
    switch (op & 7) {
    case 0: return true;             // in r,(c)
    case 1: return op != 0x71;       // out (c),r
    case 2: case 3: return true;     // sbc, adc, ld (nn),rr, ld rr,(nn)
    case 4: return op == 0x44 || op == 0x4c || op == 0x5c || op == 0x6c ||
                   op == 0x7c || op == 0x64 || op == 0x74;  // neg, mlt, tst
    case 5: return op == 0x45 || op == 0x4d;                // retn, reti
    case 6: return op == 0x46 || op == 0x56 || op == 0x5e ||
                   op == 0x76;                              // im, slp
    default: return op == 0x47 || op == 0x4f || op == 0x57 || op == 0x5f ||
                    op == 0x67 || op == 0x6f;
    }
  }
  switch (op) {
  case 0x83: case 0x8b: case 0x93: case 0x9b:  // otim, otdm, otimr, otdmr
  case 0xa0: case 0xa1: case 0xa2: case 0xa3:
  case 0xa8: case 0xa9: case 0xaa: case 0xab:
  case 0xb0: case 0xb1: case 0xb2: case 0xb3:
  case 0xb8: case 0xb9: case 0xba: case 0xbb:
    return true;
  default:
    return false;
  }
}

// The IX/IY forms UM005004 Tables 38-45 list.  Everything else after DD/FD,
// the IXH/IXL halves among it, traps on a Z180.
static bool z180_ddfd_defined(uint8_t op) {
  switch (op) {
  case 0x09: case 0x19: case 0x29: case 0x39:
  case 0x21: case 0x22: case 0x23: case 0x2a: case 0x2b:
  case 0x34: case 0x35: case 0x36:
  case 0x46: case 0x4e: case 0x56: case 0x5e: case 0x66: case 0x6e: case 0x7e:
  case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x77:
  case 0x86: case 0x8e: case 0x96: case 0x9e:
  case 0xa6: case 0xae: case 0xb6: case 0xbe:
  case 0xcb: case 0xe1: case 0xe3: case 0xe5: case 0xe9: case 0xf9:
    return true;
  default:
    return false;
  }
}

// MARK: opcodes
// jumps to an address
static inline void jump(z80* const z, uint16_t addr) {
  z->pc = addr;
  z->mem_ptr = addr;
}

// jumps to next word in memory if condition is true
static inline void cond_jump(z80* const z, bool condition) {
  const uint16_t addr = nextw(z);
  if (condition) {
    jump(z, addr);
    z->cyc += 3;
  }
  z->mem_ptr = addr;
}

// calls to next word in memory
static inline void call(z80* const z, uint16_t addr) {
  pushw(z, z->pc);
  z->pc = addr;
  z->mem_ptr = addr;
}

// calls to next word in memory if condition is true
static inline void cond_call(z80* const z, bool condition) {
  const uint16_t addr = nextw(z);
  if (condition) {
    call(z, addr);
    z->cyc += 10;
  }
  z->mem_ptr = addr;
}

// returns from subroutine
static inline void ret(z80* const z) {
  z->pc = popw(z);
  z->mem_ptr = z->pc;
}

// returns from subroutine if condition is true
static inline void cond_ret(z80* const z, bool condition) {
  if (condition) {
    ret(z);
    z->cyc += 5;
  }
}

static inline void jr(z80* const z, int8_t displacement) {
  z->pc += displacement;
  z->mem_ptr = z->pc;
}

static inline void cond_jr(z80* const z, bool condition) {
  const int8_t b = nextb(z);
  if (condition) {
    jr(z, b);
    z->cyc += 2;
  }
}

// ADD Byte: adds two bytes together
static inline uint8_t addb(z80* const z, uint8_t a, uint8_t b, bool cy) {
  const uint8_t result = a + b + cy;
  z->sf = result >> 7;
  z->zf = result == 0;
  z->hf = carry(4, a, b, cy);
  z->pf = carry(7, a, b, cy) != carry(8, a, b, cy);
  z->cf = carry(8, a, b, cy);
  z->nf = 0;
  z->xf = GET_BIT(3, result);
  z->yf = GET_BIT(5, result);
  return result;
}

// SUBstract Byte: substracts two bytes (with optional carry)
static inline uint8_t subb(z80* const z, uint8_t a, uint8_t b, bool cy) {
  uint8_t val = addb(z, a, ~b, !cy);
  z->cf = !z->cf;
  z->hf = !z->hf;
  z->nf = 1;
  return val;
}

// ADD Word: adds two words together
static inline uint16_t addw(z80* const z, uint16_t a, uint16_t b, bool cy) {
  uint8_t lsb = addb(z, a, b, cy);
  uint8_t msb = addb(z, a >> 8, b >> 8, z->cf);

  uint16_t result = (msb << 8) | lsb;
  z->zf = result == 0;
  z->mem_ptr = a + 1;
  return result;
}

// SUBstract Word: substracts two words (with optional carry)
static inline uint16_t subw(z80* const z, uint16_t a, uint16_t b, bool cy) {
  uint8_t lsb = subb(z, a, b, cy);
  uint8_t msb = subb(z, a >> 8, b >> 8, z->cf);

  uint16_t result = (msb << 8) | lsb;
  z->zf = result == 0;
  z->mem_ptr = a + 1;
  return result;
}

// adds a word to HL
static inline void addhl(z80* const z, uint16_t val) {
  bool sf = z->sf;
  bool zf = z->zf;
  bool pf = z->pf;
  uint16_t result = addw(z, get_hl(z), val, 0);
  set_hl(z, result);
  z->sf = sf;
  z->zf = zf;
  z->pf = pf;
}

// adds a word to IX or IY
static inline void addiz(z80* const z, uint16_t* reg, uint16_t val) {
  bool sf = z->sf;
  bool zf = z->zf;
  bool pf = z->pf;
  uint16_t result = addw(z, *reg, val, 0);
  *reg = result;
  z->sf = sf;
  z->zf = zf;
  z->pf = pf;
}

// adds a word (+ carry) to HL
static inline void adchl(z80* const z, uint16_t val) {
  uint16_t result = addw(z, get_hl(z), val, z->cf);
  z->sf = result >> 15;
  z->zf = result == 0;
  set_hl(z, result);
}

// substracts a word (+ carry) to HL
static inline void sbchl(z80* const z, uint16_t val) {
  const uint16_t result = subw(z, get_hl(z), val, z->cf);
  z->sf = result >> 15;
  z->zf = result == 0;
  set_hl(z, result);
}

// increments a byte value
static inline uint8_t inc(z80* const z, uint8_t a) {
  bool cf = z->cf;
  uint8_t result = addb(z, a, 1, 0);
  z->cf = cf;
  return result;
}

// decrements a byte value
static inline uint8_t dec(z80* const z, uint8_t a) {
  bool cf = z->cf;
  uint8_t result = subb(z, a, 1, 0);
  z->cf = cf;
  return result;
}

// MARK: bitwise

// executes a logic "and" between register A and a byte, then stores the
// result in register A
static inline void land(z80* const z, uint8_t val) {
  const uint8_t result = z->a & val;
  z->sf = result >> 7;
  z->zf = result == 0;
  z->hf = 1;
  z->pf = parity(result);
  z->nf = 0;
  z->cf = 0;
  z->xf = GET_BIT(3, result);
  z->yf = GET_BIT(5, result);
  z->a = result;
}

// executes a logic "xor" between register A and a byte, then stores the
// result in register A
static inline void lxor(z80* const z, const uint8_t val) {
  const uint8_t result = z->a ^ val;
  z->sf = result >> 7;
  z->zf = result == 0;
  z->hf = 0;
  z->pf = parity(result);
  z->nf = 0;
  z->cf = 0;
  z->xf = GET_BIT(3, result);
  z->yf = GET_BIT(5, result);
  z->a = result;
}

// executes a logic "or" between register A and a byte, then stores the
// result in register A
static inline void lor(z80* const z, const uint8_t val) {
  const uint8_t result = z->a | val;
  z->sf = result >> 7;
  z->zf = result == 0;
  z->hf = 0;
  z->pf = parity(result);
  z->nf = 0;
  z->cf = 0;
  z->xf = GET_BIT(3, result);
  z->yf = GET_BIT(5, result);
  z->a = result;
}

// compares a value with register A
static inline void cp(z80* const z, const uint8_t val) {
  subb(z, z->a, val, 0);

  // the only difference between cp and sub is that
  // the xf/yf are taken from the value to be substracted,
  // not the result
  z->yf = GET_BIT(5, val);
  z->xf = GET_BIT(3, val);
}

// 0xCB opcodes
// rotate left with carry
static inline uint8_t cb_rlc(z80* const z, uint8_t val) {
  const bool old = val >> 7;
  val = (val << 1) | old;
  z->sf = val >> 7;
  z->zf = val == 0;
  z->pf = parity(val);
  z->nf = 0;
  z->hf = 0;
  z->cf = old;
  z->xf = GET_BIT(3, val);
  z->yf = GET_BIT(5, val);
  return val;
}

// rotate right with carry
static inline uint8_t cb_rrc(z80* const z, uint8_t val) {
  const bool old = val & 1;
  val = (val >> 1) | (old << 7);
  z->sf = val >> 7;
  z->zf = val == 0;
  z->nf = 0;
  z->hf = 0;
  z->cf = old;
  z->pf = parity(val);
  z->xf = GET_BIT(3, val);
  z->yf = GET_BIT(5, val);
  return val;
}

// rotate left (simple)
static inline uint8_t cb_rl(z80* const z, uint8_t val) {
  const bool cf = z->cf;
  z->cf = val >> 7;
  val = (val << 1) | cf;
  z->sf = val >> 7;
  z->zf = val == 0;
  z->nf = 0;
  z->hf = 0;
  z->pf = parity(val);
  z->xf = GET_BIT(3, val);
  z->yf = GET_BIT(5, val);
  return val;
}

// rotate right (simple)
static inline uint8_t cb_rr(z80* const z, uint8_t val) {
  const bool c = z->cf;
  z->cf = val & 1;
  val = (val >> 1) | (c << 7);
  z->sf = val >> 7;
  z->zf = val == 0;
  z->nf = 0;
  z->hf = 0;
  z->pf = parity(val);
  z->xf = GET_BIT(3, val);
  z->yf = GET_BIT(5, val);
  return val;
}

// shift left preserving sign
static inline uint8_t cb_sla(z80* const z, uint8_t val) {
  z->cf = val >> 7;
  val <<= 1;
  z->sf = val >> 7;
  z->zf = val == 0;
  z->nf = 0;
  z->hf = 0;
  z->pf = parity(val);
  z->xf = GET_BIT(3, val);
  z->yf = GET_BIT(5, val);
  return val;
}

// SLL (exactly like SLA, but sets the first bit to 1)
static inline uint8_t cb_sll(z80* const z, uint8_t val) {
  z->cf = val >> 7;
  val <<= 1;
  val |= 1;
  z->sf = val >> 7;
  z->zf = val == 0;
  z->nf = 0;
  z->hf = 0;
  z->pf = parity(val);
  z->xf = GET_BIT(3, val);
  z->yf = GET_BIT(5, val);
  return val;
}

// shift right preserving sign
static inline uint8_t cb_sra(z80* const z, uint8_t val) {
  z->cf = val & 1;
  val = (val >> 1) | (val & 0x80); // 0b10000000
  z->sf = val >> 7;
  z->zf = val == 0;
  z->nf = 0;
  z->hf = 0;
  z->pf = parity(val);
  z->xf = GET_BIT(3, val);
  z->yf = GET_BIT(5, val);
  return val;
}

// shift register right
static inline uint8_t cb_srl(z80* const z, uint8_t val) {
  z->cf = val & 1;
  val >>= 1;
  z->sf = val >> 7;
  z->zf = val == 0;
  z->nf = 0;
  z->hf = 0;
  z->pf = parity(val);
  z->xf = GET_BIT(3, val);
  z->yf = GET_BIT(5, val);
  return val;
}

// tests bit "n" from a byte
static inline uint8_t cb_bit(z80* const z, uint8_t val, uint8_t n) {
  const uint8_t result = val & (1 << n);
  z->sf = result >> 7;
  z->zf = result == 0;
  z->yf = GET_BIT(5, val);
  z->hf = 1;
  z->xf = GET_BIT(3, val);
  z->pf = z->zf;
  z->nf = 0;
  return result;
}

static inline void ldi(z80* const z) {
  const uint16_t de = get_de(z);
  const uint16_t hl = get_hl(z);
  const uint8_t val = rb(z, hl);

  wb(z, de, val);

  set_hl(z, get_hl(z) + 1);
  set_de(z, get_de(z) + 1);
  set_bc(z, get_bc(z) - 1);

  // see https://wikiti.brandonw.net/index.php?title=Z80_Instruction_Set
  // for the calculation of xf/yf on LDI
  const uint8_t result = val + z->a;
  z->xf = GET_BIT(3, result);
  z->yf = GET_BIT(1, result);

  z->nf = 0;
  z->hf = 0;
  z->pf = get_bc(z) > 0;
}

static inline void ldd(z80* const z) {
  ldi(z);
  // same as ldi but HL and DE are decremented instead of incremented
  set_hl(z, get_hl(z) - 2);
  set_de(z, get_de(z) - 2);
}

static inline void cpi(z80* const z) {
  bool cf = z->cf;
  const uint8_t result = subb(z, z->a, rb(z, get_hl(z)), 0);
  set_hl(z, get_hl(z) + 1);
  set_bc(z, get_bc(z) - 1);
  z->xf = GET_BIT(3, result - z->hf);
  z->yf = GET_BIT(1, result - z->hf);
  z->pf = get_bc(z) != 0;
  z->cf = cf;
  z->mem_ptr += 1;
}

static inline void cpd(z80* const z) {
  cpi(z);
  // same as cpi but HL is decremented instead of incremented
  set_hl(z, get_hl(z) - 2);
  z->mem_ptr -= 2;
}

static void in_r_c(z80* const z, uint8_t* r) {
  *r = z->port_in(z, get_bc(z));
  z->zf = *r == 0;
  z->sf = *r >> 7;
  z->pf = parity(*r);
  z->nf = 0;
  z->hf = 0;
}

static void ini(z80* const z) {
  uint8_t val = z->port_in(z, get_bc(z));
  wb(z, get_hl(z), val);
  set_hl(z, get_hl(z) + 1);
  z->b -= 1;
  z->zf = z->b == 0;
  z->nf = 1;
  z->mem_ptr = get_bc(z) + 1;
}

static void ind(z80* const z) {
  ini(z);
  set_hl(z, get_hl(z) - 2);
  z->mem_ptr = get_bc(z) - 2;
}

static void outi(z80* const z) {
  z->port_out(z, get_bc(z), rb(z, get_hl(z)));
  set_hl(z, get_hl(z) + 1);
  z->b -= 1;
  z->zf = z->b == 0;
  z->nf = 1;
  z->mem_ptr = get_bc(z) + 1;
}

static void outd(z80* const z) {
  outi(z);
  set_hl(z, get_hl(z) - 2);
  z->mem_ptr = get_bc(z) - 2;
}

static void daa(z80* const z) {
  // "When this instruction is executed, the A register is BCD corrected
  // using the  contents of the flags. The exact process is the following:
  // if the least significant four bits of A contain a non-BCD digit
  // (i. e. it is greater than 9) or the H flag is set, then $06 is
  // added to the register. Then the four most significant bits are
  // checked. If this more significant digit also happens to be greater
  // than 9 or the C flag is set, then $60 is added."
  // > http://z80-heaven.wikidot.com/instructions-set:daa
  uint8_t correction = 0;

  if ((z->a & 0x0F) > 0x09 || z->hf) {
    correction += 0x06;
  }

  if (z->a > 0x99 || z->cf) {
    correction += 0x60;
    z->cf = 1;
  }

  const bool substraction = z->nf;
  if (substraction) {
    z->hf = z->hf && (z->a & 0x0F) < 0x06;
    z->a -= correction;
  } else {
    z->hf = (z->a & 0x0F) > 0x09;
    z->a += correction;
  }

  z->sf = z->a >> 7;
  z->zf = z->a == 0;
  z->pf = parity(z->a);
  z->xf = GET_BIT(3, z->a);
  z->yf = GET_BIT(5, z->a);
}

static inline uint16_t displace(
    z80* const z, uint16_t base_addr, int8_t displacement) {
  const uint16_t addr = base_addr + displacement;
  z->mem_ptr = addr;
  return addr;
}

static inline void process_interrupts(z80* const z) {
  // "When an EI instruction is executed, any pending interrupt request
  // is not accepted until after the instruction following EI is executed."
  if (z->iff_delay > 0) {
    z->iff_delay -= 1;
    if (z->iff_delay == 0) {
      z->iff1 = 1;
      z->iff2 = 1;
    }
    return;
  }

  if (z->nmi_pending) {
    z->nmi_pending = 0;
    z->halted = 0;
    z->iff1 = 0;
    inc_r(z);

    z->cyc += 11;
    call(z, 0x66);
    return;
  }

  if (z->int_pending && z->iff1) {
    z->int_pending = 0;
    z->halted = 0;
    z->iff1 = 0;
    z->iff2 = 0;
    inc_r(z);

    switch (z->interrupt_mode) {
    case 0:
      z->cyc += 11;
      exec_opcode(z, z->int_data);
      break;

    case 1:
      z->cyc += 13;
      call(z, 0x38);
      break;

    case 2:
      // Every interrupt this machine takes is an internal one, and UM005004
      // Table 4 and Figure 43 give its acknowledge two fixed wait states:
      // T1 T2 Tw Tw T3 Ti, two stacking writes and two vector reads.
      z->cyc += 18;
      call(z, rw(z, (z->i << 8) | z->int_data));
      break;

    default:
      fprintf(stderr, "unsupported interrupt mode %d\n", z->interrupt_mode);
      break;
    }

    return;
  }
}

// MARK: interface
// initialises a z80 struct. Note that read_byte, write_byte, port_in, port_out
// and userdata must be manually set by the user afterwards.
void z80_init(z80* const z) {
  z->read_byte = NULL;
  z->write_byte = NULL;
  z->port_in = NULL;
  z->port_out = NULL;
  z->userdata = NULL;

  z->cyc = 0;

  z->pc = 0;
  z->sp = 0xFFFF;
  z->ix = 0;
  z->iy = 0;
  z->mem_ptr = 0;

  // af and sp are set to 0xFFFF after reset,
  // and the other values are undefined (z80-documented)
  z->a = 0xFF;
  z->b = 0;
  z->c = 0;
  z->d = 0;
  z->e = 0;
  z->h = 0;
  z->l = 0;

  z->a_ = 0;
  z->b_ = 0;
  z->c_ = 0;
  z->d_ = 0;
  z->e_ = 0;
  z->h_ = 0;
  z->l_ = 0;
  z->f_ = 0;

  z->i = 0;
  z->r = 0;

  z->sf = 1;
  z->zf = 1;
  z->yf = 1;
  z->hf = 1;
  z->xf = 1;
  z->pf = 1;
  z->nf = 1;
  z->cf = 1;

  z->iff_delay = 0;
  z->interrupt_mode = 0;
  z->iff1 = 0;
  z->iff2 = 0;
  z->halted = 0;
  z->int_pending = 0;
  z->nmi_pending = 0;
  z->int_data = 0;
  z->z180_traps = 0;
  z->trap = 0;
}

// executes the next instruction in memory
void z80_execute(z80* const z) {
  z->cyc += m1_wait;
  if (z->halted) {
    exec_opcode(z, 0x00);
  } else {
    const uint8_t opcode = nextb(z);
    exec_opcode(z, opcode);
  }
}

// Takes a pending interrupt.  A Z180 samples its interrupt inputs at the end
// of an instruction (UM005004 Table 47, note 7), so a caller whose sources
// move with the instruction -- a timer that runs, an enable bit it clears --
// has to set int_pending between z80_execute and this, not before z80_step
// (HANDOFF 6.33).
void z80_process_interrupts(z80* const z) {
  process_interrupts(z);
}

// executes the next instruction in memory + handles interrupts
void z80_step(z80* const z) {
  z80_execute(z);
  process_interrupts(z);
}

// outputs to stdout a debug trace of the emulator
void z80_debug_output(z80* const z) {
  printf("PC: %04X, AF: %04X, BC: %04X, DE: %04X, HL: %04X, SP: %04X, "
         "IX: %04X, IY: %04X, I: %02X, R: %02X",
      z->pc, (z->a << 8) | get_f(z), get_bc(z), get_de(z), get_hl(z), z->sp,
      z->ix, z->iy, z->i, z->r);

  printf("\t(%02X %02X %02X %02X), cyc: %lu\n", rb(z, z->pc), rb(z, z->pc + 1),
      rb(z, z->pc + 2), rb(z, z->pc + 3), z->cyc);
}

// function to call when an NMI is to be serviced
void z80_gen_nmi(z80* const z) {
  z->nmi_pending = 1;
}

// function to call when an INT is to be serviced
void z80_gen_int(z80* const z, uint8_t data) {
  z->int_pending = 1;
  z->int_data = data;
}

// executes a non-prefixed opcode
void exec_opcode(z80* const z, uint8_t opcode) {
  z->cyc += cyc_00[opcode];
  inc_r(z);

  switch (opcode) {
  case 0x7F: z->a = z->a; break; // ld a,a
  case 0x78: z->a = z->b; break; // ld a,b
  case 0x79: z->a = z->c; break; // ld a,c
  case 0x7A: z->a = z->d; break; // ld a,d
  case 0x7B: z->a = z->e; break; // ld a,e
  case 0x7C: z->a = z->h; break; // ld a,h
  case 0x7D: z->a = z->l; break; // ld a,l

  case 0x47: z->b = z->a; break; // ld b,a
  case 0x40: z->b = z->b; break; // ld b,b
  case 0x41: z->b = z->c; break; // ld b,c
  case 0x42: z->b = z->d; break; // ld b,d
  case 0x43: z->b = z->e; break; // ld b,e
  case 0x44: z->b = z->h; break; // ld b,h
  case 0x45: z->b = z->l; break; // ld b,l

  case 0x4F: z->c = z->a; break; // ld c,a
  case 0x48: z->c = z->b; break; // ld c,b
  case 0x49: z->c = z->c; break; // ld c,c
  case 0x4A: z->c = z->d; break; // ld c,d
  case 0x4B: z->c = z->e; break; // ld c,e
  case 0x4C: z->c = z->h; break; // ld c,h
  case 0x4D: z->c = z->l; break; // ld c,l

  case 0x57: z->d = z->a; break; // ld d,a
  case 0x50: z->d = z->b; break; // ld d,b
  case 0x51: z->d = z->c; break; // ld d,c
  case 0x52: z->d = z->d; break; // ld d,d
  case 0x53: z->d = z->e; break; // ld d,e
  case 0x54: z->d = z->h; break; // ld d,h
  case 0x55: z->d = z->l; break; // ld d,l

  case 0x5F: z->e = z->a; break; // ld e,a
  case 0x58: z->e = z->b; break; // ld e,b
  case 0x59: z->e = z->c; break; // ld e,c
  case 0x5A: z->e = z->d; break; // ld e,d
  case 0x5B: z->e = z->e; break; // ld e,e
  case 0x5C: z->e = z->h; break; // ld e,h
  case 0x5D: z->e = z->l; break; // ld e,l

  case 0x67: z->h = z->a; break; // ld h,a
  case 0x60: z->h = z->b; break; // ld h,b
  case 0x61: z->h = z->c; break; // ld h,c
  case 0x62: z->h = z->d; break; // ld h,d
  case 0x63: z->h = z->e; break; // ld h,e
  case 0x64: z->h = z->h; break; // ld h,h
  case 0x65: z->h = z->l; break; // ld h,l

  case 0x6F: z->l = z->a; break; // ld l,a
  case 0x68: z->l = z->b; break; // ld l,b
  case 0x69: z->l = z->c; break; // ld l,c
  case 0x6A: z->l = z->d; break; // ld l,d
  case 0x6B: z->l = z->e; break; // ld l,e
  case 0x6C: z->l = z->h; break; // ld l,h
  case 0x6D: z->l = z->l; break; // ld l,l

  case 0x7E: z->a = rb(z, get_hl(z)); break; // ld a,(hl)
  case 0x46: z->b = rb(z, get_hl(z)); break; // ld b,(hl)
  case 0x4E: z->c = rb(z, get_hl(z)); break; // ld c,(hl)
  case 0x56: z->d = rb(z, get_hl(z)); break; // ld d,(hl)
  case 0x5E: z->e = rb(z, get_hl(z)); break; // ld e,(hl)
  case 0x66: z->h = rb(z, get_hl(z)); break; // ld h,(hl)
  case 0x6E: z->l = rb(z, get_hl(z)); break; // ld l,(hl)

  case 0x77: wb(z, get_hl(z), z->a); break; // ld (hl),a
  case 0x70: wb(z, get_hl(z), z->b); break; // ld (hl),b
  case 0x71: wb(z, get_hl(z), z->c); break; // ld (hl),c
  case 0x72: wb(z, get_hl(z), z->d); break; // ld (hl),d
  case 0x73: wb(z, get_hl(z), z->e); break; // ld (hl),e
  case 0x74: wb(z, get_hl(z), z->h); break; // ld (hl),h
  case 0x75: wb(z, get_hl(z), z->l); break; // ld (hl),l

  case 0x3E: z->a = nextb(z); break; // ld a,*
  case 0x06: z->b = nextb(z); break; // ld b,*
  case 0x0E: z->c = nextb(z); break; // ld c,*
  case 0x16: z->d = nextb(z); break; // ld d,*
  case 0x1E: z->e = nextb(z); break; // ld e,*
  case 0x26: z->h = nextb(z); break; // ld h,*
  case 0x2E: z->l = nextb(z); break; // ld l,*
  case 0x36: wb(z, get_hl(z), nextb(z)); break; // ld (hl),*

  case 0x0A:
    z->a = rb(z, get_bc(z));
    z->mem_ptr = get_bc(z) + 1;
    break; // ld a,(bc)
  case 0x1A:
    z->a = rb(z, get_de(z));
    z->mem_ptr = get_de(z) + 1;
    break; // ld a,(de)
  case 0x3A: {
    const uint16_t addr = nextw(z);
    z->a = rb(z, addr);
    z->mem_ptr = addr + 1;
  } break; // ld a,(**)

  case 0x02:
    wb(z, get_bc(z), z->a);
    z->mem_ptr = (z->a << 8) | ((get_bc(z) + 1) & 0xFF);
    break; // ld (bc),a

  case 0x12:
    wb(z, get_de(z), z->a);
    z->mem_ptr = (z->a << 8) | ((get_de(z) + 1) & 0xFF);
    break; // ld (de),a

  case 0x32: {
    const uint16_t addr = nextw(z);
    wb(z, addr, z->a);
    z->mem_ptr = (z->a << 8) | ((addr + 1) & 0xFF);
  } break; // ld (**),a

  case 0x01: set_bc(z, nextw(z)); break; // ld bc,**
  case 0x11: set_de(z, nextw(z)); break; // ld de,**
  case 0x21: set_hl(z, nextw(z)); break; // ld hl,**
  case 0x31: z->sp = nextw(z); break; // ld sp,**

  case 0x2A: {
    const uint16_t addr = nextw(z);
    set_hl(z, rw(z, addr));
    z->mem_ptr = addr + 1;
  } break; // ld hl,(**)

  case 0x22: {
    const uint16_t addr = nextw(z);
    ww(z, addr, get_hl(z));
    z->mem_ptr = addr + 1;
  } break; // ld (**),hl

  case 0xF9: z->sp = get_hl(z); break; // ld sp,hl

  case 0xEB: {
    const uint16_t de = get_de(z);
    set_de(z, get_hl(z));
    set_hl(z, de);
  } break; // ex de,hl

  case 0xE3: {
    const uint16_t val = rw(z, z->sp);
    ww(z, z->sp, get_hl(z));
    set_hl(z, val);
    z->mem_ptr = val;
  } break; // ex (sp),hl

  case 0x87: z->a = addb(z, z->a, z->a, 0); break; // add a,a
  case 0x80: z->a = addb(z, z->a, z->b, 0); break; // add a,b
  case 0x81: z->a = addb(z, z->a, z->c, 0); break; // add a,c
  case 0x82: z->a = addb(z, z->a, z->d, 0); break; // add a,d
  case 0x83: z->a = addb(z, z->a, z->e, 0); break; // add a,e
  case 0x84: z->a = addb(z, z->a, z->h, 0); break; // add a,h
  case 0x85: z->a = addb(z, z->a, z->l, 0); break; // add a,l
  case 0x86: z->a = addb(z, z->a, rb(z, get_hl(z)), 0); break; // add a,(hl)
  case 0xC6: z->a = addb(z, z->a, nextb(z), 0); break; // add a,*

  case 0x8F: z->a = addb(z, z->a, z->a, z->cf); break; // adc a,a
  case 0x88: z->a = addb(z, z->a, z->b, z->cf); break; // adc a,b
  case 0x89: z->a = addb(z, z->a, z->c, z->cf); break; // adc a,c
  case 0x8A: z->a = addb(z, z->a, z->d, z->cf); break; // adc a,d
  case 0x8B: z->a = addb(z, z->a, z->e, z->cf); break; // adc a,e
  case 0x8C: z->a = addb(z, z->a, z->h, z->cf); break; // adc a,h
  case 0x8D: z->a = addb(z, z->a, z->l, z->cf); break; // adc a,l
  case 0x8E: z->a = addb(z, z->a, rb(z, get_hl(z)), z->cf); break; // adc a,(hl)
  case 0xCE: z->a = addb(z, z->a, nextb(z), z->cf); break; // adc a,*

  case 0x97: z->a = subb(z, z->a, z->a, 0); break; // sub a,a
  case 0x90: z->a = subb(z, z->a, z->b, 0); break; // sub a,b
  case 0x91: z->a = subb(z, z->a, z->c, 0); break; // sub a,c
  case 0x92: z->a = subb(z, z->a, z->d, 0); break; // sub a,d
  case 0x93: z->a = subb(z, z->a, z->e, 0); break; // sub a,e
  case 0x94: z->a = subb(z, z->a, z->h, 0); break; // sub a,h
  case 0x95: z->a = subb(z, z->a, z->l, 0); break; // sub a,l
  case 0x96: z->a = subb(z, z->a, rb(z, get_hl(z)), 0); break; // sub a,(hl)
  case 0xD6: z->a = subb(z, z->a, nextb(z), 0); break; // sub a,*

  case 0x9F: z->a = subb(z, z->a, z->a, z->cf); break; // sbc a,a
  case 0x98: z->a = subb(z, z->a, z->b, z->cf); break; // sbc a,b
  case 0x99: z->a = subb(z, z->a, z->c, z->cf); break; // sbc a,c
  case 0x9A: z->a = subb(z, z->a, z->d, z->cf); break; // sbc a,d
  case 0x9B: z->a = subb(z, z->a, z->e, z->cf); break; // sbc a,e
  case 0x9C: z->a = subb(z, z->a, z->h, z->cf); break; // sbc a,h
  case 0x9D: z->a = subb(z, z->a, z->l, z->cf); break; // sbc a,l
  case 0x9E: z->a = subb(z, z->a, rb(z, get_hl(z)), z->cf); break; // sbc a,(hl)
  case 0xDE: z->a = subb(z, z->a, nextb(z), z->cf); break; // sbc a,*

  case 0x09: addhl(z, get_bc(z)); break; // add hl,bc
  case 0x19: addhl(z, get_de(z)); break; // add hl,de
  case 0x29: addhl(z, get_hl(z)); break; // add hl,hl
  case 0x39: addhl(z, z->sp); break; // add hl,sp

  case 0xF3:
    z->iff1 = 0;
    z->iff2 = 0;
    break; // di
  case 0xFB: z->iff_delay = 1; break; // ei
  case 0x00: break; // nop
  case 0x76: z->halted = 1; break; // halt

  case 0x3C: z->a = inc(z, z->a); break; // inc a
  case 0x04: z->b = inc(z, z->b); break; // inc b
  case 0x0C: z->c = inc(z, z->c); break; // inc c
  case 0x14: z->d = inc(z, z->d); break; // inc d
  case 0x1C: z->e = inc(z, z->e); break; // inc e
  case 0x24: z->h = inc(z, z->h); break; // inc h
  case 0x2C: z->l = inc(z, z->l); break; // inc l
  case 0x34: {
    uint8_t result = inc(z, rb(z, get_hl(z)));
    wb(z, get_hl(z), result);
  } break; // inc (hl)

  case 0x3D: z->a = dec(z, z->a); break; // dec a
  case 0x05: z->b = dec(z, z->b); break; // dec b
  case 0x0D: z->c = dec(z, z->c); break; // dec c
  case 0x15: z->d = dec(z, z->d); break; // dec d
  case 0x1D: z->e = dec(z, z->e); break; // dec e
  case 0x25: z->h = dec(z, z->h); break; // dec h
  case 0x2D: z->l = dec(z, z->l); break; // dec l
  case 0x35: {
    uint8_t result = dec(z, rb(z, get_hl(z)));
    wb(z, get_hl(z), result);
  } break; // dec (hl)

  case 0x03: set_bc(z, get_bc(z) + 1); break; // inc bc
  case 0x13: set_de(z, get_de(z) + 1); break; // inc de
  case 0x23: set_hl(z, get_hl(z) + 1); break; // inc hl
  case 0x33: z->sp = z->sp + 1; break; // inc sp

  case 0x0B: set_bc(z, get_bc(z) - 1); break; // dec bc
  case 0x1B: set_de(z, get_de(z) - 1); break; // dec de
  case 0x2B: set_hl(z, get_hl(z) - 1); break; // dec hl
  case 0x3B: z->sp = z->sp - 1; break; // dec sp

  case 0x27: daa(z); break; // daa

  case 0x2F:
    z->a = ~z->a;
    z->nf = 1;
    z->hf = 1;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
    break; // cpl

  case 0x37:
    z->cf = 1;
    z->nf = 0;
    z->hf = 0;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
    break; // scf

  case 0x3F:
    z->hf = z->cf;
    z->cf = !z->cf;
    z->nf = 0;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
    break; // ccf

  case 0x07: {
    z->cf = z->a >> 7;
    z->a = (z->a << 1) | z->cf;
    z->nf = 0;
    z->hf = 0;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
  } break; // rlca (rotate left)

  case 0x0F: {
    z->cf = z->a & 1;
    z->a = (z->a >> 1) | (z->cf << 7);
    z->nf = 0;
    z->hf = 0;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
  } break; // rrca (rotate right)

  case 0x17: {
    const bool cy = z->cf;
    z->cf = z->a >> 7;
    z->a = (z->a << 1) | cy;
    z->nf = 0;
    z->hf = 0;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
  } break; // rla

  case 0x1F: {
    const bool cy = z->cf;
    z->cf = z->a & 1;
    z->a = (z->a >> 1) | (cy << 7);
    z->nf = 0;
    z->hf = 0;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
  } break; // rra

  case 0xA7: land(z, z->a); break; // and a
  case 0xA0: land(z, z->b); break; // and b
  case 0xA1: land(z, z->c); break; // and c
  case 0xA2: land(z, z->d); break; // and d
  case 0xA3: land(z, z->e); break; // and e
  case 0xA4: land(z, z->h); break; // and h
  case 0xA5: land(z, z->l); break; // and l
  case 0xA6: land(z, rb(z, get_hl(z))); break; // and (hl)
  case 0xE6: land(z, nextb(z)); break; // and *

  case 0xAF: lxor(z, z->a); break; // xor a
  case 0xA8: lxor(z, z->b); break; // xor b
  case 0xA9: lxor(z, z->c); break; // xor c
  case 0xAA: lxor(z, z->d); break; // xor d
  case 0xAB: lxor(z, z->e); break; // xor e
  case 0xAC: lxor(z, z->h); break; // xor h
  case 0xAD: lxor(z, z->l); break; // xor l
  case 0xAE: lxor(z, rb(z, get_hl(z))); break; // xor (hl)
  case 0xEE: lxor(z, nextb(z)); break; // xor *

  case 0xB7: lor(z, z->a); break; // or a
  case 0xB0: lor(z, z->b); break; // or b
  case 0xB1: lor(z, z->c); break; // or c
  case 0xB2: lor(z, z->d); break; // or d
  case 0xB3: lor(z, z->e); break; // or e
  case 0xB4: lor(z, z->h); break; // or h
  case 0xB5: lor(z, z->l); break; // or l
  case 0xB6: lor(z, rb(z, get_hl(z))); break; // or (hl)
  case 0xF6: lor(z, nextb(z)); break; // or *

  case 0xBF: cp(z, z->a); break; // cp a
  case 0xB8: cp(z, z->b); break; // cp b
  case 0xB9: cp(z, z->c); break; // cp c
  case 0xBA: cp(z, z->d); break; // cp d
  case 0xBB: cp(z, z->e); break; // cp e
  case 0xBC: cp(z, z->h); break; // cp h
  case 0xBD: cp(z, z->l); break; // cp l
  case 0xBE: cp(z, rb(z, get_hl(z))); break; // cp (hl)
  case 0xFE: cp(z, nextb(z)); break; // cp *

  case 0xC3: jump(z, nextw(z)); break; // jm **
  case 0xC2: cond_jump(z, z->zf == 0); break; // jp nz, **
  case 0xCA: cond_jump(z, z->zf == 1); break; // jp z, **
  case 0xD2: cond_jump(z, z->cf == 0); break; // jp nc, **
  case 0xDA: cond_jump(z, z->cf == 1); break; // jp c, **
  case 0xE2: cond_jump(z, z->pf == 0); break; // jp po, **
  case 0xEA: cond_jump(z, z->pf == 1); break; // jp pe, **
  case 0xF2: cond_jump(z, z->sf == 0); break; // jp p, **
  case 0xFA: cond_jump(z, z->sf == 1); break; // jp m, **

  case 0x10: cond_jr(z, --z->b != 0); break; // djnz *
  case 0x18: z->pc += (int8_t) nextb(z); break; // jr *
  case 0x20: cond_jr(z, z->zf == 0); break; // jr nz, *
  case 0x28: cond_jr(z, z->zf == 1); break; // jr z, *
  case 0x30: cond_jr(z, z->cf == 0); break; // jr nc, *
  case 0x38: cond_jr(z, z->cf == 1); break; // jr c, *

  case 0xE9: z->pc = get_hl(z); break; // jp (hl)
  case 0xCD: call(z, nextw(z)); break; // call

  case 0xC4: cond_call(z, z->zf == 0); break; // cnz
  case 0xCC: cond_call(z, z->zf == 1); break; // cz
  case 0xD4: cond_call(z, z->cf == 0); break; // cnc
  case 0xDC: cond_call(z, z->cf == 1); break; // cc
  case 0xE4: cond_call(z, z->pf == 0); break; // cpo
  case 0xEC: cond_call(z, z->pf == 1); break; // cpe
  case 0xF4: cond_call(z, z->sf == 0); break; // cp
  case 0xFC: cond_call(z, z->sf == 1); break; // cm

  case 0xC9: ret(z); break; // ret
  case 0xC0: cond_ret(z, z->zf == 0); break; // ret nz
  case 0xC8: cond_ret(z, z->zf == 1); break; // ret z
  case 0xD0: cond_ret(z, z->cf == 0); break; // ret nc
  case 0xD8: cond_ret(z, z->cf == 1); break; // ret c
  case 0xE0: cond_ret(z, z->pf == 0); break; // ret po
  case 0xE8: cond_ret(z, z->pf == 1); break; // ret pe
  case 0xF0: cond_ret(z, z->sf == 0); break; // ret p
  case 0xF8: cond_ret(z, z->sf == 1); break; // ret m

  case 0xC7: call(z, 0x00); break; // rst 0
  case 0xCF: call(z, 0x08); break; // rst 1
  case 0xD7: call(z, 0x10); break; // rst 2
  case 0xDF: call(z, 0x18); break; // rst 3
  case 0xE7: call(z, 0x20); break; // rst 4
  case 0xEF: call(z, 0x28); break; // rst 5
  case 0xF7: call(z, 0x30); break; // rst 6
  case 0xFF: call(z, 0x38); break; // rst 7

  case 0xC5: pushw(z, get_bc(z)); break; // push bc
  case 0xD5: pushw(z, get_de(z)); break; // push de
  case 0xE5: pushw(z, get_hl(z)); break; // push hl
  case 0xF5: pushw(z, (z->a << 8) | get_f(z)); break; // push af

  case 0xC1: set_bc(z, popw(z)); break; // pop bc
  case 0xD1: set_de(z, popw(z)); break; // pop de
  case 0xE1: set_hl(z, popw(z)); break; // pop hl
  case 0xF1: {
    uint16_t val = popw(z);
    z->a = val >> 8;
    set_f(z, val & 0xFF);
  } break; // pop af

  case 0xDB: {
    const uint8_t port = nextb(z);
    const uint8_t a = z->a;
    z->a = z->port_in(z, ((uint16_t)a << 8) | port);
    z->mem_ptr = (a << 8) | (z->a + 1);
  } break; // in a,(n)

  case 0xD3: {
    const uint8_t port = nextb(z);
    z->port_out(z, ((uint16_t)z->a << 8) | port, z->a);
    z->mem_ptr = (port + 1) | (z->a << 8);
  } break; // out (n), a

  case 0x08: {
    uint8_t a = z->a;
    uint8_t f = get_f(z);

    z->a = z->a_;
    set_f(z, z->f_);

    z->a_ = a;
    z->f_ = f;
  } break; // ex af,af'
  case 0xD9: {
    uint8_t b = z->b, c = z->c, d = z->d, e = z->e, h = z->h, l = z->l;

    z->b = z->b_;
    z->c = z->c_;
    z->d = z->d_;
    z->e = z->e_;
    z->h = z->h_;
    z->l = z->l_;

    z->b_ = b;
    z->c_ = c;
    z->d_ = d;
    z->e_ = e;
    z->h_ = h;
    z->l_ = l;
  } break; // exx

  case 0xCB: exec_opcode_cb(z, nextb(z)); break;
  case 0xED: exec_opcode_ed(z, nextb(z)); break;
  case 0xDD: exec_opcode_ddfd(z, nextb(z), &z->ix); break;
  case 0xFD: exec_opcode_ddfd(z, nextb(z), &z->iy); break;

  default: fprintf(stderr, "unknown opcode %02X\n", opcode); break;
  }
}

// executes a DD/FD opcode (IZ = IX or IY)
void exec_opcode_ddfd(z80* const z, uint8_t opcode, uint16_t* const iz) {
  z->cyc += m1_wait + cyc_ddfd[opcode];
  inc_r(z);
  if (!z180_ddfd_defined(opcode) && z180_trap(z, z->pc - 2, false)) return;

#define IZD displace(z, *iz, nextb(z))
#define IZH (*iz >> 8)
#define IZL (*iz & 0xFF)

  switch (opcode) {
  case 0xE1: *iz = popw(z); break; // pop iz
  case 0xE5: pushw(z, *iz); break; // push iz

  case 0xE9: jump(z, *iz); break; // jp iz

  case 0x09: addiz(z, iz, get_bc(z)); break; // add iz,bc
  case 0x19: addiz(z, iz, get_de(z)); break; // add iz,de
  case 0x29: addiz(z, iz, *iz); break; // add iz,iz
  case 0x39: addiz(z, iz, z->sp); break; // add iz,sp

  case 0x84: z->a = addb(z, z->a, IZH, 0); break; // add a,izh
  case 0x85: z->a = addb(z, z->a, *iz & 0xFF, 0); break; // add a,izl
  case 0x8C: z->a = addb(z, z->a, IZH, z->cf); break; // adc a,izh
  case 0x8D: z->a = addb(z, z->a, *iz & 0xFF, z->cf); break; // adc a,izl

  case 0x86: z->a = addb(z, z->a, rb(z, IZD), 0); break; // add a,(iz+*)
  case 0x8E: z->a = addb(z, z->a, rb(z, IZD), z->cf); break; // adc a,(iz+*)
  case 0x96: z->a = subb(z, z->a, rb(z, IZD), 0); break; // sub (iz+*)
  case 0x9E: z->a = subb(z, z->a, rb(z, IZD), z->cf); break; // sbc (iz+*)

  case 0x94: z->a = subb(z, z->a, IZH, 0); break; // sub izh
  case 0x95: z->a = subb(z, z->a, *iz & 0xFF, 0); break; // sub izl
  case 0x9C: z->a = subb(z, z->a, IZH, z->cf); break; // sbc izh
  case 0x9D: z->a = subb(z, z->a, *iz & 0xFF, z->cf); break; // sbc izl

  case 0xA6: land(z, rb(z, IZD)); break; // and (iz+*)
  case 0xA4: land(z, IZH); break; // and izh
  case 0xA5: land(z, *iz & 0xFF); break; // and izl

  case 0xAE: lxor(z, rb(z, IZD)); break; // xor (iz+*)
  case 0xAC: lxor(z, IZH); break; // xor izh
  case 0xAD: lxor(z, *iz & 0xFF); break; // xor izl

  case 0xB6: lor(z, rb(z, IZD)); break; // or (iz+*)
  case 0xB4: lor(z, IZH); break; // or izh
  case 0xB5: lor(z, *iz & 0xFF); break; // or izl

  case 0xBE: cp(z, rb(z, IZD)); break; // cp (iz+*)
  case 0xBC: cp(z, IZH); break; // cp izh
  case 0xBD: cp(z, *iz & 0xFF); break; // cp izl

  case 0x23: *iz += 1; break; // inc iz
  case 0x2B: *iz -= 1; break; // dec iz

  case 0x34: {
    uint16_t addr = IZD;
    wb(z, addr, inc(z, rb(z, addr)));
  } break; // inc (iz+*)

  case 0x35: {
    uint16_t addr = IZD;
    wb(z, addr, dec(z, rb(z, addr)));
  } break; // dec (iz+*)

  case 0x24: *iz = IZL | ((inc(z, IZH)) << 8); break; // inc izh
  case 0x25: *iz = IZL | ((dec(z, IZH)) << 8); break; // dec izh
  case 0x2C: *iz = (IZH << 8) | inc(z, IZL); break; // inc izl
  case 0x2D: *iz = (IZH << 8) | dec(z, IZL); break; // dec izl

  case 0x2A: *iz = rw(z, nextw(z)); break; // ld iz,(**)
  case 0x22: ww(z, nextw(z), *iz); break; // ld (**),iz
  case 0x21: *iz = nextw(z); break; // ld iz,**

  case 0x36: {
    uint16_t addr = IZD;
    wb(z, addr, nextb(z));
  } break; // ld (iz+*),*

  case 0x70: wb(z, IZD, z->b); break; // ld (iz+*),b
  case 0x71: wb(z, IZD, z->c); break; // ld (iz+*),c
  case 0x72: wb(z, IZD, z->d); break; // ld (iz+*),d
  case 0x73: wb(z, IZD, z->e); break; // ld (iz+*),e
  case 0x74: wb(z, IZD, z->h); break; // ld (iz+*),h
  case 0x75: wb(z, IZD, z->l); break; // ld (iz+*),l
  case 0x77: wb(z, IZD, z->a); break; // ld (iz+*),a

  case 0x46: z->b = rb(z, IZD); break; // ld b,(iz+*)
  case 0x4E: z->c = rb(z, IZD); break; // ld c,(iz+*)
  case 0x56: z->d = rb(z, IZD); break; // ld d,(iz+*)
  case 0x5E: z->e = rb(z, IZD); break; // ld e,(iz+*)
  case 0x66: z->h = rb(z, IZD); break; // ld h,(iz+*)
  case 0x6E: z->l = rb(z, IZD); break; // ld l,(iz+*)
  case 0x7E: z->a = rb(z, IZD); break; // ld a,(iz+*)

  case 0x44: z->b = IZH; break; // ld b,izh
  case 0x4C: z->c = IZH; break; // ld c,izh
  case 0x54: z->d = IZH; break; // ld d,izh
  case 0x5C: z->e = IZH; break; // ld e,izh
  case 0x7C: z->a = IZH; break; // ld a,izh

  case 0x45: z->b = IZL; break; // ld b,izl
  case 0x4D: z->c = IZL; break; // ld c,izl
  case 0x55: z->d = IZL; break; // ld d,izl
  case 0x5D: z->e = IZL; break; // ld e,izl
  case 0x7D: z->a = IZL; break; // ld a,izl

  case 0x60: *iz = IZL | (z->b << 8); break; // ld izh,b
  case 0x61: *iz = IZL | (z->c << 8); break; // ld izh,c
  case 0x62: *iz = IZL | (z->d << 8); break; // ld izh,d
  case 0x63: *iz = IZL | (z->e << 8); break; // ld izh,e
  case 0x64: break; // ld izh,izh
  case 0x65: *iz = (IZL << 8) | IZL; break; // ld izh,izl
  case 0x67: *iz = IZL | (z->a << 8); break; // ld izh,a
  case 0x26: *iz = IZL | (nextb(z) << 8); break; // ld izh,*

  case 0x68: *iz = (IZH << 8) | z->b; break; // ld izl,b
  case 0x69: *iz = (IZH << 8) | z->c; break; // ld izl,c
  case 0x6A: *iz = (IZH << 8) | z->d; break; // ld izl,d
  case 0x6B: *iz = (IZH << 8) | z->e; break; // ld izl,e
  case 0x6C: *iz = (IZH << 8) | IZH; break; // ld izl,izh
  case 0x6D: break; // ld izl,izl
  case 0x6F: *iz = (IZH << 8) | z->a; break; // ld izl,a
  case 0x2E: *iz = (IZH << 8) | nextb(z); break; // ld izl,*

  case 0xF9: z->sp = *iz; break; // ld sp,iz

  case 0xE3: {
    const uint16_t val = rw(z, z->sp);
    ww(z, z->sp, *iz);
    *iz = val;
    z->mem_ptr = val;
  } break; // ex (sp),iz

  case 0xCB: {
    uint16_t addr = IZD;
    uint8_t op = nextb(z);
    exec_opcode_dcb(z, op, addr);
  } break;

  default: {
    // any other FD/DD opcode behaves as a non-prefixed opcode:
    exec_opcode(z, opcode);
    // R should not be incremented twice:
    z->r = (z->r & 0x80) | ((z->r - 1) & 0x7f);
  } break;
  }

#undef IZD
#undef IZH
#undef IZL
}

// executes a CB opcode
void exec_opcode_cb(z80* const z, uint8_t opcode) {
  inc_r(z);
  const bool sll = (opcode & 0xf8) == 0x30;
  if (sll && z180_trap(z, z->pc - 2, false)) return;

  // decoding instructions from http://z80.info/decoding.htm#cb
  uint8_t x_ = (opcode >> 6) & 3; // 0b11
  uint8_t y_ = (opcode >> 3) & 7; // 0b111
  uint8_t z_ = opcode & 7; // 0b111

  // UM005004 Table 39: BIT takes 6 on a register and 9 on (HL); the rotates,
  // SET and RES take 7 and 13.  (It prints 3 for SRL (HL), a misprint among
  // siblings that all say 13.)
  z->cyc += m1_wait + (x_ == 1 ? (z_ == 6 ? 9 : 6) : (z_ == 6 ? 13 : 7));

  uint8_t hl = 0;
  uint8_t* reg = 0;
  switch (z_) {
  case 0: reg = &z->b; break;
  case 1: reg = &z->c; break;
  case 2: reg = &z->d; break;
  case 3: reg = &z->e; break;
  case 4: reg = &z->h; break;
  case 5: reg = &z->l; break;
  case 6:
    hl = rb(z, get_hl(z));
    reg = &hl;
    break;
  case 7: reg = &z->a; break;
  }

  switch (x_) {
  case 0: {
    switch (y_) {
    case 0: *reg = cb_rlc(z, *reg); break;
    case 1: *reg = cb_rrc(z, *reg); break;
    case 2: *reg = cb_rl(z, *reg); break;
    case 3: *reg = cb_rr(z, *reg); break;
    case 4: *reg = cb_sla(z, *reg); break;
    case 5: *reg = cb_sra(z, *reg); break;
    case 6: *reg = cb_sll(z, *reg); break;
    case 7: *reg = cb_srl(z, *reg); break;
    }
  } break; // rot[y] r[z]
  case 1: { // BIT y, r[z]
    cb_bit(z, *reg, y_);

    // in bit (hl), x/y flags are handled differently:
    if (z_ == 6) {
      z->yf = GET_BIT(5, z->mem_ptr >> 8);
      z->xf = GET_BIT(3, z->mem_ptr >> 8);
    }
  } break;
  case 2: *reg &= ~(1 << y_); break; // RES y, r[z]
  case 3: *reg |= 1 << y_; break; // SET y, r[z]
  }

  // BIT b,(HL) performs no write cycle on real silicon; only the rotate,
  // RES and SET forms store the operand back.  Writing unconditionally is
  // harmless in RAM but visible anywhere a write has a side effect.
  if (reg == &hl && x_ != 1) {
    wb(z, get_hl(z), hl);
  }
}

// executes a displaced CB opcode (DDCB or FDCB)
void exec_opcode_dcb(z80* const z, uint8_t opcode, uint16_t addr) {
  // The Z180 has only the (IX+d) forms without a register copy, and no SLL.
  // The op code is the fourth byte but the third op code byte: d does not
  // count, hence UFO=1.
  const bool undefined = (opcode & 7) != 6 || (opcode & 0xf8) == 0x30;
  if (undefined && z180_trap(z, z->pc - 4, true)) return;
  uint8_t val = rb(z, addr);
  uint8_t result = 0;

  // decoding instructions from http://z80.info/decoding.htm#ddcb
  uint8_t x_ = (opcode >> 6) & 3; // 0b11
  uint8_t y_ = (opcode >> 3) & 7; // 0b111
  uint8_t z_ = opcode & 7; // 0b111

  switch (x_) {
  case 0: {
    // rot[y] (iz+d)
    switch (y_) {
    case 0: result = cb_rlc(z, val); break;
    case 1: result = cb_rrc(z, val); break;
    case 2: result = cb_rl(z, val); break;
    case 3: result = cb_rr(z, val); break;
    case 4: result = cb_sla(z, val); break;
    case 5: result = cb_sra(z, val); break;
    case 6: result = cb_sll(z, val); break;
    case 7: result = cb_srl(z, val); break;
    }
  } break;
  case 1: {
    result = cb_bit(z, val, y_);
    z->yf = GET_BIT(5, addr >> 8);
    z->xf = GET_BIT(3, addr >> 8);
  } break; // bit y,(iz+d)
  case 2: result = val & ~(1 << y_); break; // res y, (iz+d)
  case 3: result = val | (1 << y_); break; // set y, (iz+d)

  default: fprintf(stderr, "unknown XYCB opcode: %02X\n", opcode); break;
  }

  // ld r[z], rot[y] (iz+d)
  // ld r[z], res y,(iz+d)
  // ld r[z], set y,(iz+d)
  if (x_ != 1 && z_ != 6) {
    switch (z_) {
    case 0: z->b = result; break;
    case 1: z->c = result; break;
    case 2: z->d = result; break;
    case 3: z->e = result; break;
    case 4: z->h = result; break;
    case 5: z->l = result; break;
    case 6: wb(z, get_hl(z), result); break;
    case 7: z->a = result; break;
    }
  }

  if (x_ == 1) {
    // UM005004 Table 39: BIT b,(IX+d) takes 15, the others 19.
    z->cyc += 15;
  } else {
    wb(z, addr, result);
    z->cyc += 19;
  }
}

// executes a ED opcode
void exec_opcode_ed(z80* const z, uint8_t opcode) {
  z->cyc += m1_wait + cyc_ed[opcode];
  inc_r(z);
  if (!z180_ed_defined(opcode) && z180_trap(z, z->pc - 2, false)) return;
  switch (opcode) {
  case 0x00: case 0x08: case 0x10: case 0x18:
  case 0x20: case 0x28: case 0x30: case 0x38:
    z180_in0(z, opcode);
    break;

  case 0x01: case 0x09: case 0x11: case 0x19:
  case 0x21: case 0x29: case 0x31: case 0x39:
    z180_out0(z, opcode);
    break;

  case 0x04: z180_test(z, z->b); break;
  case 0x0C: z180_test(z, z->c); break;
  case 0x14: z180_test(z, z->d); break;
  case 0x1C: z180_test(z, z->e); break;
  case 0x24: z180_test(z, z->h); break;
  case 0x2C: z180_test(z, z->l); break;
  case 0x34: z180_test(z, rb(z, get_hl(z))); break;
  case 0x3C: z180_test(z, z->a); break;

  case 0x4C: case 0x5C: case 0x6C: case 0x7C:
    z180_mlt(z, opcode);
    break;

  case 0x64:
    z180_test(z, nextb(z));
    break; // tst a,n

  case 0x74: {
    const uint8_t mask = nextb(z);
    const uint8_t saved_a = z->a;
    z->a = mask;
    z180_test(z, z->port_in(z, get_bc(z)));
    z->a = saved_a;
  } break; // tstio n

  case 0x83: z180_otim(z, false, false); break; // otim
  case 0x8B: z180_otim(z, true, false); break;  // otdm
  case 0x93: z180_otim(z, false, true); break;  // otimr
  case 0x9B: z180_otim(z, true, true); break;   // otdmr

  case 0x47: z->i = z->a; break; // ld i,a
  case 0x4F: z->r = z->a; break; // ld r,a

  case 0x57:
    z->a = z->i;
    z->sf = z->a >> 7;
    z->zf = z->a == 0;
    z->hf = 0;
    z->nf = 0;
    z->pf = z->iff2;
    break; // ld a,i

  case 0x5F:
    z->a = z->r;
    z->sf = z->a >> 7;
    z->zf = z->a == 0;
    z->hf = 0;
    z->nf = 0;
    z->pf = z->iff2;
    break; // ld a,r

  case 0x45:
  case 0x55:
  case 0x5D:
  case 0x65:
  case 0x6D:
  case 0x75:
  case 0x7D:
    z->iff1 = z->iff2;
    ret(z);
    break; // retn
  case 0x4D: ret(z); break; // reti

  case 0xA0: ldi(z); break; // ldi
  case 0xB0: {
    ldi(z);

    if (get_bc(z) != 0) {
      z->pc -= 2;
      z->cyc += 2;
      z->mem_ptr = z->pc + 1;
    }
  } break; // ldir

  case 0xA8: ldd(z); break; // ldd
  case 0xB8: {
    ldd(z);

    if (get_bc(z) != 0) {
      z->pc -= 2;
      z->cyc += 2;
      z->mem_ptr = z->pc + 1;
    }
  } break; // lddr

  case 0xA1: cpi(z); break; // cpi
  case 0xA9: cpd(z); break; // cpd
  case 0xB1: {
    cpi(z);
    if (get_bc(z) != 0 && !z->zf) {
      z->pc -= 2;
      z->cyc += 2;
      z->mem_ptr = z->pc + 1;
    } else {
      z->mem_ptr += 1;
    }
  } break; // cpir
  case 0xB9: {
    cpd(z);
    if (get_bc(z) != 0 && !z->zf) {
      z->pc -= 2;
      z->cyc += 2;
    } else {
      z->mem_ptr += 1;
    }
  } break; // cpdr

  case 0x40: in_r_c(z, &z->b); break; // in b, (c)
  case 0x48: in_r_c(z, &z->c); break; // in c, (c)
  case 0x50: in_r_c(z, &z->d); break; // in d, (c)
  case 0x58: in_r_c(z, &z->e); break; // in e, (c)
  case 0x60: in_r_c(z, &z->h); break; // in h, (c)
  case 0x68: in_r_c(z, &z->l); break; // in l, (c)
  case 0x70: {
    uint8_t val;
    in_r_c(z, &val);
  } break; // in (c)
  case 0x78:
    in_r_c(z, &z->a);
    z->mem_ptr = get_bc(z) + 1;
    break; // in a, (c)

  case 0xA2: ini(z); break; // ini
  case 0xB2:
    ini(z);
    if (z->b > 0) {
      z->pc -= 2;
      z->cyc += 2;
    }
    break; // inir
  case 0xAA: ind(z); break; // ind
  case 0xBA:
    ind(z);
    if (z->b > 0) {
      z->pc -= 2;
      z->cyc += 2;
    }
    break; // indr

  case 0x41: z->port_out(z, get_bc(z), z->b); break; // out (c), b
  case 0x49: z->port_out(z, get_bc(z), z->c); break; // out (c), c
  case 0x51: z->port_out(z, get_bc(z), z->d); break; // out (c), d
  case 0x59: z->port_out(z, get_bc(z), z->e); break; // out (c), e
  case 0x61: z->port_out(z, get_bc(z), z->h); break; // out (c), h
  case 0x69: z->port_out(z, get_bc(z), z->l); break; // out (c), l
  case 0x71: z->port_out(z, get_bc(z), 0); break; // out (c), 0
  case 0x79:
    z->port_out(z, get_bc(z), z->a);
    z->mem_ptr = get_bc(z) + 1;
    break; // out (c), a

  case 0xA3: outi(z); break; // outi
  case 0xB3: {
    outi(z);
    if (z->b > 0) {
      z->pc -= 2;
      z->cyc += 2;
    }
  } break; // otir
  case 0xAB: outd(z); break; // outd
  case 0xBB: {
    outd(z);
    if (z->b > 0) {
      z->pc -= 2;
      z->cyc += 2;
    }
  } break; // otdr

  case 0x42: sbchl(z, get_bc(z)); break; // sbc hl,bc
  case 0x52: sbchl(z, get_de(z)); break; // sbc hl,de
  case 0x62: sbchl(z, get_hl(z)); break; // sbc hl,hl
  case 0x72: sbchl(z, z->sp); break; // sbc hl,sp

  case 0x4A: adchl(z, get_bc(z)); break; // adc hl,bc
  case 0x5A: adchl(z, get_de(z)); break; // adc hl,de
  case 0x6A: adchl(z, get_hl(z)); break; // adc hl,hl
  case 0x7A: adchl(z, z->sp); break; // adc hl,sp

  case 0x43: {
    const uint16_t addr = nextw(z);
    ww(z, addr, get_bc(z));
    z->mem_ptr = addr + 1;
  } break; // ld (**), bc

  case 0x53: {
    const uint16_t addr = nextw(z);
    ww(z, addr, get_de(z));
    z->mem_ptr = addr + 1;
  } break; // ld (**), de

  case 0x63: {
    const uint16_t addr = nextw(z);
    ww(z, addr, get_hl(z));
    z->mem_ptr = addr + 1;
  } break; // ld (**), hl

  case 0x73: {
    const uint16_t addr = nextw(z);
    ww(z, addr, z->sp);
    z->mem_ptr = addr + 1;
  } break; // ld (**),sp

  case 0x4B: {
    const uint16_t addr = nextw(z);
    set_bc(z, rw(z, addr));
    z->mem_ptr = addr + 1;
  } break; // ld bc, (**)

  case 0x5B: {
    const uint16_t addr = nextw(z);
    set_de(z, rw(z, addr));
    z->mem_ptr = addr + 1;
  } break; // ld de, (**)

  case 0x6B: {
    const uint16_t addr = nextw(z);
    set_hl(z, rw(z, addr));
    z->mem_ptr = addr + 1;
  } break; // ld hl, (**)

  case 0x7B: {
    const uint16_t addr = nextw(z);
    z->sp = rw(z, addr);
    z->mem_ptr = addr + 1;
  } break; // ld sp,(**)

  case 0x44: z->a = subb(z, 0, z->a, 0); break; // neg

  case 0x46:
  case 0x66: z->interrupt_mode = 0; break; // im 0
  case 0x56: z->interrupt_mode = 1; break; // im 1
  case 0x5E:
  case 0x7E: z->interrupt_mode = 2; break; // im 2

  case 0x76: z->halted = 1; break; // slp

  case 0x67: {
    uint8_t a = z->a;
    uint8_t val = rb(z, get_hl(z));
    z->a = (a & 0xF0) | (val & 0xF);
    wb(z, get_hl(z), (val >> 4) | (a << 4));

    z->nf = 0;
    z->hf = 0;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
    z->zf = z->a == 0;
    z->sf = z->a >> 7;
    z->pf = parity(z->a);
    z->mem_ptr = get_hl(z) + 1;
  } break; // rrd

  case 0x6F: {
    uint8_t a = z->a;
    uint8_t val = rb(z, get_hl(z));
    z->a = (a & 0xF0) | (val >> 4);
    wb(z, get_hl(z), (val << 4) | (a & 0xF));

    z->nf = 0;
    z->hf = 0;
    z->xf = GET_BIT(3, z->a);
    z->yf = GET_BIT(5, z->a);
    z->zf = z->a == 0;
    z->sf = z->a >> 7;
    z->pf = parity(z->a);
    z->mem_ptr = get_hl(z) + 1;
  } break; // rld

  default: fprintf(stderr, "unknown ED opcode: %02X\n", opcode); break;
  }
}

#undef GET_BIT
