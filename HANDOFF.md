# Eureka A4 — stav práce

Reverzné inžinierstvo ROM počítača pre nevidiacich **Eureka A4** (Robotron
P/L, Austrália, 1989) a emulátor, ktorý ju spúšťa.

Tento dokument je nosič kontextu. Zachytáva, čo je zistené, čím je to
doložené, a čo je otvorené. Podrobná mapa hardvéru je v `hardware-map.md`.

---

## 1. Zdroj

### Oficiálna dokumentácia (od 24. 8. 2026)

V `eurekatech/` je **Eureka A4 Technical Manual** od Robotronu aj
s vývojárskou diskétou. `TECHMAN1/` má 14 kapitol, 9 príloh a hlavne
zdrojové knižnice s pomenovanými adresami a bitovými maskami
(`IOPORT.LIB`, `SYSEQU.LIB`, `SYSJUMPS.LIB`, `DEVICE.LIB`, `KB.LIB`…).
`TECHMAN2/` je len Borlandov tutoriál k Turbo Pascalu, pre tento projekt
bez hodnoty.

Čo je kde:

- `IOPORT.H` + `IOPORT.LIB` — úplná mapa externých I/O portov
- `DEVICES.10` — volania zariadení vrátane chybových bitov diskety
- `SYSRAM.A` — pomenované premenné SYSRAM
- `MEMMAP.E` — fyzické mapy pamäte piatich variantov stroja
- `FILE-FMT.D` — formáty `.TEL`, `.DIA`, `.MEL`, `.DAT`, `.BAS` a textového procesora
- `PHONEMES.B` — zoznam fonémov, sedí s tabuľkou 64 alofónov na `0x1FE0`
- `READ.COM`, `TP.COM`, `TPS.COM` — dobové binárky, testovacia záťaž pre EurekaDOS

**Pred hádaním sa pozri sem.** Manuál uzavrel štyri z piatich otvorených
otázok a ukázal, že dve „zistenia" odvodené z ROM boli nesprávne.

### ROM

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
  aplikáciami). Manuál to potvrdzuje: Timer 0 obsluhuje reč **asi
  7,5 kHz** a rýchlosť kolíše podľa výšky hlasu a typu alofónu. Zvukové
  efekty bežia na dvojnásobku zvolenej frekvencie.
- Ten potenciometer nie je len hardvérový: `vmsel_mask` (`B0h` bit 6)
  v nule pripája na komparátor `vm1` práve posuvník rýchlosti reči,
  takže firmvér si jeho polohu vie prečítať.
- Heartbeat je Timer 1 na **75 Hz**. Bez neho stroj nezaznamená stlačenie
  klávesu ani neozve echo. Zapisuje aj do `power_latch` a `output_latch`,
  preto sa ich tiene v RAM musia aktualizovať *pred* zápisom na port.
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
3. **DMA kanál 1 sa neemuloval** (obsluha `DSTAT` testovala len bit 6).
4. **DMA obchádzalo ochranu pamäte** a mohlo natrvalo poškodiť obraz ROM,
   ktorý `Reset()` neobnovuje.

### Opravené po získaní manuálu

Formátovanie diskety **prejde celé** a stroj ohlási „formátování
skončeno". Tým sú prvýkrát naozaj vykonané DMA kanál 1 aj príkaz Write
Track — kód, ktorý bol napísaný, ale nikdy nespustený. Vyžiadalo si to
šesť opráv, každá odhalená až tou predchádzajúcou:

1. **INTRQ radiča bol na porte `A8h` bit 1.** Tam ale patrí komparátor
   batérie a nastavený bit znamená *vybitú batériu*. Preto každý diskový
   príkaz končil hláškou „slabá baterie" — a preto zdvihnutie prahov na
   `E0h` nič nezmenilo, lebo o výsledku rozhodoval `fdcIntrq_`, nie prah.
   Zrušené; INTRQ sa na `A8h` nedá čítať.
2. **Type I príkazy nehlásili INDEX.** Slučka na 19828 pýta po seeku bit 1
   *stavového registra radiča* (98h), nie portu `A8h`. Doplnené
   `TypeOneStatus()`: INDEX vždy (priečinok je vždy vložený a točiaci sa
   disk), TRACK 00 podľa stopy.
