# Eureka A4 — stav práce

Reverzné inžinierstvo ROM počítača pre nevidiacich **Eureka A4** (Robotron
P/L, Austrália, 1989) a emulátor, ktorý ju spúšťa.

Tento dokument je nosič kontextu. Zachytáva, čo je zistené, čím je to
doložené, a čo je otvorené. Podrobná mapa hardvéru je v `hardware-map.md`.

---

## 1. Zdroj

Analyzovaný dump: `C:\b\a4rom.dmp`, 262144 B, MD5 `9aa101ab69fc367e114e1a84b08feea1`.

Do repozitára **nepatrí** (viď `ROM-NOTICE.txt`). Nástroje ho hľadajú cez
premennú prostredia `A4ROM`, inak na ceste vyššie.

Dump je **kompletný a neporušený** — všetkých päť modulov má platný 8-bitový
súčtový kontrolný bajt (súčet modulu = 0 mod 256). Overiteľné rutinou
v samotnej ROM na fyzickej `0x1DA90`.

---

## 2. Čo je Eureka A4

Procesor **Hitachi HD64180** (Z180). Bez obrazovky — výstupom je reč.
Kódovanie textu **Kamenický / KEYBCS2**. Táto ROM je česká lokalizácia,
preklad Rudolf Volejník; autori Milan Hudecek, Mike Thompson, Rick Collett.

Operačný systém **EurekaDOS** je klon CP/M 2.2 (v ROM sú doslovné BDOS
hlášky). Aplikácie v ROM: záznamník, hodiny a kalendár, kalkulátor,
komunikácia s modemom a videotexom, telefónny zoznam s vytáčaním, BASIC,
hudobný editor, textový procesor s formátom WordStar, diskové funkcie.
Zabudovaný voltmeter a teplomer (`DVM`, `TIC`, `TIF` v BASICu).

### Moduly ROM

| rozsah | obsah | build |
|---|---|---|
| `00000–09FFF` | program reči | Jan 31 13:42:49 1992 |
| `0A000–0CFFF` | BASIC | Dec 31 11:25:53 1990 |
| `0D000–17FFF` | užívateľské programy | Jul 22 14:33:16 1992 |
| `18000–1FFFF` | operačný systém | Jul 22 14:50:14 1992 |
| `20000–3FFFF` | dáta reči (8-bit PCM) | Dec 04 17:39:25 1990 |

---

## 3. Reč a zvuk

- Fonémový syntetizátor, 64 alofónov v tabuľke na `0x1FE0` (ARPABET-ovský
  štýl, s českými `RZ`, `DJ`, `TJ`, `NJ`, `ZH`).
- Pravidlá výslovnosti a slovník cudzích slov `0x2200–0x3200`.
- 128 KB dát reči je 8-bitové unsigned PCM, znelé úseky majú konštantnú
  periódu 41–45 vzoriek.
- Vstupný jazyk syntetizátora kóduje diakritiku prefixmi: `@` = čiarka,
  `[` = háčik.
- **Generátor tónov je čisto softvérový.** Obsluha prerušenia PRT0 na
  `0x0FB6D` sčíta až štyri fázové akumulátory zo 64-bajtovej tabuľky
  priebehu a vyhodí jeden bajt na DAC (port `88h`).
- Priebehy: 10 tabuliek po 64 B od `0x0677`, hodnota = bajt − `0x20`,
  rozsah −32..+31. `*0` píla, `*1` sínus, `*8` obdĺžnik, `*9` trojuholník.
- Vzorkovacia frekvencia je φ / (20 × (RLDR0+1)). **Nie je konštantná** —
  firmvér RLDR0 prepisuje za behu (13041-krát počas jedného prechodu
  aplikáciami). To je softvérová páka na rýchlosť reči vedľa potenciometra,
  ktorý mal reálny stroj.
- Príkazový jazyk zvuku: `<frekv>[:<frekv>…][/<trvanie>][*<priebeh>]`,
  parser na `0x1636`. `:` sú súčasné hlasy (max 4), CR oddeľuje príkazy
  vnútri reťazca, prázdny reťazec ukončuje melódiu. `&` sa rozvinie na
  `!%T`, `$` na `!%T77:78`.

