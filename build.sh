#!/bin/sh
# Zostavenie emulátora krížovým prekladačom mingw-w64 na Linuxe.
# Náprotivok súboru build.bat, ktorý robí to isté z cmd.exe pod msys2.
#
# Prekladá sa tým istým Makefile a tými istými prepínačmi; mení sa len to,
# ktoré binárky prekladača sa zavolajú. Na Ubuntu sú to balíky
# g++-mingw-w64-x86-64 a binutils-mingw-w64-x86-64.
#
# Vlastný prefix sa dá podstrčiť premennou TOOLPREFIX. Má zmysel pri
# vláknach: predvolený x86_64-w64-mingw32-g++ je variant s modelom vlákien
# win32, a keby sa na ňom emulátor správal inak, sú vedľa neho varianty
# ...-g++-posix a ...-gcc-posix. Linkuje sa staticky, takže výmena runtime
# na hotovom EXE nič nepýta.

set -eu

cd "$(dirname "$0")"

TOOLPREFIX="${TOOLPREFIX:-x86_64-w64-mingw32-}"

for tool in g++ gcc windres; do
  if ! command -v "${TOOLPREFIX}${tool}" >/dev/null 2>&1; then
    echo "Nenašiel som ${TOOLPREFIX}${tool}."
    echo "Na Ubuntu ho dodajú balíky g++-mingw-w64-x86-64 a binutils-mingw-w64-x86-64."
    echo "Iný prefix prekladača sa zadá premennou TOOLPREFIX."
    exit 1
  fi
done

# Prekladové jednotky sú na sebe nezávislé, takže sa prekladajú naraz.
# Vlastný počet sa dá vnútiť premennou JOBS.
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# CXX, CC a RC sa podávajú ako priradenia na príkazovom riadku, lebo tie
# v make prebijú aj `:=` v Makefile. Vďaka tomu nie je v Makefile jediná
# zmena a preklad z msys2 zostáva taký, aký bol; premenná MINGW64 sa tu
# jednoducho nepoužije.
make -j"$JOBS" \
  CXX="${TOOLPREFIX}g++" \
  CC="${TOOLPREFIX}gcc" \
  RC="${TOOLPREFIX}windres" \
  all

echo "Vytvorené: bin/EurekaA4Emulator.exe"
