#!/bin/sh
# Zostaví testy a pustí všetkých osemnásť naraz. Náprotivok run-tests.bat.
#
# Volanie:  ./run-tests.sh                (ROM z premennej A4ROM)
#           ./run-tests.sh /cesta/k/a4rom.dmp
#
# Oproti dávke tu nie je záložná cesta k ROM ani k manuálu: tento súbor ide
# do zverejneného stromu a cesta z jedného stroja by v ňom nemala čo robiť.
# Zadajte ich premennými A4ROM a EUREKATECH.
#
# Beží to len vo WSL a je to zámer: preložené programy sú EXE pre Windows
# a spúšťa ich interop. Bez neho by sa dali zostaviť, ale nie odmerať.

set -eu

cd "$(dirname "$0")"

# Cesty sa smú zadávať oboma spôsobmi -- linuxovo (`/mnt/d/eureka`) aj
# windowsovo (`D:\eureka` alebo `D:/eureka`). Normalizujú sa hneď na
# linuxovú podobu, lebo v skripte sa na ne pozerá shell; windowsová sa z nej
# vyrobí až tam, kde ju dostane EXE.
#
# Rozhoduje sa podľa TVARU cesty, nie pokusom o prevod, a to preto, že
# `wslpath` sa nedá volať naslepo ani v jednom smere:
#   - `wslpath -u` na linuxovej ceste skončí s kódom 1 a vypíše ju ako chybu;
#   - `wslpath -w` na ceste, ktorá už je windowsová, vráti **ticho nezmysel**:
#     z `D:\eureka\A4ROM.DMP` spraví `DeurekaA4ROM.DMP` a skončí s kódom 0.
# To druhé je horšie: existenčná kontrola prejde, do testov sa dostane
# neexistujúca cesta a zlyhá každý režim, ktorý potrebuje ROM. Presne takto
# to spadlo 24. 9. 2026 -- tri PASS a dvanásť zlyhaní, ktoré vyzerali ako
# chyba emulátora.
to_unix() {
  case "$1" in
    [A-Za-z]:[\\/]*) wslpath -u "$1" ;;
    *) printf '%s' "$1" ;;
  esac
}

if [ $# -ge 1 ] && [ -n "$1" ]; then
  A4ROM="$1"
fi
A4ROM="${A4ROM:-}"
if [ -n "$A4ROM" ]; then
  A4ROM="$(to_unix "$A4ROM")"
fi
if [ -z "$A4ROM" ] || [ ! -f "$A4ROM" ]; then
  echo "Nenašiel som ROM: ${A4ROM:-(nezadaná)}"
  echo "Zadajte ju argumentom alebo premennou A4ROM."
  exit 1
fi

# Dve veci, ktoré sa tu zistili meraním 21. 9. 2026 a bez ktorých testy
# nezmerajú nič:
#
# 1. Hotové EXE sú programy pre Windows, takže cesta v argumente musí byť
#    windowsová. Linuxovú /mnt/d/... ROM neotvoria.
# 2. Cestu \\wsl.localhost\... odmietne VirtualDisk::Mount -- padne na nej
#    fs::weakly_canonical (src/virtual_disk.cpp). Priečinok diskety preto
#    NEMÔŽE byť build/testdisk v repozitári, keď repozitár leží v linuxovom
#    súborovom systéme. Musí byť na skutočnom windowsovom disku.
#    Pracovný priečinok procesu byť UNC môže, na tom nezáleží.
if ! command -v wslpath >/dev/null 2>&1; then
  echo "Nenašiel som wslpath, takže toto nie je WSL."
  echo "Testy spúšťajú EXE pre Windows a bez interopu sa nedajú odmerať."
  echo "Zostaviť sa dajú aj tak: ./build-tests.sh"
  exit 1
fi

# Lomky dopredu, nie späť, a je to to isté, čo robí run-tests.bat premennou
# `%A4ROM:\=/%`. Nutné to už nie je -- `ROM` a `DISK` sú v recepte Makefile
# v úvodzovkách, takže by prešla aj cesta so spätnými lomkami. Zostáva to
# preto, že obe dávky majú robiť to isté a rozdiel medzi nimi by sa dal
# čítať ako rozdiel v tom, čo sa meria. Windows lomku dopredu prijíma.
ROM_WIN="$(wslpath -w "$A4ROM" | tr '\\' '/')"

./build-tests.sh

# Vlastný priečinok diskety, čerstvý pri každom spustení. Skutočný disk sa
# na to použiť nedá, mení sa pod rukami -- a testy doňho zapisujú.
if [ -n "${EA4_TESTDISK:-}" ]; then
  TESTDISK="$(to_unix "$EA4_TESTDISK")"
  mkdir -p "$TESTDISK"
  TESTDISK_WIN="$(wslpath -w "$TESTDISK")"
else
  # %TEMP% sa pýta samotný Windows. Pracovný priečinok pritom musí byť na
  # windowsovom disku: cmd.exe spustené s UNC cestou vypíše najprv hlášku
  # "UNC paths are not supported" a tá by skončila v premennej ako časť
  # cesty. Preto to `cd /mnt/c` a preto sa berie posledný riadok.
  WINTMP="$(cd /mnt/c 2>/dev/null && cmd.exe /c "echo %TEMP%" 2>/dev/null | tr -d '\r' | tail -n 1)"
  TESTDISK_WIN="${WINTMP}\\ea4-testdisk"
fi

# Až sem sa nič nemazalo. Nižšie je rm -rf, takže cesta musí byť najprv
# overená: keby sa do nej dostala hláška alebo prázdny reťazec, mazalo by sa
# niečo iné, než sa myslí -- a ticho.
case "$TESTDISK_WIN" in
  [A-Za-z]:\\?*) ;;
  *)
    # printf, nie echo: `echo` v dash vyklada `\b` a `\t`, takže by windowsovú
    # cestu v hláške rozsypal práve tam, kde má byť vidieť, čo je na nej zle.
    printf 'Priečinok diskety nevyšiel ako cesta na windowsovom disku: %s\n' "$TESTDISK_WIN"
    echo "Zadajte ho premennou EA4_TESTDISK (napríklad /mnt/d/temp/ea4disk)."
    exit 1
    ;;
