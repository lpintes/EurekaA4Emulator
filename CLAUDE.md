# Pokyny pre agentov

## Najprv si prečítaj HANDOFF.md

`HANDOFF.md` je nosič kontextu tohto projektu: čo je zistené, čím je to
doložené, čo je otvorené a v akom poradí pokračovať. Bez neho budeš
znovu odvodzovať veci, ktoré sú už overené. Mapa hardvéru je
v `hardware-map.md`.

## Máme oficiálny manuál — pozri doň skôr, než začneš odvodzovať

V `eurekatech/` je **Eureka A4 Technical Manual** od Robotronu aj
s vývojárskou diskétou. Nie je to doplnok, je to **prameň**: príloha H
(`TECHMAN1/IOPORT.H` a `IOPORT.LIB`) je úplná mapa externých I/O portov
s názvami signálov a bitovými maskami, `DEVICES.10` má volania
zariadení, `SYSRAM.A` pomenované premenné, `MEMMAP.E` mapy pamäte
a `FILE-FMT.D` formáty súborov.

Dvakrát sa už stalo, že odvodenie zo samotnej ROM vyzeralo doložene
a bolo nesprávne — vždy tak, že si **susednosť pomýlilo s príčinnosťou**
(prehliadnutý swap registrov, zápisy do DAC vedľa nesúvisiaceho bitu).
Manuál to oba razy rozhodol za pár minút. Keď narazíš na port, bit alebo
volanie, hľadaj najprv tam.

Pozor: obsah `eurekatech/` je materiál tretích strán (Robotron
a Borland) a MIT licencia projektu sa naň nevzťahuje. Viď
`eurekatech/PUVOD.md`.

## O používateľovi

Používateľ je **nevidiaci**, pracuje s čítačom obrazovky (NVDA), hovorí
po slovensky. Z toho plynie:

- Odpovedaj po slovensky.
- Formátuj pre čítač obrazovky: nadpisy a zoznamy áno, ASCII grafika a
  zarovnávanie medzerami nie. Tabuľky používaj striedmo.
- Neodkazuj sa na to, „čo vidno na obrazovke". Výstup nástrojov, ktorý je
  dôležitý, zopakuj v texte.
- Pri tomto projekte je zvuk celé používateľské rozhranie emulovaného
  stroja, nie doplnok. Zmeny, ktoré zhoršia zvuk, sú vážne.

## ROM

Dump je **mimo repozitára** na `C:\b\a4rom.dmp` (262144 B, MD5
`9aa101ab69fc367e114e1a84b08feea1`). Je to firmvér — nikdy ho
necommituj a nikam ho neposielaj. `.gitignore` vylučuje `*.dmp` aj celý
`audio/`, lebo tie WAV sú prevedený obsah ROM.

Nástroje ho hľadajú cez premennú prostredia `A4ROM`.

## Adresy

Adresy sú v celom projekte **fyzické**, teda offsety v dumpe, pokiaľ nie
je výslovne napísané inak. Logická adresa závisí od stavu MMU; pri
common area 1 s CBR=`0Bh` platí fyzická = logická + `B000h`.

## Zostavenie

Prekladá sa **mingw64 z msys2**, ktorý je na stroji v
`C:\msys64\mingw64\bin`. Iný prefix sa dá podstrčiť premennou
`MINGW64`. Visual Studio už netreba.

Emulátor: `build.bat` z ľubovoľného príkazového riadka — cestu k mingw
si predradí sám a na globálny PATH sa nespolieha. Hotové EXE ide do
`bin\`, medzivýstupy do `build\`; oba sú v `.gitignore`.

Dve veci, bez ktorých sa emulátor nezlinkuje: `-municode`, lebo vstupný
bod je `wmain`, a `-static -static-libgcc -static-libstdc++`, inak EXE
pýta mingw DLL a mimo msys2 shellu sa nespustí.

`z80.c` sa prekladá ako C, nie C++; `build.bat` to už rieši.

Sonda a testy: `build-tests.bat`. Zostaví `bin\diag_probe.exe`,
`bin\integration_test.exe` a `bin\codec_test.exe`, a linkuje ich proti
objektom z `build\`. `build.bat` si volá **vždy**, takže objekty nikdy
nie sú staršie než zdrojáky. Kým tam bola podmienka na ich existenciu,
testy sa dali zlinkovať proti kódu, ktorý sa nepreložil, a meranie
ukazovalo správanie, ktoré už v zdrojáku nebolo.

Pozor: `codec_test` má obyčajný `main`, takže sa prekladá **bez**
`-municode`; s ním linker spadne na chýbajúcom `wWinMain`.

## Diagnostická sonda

`tests/diag_probe.cpp` je hlavný vyšetrovací nástroj. Nabootuje ROM
a vypíše, čo model hardvéru neobsluhuje.

```
diag_probe ROM DISK_FOLDER boot
diag_probe ROM DISK_FOLDER sweep 4000000
diag_probe ROM DISK_FOLDER seq 15000000 kD7 Y Y
diag_probe ROM DISK_FOLDER trace 20000000 formatovaci
```

Pred hádaním, čo firmvér robí, ho radšej spusti a pozri sa. Takto sa
našla chyba `BIT b,(HL)` v jadre aj to, kde presne viazlo formátovanie.

Sondu si pokojne dopĺňaj o dočasnú inštrumentáciu (počítadlá, záznam
udalostí, histogram PC) v kópii zdrojáka v scratchpade — repozitár tým
nešpiň. Takto sa odhalilo, že firmvér povolí DMA skôr, než vydá príkaz
radiču.

## Štýl kódu

Emulátor je C++20, dvojmedzerové odsadenie, `PascalCase` metódy, členy
s podtržníkom na konci. **Komentáre v zdrojákoch po anglicky**,
dokumentácia a používateľské reťazce po slovensky alebo česky.

Komentuj *prečo*, nie *čo*. Pri hardvérových predpokladoch pripíš
adresu v ROM, ktorá ich dokladá — inak ich nikto neskôr neoverí.

## Git

Commituj len keď o to používateľ požiada. Nepushuj bez vyzvania.