3. **DMA sa spúšťala pri zápise do DSTAT.** Firmvér ale povolí DMA
   (19ED1) **skôr**, než vydá Write Track (19EE8), takže celá stopa
   odtiekla do radiča, ktorý ešte neprenášal, a BUSY nikdy nezhaslo.
   Kanál sa teraz „zaparkuje" a rozbehne až keď radič dáta naozaj chce.
4. **Zaparkovanie nerozlišovalo smer.** Nevyprázdnený buffer po
   overovacom čítaní vyzeral ako radič čakajúci na dáta, takže druhý
   Write Track opäť prišiel o obsah. `FdcWantsDma()` teraz porovnáva smer
   prenosu z DCNTL s tým, či radič zapisuje.
5. **Čítacie príkazy držali BUSY, kým buffer niekto nevyprázdnil.**
   Skutočný WD177x sektor dočíta aj bez obsluhy DRQ — nastaví Lost Data,
   ale BUSY zhasne. Firmvér po formáte overuje stopu bez DMA, takže na
   starom modeli visel na 19EF3. Čítania teraz končia stavom DRQ bez BUSY.
6. **Krokovacie príkazy (`20h`–`70h`) sa neemulovali.** Formát chodí po
   disku Step In (`50h`), nie seekom, takže sa všetkých 160 stôp
   zapisovalo na stopu 0. Doplnené vrátane príznaku U a smeru kroku.

Naviac: overovacie čítanie práve naformátovanej stopy uspeje aj mimo
obrazu 800 KB. Firmvér zapíše **162 logických stôp** (0–161, teda
o cylinder viac, než má dátová oblasť) a každú si overí; skutočná
mechanika sa na cylinder 80 posunie a práve zapísanú stopu prečíta.

Zmerané po oprave: 162 zápisov stopy, 1620 overovacích čítaní, 81 krokov,
žiadna stopa zapísaná dvakrát.

Ďalej opravené podľa manuálu:

- **RTC dostal zapuzdrenie.** Čítanie registra `90h` zapuzdrí ostatné,
  takže dávkové čítanie nepreskočí sekundu. Poradie registrov zostalo —
  bolo správne, viď `hardware-map.md`.
- **Adresný priestor zúžený na 19 bitov** (`kPhysicalMask = 0x7ffff`).

### Diagnostika

`--diag` zapne záznam: zahodené zápisy pod `kRamBase`, externé porty bez
modelu, interné registre Z180 bez správania, zmeny troch riadiacich
latchov po bitoch, kruhový záznam 512 udalostí. Výpis `Ctrl+Shift+D`
alebo pri ukončení. Voliteľné trasovanie konkrétnych portov aj vtedy, keď
ich model implementuje (`Diagnostics::set_trace`).

Aktuálny stav: **žiadne externé porty bez modelu** naprieč všetkými
aplikáciami. Zahodené zápisy sú jediné tri, na `1C1FA`–`1C1FC` z adresára
disku (viď 6.2); `kRamBase = 0x70000` teda drží.

Pozor pri hodnotení: `boot` tie zápisy neukáže, lebo sa k nim nedostane.
Treba `sweep`, prípadne `seq kC7`.

---

## 6. Otvorené otázky

Pôvodných päť otázok manuál a následné meranie uzavreli. Zostáva iné.

### 6.1 Zvuk — najväčší nevyužitý priestor

`RenderAudio()` je zero-order hold bez filtrácie, výstup 48 kHz, takže
všetko nad polovicou vzorkovacej frekvencie DAC sa zrkadlí ako aliasing.
Ignoruje sa bit povolenia zvuku (`80h` bit 7) aj `filtersel_mask`
(`B0h` bit 4), ktorým rečový engine prepína medznú frekvenciu filtra.
Latencia až 240 ms.

Teraz je známa aj cieľová frekvencia: **Timer 0 obsluhuje reč asi
7,5 kHz** a kolíše podľa výšky hlasu a typu alofónu (kapitola 12).
Zvukové efekty bežia na dvojnásobku zvolenej frekvencie (kapitola 14).
Predchádzajúci odhad ~11 kHz bol vysoko. WAV-y v `audio/` sa dajú
prerenderovať na správnu frekvenciu namiesto štyroch hádaných.

Pre nevidiaceho používateľa je zvuk celé rozhranie, takže toto je
najnaliehavejšia položka.

