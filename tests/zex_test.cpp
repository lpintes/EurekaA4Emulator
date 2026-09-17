// Runs ZEXDOC (or ZEXALL) on the bare Z80 core, outside the machine model.
//
// The Z180 manual check (HANDOFF 6.33) covered timing and the Z180
// extensions, not the results of the ordinary Z80 instructions.  ZEXDOC
// compares a CRC of every documented flag and register after each test
// group against a real Z80, so it catches what the manual cannot.
//
// The program is not in the repository (it is GPL, and it is someone else's
// work); pass its path.  Only BDOS functions 2 and 9 are served -- that is
// all ZEXDOC asks for -- and a jump to 0000h ends the run.
//
//   zex_test PATH\zexdoc.com
//
// PASS means the program ran to its end and printed no "ERROR".  ZEXALL
// also checks the undocumented X and Y flags, which nothing here depends
// on; ZEXDOC is the yardstick (ea4-z8y).

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

extern "C" {
#include "z80.h"
}

namespace {

std::uint8_t memory[0x10000];

std::uint8_t ReadByte(void*, std::uint16_t addr) { return memory[addr]; }

void WriteByte(void*, std::uint16_t addr, std::uint8_t value) {
  memory[addr] = value;
}

std::uint8_t PortIn(z80*, std::uint16_t) { return 0xff; }

void PortOut(z80*, std::uint16_t, std::uint8_t) {}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: zex_test PATH\\zexdoc.com\n";
    return 2;
  }
  std::ifstream file(argv[1], std::ios::binary);
  const std::vector<char> program{std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>()};
  if (program.empty() || program.size() > 0x10000 - 0x100) {
    std::cerr << "FAIL cannot load " << argv[1] << "\n";
    return 1;
  }
  for (std::size_t i = 0; i < program.size(); ++i) {
    memory[0x100 + i] = static_cast<std::uint8_t>(program[i]);
  }
  // The programs take their stack from the BDOS address at 0006h.
  memory[0x0005] = 0xc9;  // RET, so a call to BDOS returns by itself
  memory[0x0006] = 0x00;
  memory[0x0007] = 0xf0;

  z80 cpu;
  z80_init(&cpu);
  cpu.read_byte = ReadByte;
  cpu.write_byte = WriteByte;
  cpu.port_in = PortIn;
  cpu.port_out = PortOut;
  cpu.pc = 0x100;

  std::string output;
  std::uint64_t instructions = 0;
  while (cpu.pc != 0x0000) {
    if (cpu.pc == 0x0005) {
      if (cpu.c == 2) {
        output += static_cast<char>(cpu.e);
        std::cout << static_cast<char>(cpu.e) << std::flush;
      } else if (cpu.c == 9) {
        for (std::uint16_t p = (cpu.d << 8) | cpu.e; memory[p] != '$'; ++p) {
          output += static_cast<char>(memory[p]);
          std::cout << static_cast<char>(memory[p]);
        }
        std::cout << std::flush;
      }
    }
    z80_step(&cpu);
    ++instructions;
  }

  const bool finished = output.find("Tests complete") != std::string::npos;
  const bool errors = output.find("ERROR") != std::string::npos;
  const bool passed = finished && !errors;
  std::cout << "\n"
            << (passed ? "PASS" : "FAIL") << " instructions=" << instructions
            << " finished=" << finished << " errors=" << errors << "\n";
  return passed ? 0 : 1;
}