Vyrenderované melódie z ROM sú v `audio/melodie/`, dáta reči ako WAV
v `audio/` (štyri vzorkovacie frekvencie, lebo presná nie je známa).

---

## 4. Braillove tabuľky

Dve 64-bajtové tabuľky na `0x1D7A0` (počítačový braill s číslicami
v spodnej časti bunky) a `0x1D7E0` (česká literárna sada s diakritikou).

Poradie bitov indexu: **bit0 = bod 3, bit1 = bod 2, bit2 = bod 1,
bit3 = bod 4, bit4 = bod 5, bit5 = bod 6** — teda poradie klávesov
perkinsovej klávesnice zľava doprava.

---

## 5. Emulátor

Natívny Windows, MSVC, `build.bat`. Jadro je superzazu z80 doplnené
o inštrukcie Z180 (`IN0`, `OUT0`, `MLT`, `TST`, `TSTIO`, `OTIM`/`OTDM`,
`SLP`).

### Overený stav

Emulátor **nabootuje, vysloví „inicializace eureky" a otvorí všetky
aplikácie z ROM**. Overené sondou (`tests/diag_probe.cpp`, režim `sweep`).

Mapa funkčných klávesov:

| kláves | aplikácia | kláves | aplikácia |
|---|---|---|---|
| F1 | záznamník | Shift+F1 | textový procesor |
| F3 | kalkulátor | Shift+F3 | teplomer |
| F4 | komunikácia | Shift+F4 | voltmeter |
| F5 | telefónny zoznam | Shift+F5 | databáza |
| F6 | BASIC | Shift+F6 | diskové funkcie |
| F7 | hudobný editor | Shift+F7 | spustiť program z disku |
| F8 | adresár disku | Shift+F8 | formátovať disk |

Prompt „ano nebo ne?" odpovedá na **`Y`**, nie na `a` — na `0x19FD1` je
`CP 59h` dvakrát po sebe, zjavne pozostatok z anglickej verzie.

### Opravené počas analýzy

1. **`BIT b,(HL)` robil zápis do pamäte.** `exec_opcode_cb` zapisoval
   operand späť bezpodmienečne. Na kremíku `BIT` žiadny zápisový cyklus
   nerobí. Doplnená podmienka `x_ != 1`.
2. **`OTIM`/`OTDM` posielal B na horný bajt adresy.** Na Z180 tam patrí
   nula; tento stroj horný bajt dekóduje (hodiny na `0190h`/`0290h`).
3. **Chýbal INTRQ disketovej radiča.** Bez neho každý diskový príkaz
   vypršal — viď sekciu 6.
4. **DMA kanál 1 sa neemuloval** (obsluha `DSTAT` testovala len bit 6).
5. **DMA obchádzalo ochranu pamäte** a mohlo natrvalo poškodiť obraz ROM,
   ktorý `Reset()` neobnovuje.

### Diagnostika

`--diag` zapne záznam: zahodené zápisy pod `kRamBase`, externé porty bez
modelu, interné registre Z180 bez správania, zmeny troch riadiacich
latchov po bitoch, kruhový záznam 512 udalostí. Výpis `Ctrl+Shift+D`
alebo pri ukončení. Voliteľné trasovanie konkrétnych portov aj vtedy, keď
ich model implementuje (`Diagnostics::set_trace`).

Aktuálny stav: **žiadne zahodené zápisy, žiadne externé porty bez modelu**
naprieč všetkými aplikáciami. `kRamBase = 0x70000` teda drží.

---

## 6. Otvorené otázky

### 6.1 Bit „slabá baterie" blokuje formátovanie

Formátovanie je najbližší cieľ, lebo je to **jediná cesta, ktorá spustí
DMA kanál 1 a príkaz Write Track** — obe sú v emulátore napísané, ale ani
raz neoverené.

Chybový bajt sa rozhoduje na `0x13D09`:

| bit | hláška |
|---|---|
| 0 | v jednotce není disk — **vyriešené**, bol to timeout, chýbal INTRQ |
| 1 | slabá baterie — **blokuje** |
| 2 | formátovací chyba |
| 6 | disk je chráněn proti zápisu (jediný, čo zodpovedá WD177x) |

Bajt prichádza reťazou `0x13CBE` → `0x199C7` → `0x19828`.
Overené a **vylúčené**: komparátory DAC v `ReadInputBuffer()` to nie sú —
zdvihnutie oboch prahov na `E0h` hlášku nezmenilo.

