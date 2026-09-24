#!/bin/sh
# Zostavenie testov a sondy krížovým prekladačom mingw-w64 na Linuxe.
# Náprotivok súboru build-tests.bat.
#
# Cieľ `tests` obsahuje aj emulátor, aby jedno spustenie dalo hotový celý
# strom — rovnako ako to robí dávka.
#
# Zastaraný objekt tu nemôže vzniknúť: o závislostiach na hlavičkách vie
# make zo súborov .d, ktoré vypisuje gcc -MMD. Žiadna podmienka typu „ak už
# existuje, preskoč" sem preto nepatrí.

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

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

make -j"$JOBS" \
  CXX="${TOOLPREFIX}g++" \
  CC="${TOOLPREFIX}gcc" \
  RC="${TOOLPREFIX}windres" \
  tests

echo "Vytvorené: bin/codec_test.exe, bin/disk_test.exe, bin/settings_test.exe, bin/zex_test.exe, bin/diag_probe.exe, bin/integration_test.exe"