### 6.2 Zahodené zápisy na 1C1FA–1C1FC

Adresár disku (`C7`) trikrát zapíše na logickú `C1FA` s BBR=`10h`, čo
padne do ROM na fyzickú `1C1FA`. Zahodenie je pravdepodobne správne
(na kremíku sa zápis do ROM tiež stratí), ale stojí za overenie, či
model BBR v tej chvíli sedí.

**Nie je to regresia** — overené zostavením verzie z `HEAD` v gite.
Tvrdenie „žiadne zahodené zápisy naprieč všetkými aplikáciami"
v predchádzajúcej verzii tohto dokumentu bolo nepresné; sonda to hlási
len pri `sweep` a `seq kC7`, nie pri `boot`.

### 6.3 Port A8h bit 5

Manuál hovorí, že je to `dcd0_mask`, detekcia nosnej z modemu. Táto ROM
ho ale pollne aj v ceste tlačového výstupu (18F99, 18FB1, 19097). Buď je
česká verzia zapojená inak, alebo je odvodenie „pripravenosť tlačiarne"
nepresné. Bez sériovej relácie sa to nerozhodne.

### 6.4 Bity latchov, ktoré sa nikdy nezmenili

Počas prechodu aplikáciami zostali nedotknuté `B0` bit 0 (`fdc_side`)
a `B0` bit 7 (`rts1_mask`). Prvý sa zmení pri diskovej práci s druhou
stranou, druhý potrebuje sériovú reláciu. `A0` bit 2 (`pol_relay`) sa
zmení až pri práci s telefónnou linkou.

### 6.5 Formátovanie nemaže hostiteľský priečinok

Emulátor formátovanie dokončí a ohlási úspech, ale obsah stopy zahodí:
virtuálny disk je hostiteľský priečinok a mazať používateľove súbory
preto, že emulovaný stroj formátoval, nie je rozhodnutie tohto modelu.
Ak by sa to niekedy malo zmeniť, musí to byť vedomé a s potvrdením.

### 6.6 Uzavreté otázky

| bývalá otázka | výsledok |
|---|---|
| „slabá baterie" blokuje formátovanie | vyriešené, viď sekcia 5 |
| periféria na CSI/O | je to klávesnica IBM PC (`FFh` = reset, `AAh` = BAT passed) |
| nedotknuté bity latchov | pomenované z `IOPORT.LIB`, tri pôvodné výklady boli chybné |
| zápis `ADh` na port `88h` | prah komparátora pre stráženie batérie počas diskovej operácie |
| RTC — mapovanie registrov | emulátor bol **správne**; navrhovaná oprava by hodiny rozbila |
| adresný priestor | maska zmenená na `0x7ffff`, 19 bitov HD64180 |

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

1. **Zvuk** (6.1) — najväčší dopad na použiteľnosť a teraz aj bez
   neznámych: cieľová frekvencia DAC je asi 7,5 kHz, filter sa prepína
   `B0h` bitom 4, povolenie zvuku je `80h` bit 7.
2. **Prerenderovať `audio/`** na správnu frekvenciu namiesto štyroch
   hádaných (`tools/melodies.py` a export dát reči).
3. **Formáty súborov z `FILE-FMT.D`** — telefónny zoznam, diár, melódie,
   databáza a texty sa dajú konvertovať do a z hostiteľských formátov.
   Doteraz to nešlo, lebo formáty neboli známe.
4. **Zahodené zápisy na 1C1FA** (6.2) — krátke, ale treba disassemblovať
   cestu adresára disku.
5. **Sériová relácia** — odblokuje `B0h` bit 7, `A8h` bity 2 a 5 a
   rozhodne otázku 6.3.

### Čím sa dá testovať

Manuálová disketa priniesla dobové binárky. `tests/integration_test.cpp`
v režime `com` očakáva `READ.COM` v priečinku disku — je
v `eurekatech/TECHMAN1/READ.COM`. Obidva integračné testy prechádzajú:

```
integration_test ROM DISK_FOLDER bas   -> PASS
integration_test ROM DISK_FOLDER com   -> PASS (196 BIOS čítaní)
```

`TP.COM` a `TPS.COM` (Turbo Pascal) sú zatiaľ nevyskúšané a boli by
podstatne tvrdším testom EurekaDOS.