Postup: `diag_probe ROM disk trace 20000000 baterie` a pozrieť koniec
kruhového záznamu; prípadne rozšíriť trasované porty.

### 6.2 Periféria na CSI/O

Kód na `0x18801–0x188B0` a `0x1E001` používa Z180 CSI/O (CNTR `0Ah`,
TRDR `0Bh`) s bitmi 2 a 3 portu `B0h` ako ručne kývanými linkami. Vyšle
`FF`, prepne linky, čaká na odpoveď **`AA`** s timeoutom `0x6000`.
Zariadenie nepomenované. Kandidát: meracia periféria (`DVM`, `TIC`, `TIF`).

### 6.3 Nedotknuté bity latchov

Počas celého prechodu aplikáciami sa nikdy nezmenili: **`B0` bit 0**
(hustota diskety), **`A0` bit 2** (výber výstupného zariadenia).
`B0` bit 7 (RTS pre ASCI1) tiež nie — treba sériovú reláciu.

### 6.4 Zápis `ADh` na port `88h`

Deje sa na `0x19A01` pred **každým** príkazom radiča. Rutina hneď za ním
(`0x19A3E`) je kontrola BUSY s Force Interrupt, takže s batériou to
nesúvisí. Čo ten zápis ovláda, otvorené.

### 6.5 RTC — mapovanie registrov

Emulátor má v `ReadRtc()` prehodené **hodiny so sekundami** a **mesiac
s dňom**. Správne poradie od `case 0`: stotiny, sekundy, minúty, hodiny,
deň, mesiac, rok. Dôkaz je v `hardware-map.md`. Hodnoty sú binárne, nie
BCD. Emulátor navyše číta hostiteľský čas pri každom registri zvlášť,
takže dávkové čítanie môže preskočiť sekundu.

### 6.6 Adresný priestor

`kPhysicalSize = 1<<20` a maska `0xfffff`. Najvyššia priama hodnota, akú
ROM vloží do CBR alebo BBR, je `0x70`, čo s common area 1 dá `0x7FFFF` —
presne strop 19-bitového HD64180. Odporúčam masku `0x7ffff`.

### 6.7 Kvalita zvuku

`RenderAudio()` je zero-order hold bez filtrácie; DAC beží okolo 11 kHz,
výstup 48 kHz, takže všetko nad ~5,7 kHz sa zrkadlí ako aliasing.
Ignoruje sa aj bit povolenia zvuku (`80h` bit 7). Latencia až 240 ms.
Pre nevidiaceho používateľa je zvuk celé rozhranie, takže toto je
najväčší nevyužitý priestor.

---

## 7. Nástroje

V `tools/`, čistý Python 3, bez závislostí. ROM sa berie z `$A4ROM`.

| nástroj | čo robí |
|---|---|
| `z180dis.py` | disassembler Z80/Z180 (knižnica) |
| `z180len.py` | dĺžky inštrukcií (knižnica) |
| `dis.py LO:HI …` | disassembluje rozsahy fyzických adries |
| `strings_kam.py MIN LO HI` | reťazce s dekódovaním Kamenických |
| `rommap.py` | entropia a zloženie ROM po 4 KB blokoch |
| `ports.py` | mapa I/O portov lineárnym rozmetaním inštrukcií |
| `latches.py` | všetky odkazy na tiene latchov + súhrn po bitoch |
| `melodies.py` | vyrenderuje melódie z ROM do `audio/melodie/` |

Príklad:

```
set A4ROM=C:\b\a4rom.dmp
python tools\dis.py 19837:19880
python tools\strings_kam.py 8 0xd000 0xe000
```

---

## 8. Ako pokračovať

Poradie podľa pomeru prínos/námaha:

1. **Zvuk** (6.7) — najväčší dopad na použiteľnosť, žiadne neznáme.
2. **RTC** (6.5) — dve prehodenia a snímka času, všetko doložené.
3. **Batéria** (6.1) — odblokuje formátovanie a tým overenie DMA kanála 1.
4. **Adresný priestor** (6.6) — jednoriadková zmena, ale mení správanie.
5. **CSI/O** (6.2) — najviac neznáma, najmenej naliehavá.