esac

TESTDISK="$(wslpath -u "$TESTDISK_WIN")"
# Do make ide cesta s lomkami dopredu, z rovnakého dôvodu ako pri ROM vyššie.
TESTDISK_MAKE="$(printf '%s' "$TESTDISK_WIN" | tr '\\' '/')"
rm -rf "$TESTDISK"
mkdir -p "$TESTDISK"

# Pravý READ.COM z vývojárskej diskety potrebujú DVA režimy: `com` ho spustí
# a `wp` ním overuje, že z chránenej diskety sa dá čítať. Keď manuál nie je
# po ruke, oba sa vynechajú cez SKIP_MODES -- zlyhanie by ukazovalo na
# emulátor, hoci chýba len súbor, ktorý sa nedistribuuje.
#
# SKIP_MODES ide do make PROSTREDÍM, nie argumentom: sú to dve slová a make
# by to druhé vzal ako ďalší cieľ.
EUREKATECH="${EUREKATECH:-}"
if [ -n "$EUREKATECH" ]; then
  EUREKATECH="$(to_unix "$EUREKATECH")"
fi
SKIP_MODES=""
if [ -n "$EUREKATECH" ] && [ -f "$EUREKATECH/TECHMAN1/READ.COM" ]; then
  cp "$EUREKATECH/TECHMAN1/READ.COM" "$TESTDISK/READ.COM"
else
  echo
  echo "Nenašiel som \$EUREKATECH/TECHMAN1/READ.COM, preskakujem com a wp."
  echo "Je to súbor z Technical Manuálu, ktorý leží mimo repozitára."
  echo "Cestu k priečinku zadajte premennou EUREKATECH."
  echo "Testov bude šestnásť a nie je to regresia."
  SKIP_MODES="com wp"
fi
export SKIP_MODES

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
TOOLPREFIX="${TOOLPREFIX:-x86_64-w64-mingw32-}"

# -k dobehne aj po prvom zlyhaní, nech je vidieť všetky naraz.
# --output-sync=target drží výpis každého testu pohromade; bez neho sa
# riadky sedemnástich procesov premiešajú a výsledok sa nedá prečítať.
echo
echo "=== Testy ==="
if make -j"$JOBS" -k --output-sync=target \
     CXX="${TOOLPREFIX}g++" \
     CC="${TOOLPREFIX}gcc" \
     RC="${TOOLPREFIX}windres" \
     ROM="$ROM_WIN" \
     DISK="$TESTDISK_MAKE" \
     check; then
  echo
  echo "VÝSLEDOK: všetky testy prešli."
else
  echo
  echo "VÝSLEDOK: niektoré testy ZLYHALI, viď riadky vyššie."
  exit 1
fi
