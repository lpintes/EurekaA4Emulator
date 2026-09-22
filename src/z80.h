#ifndef Z80_Z80_H_
#define Z80_Z80_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct z80 z80;
struct z80 {
  uint8_t (*read_byte)(void*, uint16_t);
  void (*write_byte)(void*, uint16_t, uint8_t);
  uint8_t (*port_in)(z80*, uint16_t);
  void (*port_out)(z80*, uint16_t, uint8_t);
  void* userdata;

  unsigned long cyc; // cycle count (t-states)

  // Wait states added to **every** memory cycle, the M1 opcode fetch included:
  // DCNTL bits 7-6 (MWI1, MWI0) on a Z180, 0 to 3 of them (UM005004 Table 4).
  // The instruction tables in z80.c count three T-states per memory cycle, so
  // this is charged per access instead of being baked into them.  Zero unless
  // the host sets it, which is what the Eureka's firmware leaves it at.
  uint8_t mem_wait;

  uint16_t pc, sp, ix, iy; // special purpose registers
  uint16_t mem_ptr; // "wz" register
  uint8_t a, b, c, d, e, h, l; // main registers
  uint8_t a_, b_, c_, d_, e_, h_, l_, f_; // alternate registers
  uint8_t i, r; // interrupt vector, memory refresh

  // flags: sign, zero, yf, half-carry, xf, parity/overflow, negative, carry
  bool sf : 1, zf : 1, yf : 1, hf : 1, xf : 1, pf : 1, nf : 1, cf : 1;

  uint8_t iff_delay;
  uint8_t interrupt_mode;
  uint8_t int_data;
  bool iff1 : 1, iff2 : 1;
  bool halted : 1;
  bool int_pending : 1, nmi_pending : 1;

  // Z180 undefined op code trap (UM005004 p. 70-71).  Off, an undefined code
  // runs as it would on a Z80, which is what ZEXDOC checks on the bare core.
  // On, it pushes the PC and jumps to 0000h, and trap says which byte was
  // undefined: 1 the second (UFO=0), 2 the third (UFO=1).  The caller moves
  // it into ITC and clears it.
  bool z180_traps : 1;
  uint8_t trap;
};

void z80_init(z80* const z);
void z80_step(z80* const z);
void z80_execute(z80* const z);
void z80_process_interrupts(z80* const z);
void z80_debug_output(z80* const z);
void z80_gen_nmi(z80* const z);
void z80_gen_int(z80* const z, uint8_t data);

#endif // Z80_Z80_H_
