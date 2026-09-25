# Eureka A4 — stav práce

Reverzné inžinierstvo ROM počítača pre nevidiacich **Eureka A4** (Robotron
P/L, Austrália, 1989) a emulátor, ktorý ju spúšťa.

Tento dokument je nosič kontextu. Zachytáva, čo je zistené, čím je to
doložené, a čo je otvorené. Podrobná mapa hardvéru je v `hardware-map.md`.

**Od 7. 9. 2026 platí jedna zmena v tom, ako sa tento dokument číta.**
Otvorená práca sa eviduje v **beads** (`bd`, prefix `ea4`): `bd ready`
povie, čo sa dá robiť, `bd list --priority 1`, čo horí, a obsah je aj
v `.beads/issues.jsonl`, čo je commitovaný text čitateľný bez `bd`.
Tento dokument zostáva **doložením** — prečo je niečo pravda a čím je to
zmerané. Bead je naň ukazovateľ, nie jeho kópia, a čísla `6.x` sa
nemenia; odkazuje sa na ne zo zdrojákov.

Prakticky to znamená, že sekcia 6 aj poradie v sekcii 8 sa ďalej čítajú
ako rozbor, nie ako pracovný zoznam. Ten je v `bd`. Keď sa niečo z toho
uzavrie, zavrieť patrí **oboje** — bead aj odsek tu.

---

## 1. Zdroj

### Oficiálna dokumentácia (od 24. 8. 2026)

V `eurekatech/` je **Eureka A4 Technical Manual** od Robotronu aj
s vývojárskou diskétou. `TECHMAN1/` má 14 kapitol, 9 príloh a hlavne
zdrojové knižnice s pomenovanými adresami a bitovými maskami
(`IOPORT.LIB`, `SYSEQU.LIB`, `SYSJUMPS.LIB`, `DEVICE.LIB`, `KB.LIB`…).
`TECHMAN2/` je len Borlandov tutoriál k Turbo Pascalu, pre tento projekt
bez hodnoty.

**Zmenené 20. 9. 2026 (`ea4-fo1`): priečinok už nie je v repozitári.**
O obsahu platí všetko vyššie aj nižšie, len leží mimo stromu — je to
materiál Robotronu a Borlandu a projekt sa má dať zverejniť. Cesta je
v premennej `EUREKATECH`, inak `C:\b\eurekatech`; odkazy tvaru
`eurekatech/TECHMAN1/…` čítaj ako relatívne voči nej.

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
  priebehu a vyhodí jeden bajt na DAC (port `88h`). Za behu sa vykonáva
  z kópie v RAM (`7B01D`–`7B051`) a pri hraní melódie beží **11 378-krát
  za sekundu** (RLDR0 = `26`); zmerané histogramom PC, viď 6.10.
- Priebehy: 10 tabuliek po 64 B od `0x0677`, hodnota = bajt − `0x20`,
  rozsah −32..+31. `*0` píla, `*1` sínus, `*8` obdĺžnik, `*9` trojuholník.
- Vzorkovacia frekvencia je φ / (20 × (RLDR0+1)). **Nie je konštantná** —
  firmvér RLDR0 prepisuje za behu (13041-krát počas jedného prechodu
  aplikáciami). Manuál to potvrdzuje: Timer 0 obsluhuje reč **asi
  7,5 kHz** a rýchlosť kolíše podľa výšky hlasu a typu alofónu. Zvukové
  efekty bežia na dvojnásobku zvolenej frekvencie.
  **Doplnené 14. 9. 2026:** veľkú časť tých prepisov robí pravdepodobne
  posuvník rýchlosti, ktorý emulátor držal na pevných `80h` — presne na
  úrovni ticha, kde firmvér reštartuje časovač každú ôsmu vzorku pauzy
  (114 zápisov `RLDR0` na vetu namiesto jedného). Od 6.34 stojí predvolene
  na `84h`; počet sa odvtedy znova nemeral.
- Ten potenciometer nie je len hardvérový: `vmsel_mask` (`B0h` bit 6)
  v nule pripája na komparátor `vm1` práve posuvník rýchlosti reči,
  takže firmvér si jeho polohu vie prečítať.
  **Opravené 14. 9. 2026:** nie v nule, ale v **jednotke** — tak to píše
  `IOPORT.H` (príloha H), tak bit nastavuje firmvér na `004EB` a tak to mal
  aj `machine.cpp`; mýlila sa len táto veta. Ako firmvér posuvník číta a na
  čo ho použije, je v 6.34.
- Heartbeat je Timer 1 na **75 Hz**. Bez neho stroj nezaznamená stlačenie
  klávesu ani neozve echo. Zapisuje aj do `power_latch` a `output_latch`,
  preto sa ich tiene v RAM musia aktualizovať *pred* zápisom na port.
- Príkazový jazyk zvuku: `<frekv>[:<frekv>…][/<trvanie>][*<priebeh>]`,
  parser na `0x1636`. `:` sú súčasné hlasy (max 4), CR oddeľuje príkazy
  vnútri reťazca, prázdny reťazec ukončuje melódiu. `&` sa rozvinie na
  `!%T`, `$` na `!%T77:78`.
- **Rečový modul má na `0100h` vlastnú tabuľku skokov.** Stuby SYSJUMPS od
  `CECFh` (v ROM `1D2CF`) nastavia `L`, prepnú MMU a skočia na `0100h+L`:
  `.speak` je `0103h` → `03C5`, `.spchar` `0106h` → `0304`, `.splush`
  `0109h` → `03C4` a `.spconv` `010Fh` → `02D5`.
- **Prepis reči (`TakeSpeechInput`) berie `.speak` aj `.spconv`** (od
  10. 9. 2026, bead ea4-l16). Kým bral len `0103h`, chýbali v ňom celé vety:
  oznámenie času po F2 („9 hodin“ a po nej „45 minut“) aj „diář“ po
  `Shift+F2`. `.spconv` hovorí obsah konverznej pamäte `C7E2h` po nulový bajt
  (kopírovacia slučka `002E3`) a za celý `sweep` sú cez neho práve tieto tri
  vety — zmerané inštrumentáciou všetkých vstupov tabuľky. Drží to režim
  `rtc` v `integration_test`, overené mutáciou: záznam bez `.spconv` dá
  `cas=chyba`. Test žiada obe vety v poradí, lebo druhá je tá, ktorá visí na
  časovaní: minúty prídu asi milión inštrukcií po hodinách, až keď prvá veta
  dohovorí. Hodiny stroja si pritom nastaví na sedem minút po celej, lebo
  **o celej hodine je minútová veta prázdna** — druhé `.spconv` príde, ale
  v pamäti je len nulový bajt, a stroj povie iba „11 hodin“. ROM slová
  skloňuje („1 HODINA“, „2 HODINY“, „0 HODIN“), preto sa porovnávajú len
  kmene `HODIN` a `MINUT`.
- **`.spchar` sa do prepisu zámerne nedáva.** Znak od `09h` vysloví ako
  ozvenu klávesu (v BASICu „b“), bajt `00h`–`08h` a medzeru použije ako
  index do tabuľky deviatich klikov na `00369` a na `C7E2h` z nej zloží `!%`
  a príkaz. Počas `sweep` dostáva len `07h` pri každom otvorení aplikácie
  a `08h`. Nie je v tom nič, čo by volajúci nevedel sám, a ozvena napísaného
  textu by kontrole typu „prepis obsahuje…“ vedela vyhovieť bez toho, aby
  stroj niečo povedal. Tabuľka je v `hardware-map.md` pri klikoch.

Vyrenderované melódie z ROM sú v `audio/melodie/` (`tools/melodies.py`,
**11 378 Hz** — takt DAC pri melódii, φ/540, viď 6.10), dáta reči ako WAV
v `audio/` (`tools/speech_wav.py`, **7493 Hz** = φ/(20·41), RLDR0=`28h`,
viď 6.1). Celý `audio/` je mimo gitu — sú to prevedené dáta ROM.

---

## 4. Braillove tabuľky

**Tri** 64-bajtové tabuľky, nie dve: `0x1D7A0` (počítačový braill
s číslicami v spodnej časti bunky), `0x1D7E0` (česká literárna sada
s diakritikou) a `0x1D820` (číselný režim, `a`–`j` sú `1`–`0`). Vyberá
medzi nimi rutina na `0x1D784` podľa `C835h` a `C668h`.

Poradie bitov indexu: **bit0 = bod 3, bit1 = bod 2, bit2 = bod 1,
bit3 = bod 4, bit4 = bod 5, bit5 = bod 6** — teda poradie klávesov
perkinsovej klávesnice zľava doprava.

To isté poradie má aj **port `89h`**: ROM tým bajtom tabuľku priamo
indexuje (`1D79C`). Kto by chcel simulovať braillovu klávesnicu, nemusí
prekladať nič — stačí vydať bity na port a ROM si znak nájde sama.

---

## 5. Emulátor

Natívny Windows, mingw64 z msys2, `build.bat`. Jadro je superzazu z80 doplnené
o inštrukcie Z180 (`IN0`, `OUT0`, `MLT`, `TST`, `TSTIO`, `OTIM`/`OTDM`,
`SLP`).

### Overený stav

Emulátor **nabootuje, vysloví „inicializace eureky" a otvorí všetky
aplikácie z ROM**. Overené sondou (`tests/diag_probe.cpp`, režim `sweep`).

Mapa funkčných klávesov:

| kláves | aplikácia | kláves | aplikácia |
|---|---|---|---|
| F1 | záznamník | Shift+F1 | textový procesor |
| F2 | hodiny a kalendár | Shift+F2 | diář |
| F3 | kalkulátor | Shift+F3 | teplomer |
| F4 | komunikácia | Shift+F4 | voltmeter |
| F5 | telefónny zoznam | Shift+F5 | databáza |
| F6 | BASIC | Shift+F6 | diskové funkcie |
| F7 | hudobný editor | Shift+F7 | spustiť program z disku |
| F8 | adresár disku | Shift+F8 | formátovať disk |
| F9 | režim | Shift+F9 | stav batérie |
| F10 | kde som | Shift+F10 | sebekontrola |

F2 sa pri otvorení neohlási — že to sú hodiny a kalendár, povie až F10.

**Predošlá veta neplatí (10. 9. 2026), a neplatilo ani „(mlčí)“ pri
`Shift+F2` v tabuľke.** F2 pri otvorení povie čas dvoma vetami — „9 hodin“
a keď dohovorí, „45 minut“ — a `Shift+F2` povie „diář“. Obe tvrdenia vznikli
z prepisu reči, ktorý tie vety nezachytával: idú cez `.spconv`, nie cez
`.speak`. Rozpísané v sekcii 3 (bead ea4-l16).
F9 a F10 nie sú klávesy, ale akordy medzerníka a braillových bodov; viď
`hardware-map.md`.

Prompt „ano nebo ne?" odpovedá na **`Y`**, nie na `a` — na `0x19FD1` je
`CP 59h` dvakrát po sebe, zjavne pozostatok z anglickej verzie.

### Opravené počas analýzy

1. **`BIT b,(HL)` robil zápis do pamäte.** `exec_opcode_cb` zapisoval
   operand späť bezpodmienečne. Na kremíku `BIT` žiadny zápisový cyklus
   nerobí. Doplnená podmienka `x_ != 1`.
2. **`OTIM`/`OTDM` posielal B na horný bajt adresy.** Na Z180 tam patrí
   nula; tento stroj horný bajt dekóduje (hodiny na `0190h`/`0290h`).
2. **DMA kanál 1 sa neemuloval** (obsluha `DSTAT` testovala len bit 6).
3. **DMA obchádzalo ochranu pamäte** a mohlo natrvalo poškodiť obraz ROM,
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
   `TypeOneStatus()`: INDEX pri vloženom médiu (pripojený priečinok aj
   disketa v RAM sú vždy vložený a točiaci sa disk), TRACK 00 podľa
   stopy. Bez média INDEX nepríde — viď 6.6.
3. **DMA sa spúšťala pri zápise do DSTAT.** Firmvér ale povolí DMA
   (19ED1) **skôr**, než vydá Write Track (19EE8), takže celá stopa
   odtiekla do radiča, ktorý ešte neprenášal, a BUSY nikdy nezhaslo.
   Kanál sa teraz „zaparkuje" a rozbehne až keď radič dáta naozaj chce.
4. **Zaparkovanie nerozlišovalo smer.** Nevyprázdnený buffer po
   overovacom čítaní vyzeral ako radič čakajúci na dáta, takže druhý
   Write Track opäť prišiel o obsah. `FdcWantsDma()` teraz porovnáva smer
   prenosu z DCNTL s tým, či radič zapisuje.
4. **Čítacie príkazy držali BUSY, kým buffer niekto nevyprázdnil.**
   Skutočný WD177x sektor dočíta aj bez obsluhy DRQ — nastaví Lost Data,
   ale BUSY zhasne. Firmvér po formáte overuje stopu bez DMA, takže na
   starom modeli visel na 19EF3. Čítania teraz končia stavom DRQ bez BUSY.
5. **Krokovacie príkazy (`20h`–`70h`) sa neemulovali.** Formát chodí po
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

### Klávesnica prepísaná

Funkčné klávesy nerobili v BASICu ani v hudobnom editore nič a šípka
v adresári disku hlásila „opouštím adresář disku". Boli to štyri
nezávislé chyby a každá z nich sama stačila na nezmysel:

1. **Kurzorové klávesy sa vydávali na porte `89h`.** Tam sú braillove
   body, kurzory sú na `8Ch`. Šípka vľavo tak bola bod 3 a stroj
   poslušne vykonal, čo braillov znak v tej aplikácii znamená. Text
   prílohy H si riadky 0 a 2 prehodil; rozhodla ROM, viď
   `hardware-map.md`.
2. **Stlačenie trvalo 25 skenov, nie čas.** Skeny počítala aj obsluha
   generátora tónov (00642), ktorá beží rýchlosťou DAC — kláves stlačený
   počas tónu tak zhasol za tri milisekundy a heartbeat na 75 Hz ho
   nikdy nevidel. Preto nefungoval hudobný editor. Stlačenie teraz trvá
   **120 ms hosťovského času** a pustenie 80 ms, aby ROM videla aj to.
3. **Klávesy s bitom 7 sa pri čakaní BIOS-u podstrčili priamo do
   `keys_`.** Lenže ROM s nimi robí oveľa viac, než že ich podá
   programu: dekóduje ich vo svojom heartbeate a MODE, WHERE aj
   prepínanie aplikácií platia všade. Skratka ich v BASICu umŕtvila.
   Teraz idú vždy cez porty klávesnice a `InterceptBios` na čas ich
   priechodu prepúšťa konzolový vstup ROM-ke.
4. **F9 a F10 nevydávali vôbec nič** — hľadali sa medzi ôsmimi
   funkčnými klávesmi, kde nie sú. Sú to akordy medzerníka a bodov
   (1D541).

Naviac: ALT je medzerník, a keďže dekodér zlučuje všetko stlačené do
troch tikov do jedného akordu, medzerník musí ísť dole o skúsenosť
skôr. Emulátor preto ALT-akord stláča na dvakrát (80 ms + 120 ms).
Držaný kláves sa nepremieňa na sériu ťuknutí, ale predlžuje stlačenie,
takže opakovanie robí typematic ROM (1 s, potom 7,5-krát za sekundu),
nie Windows. Ak hostiteľ hlási aj pustenie klávesu — konzola áno —
`ReleaseKey` stlačenie skráti na 40 ms, aby ťuknutie neodpovedalo
oneskorene.

Overuje to `integration_test ROM DISK kbd`: prejde 38 kódov, ktoré
vie hostiteľská klávesnica vyrobiť, a porovná ich s tým, čo dekodér ROM
zapísal na C638h. Bez neho sa prehodený riadok nijako neprejaví — nič
nezahlási chybu, len sa deje niečo iné.

Každý kód musí začať na stroji, ktorého sa nič iné nedotklo, a nie je to
opatrnosť pre opatrnosť: **tie klávesy konajú**. Kým boli kurzory na
porte 89h, dekódovali sa ako braillove body a aplikácia urobila, čo bod
znamená — šípka vľavo bola bod 3, čiže v adresári disku „opouštím
adresář". Jedna zle dekódovaná klávesa tak odniesla stroj tam, kde sa
ďalšia nedala dekódovať vôbec, a jedna chyba sa vrátila ako
tridsaťosem. Tá izolácia je to, vďaka čomu je regresia čitateľná.

Získavala sa ale rebootom, čo stálo 8M inštrukcií na klávesu, spolu asi
300M a dve tretiny celého behu `kbd`. Teraz sa bootuje raz a pred každou
klávesou sa stav obnoví z kópie (`EurekaMachine::CopyStateFrom`).
Zmerané: všetkých 38 kódov sa dekóduje rovnako a `kbd` spadol z 27 s na
11 s. Jediná vec, ktorú kópia neprenesie, je `cpu_.userdata` — ukazuje
na stroj a musí ukazovať na ten, ktorý stav prevzal; inak by callbacky
obnoveného stroja siahali do snímky.

Tú istú snímku používajú aj `CheckBraille`, `CheckBrailleShiftSpace`
a `CheckAltGr`. Merateľne to už nepridá skoro nič — tri booty sa v
rozptyle stratia — ale nie je dôvod ich platiť.

**`CheckPcKeyboard` snímku použiť nesmie** a to je vecné, nie
kozmetické: predmetom tej kontroly je práve boot. Dva break kódy musia
čakať skôr, než stroj vykoná prvú inštrukciu, lebo overujú, že
neprežijú príkaz Reset — a kým existuje snímka, ROM sa už dávno
rozhodla, či je klávesnica pripojená.

Pozor pri meraní na tomto stroji vôbec: procesor vie spadnúť na 1200 MHz
z 2601 a zostať tam. Čísla namerané v takom stave sa s ostatnými
porovnať nedajú, aj keby vyzerali rozumne. Overuj takt počas záťaže,
nie v pokoji — v pokoji hlási minimum vždy.

### Dva režimy písania

Toľko, koľko mal stroj klávesníc. Prepína sa medzi nimi `F11`, `Ctrl+K`:

| režim | čo je pod rukami |
|---|---|
| **externá klávesnica** (štartový) | všetko ide scancodmi po sériovom porte, ROM si prekladá sama |
| **braillovská klávesnica** | dvadsať klávesov: body na `F D S J K L`, medzerník, funkčné, kurzory, shift |

**Default zanikol 26. 8. 2026** (6.11). Nebol to režim stroja, bola to
obchádzka: text z neho išiel skratkou do fronty ROM, čo skutočná Eureka
nevedela urobiť, a keď sa zrušilo parkovanie, prestal fungovať aj tak.
Štartuje sa v **exterke**, teda v stave „Eureka s pripojenou klávesnicou".

**Od 16. 9. 2026 to platí len pre stroj bez nastavení** (ea4-0vw). Režim
klávesnice sa ukladá do `nastavenia.txt`, takže sa štartuje na tej
klávesnici, na ktorej sa skončilo; slovo „(štartový)" v tabuľke vyššie
hovorí o prvom spustení, keď si emulátor nemá čo pamätať. Prepínače
`--braille` a `--pc` zapamätanú voľbu prebijú. Zvyšok je v 6.18,
v podsekcii „Čo zostáva".

Cena, ktorú 6.11 za to priznávala — že sa nebude dať napísať
`ľ ĺ ŕ ô ä Ľ` — **žiadna cena nebola**. Preverené pred zrušením, ako si
tá poznámka pýtala: v Kamenických majú tie znaky kódy `8C 8D AA 93 84 9C`,
teda všetky nad `7Fh`, a staré `QueueKey` posielalo každý taký bajt do
`PressMembraneKey`. Napísať sa nedali nikdy — stlačili sa ako akord.
Napísať `ä` znamenalo šípku vľavo a napísať `Á` (`8Fh`) štyri kurzory
naraz, teda vypnutie stroja.

~~`Ctrl+K` a nie `Ctrl+Shift+K`~~ — a od 28. 8. 2026 `F11`, `Ctrl+K`, lebo
okno už Eureke `Ctrl+písmeno` neberie vôbec (viď 6.18). Meranie, ktoré
k tomu patrí, platí ďalej a je dobré ho mať: ROM na exterke Ctrl+písmeno
**rešpektuje**, dekodér ho na `1DE0E` maskuje cez `AND 1Fh` na riadiaci
znak. Zmerané, že sa písmeno nenapíše: `a`, `b`, Ctrl+K, `c`, `d` dá riadok
„abcd". Žiadna aplikácia ROM na to viditeľne nereaguje — ale rozhodovať
sa podľa toho už netreba, hosť tie kódy dostáva.

### Braillovská klávesnica

`F11`, `Ctrl+K` prepne písanie na šesť bodových klávesov: `F D S` sú
body 1, 2, 3 a `J K L` body 4, 5, 6, medzerník zostáva medzerníkom.
**Písmená sa v tomto režime nepíšu** — kto v ňom chce písať, píše bodmi,
tak ako na stroji. Stlačené `a` neurobí nič a pri `--diag` to trasovanie
klávesov povie, lebo inak je „nič" na nerozoznanie od „kláves neprišiel".
S textom padlo aj Ctrl+písmeno, a nie je to strata: na tejto klávesnici
riadiaci kláves nie je, riadiace znaky sa na nej robia akordom.
Akord sa zbiera, kým sú prsty dole, a vydá sa naraz pri pustení
posledného — to je perkinsovské správanie a `ReleaseKey` ho umožňuje.

**Toto už neplatí (11. 9. 2026, ea4-v1j).** Hostiteľ akord nezbiera do
jedného rámca odoslaného pri pustení posledného prsta — drží živý stav
troch riadkov matice (`HoldMembrane`, `machine.h`) a ROM vidí každú zmenu
tak, ako na kremíku. Odmerané oboje, čo si k tomu pýtal majiteľ
(7. 9. 2026): postupné pridávanie bodov 1, potom 2, potom 4 je pri
každom kroku počuteľné — vidno to len na `zvuk`, nie v prepise reči,
lebo ozvena ide cez `.spchar` (sekcia 3) — a pustenie jedného bodu z troch
držaných je ticho, keď sa matica vráti do stavu, ktorý už bola videla.
Čo sa napíše, keď sa zvyšok pustí, **nie je** posledný stav pred pustením
— je to zjednotenie všetkých bodov, ktorých sa počas jedného súvislého
stlačenia dotkli prsty. Akord 1-2-4 dá „f" bez ohľadu na to, ktorý bod sa
pustí prvý; pôvodná spomienka na „b" pri čiastočnom pustení sa pri meraní
(sondou aj naživo v emulátore) nepotvrdila — bez skutočného stroja sa
nedá rozhodnúť, či ide o nepresnosť spomienky, alebo o niečo, čo
klávesnica PC vlastnou obmedzenou rollover schopnosťou nevie predviesť.
Drží to `integration_test … akord`.

**Shift patrí do akordu, nie len vedľa neho** (doplnené 26. 8. 2026).
Je to dvadsiaty kláves braillovej klávesnice a leží na riadku `8Ch`,
bit 6. Dekodér ROM ho číta na dvoch miestach a obe robia niečo, čo sa
bez neho nedá vyrobiť vôbec:

- `1D60C` — pri bodovom akorde pošle písmeno cez `D72D`, takže vznikne
  **veľké písmeno**. Zmerané: tie isté štyri akordy dajú bez shiftu
  „ahoj" a so shiftom na prvom „Ahoj".
- `1D52F` — pri **samotnom medzerníku** nevydá medzeru, ale `1Bh`, teda
  **Escape**. Zmerané: `C638h` = `1Bh`. Je to jediný kód na tomto stroji,
  ktorý potrebuje dva riadky naraz, a preto je to zároveň najostrejšia
  skúška, či hostiteľ shift naozaj stláča ako kláves.

Kým `PressBraille` písal len riadok `89h`, nebolo dostupné ani jedno.
Hostiteľská strana shift zbiera po celý akord, na stlačeniach aj
pusteniach: kto pustí shift skôr než posledný bod, ten posledný záznam
už `SHIFT_PRESSED` nenesie.

**A patrí aj vedľa akordu — ako držaný kláves** (doplnené 28. 8. 2026,
nahlásené používateľom). Predošlý odsek je pravdivý a bol neúplný: shift
zbieraný do akordu stačí na všetko, čo akord vyrába, ale **samotný shift
sa do stroja nedostal vôbec**, a to je tretia vec, ktorú tento kláves na
Eureke robí — **zastaví reč**. V braillovskom režime to bol bežný spôsob,
ako sa pozastavilo plynulé čítanie v textovom procesore.

Cesta je celá mimo dekodéra klávesnice:

- `C621h` je `FFh` **len počas reči** — nastaví sa na `002D5` pri spustení
  dávky a vynuluje na `0055E`, keď dohovorí.
- Nový stlačený kláves ho skopíruje do `spabrt` (`C620h`). Robia to dve
  miesta nezávisle: vzorkovacia slučka syntetizátora na `0063D` (`OR (HL)`
  / `CP (HL)` proti tieňom `C62E`–`C630`, teda „má port bit, ktorý tieň
  nemá?") a zachytávacia rutina heartbeatu na `1D216` (`CPL` / `AND B`).
- `SYSJUMPS.11` to pomenúva priamo: `.sprem` vracia, koľko znakov sa
  nestihlo povedať, „**used by Word Processor to calculate what word was
  being spoken when 'continuous speak' mode is aborted**".

Dôležité pri tom je, že tieň `C62E`–`C630` sa plní **surovým** bajtom
(`LD (HL),B` na `1D22A`), zatiaľ čo maska `0Fh` sa týka len záchytu pre
dekodér. Preto shift v tieni je, hoci v kóde klávesu nie je — a preto
stlačenie shiftu je pre vzorkovaciu slučku viditeľná zmena.

Odmerané na bežiacej ROM: bez shiftu trvá veta „hlavní menu" **783 493**
krokov; so shiftom stlačeným na kroku 200 000 padne `spabrt` po 2 390
krokoch (asi 0,4 ms, lebo slučka beží rýchlosťou DAC) a veta končí na
**202 880**. Je z toho test `CheckBrailleShiftStopsSpeech` v režime `kbd`;
overené aj to, že po odpojení shiftu na porte zvoní.

Riešenie na strane emulátora je `HoldShift(bool)`: bit `40h` sa **prilepí
k riadku `8Ch` pri čítaní portu**, mimo frontu rámcov. Fronta je
postupnosť, ktorú stroj prehráva, kým shift je kláves, ktorý používateľ
drží cez celú tú postupnosť — sú to dve rôzne veci a nepatria do jednej
štruktúry. Rámec, ktorý si bit nesie sám (akord veľkého písmena), sa tým
nemení, takže `PressBraille` zostal ako bol.

**Shift už nie je jediný kláves mimo frontu (11. 9. 2026, ea4-v1j).** Bol
to precedens — komentár pri `membraneShift_` to hovoril priamo — a teraz
platí pre zvyšných devätnásť klávesov cez `HoldMembrane`. Shift zostal
oddelený zámerne, nie zabudnutý: nejde cez debounce frontu, ktorou
`HoldMembrane` prechádza (`kHoldStepMs`), lebo naň nič nebrzdí — pozri
komentár nad `HoldShift` v `machine.h`.

Overené aj to, čo sa dalo pokaziť: `Shift+F9` a `Shift+F10` sú akordy
medzerníka, ktoré shift bit zámerne nenesú, a s držaným shiftom
dekódujú **rovnako** („nabitá", „sebekontrola"). Naopak `F1` sa
s držaným shiftom stane `Shift+F1` („textový procesor" namiesto
„záznamník") — to nie je chyba, to robí aj skutočný stroj.

Hostiteľ shift **synchronizuje, neprepína**: pri každej klávesovej
udalosti nastaví `HoldShift` podľa živého `SHIFT_PRESSED`, a to **pred**
strážou na Ctrl. Akcelerátorová tabuľka zje stlačenie, nikdy nie
pustenie, takže prepínanie by shift raz nechalo dole navždy a odvtedy by
sa potichu písali samé veľké písmená. Nuluje sa aj pri prepnutí režimu
a strate fokusu.

Emulátor **neprekladá nič**. Pošle šesť bitov na riadok `89h`, shift na
`8Ch` a znak si nájde ROM sama (`1D79C`), takže fungujú všetky tri
tabuľky vrátane číselného režimu aj akordy s medzerníkom, o ktorých
nevieme. Doložené: štyri akordy napíšu v textovom procesore „Ahoj"
a `Home` to prečíta späť — presne to robí `integration_test … kbd`.

**Doplnené 11. 9. 2026 (ea4-cb4, ea4-e9k) — dva z týchto akordov už
zmerané sú.** `medzerník+F1`…`F8` sa dekóduje ako Alt+Fn (bit `k_alt`,
`1D4F3`): meno funkcie sa povie, ale aplikácia sa nespustí. Overené na
`medzerník+F4`: povie „komunikace" (rovnako ako samotné F4), no
nasledujúci samostatný F1 dá „zaznamnik" — sme stále v hlavnom menu,
zatiaľ čo po samotnom F4 dá F1 „vloz telefonni cislo" (sme v komunikácii).
`medzerník+hore/dole` číta celý riadok (A1h/A2h), presne ako v sekcii
„Čítacie klávesy textového procesora". `medzerník+Home/End/PgUp/PgDn`
reagujú (nie sú ticho), ale čo presne čítajú, zmerané nie je: v
`eurekatech/` nie je používateľský návod k textovému procesoru a
jednoriadkový testovací text nevie rozlíšiť „riadok" od dlhšieho úseku —
neodhadovať.

**Kurzory sa v tomto režime chordujú tiež** a je to tak zámerne: kto píše
na braillovej klávesnici, čaká, že sa aj kurzory budú správať ako na
Eureke. Nie je to zvláštnosť tohto režimu — chordovanie je zapnuté všade
okrem exterky, kde sa scancody prekladajú po jednom a chordovať sa
nedajú (viď „Vypínanie stroja").

**Mechanizmus je od 11. 9. 2026 iný, záver rovnaký.** Predošlý odsek
opisoval odosielanie hotového rámca — jeden kurzor ide hneď, druhý ho
zmení na čakajúci akord. Teraz kurzory (aj F1–F8) žijú v tej istej živej
vrstve ako bodky (`HoldMembrane`), takže ROM vidí zmenu okamžite, nie až
pri pustení. Skladanie viacerých kurzorov naraz (napr. Home = hore+vľavo)
funguje rovnako ako predtým, len bez čakania na pustenie — pozri ea4-v1j.

### Čítacie klávesy textového procesora

Od majiteľa, a potom zmerané, lebo testy sa o ne opierajú. Riadok
**nečíta šípka hore** — tá povie len znak pod kurzorom. Riadok prečíta
**medzerník + šípka hore**, teda kód `A1h`.

Napísané „Ahoj Svete", kurzor na konci, a stlačené jedno:

| kláves | kód | povie |
|---|---|---|
| medzerník + hore / dole | `A1h` / `A2h` | „Ahoj Svete." — **celý riadok** |
| Home | `85h` | „Svete " — slovo, v ktorom je kurzor |
| End | `86h` | „Ahoj " |
| shift + hore | `91h` | „strana 1" |
| šípka hore / dole | `81h` / `82h` | nič, len klik `!%E1` |
| šípka vľavo / vpravo | `84h` / `88h` | klik; na okraji navyše `!%E7` |

Na externej klávesnici je to **ľavý Alt + šípka hore** a overené je to
zmeraním, nie odvodením: ROM na `1DD9E` prisadí k šípke bit 5, keď je
v `C670h` bit 3 (ľavý Alt, `DF05[38h]` = `F8h`), takže z `81h` vznikne
to isté `A1h`, ktoré robí medzerníkový akord. Odoslané ako `38 E0 48
E0 C8 B8` stroj povie „Ahoj Svete.".

Pozor na to pri testoch: `tests/README.md` roky tvrdil, že `kbd` si
nechá „prečítať riadok". Nechá si prečítať **slovo**. Test tým neprešiel
omylom — píše jedno slovo, takže je to to isté — ale popis bol nesprávny
a je opravený.

### Klávesnica IBM PC na sériovom porte

`F11`, `Ctrl+K` prepne hostiteľskú klávesnicu na tú, ktorá sa k Eureke
pripájala zvonku. Emulátor **neprekladá zase nič**: Windows dáva
v `wVirtualScanCode` rovno scancode XT sady 1 a príznak `ENHANCED_KEY`
je na drôte prefix `E0h`, takže je to vodič, nie tabuľka.

Cesta v ROM: prerušenie CSI/O → vektor `C18C` → `CD62` naplánuje úlohu
`D409` → `DD2E`. Tam sa scancode preloží tabuľkami `DF05` (základná),
`DF5E` (shift) a `DF98` (pravý Alt), rozšírené kódy tabuľkou `DFD6`.

Dve pasce, ktoré zlyhávajú **potichu** a stáli celý večer; podrobne
v `hardware-map.md`:

1. **Prijatie bajtu zhodí `RE`** — je to vlastnosť 64180, preto ROM
   prijímač po každom bajte znovu zapína (`1E012`). Bez toho zožerie
   zahadzovacie čítanie `TRDR` na `1DFFE` druhý bajt sekvencie `E0h`:
   písmená chodia, šípky nie.
2. **Reset (`FFh`) musí vyprázdniť frontu.** Inak stačí kláves pustený
   pred prvou inštrukciou stroja — Enter, ktorým sa emulátor spustil —
   a ROM si namiesto `AAh` vytiahne break kód, usúdi, že klávesnica nie
   je pripojená, a prijímač vypne natrvalo.

Čo z toho plynie a je prekvapivé: **ROM očakáva českú QWERTZ
klávesnicu.** Na `15h` je `z` a na `2Ch` `y`, nezhiftovaná číselná rada
dáva `ěščřžýáíé` a číslice sú až so shiftom. Pravý Alt je modifikátor
pre `@ # $ ~ ^ & * { } [ ] ' \``. A ROM si sama mapuje aj funkčné
klávesy a kurzory na kódy Eureky (`DF05` od `3Bh`: F1–F10 → `C0h`–`C9h`,
šípky, Home, End, PgUp, PgDn, Insert, Delete).

Model v emulátore je malý, lebo hardvér je malý: `CNTR` bit 7 je EF,
bit 6 EIE, bit 5 RE, bit 4 TE. Bajt sa doručí, keď je RE zapnuté
a predchádzajúci je vyzdvihnutý; čítanie `TRDR` EF zhodí; pri EIE sa
vyvolá prerušenie s vektorom `0Ch`.

Dve veci, na ktorých to stálo:

- **Odpoveď `AAh` musí meškať.** ROM po vydaní `FFh` (Reset) zapne
  prijímač a urobí *zahadzovacie* čítanie `TRDR` (18841). Odpoveď
  doručená okamžite by v ňom zmizla a stroj by usúdil, že klávesnica
  nie je. Emulátor ju dá o 30 ms, čo je hlboko pod timeoutom asi 180 ms
  na 18847.
- **Stroj nesmie byť zaparkovaný.** Kým `InterceptBios` zastavoval CPU
  na čakaní na kláves, scancode nemal ako doraziť — prerušenie sa nemalo
  kedy vyvolať. Riešil to `HardwareInputBusy()`, ktorý parkovanie vypínal,
  kým bol vstup na ceste cez hardvér. Parkovanie aj tá poistka zanikli
  spolu s defaultom; procesor beží stále, tak ako na stroji.

Skratka, ktorá tu roky stála, je preč aj s režimom. Slovenské
`ľ ĺ ŕ ô ä Ľ` sa napísať nedá — v tabuľkách klávesnice PC (`DF05`,
`DF5E`, `DF98`) ani v braillových (`D7A0`, `D7E0`, `D820`) tie znaky nie
sú, sú tam len české `é č ě ž ů ý á í ú ň š ř`. Je to česká ROM, takže
slovensky nevedela písať ani skutočná Eureka. Že to vedel default, sa
roky písalo aj sem a **nebola to pravda**; ako to bolo naozaj, je pri
dvoch režimoch písania v sekcii 5.

#### Dve pasce okolo AltGr (nájdené a opravené 26. 8. 2026)

Hlásené z používania: po pravom Alte zostane pravý Alt akoby stlačený.
Diagnostikované zo záznamu `--diag`, obe pasce zmerané, obe boli naše.

**Prvá: Windows na pustení AltGr nehlási `ENHANCED_KEY`.** V zázname je
to čierne na bielom — stlačenie príde so `stav=0109h` (`0100h` je
`ENHANCED_KEY`), pustenie so `stav=0000h`. `SendScanCode` sa podľa toho
príznaku rozhodoval, či predradiť `E0h`, takže poslal `E0 38` a späť už
len holé `B8`. A holé `B8` ide v ROM druhou cestou: `AND 7Fh`, `DF05[38h]`
= `F8h`, teda **ľavý** Alt, a zhodí bit 3. Bit 4, ktorý nastavilo `E0 38`,
zostane navždy. Zmerané na `C670h`: po `E0 38` je `10h`, po holom `B8`
stále `10h`, po `E0 B8` konečne `00h`.

**Druhá: Windows k AltGr pridáva ľavý Ctrl.** Na rozloženiach, ktoré
AltGr majú, sa robí ako Ctrl+Alt a hlási sa oboje. Skutočná klávesnica PC
po drôte pošle **len pravý Alt**, takže ten Ctrl je artefakt hostiteľa
a nesmie na drôt: so stlačeným Ctrl ROM na `1DE16` maskuje každý znak nad
`3Fh` cez `AND 1Fh`. Zmerané: AltGr+`2` bez neho napíše `@`, s ním
nepríde **nič** (`40h AND 1Fh` = `00h`).

**Oprava nie je záplata na `E0`, je to zmena zdroja pravdy.** Modifikátory
sa už nepreposielajú ako udalosti; `SendScanCode` ich preskakuje
a `SyncModifiers` ich odvodzuje z `dwControlKeyState`, ktorý Windows hlási
pri **každom** zázname. Rozdiel proti tomu, čo stroj drží, sa dopošle —
najprv pustenia, potom stlačenia. Tým zaniká celá trieda týchto chýb,
vrátane pustenia strateného pri strate zamerania okna; je to ten istý
problém, kvôli ktorému existuje `ForgetStaleArrows` pre kurzory.

Padla s tým aj tretia, menšia: skratka `Ctrl+K` sa spúšťala pri AltGr+K,
lebo `ctrl` bol vtedy pravda. Skratky teraz čítajú ten istý `WantedModifiers`
ako stroj, takže Ctrl z AltGr nevidia.

Drží to `integration_test ROM DISK kbd`, časť `altgr`: v BASICu napíše
AltGr+`2` a hneď za tým ten istý kláves sám, a čaká `40 88` — teda `@`
a `ě`. Odskúšané mutáciou: keď sa pustenie pošle holým `B8`, príde `40 40`,
lebo AltGr zostal visieť. Majiteľ opravu odskúšal aj ručne.

### Vypínanie stroja

Eureka sa vypínala **štyrmi kurzorovými klávesmi naraz z hlavného menu**:
zahlásila „konec" a zhasla. Emulátor to teraz robí tiež. Celá cesta
v ROM je rozpísaná v `hardware-map.md` pri porte `B8h`; sem patrí, čo
z nej plynie pre model.

- Kód klávesu je **8Fh** (`k_udlr` v `KB.LIB`) a jediný test naň je
  `CP 8Fh` na 18154, v slučke hlavného menu. Inde chord nerobí nič.
- Vypnutie ide cez **odpočítavadlo nečinnosti** `C66Ch`. Chord ho
  nastaví na 1, takže vypne najbližší tik heartbeatu; bez klávesu
  odpočíta reload `57E4h` = 22500 tikov = **presne 5 minút** a 30 sekúnd
  vopred zahrá výstražnú znelku. Je to teda **jeden mechanizmus**, nie
  dva.
- Samotný vypínač je `IN A,(B8h)` na 1D144 s `JR $-2` za ním. Kým bol
  port bez modelu, emulátor v tej dvojici inštrukcií točil navždy,
  s vypnutými prerušeniami: ticho, hluchý a s vyťaženým jadrom.

Čo robí emulátor teraz: `Step()` vráti `false` a stroj **stojí celý**,
ako keď zhasne napájanie. RAM ani hodiny sa nemažú — na skutočnom stroji
majú vlastné napájanie, ktoré sa nikdy neodpájalo (`GLOSSARY.TXT`), a
práve preto vie firmvér nadviazať tam, kde používateľ skončil. `main.cpp`
počká, kým dohrá „konec" (`AudioPlayer::Close()` volá `waveOutReset` a
inak by koncovku odrezal), zapíše disk a skončí.

**Posledná veta už neplatí (9. 9. 2026).** Emulátor po vypnutí
neskončí: okno zostáva otvorené, vlákno stroja beží ďalej a späť ho
zapne `Ctrl+P` teplo alebo `Reset` studeno. Dôvod je v 6.31, ktorá je
celá uzavretá a v archíve. Zvyšok odseku platí — o modeli sa nezmenilo
nič okrem toho, že `Reset()` sa rozdelil na studenú a teplú polovicu;
zmenilo sa hlavne to, čo s ním robí hostiteľ.

**Pozor pre každého, kto píše slučku okolo `Step()`.** Vypnutý stroj už
neprikročí, takže `cycles()` ani `instructions()` sa nehýbu a slučka
tvaru „bež, kým nevyprší rozpočet" sa **zacyklí**. `RunUntilPrompt`
v sonde aj v integračnom teste to už ošetrujú.

Ako sa to dá vyvolať z hostiteľa: kurzorové klávesy sú **jedna
klávesnica, nie štyri klávesy** — ROM ich číta ako bitovú množinu na
riadku `8Ch`. `main.cpp` preto jeden kurzor pošle hneď (aby navigácia
zostala okamžitá a typematic ROM ďalej opakoval), ale keď na ňom pristane
druhý prst, čaká na pustenie posledného a pošle zjednotenie — presne ako
braillovský akord. Že sa ten prvý kláves stihol vydať sám, nie je chyba:
na skutočnej membráne vyzerajú prsty dopadnuté o viac než jeden sken
rovnako.

Jedna poistka tam je zámerne: keď okno stratí zameranie uprostred
stlačenia, pustenie klávesu dostane niekto iný a emulátor by ten kurzor
navždy považoval za držaný — a tým by umŕtvil všetky ďalšie jednotlivé
šípky. Sada stlačených sa preto zabudne, ak z nej dve sekundy nič
nepríde. Windows držaný kláves opakuje asi tridsaťkrát za sekundu, takže
skutočne držaný prst sa tak nikdy nestratí, a dve sekundy sú zároveň nad
najdlhším oneskorením prvého opakovania, aké Windows ponúka — pomaly
skladaný akord sa teda nerozpadne.

V režime **externej klávesnice** sa stroj vypnúť nedá, a je to tak
správne — skutočná Eureka to tiež nevedela, `8Fh` nie je ani v jednej
z jej štyroch prekladových tabuliek scancodov.

Drží to `integration_test ROM DISK power`: stlačí 8Fh, počká na „konec",
a overí, že stroj stojí, že `C45Ah` je `FFh` a že sa počítadlá naozaj
zastavili. Odskúšané mutáciou — keď sa `B8h` vráti k `return 0xff`,
test spadne na dvoch z tých kontrol.

#### Uzavreté: znaky nad 7Fh sa stláčali ako akordy

Preverené 26. 8. 2026, tesne pred zrušením defaultu, ako si táto poznámka
pýtala — a dopadlo to opačne, než znel predpoklad. Staré `QueueKey`
posielalo každý bajt s bitom 7 do `PressMembraneKey`, takže **žiadny
akcentovaný znak sa v defaulte nikdy nenapísal**: `ä` (`84h`) bola šípka
vľavo, `Á` (`8Fh`) štyri kurzory naraz, teda vypnutie stroja, a `ľ ĺ ŕ ô Ľ`
(`8C 8D AA 93 9C`) nejaký iný kurzorový akord.

Tvrdenie, že default je jediná cesta k `ľ ĺ ŕ ô ä Ľ`, teda nebolo len
neúplné, bolo nesprávne. Cesta k nim neexistovala. `QueueKey` dnes berie
už len kódy klávesov, text ide výhradne cez `QueueText`, a otázka tým
zaniká.

### Diagnostika

`--diag` zapne záznam: zahodené zápisy pod `kRamBase`, externé porty bez
modelu, interné registre Z180 bez správania, zmeny troch riadiacich
latchov po bitoch, kruhový záznam 512 udalostí. Výpis `F11`, `Ctrl+D`
alebo pri ukončení. Voliteľné trasovanie konkrétnych portov aj vtedy, keď
ich model implementuje (`Diagnostics::set_trace`).

Pri **vypnutej** diagnostike `Ctrl+D` nič nevypisuje a povie to dialógom;
prečo práve dialógom a prečo z vlákna okna, je v 6.20.

Aktuálny stav: **žiadne externé porty bez modelu** naprieč všetkými
aplikáciami. Zahodené zápisy sú jediné tri, na `1C1FA`–`1C1FC` z adresára
disku (viď 6.2); `kRamBase = 0x70000` teda drží.

Pozor pri hodnotení: `boot` tie zápisy neukáže, lebo sa k nim nedostane.
Treba `sweep`, prípadne `seq kC7`.

---

## 6. Otvorené otázky

Pôvodných päť otázok manuál a následné meranie uzavreli. Zostáva iné.

Uzavreté zásahy sa sťahujú do `HANDOFF-archiv.md`, ale **len keď ich záver
niekde inde naozaj stojí** — v kóde, v `CLAUDE.md`, v `hardware-map.md`
alebo v README. Čísla zostávajú platné: po presunutom zásahu tu zostáva
odsek s tým istým číslom, lebo odkazy `6.x` sú roztrúsené po zdrojákoch aj
po ostatných dokumentoch. A otvorený zvyšok uzavretej témy zostáva tu, nie
v archíve — viď 6.1.

### 6.1 Zvuk — rekonštrukčný filter aj latencia hotové

**Uzavreté, celé znenie aj s meraniami je v `HANDOFF-archiv.md`.** Za DAC
patrí rekonštrukčný filter aj väzobný kondenzátor a v kóde ich drží
`machine.cpp` (`RenderAudio`, `couplingState_`); latencia je 23 ms a meria
sa cez `waveOutGetPosition`, nie cez účtovníctvo blokov. Otvorené z tejto
témy zostáva len toto:

#### Čo zostáva

- ~~**WAV-y v `audio/`** sú stále v štyroch hádaných frekvenciách;
  prerenderovať podľa známych ~7,5 kHz.~~ **Hotové 10. 9. 2026 (`ea4-upc`):**
  pribudol `tools/speech_wav.py` — dáta reči `20000h–40000h` ako 8-bit WAV
  pri **7493 Hz** = round(6 144 000 / (20·41)), teda RLDR0=`28h`, čo sú tie
  „asi 7,5 kHz“ z manuálu (RLDR0=`15h` je dvojnásobok pre efekty).
  `tools/melodies.py` prešiel z 22050 na **11 378 Hz** (φ/540, RLDR0=`26`,
  6.10). Staré hádané súbory (`rec_*Hz.wav`, `ukazka_8000Hz.wav`) zmazané.
  `audio/` je mimo gitu, takže commitovaný je len nástroj.
- `pol_voice` (`A0h` bit 3) sa nemodeluje. Manuál píše, že býva trvalo
  zapnutý, takže hradenie zvuku naň by len riskovalo trvalé ticho.
- Pod ~20 ms sa s `waveOut` ísť nedá. Ak by to niekedy bolo treba,
  cesta je WASAPI v zdieľanom režime riadený udalosťou, prípadne
  `IAudioClient3`. Je to podstatne väčší zásah a dnešných 23 ms ho
  nezdôvodňuje.

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

**Doplnené 7. 9. 2026: pri `fdc_side` je veta „zmení sa pri diskovej práci
s druhou stranou" príliš pokojná.** `HARDWARE.1` hovorí, že mechanika je
3,5" **obojstranná, 80 stôp**, 1 MB nenaformátovaných → 800 K, z toho 8 K
adresár a **792 K dáta** — čo presne sedí s našimi 396 blokmi po 2 KiB.
Druhá strana teda nie je zvláštny prípad, je to bežná polovica každej
plnšej diskety, a niečo ju prepnúť malo. Model stranu vie
(`machine.cpp:912` ju berie z `outputLatch_ & hw::kFdcSide`,
`TrackFormatted` aj `ReadPhysicalSector` ju prijímajú), takže nepokrytá je
skôr **cesta ovládača v ROM**: `InterceptBios` ju obchádza, takže kód,
ktorý stranu prepína, takmer nebeží. Overiť sa to dá technikou z 6.30 —
vypnúť obídenie BIOS-u a nechať bežať pôvodný ovládač.

### 6.5 Formátovanie nemaže hostiteľský priečinok

Emulátor formátovanie dokončí a ohlási úspech, ale obsah stopy zahodí:
virtuálny disk je hostiteľský priečinok a mazať používateľove súbory
preto, že emulovaný stroj formátoval, nie je rozhodnutie tohto modelu.
Ak by sa to niekedy malo zmeniť, musí to byť vedomé a s potvrdením.

**Doplnené 28. 8. 2026: teraz je médium, na ktorom formátovanie naozaj
niečo robí** — nenaformátovaná disketa v pamäti (6.22). `VirtualDisk` má
bitovú mapu naformátovaných stôp, Write Track ju zapĺňa a robí to **len
pre disketu bez priečinka**; nad priečinkom platí odsek vyššie bez zmeny.
Držia to kontroly v `disk_test` aj režim `format` v `integration_test`.

**Doplnené 31. 8. 2026 (6.28):** `Media::kRam` už neexistuje a podmienka
sa volá `VirtualDisk::formatting_erases()`. Je to zámerne **vlastný
predikát**, nie `!has_home()`, hoci má rovnakú odpoveď: dôvod je iný
(priečinok je filesystem, nie magnetická plocha) a zdieľanie jedného slova
dvoma otázkami je presne to, čo spôsobilo 6.26.

Pri tom sa odmeralo, ako formátovanie z pohľadu firmvéru vyzerá, a dve
veci z toho boli prekvapenie:

- **ROM sa pýta dvakrát.** Najprv „mám formátovat disk, ano nebo ne?“
  a po potvrdení „disk je už naformátován. Přeformátovat, ano nebo ne?“
  Tá druhá otázka príde **vždy**, aj na diskete, na ktorej nie je jediná
  naformátovaná stopa. Nie je to zistenie o médiu, je to druhé
  potvrdenie deštruktívneho úkonu — vyzerá to ako tvrdenie a nie je.
  **Táto odrážka už neplatí (1. 9. 2026, viď 6.30).** Je to tvrdenie
  o médiu a ROM ho robí vedome: `13CC3` volá `fdc_ctl_disk_test` a `AND 04h`
  na `13CC6` tú otázku **preskočí**, keď je disketa nenaformátovaná. Vyzerala
  ako bezpodmienečná preto, že vtedajší model na tú kontrolu vždy odpovedal
  „naformátovaná“.
- **Odpovedá sa `y`, nie `a`.** Otázka je česká, kláves anglický;
  v manuáli tomu zodpovedá `GETYN.H`. `a` sa berie ako nie a stroj
  povie „příkaz zrušen“. Zistené meraním, keď test odpovedal po česky.
- **A pozor, ktorý fyzický kláves to je.** Odmerané výpisom
  z `QueueText`: `y` je scancode **`2C`**, teda tam, kde má americká
  klávesnica `Z` — tabuľka klávesnice v ROM je česká QWERTZ. Emulátor
  posiela polohu klávesu a preklad robí ROM, takže rozloženie nastavené
  vo Windows do toho nehovorí. Na QWERTY klávesnici sa teda na túto
  otázku odpovedá klávesom označeným `Z`. Platí to pre celé písanie,
  nielen pre formátovanie, a je to prvý podozrivý vždy, keď „stroj na
  napísané písmeno nereaguje“.

**Firmvér médium pri tom vôbec nečíta.** Odmerané inštrumentáciou
`StartFdcCommand` a BIOS stubu: za celý beh do prvej otázky vydá ROM
**tri** FDC príkazy, všetky Type I (Restore a dva Seeky), a **nula**
BIOS čítaní. Až po druhom `y` ich je 1835. Preto neexistuje spôsob, ako
by hláška „disk je už naformátován“ mohla o médiu čokoľvek vedieť —
a preto sa aj nenaformátovaná disketa dá naformátovať bez obchádzok.

### 6.6 Disketa je nepovinná

`VirtualDisk` má tri stavy: žiadne médium, hostiteľský priečinok
a disketa žijúca len v pamäti (`--ram-disk`). Bez média nedáva Type I
príkaz impulz INDEX (bit 1 na porte `98h`), lebo ten pochádza z diery
v médiu; TRACK 00 zostáva, je to snímač mechaniky. Čítania a zápisy
sektorov končia na Record Not Found a hneď zdvihnú INTRQ, inak by
firmvér čakal na prerušenie, ktoré nepríde.

Overené sondou: bez diskety stroj nabootuje normálne, `F8` (adresár)
aj `Shift+F6` (diskové funkcie) ohlásia **„vadný disk"** a vrátia sa do
menu. Manuál (`DEVICES.10`, `fdc_ctl_chkdsk`) pozná aj presnejšie
„disk není založen", ale tá hláška vychádza z vlastnej kontroly INDEX
v ROM, ktorou tieto dve cesty neprechádzajú — rovno čítajú. Na skutočnom
stroji by prázdna mechanika dala tiež RNF, takže hlásenie sedí.

Skúšané a zavrhnuté: nechať bez diskety bežať pôvodný ovládač v ROM
namiesto obídenia BIOS-u (`InterceptBios`, prípady 13 a 14). Hláška
vyšla rovnaká, len o ~20 000 inštrukcií drahšie.

**Toto už neplatí (1. 9. 2026, viď 6.30).** Odsek vyššie hovorí, že
„vadný disk" sedí a že presnejšia hláška vychádza z kontroly, ktorou tieto
cesty neprechádzajú. Obe vety boli nesprávne, a rovnakým spôsobom: cesty ňou
prechádzajú, len jej model neodpovedal. `fdc_ctl_disk_test` vydá **Seek
s verify** (14h na `1980A`) a číta ho troma spôsobmi; model vracal po každom
Type I ten istý stav bez ohľadu na príkaz, takže verify vždy uspel a všetko
skončilo na čítaní, ktoré firmvér ohlásil ako „vadný disk". Prázdna
mechanika teraz povie **„disk není založen"** (adresár) a **„v jednotce
není disk"** (diskové funkcie), nenaformátovaná disketa **„disk není
naformátován. Chceš jej naformátovat?"**. Ten posledný odsek o pôvodnom
ovládači platí ďalej, ale jeho záver sa obrátil: s opraveným modelom
firmvér **„disk není založen" povie sám**, takže obídenie BIOS-u ho
reprodukuje, nenahrádza.

### 6.7 Zápis späť je viazaný na kľud disku, nie na hodiny

Pôvodne sa `Flush` volal každé dve sekundy. To je nezávislé od hosťa,
takže export mohol trafiť rozpísanú úpravu adresára. Najhoršie na tom
bolo mazanie: položka zmazaná na ceste k prepísaniu vyzerá ako zmazaný
súbor a hostiteľský súbor putoval do `.eureka-trash`. Teraz sa
zapisuje, až keď je disk špinavý, radič nie je uprostred prenosu
a od posledného zápisu ubehla sekunda hosťovho času
(`EurekaMachine::DiskSettled`).

### 6.8 Konzola zdvojovala každý znak

BASIC hlásil „hhoottoovvoo". Príčina: BIOS sa zachytáva na dvoch
miestach — skoková tabuľka na `C100` a stuby na `C03A` — a každá
položka `C100` je `JP` do príslušného stubu na `C03A`. Volanie cez
tabuľku teda prejde oboma bodmi. Funkciám, ktoré vybavíme cez
`ReturnFromCall`, to nevadí, lebo sa k stubu nikdy nedostanú. Výstup na
konzolu je jediná funkcia, ktorú **zachytíme a necháme bežať ďalej**,
takže sa započítala dvakrát. Zachytáva sa už len na úrovni stubu.

Reč postihnutá nebola — tá má vlastný hook na `0x0103`.

Test to nechytil, lebo kontroloval `console.size() >= 40`, a zdvojený
výstup ten limit spĺňal ľahšie než správny. `integration_test` teraz
kontroluje obsah (`hotovo`, `RUN`, `Read which file?`), nie dĺžku.

### 6.9 Hudba sa nedá prerušiť — medzerník vyriešený

Hlásené **majiteľom skutočného stroja z ostrého používania**, nie
z merania. Rozpadá sa to na dve veci a prvá je od 26. 8. 2026 hotová.

**Prvá polovica je uzavretá, celé znenie aj s meraniami je
v `HANDOFF-archiv.md`.** Prehrávacia slučka na `10F10` číta porty `89h`
a `8Ch` **priamo**, nie cez frontu, takže znelku zastaví jedine kláves
braillovej klávesnice — hociktorý bod, medzerník alebo kurzor. Emulátor ho
tam po zrušení defaultu (6.11) doručí v oboch režimoch, ktoré zostali, a drží
to `integration_test ROM DISK hudba` dvoma behmi: s medzerníkom a bez ničoho.

**Doplnené 7. 9. 2026, a je to prekvapivé dosť na to, aby to tu stálo
výslovne: z externej klávesnice sa hudba zastaviť NEDÁ — ani na skutočnom
stroji.** Overil to majiteľ naživo. Vyplýva to z odseku vyššie (slučka
číta `89h` a `8Ch` priamo, teda vidí len dvadsaťklávesovú membránu), ale
odvodiť sa to dá až spätne, kým hlásenie znie „nedá sa prerušiť ničím“.
Je to **pravdepodobný pôvod tých hlásení**: kto skúšal medzerník na
exterke, robil vec, ktorá na kremíku nefunguje tiež. Emulátor je tu
verný a nie je čo opravovať.

Otvorená zostáva druhá polovica:

#### Umŕtvená klávesnica po čase — otvorené

Toto je druhá, samostatná vec a postup na reprodukciu stále nikto nemá:

- **Úplná nemožnosť prerušiť skladbu čímkoľvek** — stroj sa do toho stavu
  dá dostať, ale nie na povel. Je to teda skôr stav, do ktorého emulátor
  občas spadne, než trvalá vlastnosť prehrávania.
- **Po čase občas prestanú fungovať aj šípky mimo hudby** — do adresára
  disku sa dá vojsť, ale nedá sa v ňom listovať. Ak sú tie dva javy
  jeden, hľadá sa niečo, čo umŕtvi klávesnicu **naprieč aplikáciami**
  (zaseknutý stav dekodéra ROM alebo fronty emulátora), nie chyba
  prehrávacej slučky editora.

Čo je k tomu známe, aby sa to znovu neodvodzovalo:

- **Nesúvisí to s dĺžkou stlačenia.** Tá je v cykloch hosťa
  (`kPressMs` = 120 ms) a rámce klávesnice sa posúvajú až pri čítaní
  portu `89h`, takže je nezávislá od toho, ako často beží hostiteľská
  slučka.
- **Klávesnica je počas hudby snímaná často, nie zriedka** — slučka sa
  na porty pozrie raz za takt skladby.
- Predchádzajúca oprava hudobného editora (sekcia 5, bod 2) riešila iný
  jav — že sa kláves *stratil*.
- Stojí za pozretie **`C598h`/`C599h`**: `10F21` ich porovnáva a pri
  nezhode zo slučky vyskočí cez `CALL NZ,F172h`. Sú to jediné dva bajty,
  ktoré prehrávač číta a nikde v ROM sa nezapisujú absolútnou adresou,
  takže sa do nich píše cez ukazovateľ a kto ich plní, zatiaľ nevieme.
  Ak je to počítadlo udalostí klávesnice, je to druhá cesta von zo
  slučky a možný kandidát na zaseknutie.

### 6.10 Tempo hudby — uzavreté

**Uzavreté 20. 9. 2026, celé znenie aj s meraniami je
v `HANDOFF-archiv.md`.** Dĺžku noty odmeriava prázdna čakacia slučka
v ROM na `10E5D`, nie časovač, a dostáva len to, čo zostane z 540
T-stavov po obsluhe generátora tónov — preto sa percento v cene obsluhy
premietne do ôsmich percent v tempe. Emulátor hral najprv o 14,5 %
pomalšie; časovanie Z180 s jedným T-stavom navyše na cyklus M1 to
12. 9. 2026 zmenilo na 3,9 % rýchlejšie a tri čakacie stavy `TMDR0L`
(`hw::kTmdr0lWaits`) na **0,2 %**, overené druhou nezávislou nahrávkou
skutočného stroja a vypočuté majiteľom.

Dve veci odtiaľ, ktoré platia ďalej a inde sa nedozvedia:

- **Tabuľky cyklov sa menia len spolu s čakacími stavmi.** Samotné
  hodnoty Z180 by minuli skutočný stroj o 100 %, takže výmena tabuliek
  bez modelu čakania emulátor **zhorší**.
- **Reč z toho nevyplýva.** Beží z časovača, takže zmena časovania
  inštrukcií ju nehýbe: pri zásahu, ktorý stlačil tempo hudby o 3,9 %,
  sa dĺžka štartovej hlášky zmenila o 0,03 %. Kto siahne na cykly,
  meria oboje zvlášť.

### 6.11 Funkčné klávesy hladujú: emulátor odreže ROM od jej vlastnej fronty

**Uzavreté, celé znenie aj s meraniami je v `HANDOFF-archiv.md`.** Funkčný
kláves nedoručuje kód, ale natlačí text do **vlastnej fronty ROM**
(`fk_table` `$C622`); emulátor zachytával BIOS console input a parkoval
procesor, takže sa ROM k vlastnému textu nikdy nedostala. Zachytávanie aj
parkovanie sú preč, s nimi zanikol režim `default` (dnes sú dva režimy,
prepína `Ctrl+K`) a `QueueText` píše scancodmi cez tabuľku obrátenú z ROM.
Stav drží sekcia 5 (Dva režimy písania, Braillovská klávesnica) a kód
v `machine.cpp` a `emulator_thread.cpp`. Otvorené z tejto témy zostáva
toto:

#### Nedoriešené vedľa toho

**Sonda už formátovanie stíha** — token `?text`. Kým sa čakalo len tichom,
`RunUntilPrompt` sa vracal pol sekundy po stíchnutí konzoly, formát je dlhé
ticho a `seq 15000000 kD7 Y Y` skončil po necelých štyroch miliónoch
inštrukcií, ešte pred jeho koncom. Chýbal spôsob, ako povedať „a teraz už
len bež"; `?text` je presne on. **Odmerané 29. 8. 2026:**
`seq 300000000 kD7 Y Y ?skonceno` prejde celý formát a stroj povie
„formatovani skonceno". Pozor, `tests/README.md` o tom hovoril ešte starým
znením a je opravený v tom istom kroku.

**Meno súboru pri `SAVE` sa do jednoriadkového editora nedostane.** Pôvodné
vysvetlenie sa odvolávalo na parkovanie a procesor stojaci na `D874`;
parkovanie už neexistuje, takže dôvod treba nájsť znovu. **Odmerané
29. 8. 2026** na priečinkovej diskete: `kC5 . "10 PRINT 1~" kC4 . "TESTX~"`
skončí hláškou „syntakticka chyba" a `stav` hlási `suborov=0`, teda sa
neuložilo nič. Stroj pritom **nestojí** — `spin:2000000` ho nájde v hlavnom
rozdeľovači udalostí (`19B4B`, `19B4D`, `19B65`–`19B68`, po ~93 000
priechodov na adresu), teda čaká na vstup ako na kremíku. Makro `save"` sa
teda do riadku nedostane skôr, než sonda dopíše meno. Môže to byť aj
artefakt sondy — tá podáva klávesy bez pustenia a bez ľudského tempa, na
rozdiel od okna; majiteľ mal ukladanie v exterke ručne funkčné (viď archív).

**Doplnené 7. 9. 2026: „dôvod treba nájsť znovu" už neplatí — dôvod je
známy a je to artefakt sondy.** Majiteľ potvrdil, že pri používaní
emulátora ukladanie funguje. `tests/diag_probe.cpp` pritom volá
`machine->QueueKey` a `machine->QueueText` **priamo** (riadky 320, 331,
354), teda nejde ani cez `emulator_thread`, ani cez spracovanie klávesov
v okne — je to iný producent klávesov než tá cesta, ktorou chodia pri
používaní. Zostáva z toho **diera v pokrytí**, nie chyba: ukladanie
v BASICu nemá automatický test. A zostáva jedno pozorovanie, ktoré sa
nemá zamiesť — že rýchly vstup ten scenár zhodí; pri používaní sa to
neprejaví, ale keby sa to raz ozvalo pri rýchlom písaní, toto je prvé
miesto, kam sa pozrieť.

### 6.12 Kapacita diskety je z manuálu, nie z odhadu

**Uzavreté, celé znenie aj s meraniami je v `HANDOFF-archiv.md`.** DPB, ktorý
firmvér vydáva na BDOS 31 (`BDOS.8`), dáva blok 2 KiB, 400 blokov, z toho
štyri drží adresár — teda **396 voľných blokov = 792 KiB a 256 položiek**.
Konštanty vo `virtual_disk.cpp` tomu odpovedali presne; chyby boli v tichých
stratách okolo tej kontroly (nečitateľný súbor, nezhoda kontroly a importu,
useknuté prehľadávanie priečinka) a všetky sú opravené. Drží to `disk_test`
a pravidlo stojí aj v `CLAUDE.md`. Otvorené z tejto témy zostáva jedno:

**Číslovanie logických sektorov.** `BIOS.9` v úvode kapitoly píše, že logické
sektory sú číslované **1 až 40**, kým `VirtualDisk::ReadRecord` berie
0 až 39. Pre BDOS je nula správne — inak by test `com` nemohol prejsť —
ale program, ktorý si volá `bios_setsec` sám, by mal u nás všetko
posunuté o jeden záznam a sektor 40 by skončil chybou.

### 6.13 Časové funkcie — vyriešené

**Uzavreté 26. 8. 2026, celé znenie v `HANDOFF-archiv.md`.** Budík,
odbíjanie hodín, diár aj automatické vypnutie fungujú; RTC procesor
neprerušuje a udalosti dopočítava `UpdateRtcEvents()`. Drží to
`integration_test ROM DISK rtc` a odkazuje sem `hardware-map.md`.
V archíve sú aj vedľajšie zistenia o českej QWERTZ a o parseri času —
dvojbodka ako oddeľovač neprejde.

### 6.14 Uzavreté otázky

| bývalá otázka | výsledok |
|---|---|
| „slabá baterie" blokuje formátovanie | vyriešené, viď sekcia 5 |
| periféria na CSI/O | je to klávesnica IBM PC (`FFh` = reset, `AAh` = BAT passed) |
| nedotknuté bity latchov | pomenované z `IOPORT.LIB`, tri pôvodné výklady boli chybné |
| zápis `ADh` na port `88h` | prah komparátora pre stráženie batérie počas diskovej operácie |
| RTC — mapovanie registrov | emulátor bol **správne**; navrhovaná oprava by hodiny rozbila |
| adresný priestor | maska zmenená na `0x7ffff`, 19 bitov HD64180 |
| ktorý akord je Insert a ktorý Delete | `KB.LIB` mal pravdu: `8Dh` a `8Eh`. Rozhodla vlastná tabuľka ROM pre klávesnicu PC (`DF05`, scancode `52h` → `8Dh`, `53h` → `8Eh`). Emulátor držal variant z `KB.H` a bol opravený. |

### 6.15 Zachovanie RAM medzi behmi — rozhodnuté, nespravené

**Rozhodnuté 26. 8. 2026:** obsah RAM sa bude ukladať do súboru
(pracovne `RAM.dat`) a ďalšie spustenie emulátora nebude tvrdý reset, ale
zapnutie. Tvrdý reset zostane, ale ako **voľba**, nie ako jediná cesta.

Je to model hardvéru, nie pohodlie. Skutočná Eureka RAM ani hodiny nikdy
neodpájala od napájania (`GLOSSARY.TXT`), takže vypnutie bolo pohotovostný
stav a stroj sa po zapnutí vrátil tam, kde používateľ skončil.

Čo pre to už je hotové a čo bude treba:

- Vypnutie je modelované a `EurekaMachine::PowerDown()` je jediné miesto,
  kde stroj zastane — tam patrí uloženie.
- Firmvér má na to **vlastnú značku**: `C45Ah` je pri vypnutí `FFh`
  a boot ju číta na 180CB. Sprístupňuje ju `power_down_marker()`.
  Ak sa RAM obnoví aj s tou hodnotou, ROM sa sama rozhodne pokračovať;
  ak sa RAM vynuluje, urobí plnú inicializáciu. **Nič sa nemusí
  obchádzať.**
- `Reset()` dnes RAM vymaže, a preto je to tvrdý reset. Zapnutie
  s obnovenou RAM bude iná cesta než `Reset()`, nie jeho parameter —
  `Reset()` totiž vracia aj MMU, časovače a periférie do stavu po zapnutí,
  čo treba oboje.
- Otvorené: čo s obsahom, ktorý vznikol pri inej ROM alebo inom
  priečinku disku. Uložená RAM je plná ukazovateľov do FCB a do
  adresára, takže obnoviť ju nad iným diskom nie je bezpečné; súbor by
  si mal niesť aspoň MD5 ROM a identitu disku a pri nezhode ponúknuť
  tvrdý štart.

**Doplnené 6. 9. 2026.** Štyri veci, a prvá z nich opravuje odsek vyššie.

#### Značka `C45Ah` neznamená „pokračuj", ale „nezobúdzaj sa"

Vyššie stojí, že ROM sa podľa nej „sama rozhodne pokračovať". **To
neplatí.** Je to tá istá zámena susednosti za príčinnosť, pred ktorou
varuje `CLAUDE.md` — značka pri vypnutí naozaj vzniká, ale boot ju
používa na niečo iné. Rozobratá celá cesta:

- `18000` nastaví `CBAR=D0h` a `BBR=70h`, takže logická `0000`–`CFFF` je
  RAM od `70000h`. Na `18011` prečíta `IN A,(C)` s `BC=0290h`, teda
  `rtc_status`, a odloží ho na logickú `0040h` (fyz. `70040h`).
- `180C2` z toho bajtu testuje cez `RRCA` bit 0. To je `kRtcEventAlarm`
  (`eureka_io.h`, príloha H): **zobudil ma budík?**
- Bit 0 = 0 → `180D2`, teda `CFC4h` → `1D379` = `schedule_alarm_addr`
  (`D031h` v `SYSEQU.LIB`); potom sa značka na `180D6` zmaže a beh ide do
  hlavného menu. To je normálne zapnutie rukou.
- Bit 0 = 1 → `180C8` `CFEBh` → `1D365` = `service_alarm_addr` (`D02Bh`),
  teda obsluha budíka — a **až potom** `180CF` `JP NZ,CFD9h`. `CFD9h` je
  slot SYSJUMPS na `1D3D9` so skokom na `CD32h`, čo je fyz. `1D132`:
  **rutina vypnutia**, tá istá, ktorá tam značku napísala.

Mapovanie `CC00h` ↔ `1D000h` je isté z dvoch strán: prenos RAMCODE na
`18025` (`D000h`/banka 01 → `CC00h`/banka 07, 400h bajtov) a `1D2B8`,
ktorá je `.skssp` (`POP HL` / `CALL CEC4h` / `PUSH HL`) a cez `CEC4h` =
`1D2C4` prechádza nulou ukončený reťazec.

Zmysel je teda opačný: hodiny bežia aj vo vypnutom stroji, budík ho
zobudí, firmvér ho obslúži a podľa `C45Ah` zistí, že sa má **zase
vypnúť**. Že sa Eureka vrátila tam, kde používateľ skončil, nie je
zásluha tejto značky — je to tým, že RAM sa nemazala a `magic` na
`C45Bh` sedel.

Pre uchovanie RAM je to dobrá správa, lebo je to ešte jednoduchšie:
**stačí obnoviť RAM a naštartovať od nuly.** Boot prebehne celý, `magic`
bude sedieť, trvalé dáta na `C43Ch`–`C508h` sa nevymažú a stroj sa ohlási
bez „inicializace eureky". Značku netreba ošetrovať — ale treba vedieť,
čo robí: keby sa RAM obnovila v okamihu, keď `rtc_status` hlási budík,
stroj by sa po nej hneď vypol.

**Doplnené 11. 9. 2026 (6.32):** výklad „obslúži a zase vypne“ platí len
pre budík, na ktorý nikto neodpovie. `JP NZ,CFD9h` na `180CF` číta `C45Ah`
až **po** obsluhe, a kláves, ktorý budík odklikne, značku vynuluje — stroj
potom zostane v hlavnom menu. Zmerané sondou aj ručne.

Odtiaľ je aj samotná veta: `18132` volá `CFDCh` (`.skssp`) s reťazcom
`inicializace eureky` na `18135`. Spúšťa ju jediná podmienka — `18126`
porovná `(C45Bh)` s `55AAh`. Pri nezhode navyše `1805E` vymaže
`C43Ch`–`C508h`, `LDIR` s `BC=00CDh`, čo je presne blok, ktorý `MEMORY.5`
volá „Permanent data storage".

#### Osem bajtov je mimo tých 64 kB

`rtcRam_` (`machine.h`) je RAM v obvode hodín, porty `190h`–`197h`: čas
a dátum najbližšieho budíka. Nie je v `memory_`, takže uloženie RAM ho
nezachytí a `Reset()` ho vymaže. Patrí do toho istého súboru — je to
malé a preto sa na to ľahko zabudne, a strata budíkov by bola tichá.

#### Zmeškané budíky treba odmerať

Hodiny berú čas z hostiteľa (`CurrentRtcRegisters` volá `localtime_s`),
takže medzi dvoma behmi môže ubehnúť ľubovoľne veľa času. `SYSJUMPS.11`
popisuje `schedule_alarm` slovami „process past due alarms". Čo urobí
s budíkom starým týždeň, **nevieme a treba to odmerať sondou**, nie
odhadnúť.

**Odmerané 10. 9. 2026 — „nevieme" už neplatí.** Zmeškaný budík sa ani
nezahodí, ani nezazvoní sedemkrát: pri zapnutí sa **prepíše na najbližší
budúci výskyt**. Čas, mesiac a rok zostávajú, mení sa deň, a mení sa aj
mesiac, keď treba. Stroj sa pritom nevypne — `C45Ah` je po štarte `00`, teda
boot išiel vetvou „zapnuté rukou" (`180D2` → `schedule_alarm_addr`), presne
ako hovorí podsekcia o značke vyššie.

**Neozve sa ani nič, a to je merané reproduktorom, nie prepisom reči.** Po
`zapni` príde 24 000 vzoriek s rozkmitom `0..0`, teda úplné ticho; pre
porovnanie samotné vypnutie povie „konec" a dá rozkmit `−27789..29542`.
Prepisom reči by sa to tvrdiť nedalo: ten berie len bajt, ktorý dostane
rečový stroj na `0103h`, a oznámenie času tadiaľ nejde (bead ea4-l16), takže
prázdny prepis nie je dôkaz ticha.

**Doplnené v ten istý deň:** oznámenie času už v prepise je, záznam berie aj
`.spconv` (sekcia 3). Reproduktor tým neprestáva byť správne meradlo: kliky
cez `.spchar` do prepisu zámerne nejdú, takže prázdny prepis dôkaz ticha nie
je ani teraz.

Tri behy, budík vždy nastavený rukou cez F2 → Shift+F3, potom `vypni`,
posun hodín a `zapni`:

- hodiny o týždeň dopredu, denná doba **ešte len príde** (budík 09:04,
  hodiny 17. 9. 09:01): `den` `0A` → `11`, teda **dnes** 17. 9.
- hodiny o týždeň a dve hodiny (budík 09:05, hodiny 17. 9. 11:01):
  `den` `0A` → `12`, teda **zajtra** 18. 9.
- hodiny o štyridsať dní (budík 09:06, hodiny 20. 10. 11:03): `mes`
  `09` → `0A` a `den` `0A` → `15`, teda **21. 10.** Prechod cez mesiac
  firmvér zvláda sám.

Maska `rtc_mask` zostáva vo všetkých troch `01`, `rtc_status` `00`.
Pre uchovanie RAM z toho plynie, že snímka stará týždeň je v tejto veci
verná: budík z nej ožije nastavený na najbližší termín, nie na dávny.

**Pri tom meraní sa opravila aj jedna vec o pár riadkov vyššie.** Slot
`CFEBh` nie je `.service_alarm`, ale **`.service_alarm1`** — „service alarm
disregarding RTC status" (`SYSJUMPS.11`). Cieľ `1D365` platí ďalej, mení sa
meno. Doložené odčítaním celej tabuľky SYSJUMPS z ROM: `SYSEQU.LIB` má
`njumps = 32` a `jump_table = system_addr − 96`, teda základ `CFA0h`
(fyz. `1D3A0`), a poradie slotov sedí s `SYSJUMPS.LIB` na troch kotvách —
`CFC4` → `CF79` je `.schedule_alarm`, `CFD9` → `CD32` je `.go_to_sleep`
a `CFDC` → `CEB8` je `.skssp`. `.service_alarm` je `CFD3h` → `CF54h`.
Zmysel odseku sa nemení, skôr sa dopĺňa: boot si stav hodín prečítal už na
`18011`, takže volá práve tú verziu, ktorá sa naň druhý raz nepýta.

Merať sa to dá len sondou a len s dvoma vecami, ktoré predtým nemala:
`cas:+7d` posunie hodiny, ktoré RTC hlási (skutočný týždeň sa vyčkať
nedá), a `budik` vypíše hodiny vedľa alarmových registrov. Obe pribudli
týmto meraním; podrobnosti sú v `tests/README.md`.

#### Disketa je krytá — odsek vyššie je prísnejší, než treba

Veta o ukazovateľoch do FCB a adresára je z 26. 8., teda spred uzavretia
6.17. EurekaDOS sa k disku prihlasuje sám a robí to **dátovo**: 64
kontrolných súčtov adresára (`CKS = 256/4` v `BDOS.8`, „so that disk
changes can be detected"). Obnovená RAM s cudzími súčtami je pre firmvér
ten istý vstup ako disketa vymenená za behu, a tá je odmeraná. Zostáva
teda **MD5 ROM**; identitu disku súbor niesť nemusí. Ostrá zostáva jedna
trhlina, tá istá ako v 6.17: relogovanie je overené ručne a raz, a na
ceste „za behu", nie „cez vypnutie a zapnutie".

#### Kedy sa neukladá

`PowerDown()` je riadené vypnutie a stroj v ňom stojí. Zatvorenie okna
krížikom uprostred zápisu na disketu je na skutočnom stroji odpojenie
batérie — a po ňom prišla presne „inicializace eureky". Neuložiť snímku
v tom prípade teda nie je diera, je to vernosť hardvéru.

**Opravené 9. 9. 2026:** tu stálo „vybratie batérie“. Eureka má **jednu**
batériu a nevyberá sa — je to zaliaty olovený gél, ktorý sa dá odpojiť
vypínačom v dierke na ľavej hrane (`HARDWARE.1`, `INSTALL.2`). Zmysel
odseku sa nemení, len meno úkonu.

#### Čo tým prestalo platiť inde

Tú istú nesprávnu vetu o `C45Ah` nesú ešte tri miesta, a **nie sú
opravené**: `hardware-map.md` (odsek o rutine `1D132`), komentár pri
`EurekaMachine::power_down_marker()` v `machine.h` a komentár pri
`WM_EMU_POWERED_OFF` v `main_window.cpp`. Text `MessageBox`-u pre
používateľa je v poriadku — hovorí o RAM a hodinách pod napätím, nie
o značke.

**Doplnené 7. 9. 2026: „nie sú opravené" už neplatí, opravené sú všetky
tri.** Spravili to commity `067cb19` (mapa hardvéru) a `fab9c38` (HANDOFF)
zo 6. 9. 2026. `hardware-map.md` nesie pri rutine `1D132` výslovné
„Neplatí, čo tu stálo do 6. 9. 2026", `machine.h` pri
`power_down_marker()` aj `main_window.cpp` pri `WM_EMU_POWERED_OFF`
hovoria, že návrat tam, kde používateľ skončil, robí prežitá RAM
s `magic` `55AAh` na `C45Bh`, a nie tá značka. Odsek vyššie zostáva, lebo
popisuje stav, v akom to bolo 6. 9. ráno; sám o sebe sa však čítal ako
zoznam nespravenej práce a raz už takmer poslal agenta opraviť hotové.

**Doplnené 9. 9. 2026:** odrážka „`Reset()` dnes RAM vymaže… zapnutie
s obnovenou RAM bude iná cesta než `Reset()`“ je **spravená** —
`EurekaMachine::PowerOn()` existuje a je odmeraný, viď dodatok k 6.31.
Zvyšok tejto sekcie platí ďalej: snímka do súboru, MD5 ROM ani osem bajtov
`rtcRam_` v nej zatiaľ nie sú.

**Doplnené 10. 9. 2026: „zatiaľ nie sú" už neplatí — snímka do súboru je
hotová (beady `ea4-aip.2`, `ea4-te9`, `ea4-9fq`).**

- `EurekaMachine::SaveSnapshot` / `LoadSnapshot` (`machine.cpp`) serializujú
  64 KiB RAM od `kRamBase`, osem bajtov `rtcRam_` a v hlavičke **MD5 ROM**
  (`src/md5.*`, RFC 1321, overené proti trom štandardným vektorom). Formát
  nesie verziu v magicu `EA4RAM01`; staršia alebo poškodená snímka sa číta
  ako `kCorrupt`, snímka spod inej ROM ako `kRomMismatch` a v oboch
  prípadoch sa **nič neobnoví**.
- `LoadSnapshot` pri `kOk` naplní `memory_` aj `rtcRam_`; volajúci potom
  volá `PowerOn()`, nie `Reset()`. Presne cesta z tejto sekcie: boot
  prebehne celý, `magic` `55AAh` na `C45Bh` sedí, `1805E` sa preskočí,
  stroj sa ohlási bez „inicializace eureky".
- Hostiteľ (`main.cpp`): pri štarte skúsi `Settings::SnapshotFile()`
  (`config/` vedľa EXE, inak `%APPDATA%\EurekaA4\pamat.bin`) a súbor
  **skonzumuje** — prečíta a zmaže. Preto holé zavretie okna (na stroji
  odpojenie batérie) nabehne na tvrdo a čerstvá snímka vzniká len ďalším
  skutočným vypnutím. Zápis je pri ukončení, `iff machine->powered_off()`;
  pri nezhode MD5 sa `main.cpp` pýta `MessageBox`-om (Áno = tvrdý štart,
  Nie = koniec), lebo je to rozhodnutie používateľa a konzola nemusí byť.
- Drží to `integration_test … snimka`: round-trip RAM aj `rtcRam_` (cez
  bajtové porovnanie znovuuloženej snímky, teda nezávisle od MMU), že
  `PowerOn` na obnovenej RAM nepovie „inicializace" a studený `Reset`
  áno, a všetky tri odmietnutia (`kMissing`, `kCorrupt`, `kRomMismatch`).

**Doplnené v ten istý deň: prepínač je tiež hotový (`ea4-dh1`).** `Settings`
nesie `keep_ram()` (kľúč `zachovat-ram` v `nastavenia.txt`, predvolene
zapnuté; chýbajúci kľúč = zapnuté, aby starší súbor ticho neprestal
uchovávať RAM). V dialógu Nastavenia je skupina „Pamäť" s jedným
zaškrtávacím políčkom. `main.cpp` gejtuje načítanie aj zápis snímky týmto
prepínačom; keď sa vypne — v dialógu alebo ručnou úpravou súboru —
`main_window.cpp` aj `main.cpp` zmažú `pamat.bin`, aby zapnutie o mesiac
neobnovilo dávny stav. Bez potvrdzovacieho dialógu: prepínač je dosť
výslovný úkon (rozhodnuté 10. 9. 2026). Drží to `settings_test`
(`KeepRamSwitchRoundTrips`).

**Tým je 6.15 celá spravená.** Zostáva len doloženie: relogovanie disku po
ceste „vypnutie a zapnutie" je overené len ručne (`ea4-rwe`).

**Doplnené (majiteľ): zavretie okna sa pýta, keď by stav zahodilo.** Keď je
`keep_ram()` zapnuté a stroj beží, `WM_CLOSE` sa najprv spýta
(`MainWindow::ConfirmClosingWithoutPowerDown`, `MB_YESNO`, predvolene Nie) —
zavretie bez vypnutia je odpojenie batérie a snímka sa nezapíše. Vypnutý
stroj sa nepýta (snímku zapíše ukončenie) ani vypnutý prepínač (niet čo
strážiť). Cesta chyby disku ho obchádza cez `forceClose_`: worker už stojí,
niet ako vypínať. Vypnutie prepínača v Nastaveniach potvrdenie **nemá** —
je to dosť výslovný úkon (rozhodnuté s majiteľom). Tým prestala platiť veta
v `CLAUDE.md`, že cesta `F12` → Súbor → Skončiť je „bezpodmienečná"; upravená
je na to, že tú cestu Eureke nikdy neberie, ale otázku môže položiť.

### 6.16 Lupanie: chýbal väzobný kondenzátor — opravené

**Uzavreté 26. 8. 2026, celé znenie v `HANDOFF-archiv.md`.** Držaná
hodnota DAC, ktorú aplikácia nechá za sebou, sa už na výstup nedostane;
filter je v `machine.cpp` a drží ho `integration_test ROM DISK dc`.

### 6.17 Výmena diskety za behu — EurekaDOS sa preloguje sám

**Uzavreté 26. a 28. 8. 2026, celé znenie v `HANDOFF-archiv.md`.** Výmena
za behu nepotrebuje reset ani žiadny signál od hardvéru — doložené
manuálom aj meraním na bežiacom emulátore. Odkazuje sem `machine.cpp`,
`main_window.cpp` a `disk_test.cpp`. Ostáva jedna trhlina: overené je to
ručne a raz, testom tá cesta krytá nie je.

### 6.18 GUI — okno, klávesnica a NVDA

**Uzavreté, celé znenie aj s meraniami je v `HANDOFF-archiv.md`.** Emulátor
má vlastné okno Win32; `src/win/` je tenká obálka, ktorá o emulátore nevie
nič, stroj a zvuk bežia na vlastnom vlákne (`src/emulator_thread.*`),
dialógy sú skutočné dialógy zo šablón v `.rc` a konzola prestala byť
povrchom — je to diagnostická plocha. Skratky sú dvojhmatové: `F11`
a potom `Ctrl+písmeno`, takže okno berie Eureke len `F12`, `F11`
a `Shift+F11`. Nad oknom stroja spí NVDA vďaka doplnku v `nvda-addon/`,
a nespí nad ponukou ani dialógmi. Pravidlá z toho stoja v `CLAUDE.md`
(rozvrstvenie GUI, klávesnica a kto ju vlastní, čítačka obrazovky ako
tretí hráč, konzola je diagnostika) a v README. Otvorené z tejto témy
zostáva toto:

#### Čo zostáva

- **Ktoré nastavenia majú beh prežiť.** Súbor `nastavenia.txt` už existuje
  a drží poslednú disketu, sloty a zámky (6.22), ale dva prepínače
  samotného dialógu `Nastavenia` — režim klávesnice a diagnostika — sa
  neukladajú. Čo z nich má beh prežiť, je tá istá otázka ako uchovanie RAM
  (6.15) a rozhodne sa s ňou.

  **Hotové 16. 9. 2026 (ea4-0vw).** Odsek vyššie už v prvej časti neplatí:
  režim klávesnice sa ukladá, diagnostika zámerne nie. V súbore je ako
  `klavesnica=braillovska` alebo `klavesnica=externa`; `main.cpp` ho po
  `settings.Load()` podáva do `emulator.Start` a prepínače `--braille`
  a `--pc` ho prebijú, lebo výslovná odpoveď tohto spustenia má prednosť
  pred odpoveďou minulého. Zapisuje ho `MainWindow::RememberMode`
  z `WM_EMU_STATE` — to je jediné miesto, kam dôjdu **všetky štyri** cesty
  zmeny (dve položky ponuky Klávesnica, `Ctrl+K` a dialóg `Nastavenia`),
  a vlákno tú správu posiela až po skutočnej zmene; režim, v ktorom už je,
  zahodí skôr. Zápis je strážený porovnaním, lebo tá istá správa nesie aj
  zmenu diagnostiky — bez stráže by každé `Ctrl+D` prepísalo súbor.
  Diagnostika sa neukladá preto, že zapamätaná by pri štarte otvorila
  konzolu, ktorú si nikto nevyžiadal, a tá vezme fokus.
  Štart **nič nepípne** (rozhodnuté majiteľom): tón patrí zmene a štart
  zmena nie je, režim nesie titulok a `NVDA+T` naň odpovie kedykoľvek.
  Drží to `KeyboardModeRoundTrips` v `settings_test`, vrátane toho, že
  chýbajúci kľúč **aj neznáma hodnota** znamenajú externú klávesnicu:
  prečítané ako braillovská by to podalo klávesnicu, ktorá píše len
  akordmi, a nikde by nestálo prečo.

**`PressMembraneKey` zahadzuje bit Altu pri funkčných klávesoch.**
Tretia z troch vecí, ktoré našla kontrola dokumentácie 27. 8. 2026 —
prvé dve sú uzavreté a v archíve, táto nie. Pre `kind == 0xc0`
nastavuje len `row1` a `row2`, `row0` nie,
takže medzerník sa na drôt nedostane. Odmerané: `kE3` (Alt+F4) sa správa
presne ako `kC3` — **vojde do komunikácie namiesto toho, aby ju pomenovala**,
a Escape za tým povie „ukoncit?“. V braillovskom režime je teda celá rada
`Alt+F1`–`Alt+F8` nedostupná a mlčky robí niečo iné, než sa žiada. Na
skutočnom stroji je medzerník + F1 stlačiteľný a dekodér na `1D4F3` z neho
bit 5 (`k_alt`) poskladá, takže model je tu chudobnejší než hardvér.
Oprava je asi na jeden riadok (`frame.row0 = alt ? 0x80 : 0` aj pre funkčné
klávesy) plus otázka, či treba predsadiť ten istý rámec so samotným
medzerníkom, aký sa dnes predsadzuje kurzorom (`kModifierMs`). **Neopravené
— treba to odmerať, nie odhadnúť.**

**Opravené 11. 9. 2026, ale nie tým jednoriadkovým patchom (ea4-v1j,
ea4-cb4).** `PressMembraneKey` ostal presne taký, aký je opísaný vyššie —
namiesto záplaty na ňom dostal medzerník (a celých devätnásť ostatných
klávesov) živú vrstvu `HoldMembrane`, ktorá sa ORuje do portov nezávisle
od frontu rámcov, presne ako dnes `membraneShift_`. Odmerané:
`medzerník+F4` cez `HoldMembrane` povie „komunikace" a **nevojde** —
nasledujúci samostatný F1 dá „zaznamnik" (hlavné menu), kým po samotnom
F4 dá F1 „vloz telefonni cislo" (vnútri komunikácie). Rada `Alt+F1`–`Alt+F8`
je tak dostupná cez živú vrstvu; `PressMembraneKey`/`kE3` (jeden hotový
akord poslaný naraz) má bug ďalej — nie je dôvod ho opravovať, lebo
interaktívna cesta ním už nejde.

Pri hľadaní alternatív sa našlo aj toto, a môže sa hodiť: tabuľka
rozšírených klávesov na `1DFD6` je zoznam dvojíc ukončený `00 00`
(kurzory, Home, End, PgUp, PgDn, Insert, Delete, pravý Ctrl, pravý Alt,
PrtSc, Enter z numerickej časti). **Nie je v nej `5Dh`**, teda kláves
Aplikácie, ani `5Bh`/`5Ch` — v roku 1992 neexistovali. Kláves Aplikácie
je jediný naozaj voľný kláves, aký sa našiel; nepoužil sa preto, že na
mnohých klávesniciach chýba, ale ak by raz bolo treba tretiu skratku,
toto je miesto, kde ju hľadať.

Ostáva otvorené a **neodložené len preto, že sa naň zabudlo**:

- **Medzerníkové akordy na ovládanie emulátora** — nápad používateľa,
  27. 8. 2026, a fyzikálne sedí: na dvadsaťklávesovej klávesnici niet Ctrl,
  takže medzerník je prirodzený modifikátor, a hardvér ho tak aj vidí
  (`IOPORT.H`: medzerník je bit 7 portu `bkb_row0`, teda ten istý port ako
  šesť bodov; `BrailleBit` mu už dáva `0x80`).

  **Pozor, priestor nie je prázdny.** `C-DEVEL.G:39` hovorí: „Control D
  (Dots 4,5,6 held down with the Space Bar, then type a D)" — Space+body456
  je Eurekin **vlastný prefix Ctrl** a potom sa čaká celé písmeno, čiže je
  to dvojkrokový stavový automat. A shift s holým medzerníkom je Escape
  (`1D52F`). Voľnú množinu treba **odmerať, nie odhadnúť**.

  Obava z chŕlenia medzier neplatí: hostiteľ držaný medzerník do stroja
  neposiela vôbec. Akord sa skladá, kým sú prsty dole, a `PressBraille`
  vloží jeden stlačovací a jeden púšťací rámec bez akéhokoľvek stavu.
  Rozhodovací bod pre „je to náš akord?" už teda existuje — pri pustení
  posledného prsta, bez časovača a bez prahu. Cena je tá, čo platí už dnes:
  holý medzerník sa doručí až pri pustení.

  Návrh: **len v braillovskom režime.** V externom režime sa prsty pri písaní
  prekrývajú a medzerník ešte dole pri ďalšom písmene by robil falošné
  akordy; navyše tam Ctrl je. A každý emulátorový akord sa musí ozvať inak
  než Eurekiným hlasom, inak sa „akord nefunguje" nedá odlíšiť od „akord
  zožral stroj" — oboje je ticho.
- **Sonda na to, čo Eureka z klávesnice zje** — dvojica krátkych `.COM`
  programov, ktoré ohlásia prijatý kód a skončia na Escape. Cesta je
  v manuáli celá:

  - surový kód: `ld b,2` (`dev_kb`, `DEVICE.LIB:24`) a BIOS volanie **18**
    (`BIOS_DEV_INPUT`, `BIOS.H`); `ld hl,(0001h)` dá WBOOT, teda položku 1,
    takže položka 18 je `HL+51`. Kód klávesy príde v `A` — hodnoty z `KB.H`.
  - čo z toho pustila konzola: `BIOS_DEV_CONTROL` = 22, `B` = 11
    (`dev_console`), `C` = 7 (`con_ctl_getkey`).

  Cenná je až **odlišnosť** oboch: čo ovládač vyrobí a konzola nedoručí,
  to zjedla konzola. Výpis cez BDOS 2 ide na `cono_list`, takže stroj to aj
  povie a náš stub BIOS funkcie 4 to zapíše na hostiteľskú konzolu — jeden
  program pre skutočný stroj aj pre nás. Assembler netreba, stačí Python
  v `tools/`, a výsledok sa dá overiť disassemblovaním cez `z180dis.py`.

  **Obmedzenie, ktoré treba povedať vopred:** takto sa zmeria len to, čo je
  voľné, kým beží program. Hlavné menu je tretia vrstva nad oboma sondami
  a môže zjesť aj to, čo obe videli. Druhú polovicu musí rozhodnúť
  `diag_probe` a počúvanie priamo v menu.

**Neodskúšané v ostrej relácii s NVDA zostáva:** `NVDA+T` počas spánku, dialógy
(Nastavenia, Pomocník) a prechod cez `Shift+F11` tam a späť. Zo zdrojákov aj
z merania to vychádza, ale povedané to nie je.

**Doplnené 7. 9. 2026: toto už neplatí.** Majiteľ potvrdil, že doplnok sa
od začiatku používa aj ladí v ostrých reláciách s NVDA a funguje, ako má.
Odsek vyššie zostáva ako záznam toho, čo bolo overené len zo zdrojákov
v čase, keď vznikol; ako zoznam nedokončenej práce sa čítať nemá.

### 6.19 Podpriečinky sa ignorujú — rozhodnuté

**Uzavreté 28. 8. 2026, celé znenie v `HANDOFF-archiv.md`.** CP/M adresáre
nemá, podpriečinky sa preskakujú a nehlási sa to nijako; dôvod je
v `CLAUDE.md` v sekcii o konzole.

### 6.20 Ctrl+D vyzeral pokazene, keď bol len vypnutý — opravené

**Uzavreté 28. 8. 2026, celé znenie v `HANDOFF-archiv.md`.** O hlásení
rozhoduje okno a ide do `MessageBox`, nie do konzoly, ktorá v tej chvíli
ešte neexistuje; pravidlo je v `CLAUDE.md`.

### 6.21 clangd hlásil chyby v kóde, ktorý sa prekladá čisto

**Uzavreté 28. 8. 2026, celé znenie v `HANDOFF-archiv.md`.** Rieši to
`.clangd` v koreni repozitára a hovorí o ňom `CLAUDE.md` v Zostavení.

### 6.22 Správa diskiet — celá spravená

**Uzavreté 9. 9. 2026, celé znenie v `HANDOFF-archiv.md`.** Hotové je
všetko: perzistencia a výmena diskety za behu, ponuka `Disketa`, sloty
rýchlej voľby, dialóg `Nová disketa`, zámok proti zápisu aj rozdeľovač
kolekcie so sprievodcom. Vrstvy stoja v `src/disk_stash.*`,
`src/disk_layout.*`, `src/disk_split.*` a `src/settings.*`, pravidlá
o nich v `CLAUDE.md` a držia ich `disk_test`, `settings_test`
a `integration_test`.

V archíve má sekcia nadpis „návrh, sčasti spravené“ — taká vznikla a text
sa pri sťahovaní neupravuje. Že je celá spravená, hovorí jej vlastná
posledná veta.

### 6.23 Export orezával programy BASICu na prvom 1Ah — opravené

**Uzavreté 29. 8. 2026, celé znenie v `HANDOFF-archiv.md`.** Textové typy
sú allowlist v `VirtualDisk::IsTextType`, `BAS` v ňom nie je a drží to
`disk_test` kontrolou `klasifikacia_typov_je_pribita`. Pravidlo je
v `CLAUDE.md`, podrobnosti aj v `tests/README.md`.

### 6.24 Disketa je objekt, nie recept — hromadné kopírovanie to odhalilo

**Uzavreté, celé znenie aj s meraniami je v `HANDOFF-archiv.md`.** Slot
rýchlej voľby s `*pamat` vyrobil pri každom vložení novú prázdnu disketu,
takže Eurekine hromadné kopírovanie prišlo pri druhej výmene o to, čo samo
zapísalo — tichá strata dát. Sloty 1 až 9 sú odvtedy miesta, kde diskety
**sú** (`src/disk_stash.*`), priečinková disketa sa neodkladá, polička na
disketu bez slotu neexistuje a na disketu v pamäti bez slotu sa pýta
`MainWindow::ConfirmLosingDiskette`. Pravidlo stojí v `CLAUDE.md`
(rozvrstvenie GUI) a drží ho `disk_test`. Otvorené z tejto témy zostáva
toto:

**Druhý dôsledok toho istého pravidla, doplnený 30. 8. 2026: 6.26,
uzavretý 31. 8. 2026 spolu s 6.28.** Ak je disketa objekt, musí sa dať
zamknúť bez ohľadu na médium. Dialóg slotov to popieral zošedeným
políčkom; teraz to políčko platí pre disketu v ktoromkoľvek slote a šedé
je len tam, kde ešte žiadna disketa nie je.

**Čo zostalo nedokončené:** režim `kopia` v `integration_test`, ktorý mal
ten scenár prehrať sám. Zápasil som so synchronizáciou reči — hlášky
chodia oneskorene, kláves reč preruší a „ano“ + „kopíruji“ dá reťazec,
v ktorom sa dá nájsť „okop“ — a nedotiahol som ho, tak som ho zahodil,
aby v sade nezostal zlyhávajúci test. Zásobník drží `disk_test`, ale
**celá cesta cez ROM zatiaľ testom krytá nie je**; kto sa k tomu vráti,
pozor na tie tri pasce a na `diag_probe`, ktorý na to má tokeny (`?text`,
`slot1`/`slot2`, `stav`, `spin:`).

### 6.25 Ponuka `Disketa` mala jeden úkon pod tromi menami — zjednotené

**Nahlásil majiteľ 30. 8. 2026** vetou, ktorá je presnou diagnózou: „na
začiatku sme mali vložiť disketu, potom pribudlo *nová disketa*, akoby to
isté, potom *správa diskiet*“. Nebol to zmätok používateľa, bol to zmätok
ponuky.

**Príčina.** V ponuke sú vecne len štyri úkony — vložiť, vysunúť,
zamknúť, uložiť kópiu — a k tomu upratovanie slotov. Vkladanie ale malo
**tri vchody s tromi menami** (`Vložiť z priečinka…`, `Nová disketa…`,
sloty), hoci sa líšili iba tým, **odkiaľ disketa príde**. „Nová disketa“
je zdroj, nie úkon, a keďže bola pomenovaná slovesom, čítala sa ako
samostatná schopnosť programu.

Doložené to bolo tým, že sa obe cesty stretávali na tom istom riadku:
`ID_DISK_INSERT` aj `NewDiskDialog::Kind::kFolder` volali
`PostMountDisk(folder, settings_.disk_locked(folder))` a obe najprv
`ConfirmLosingDiskette()`. Nie podobné — **identické**. A prepínač, ktorý
to robil, sa volal „Z existujúceho priečinka“ v dialógu s titulkom **Nová
disketa**, teda dialóg si protirečil s vlastným menom.

Druhá polovica bola inflácia podstatných mien: **disketa, priečinok,
pamäť, slot, rýchla voľba** — päť slov na dve veci. „Priečinok“ je
mechanizmus, ktorý presiakol do reči; pre používateľa je priečinok
disketa. A „slot“ so „rýchlou voľbou“ boli dve mená na jedno miesto, čo
je v ponuke, po ktorej sa chodí sluchom, o jedno meno viac.

**Čo sa spravilo (rozhodol majiteľ, obe otázky):**

- Ponuka pomenúva úkon, nie zdroj: `Vložiť disketu z priečinka…`
  (`Ctrl+I`) a `Vložiť novú disketu…` (`Ctrl+M`). Rozdiel v mene je
  presne ten, čo je v skutočnosti.
- `Kind::kFolder` zrušený aj s prepínačom, s vetvou v `ID_DISK_NEW`
  a s druhým `win::PickFolder` v `NewDiskDialog::OnCommand`.
  `IDC_NEW_FOLDER` (1030) sa **nepriraďuje znovu**, aby stará `.res` nemohla
  padnúť na to číslo.
- Zvyšné tri prepínače menujú to, na čom záleží — či sa disketa uchová
  sama: `Trvalá`, `Dočasná v pamäti`, `Dočasná v pamäti,
  nenaformátovaná`. Predtým menovali mechanizmus („nový prázdny
  priečinok“).

  **Opravené v ten istý deň po pripomienke majiteľa:** prvé znenie toho
  prostredného bolo „Dočasná v pamäti — **zanikne s emulátorom**“, a to
  je nepravda. Disketa v pamäti zanikať nemusí: `Ctrl+U` ju uloží
  kedykoľvek a `main.cpp` sa pri ukončení pýta sám. Vyhlásiť stratu za
  hotovú vec je varovanie pred niečím, čomu program práve bráni. Znie to
  preto `prežije, len keď ju uložíte do priečinka` — povie aj podmienku,
  aj to, že cesta existuje. Poučenie je všeobecné: **popiska, ktorá raz
  prestrelí, je popiska, ktorej sa nabudúce neverí** — a v programe, kde
  je zvuk a text celé rozhranie, je to draho zaplatené.
- „Rýchla voľba“ zrušená ako druhé meno slotu: `Spravovať sloty…`,
  titulok `Sloty s disketami`, a rovnaké slovo v hláškach o prázdnom
  a stratenom slote.

  **Tlačidlo `Priradiť priečinok…` sa najprv premenovalo na `Priradiť
  disketu…` a majiteľ to ešte v ten deň zamietol — právom.** Volá
  `win::PickFolder`, takže vie do slotu dať len cestu; značka `*pamat` sa
  doň dostala jedine tlačidlom `Sem vloženú disketu`, a to len keď
  disketa v pamäti zrovna bola v mechanike. Meno teda sľubovalo obe veci
  a robilo jednu — tá istá chyba ako „zanikne s emulátorom“ o pár riadkov
  vyššie, len ju tentoraz spravil agent pri „zjednocovaní slovníka“.
  **Zjednocovanie mien je zmena významu, nie kozmetika**; nové meno treba
  overiť proti tomu, čo funkcia naozaj robí.

- **Slot má odvtedy štyri tlačidlá**, pribudlo `Sem novú v pamäti`
  (`IDC_SLOT_NEWRAM`), ktoré slotu dá `kSlotRam`. Nič nevyrába — disketu
  v pamäti robí worker pri prvom vložení slotu, ktorý ešte nič odložené
  nemá.

  Majiteľ navrhol použiť na to rovno dialóg `Nová disketa`. Zamietnuté
  z vecného dôvodu, nie z vkusu: **slot unesie práve dve veci** (cesta
  alebo `*pamat`), kým ten dialóg ponúka tri druhy, z ktorých
  nenaformátovaná do slotu nesmie (`RefreshEnabled` jej výber slotu
  zošedí) a „trvalá — nový priečinok“ si slot priradí vlastným
  comboboxom. Zdieľaný dialóg by pred dvojznačnú otázku postavil štyri
  voľby, z toho dve slepé.

  Pri tom sa opravila aj **kolízia mnemoník**, ktorá tam bola predtým:
  `&Vyprázdniť slot` a `Sem &vloženú disketu` majú pre Windows to isté
  písmeno, takže `Alt+V` medzi nimi prepínal namiesto stlačenia. Teraz
  `Vyprázdniť s&lot`.

**Čo to stálo.** Priečinkovej diskete sa už nedá priradiť slot v tom
kroku, v ktorom sa vkladá — combobox v dialóge zostal, ale ten dialóg
priečinkové diskety už nevkladá. Náhrada je `Spravovať sloty → Sem
vloženú disketu`. Pozor, netýka sa to diskety v pamäti: tá sa do slotu
priraďuje pri vytvorení ďalej, a to je vec, ktorú si majiteľ vypýtal
28. 8. 2026 (viď 6.22, „Slot unesie aj disketu v pamäti“).

**Vedľajší nález, opravený v `SlotsDialog::SetSlotAndRefresh`.** Slot,
ktorému sa v jednom sedení dialógu najprv dalo `Sem vloženú disketu`
a potom sa prepísal (`Priradiť priečinok…`), si nechal `assignedCurrent_`.
Okno preto po OK zapísalo do nastavení priečinok a **zároveň** poslalo
`PostAssignSlot` na to isté číslo, takže worker pod ním zaparkoval disketu
z pamäte. A keďže `kInsertSlot` berie zo zásobníka **skôr**, než sa pozrie
na cestu (`emulator_thread.cpp`, vetva `stash.Take(command.slot)`), `Ctrl+3`
by podal disketu z pamäte, kým ponuka menuje priečinok. Tichý rozpor medzi
nastaveniami a zásobníkom, nie strata dát. Sľub sa teraz ruší pri každom
prepísaní slotu; `Vyprázdniť slot` už na to nepotrebuje vlastnú vetvu.
Nebolo to zadanie — je to chyba, ktorá tam bola predtým, a opravená je
preto, že nové tlačidlo by ju zdedilo. Testom krytá nie je.

**Čo sa nemenilo, hoci to tak vyzerá.** Zámok je aj v ponuke (`Ctrl+Z`,
médium v mechanike), aj políčkom v dialógu slotov (vybraný slot). Nie sú
to dve mená na jednu vec, sú to dve rôzne otázky o tej istej vlastnosti
a `Settings::SameDisk` ich drží v zhode.

Overené: `build.bat` bez varovania, dvanásť `PASS`, a všetky nové
reťazce nájdené v hotovom EXE v UTF-16 vrátane diakritiky (`Trvalá — nový
priečinok`, `Sloty s disketami`, `Vložiť disketu z priečinka`), staré
(`Z &existujúceho priečinka`, `Rýchla voľba`) v ňom už nie sú.
Kontrolovaná bola aj negatívna vzorka, aby test naozaj meral.

### 6.26 Zámok diskety v pamäti — model to vie, dialóg to popiera — opravené

**Nahlásil majiteľ 30. 8. 2026** vetou, ktorá je pravidlo, nie pripomienka:
*„Ak disketa je objekt, MUSÍ sa dať zamknúť. A je jedno či je/nie je
v pamäti.“* Je to priamy dôsledok 6.24 a treba ho brať tak.

**Model to už tak robí a je to odmerané.** Zámok je člen `VirtualDisk`
(`write_protected_`), `DiskStash::Put` odkladá celý objekt kópiou
a `Machine::InsertDisk` je `disk_ = disk`, takže notch cestuje s diskettou.
Odmerané sondou na bežiacej ROM:

```
diag_probe ROM DISK seq 12000000 slot2 +wp stav slot1 stav slot2 stav
slot2  -> [vlozeny slot 2: v pamati]
+wp    -> [zamok proti zapisu zapnuty]
stav   -> [v pamati, suborov=0, zamknuta]
slot1  -> [vlozeny slot 1: priecinok]
slot2  -> [vlozeny slot 2: v pamati]
stav   -> [v pamati, suborov=0, zamknuta]
```

Disketa v pamäti odišla do slotu zamknutá a vrátila sa zamknutá. (Riadok
`slot1` hovorí „zamknuta“ z iného dôvodu — sonda si slot 1 zamyká sama
v svojej vetve — takže dôkazom je prvý a posledný `stav`, nie ten
prostredný.)

**Chybný je hostiteľský dialóg.** V `Sloty s disketami` je políčko
`Zamknúť túto disketu proti zápisu` pri pamäťovom slote **zošedené**, lebo
`SlotLockIsRemembered` (kedysi `SlotCanLock` — to meno bolo časť problému)
sa pýta na to, či sa zámok dá **zapamätať**, a odpoveď použije na to, či sa
dá **nastaviť**. Sú to dve rôzne veci:

- **zámok diskety** — vlastnosť objektu, funguje na akomkoľvek médiu, trvá
  presne tak dlho ako disketa,
- **zapamätaný zámok** — riadok `zamok1=` v `nastavenia.txt`, vedený podľa
  cesty; `Settings::SetDiskLocked` pamäťový zahodí (`settings.cpp:244`),
  a správne, lebo by ukazoval na nič.

Zošedené políčko teda tvrdí „táto disketa sa nedá zamknúť“, čo je nepravda.
Pravda je „tento zámok neprežije ukončenie emulátora“. Je to ten istý druh
prestrelenej popisky ako „zanikne s emulátorom“ v 6.25 — a všimnime si, že
to už je tretíkrát v jednom dni: **keď sa niečo nedá uložiť, neznamená to,
že sa to nedá nastaviť.** Stojí za to hľadať ďalšie miesta, kde je
perzistencia zamenená za schopnosť.

**Rozhodnuté a spravené 31. 8. 2026 (majiteľ zadal „všetko" spolu
s 6.28).** Z troch možností, ktoré tu stáli — disketa v slote, slot ako
predpis, disketa, čo ešte neexistuje — platí **prvá**: políčko sa vzťahuje
na **disketu, ktorá je v tom slote**. Dôvod nie je vkus. Pri priečinkovom
slote to už tak bolo: zoznam `zamok1=` je vedený **podľa cesty**, nie podľa
čísla slotu, takže cesta je len *meno diskety*. Pri neuloženej diskete je
tým menom sám objekt v zásobníku. Je to tá istá vec bez mena, ktoré by sa
dalo zapísať do súboru — nie druhý druh zámku.

Tretia možnosť **zanikla**, a zabila ju veta majiteľa. Skúsil som dôvod
zošedenia napísať do zoznamu (`Neuložená disketa (vznikne pri vložení)`)
a on to zamietol dvakrát: každé slovo v tom riadku sa číta nahlas pri každej
šípke, a hlavne — **disketa vzniká pri vytvorení, nie pri vkladaní**. To, že
ju worker vyrábal až pri prvom vložení, je naše účtovníctvo, nie jeho úkon.

Keď to bolo raz vyslovené, ukázalo sa, že chyba nebola v texte, ale
o poschodie nižšie: **tlačidlo `Sem novú neuloženú` sa volá po vytvorení
a nič nevytváralo.** Zapísalo len značku `*pamat`. Preto som ten text vôbec
musel písať — vysvetľoval som popisku, ktorá mala byť pravdivá sama. Odvtedy:

- `DiskStash::CreateEmptyIfMissing` a príkaz `kEnsureSlotDisk`. Tlačidlo
  disketu naozaj vyrobí (pri OK, cez frontu — zásobník je workerov).
- `main.cpp` po `Start()` pošle ten istý príkaz za každý slot, ktorý majú
  nastavenia označený `*pamat`. Značka reštart prežije, polička nie, takže bez
  toho by slot po reštarte znovu nemal disketu. Deväť prázdnych stojí najviac
  ~7 MB haldy a nesú nula súborov, takže otázka pri ukončení sa na ne nepýta.
- Políčko zámku je odteraz zošedené **len pri naozaj prázdnom slote**. Žiadna
  výnimka, žiadny text.

**Pri tom vypadla latentná chyba z rodiny 6.25.** `kInsertSlot` bral
z poličky **skôr**, než sa pozrel na to, čo slot menuje. Keď sa použitý
pamäťový slot prepísal na priečinok, `Ctrl+3` naďalej podával disketu
z poličky, kým ponuka menovala priečinok — tiché a nesprávne. Poradie je teraz
opačné: rozhoduje značka slotu, polička až potom. Odložená disketa sa pritom
**nezahadzuje** — môže na nej niečo byť a `main.cpp` sa na ňu pri ukončení
spýta. Nedosiahnuteľná do konca relácie je rozhodnutie používateľa; zmazaná
bez opýtania by nebola.

Drží to `SlotGetsItsDisketteWhenItIsSetUp` v `disk_test`, a jeho podstatná
polovica je druhé volanie: „vyrobiť“, ktoré by pri ďalšom OK bežalo znovu, by
vrátilo prázdnu disketu namiesto tej popísanej — tá istá tichá strata ako
6.24, len dosiahnutá z dialógu namiesto z klávesu.

Druhá možnosť (slot ako predpis) je zamietnutá: bola by to nová vlastnosť
slotu a spor s 6.24.

**Ako je to spravené.**

- `DiskStash::SetWriteProtected` a `WriteProtected` — zámok sa dá hýbať aj
  na diskete, ktorá leží na poličke.
- `EmulatorThread::PostSetSlotWriteProtect` → `kSetSlotWriteProtect`.
  Worker rozhodne, kde tá disketa je: keď je slot ten, čo je práve
  v mechanike, ide zámok na `machine`, a okno o tom dostane
  `WM_EMU_DISK_CHANGED` (titulok, tón); inak na zásobník a nehlási sa nič,
  lebo sa nič viditeľné nezmenilo.
- Worker publikuje dve atomické bitové masky, `stash_holds()`
  a `stash_locked()`. Bez nich sa okno nemá ako spýtať — zásobník je jeho.
- `SlotsDialog` dostáva navyše `Present` a `currentLocked`. **Prestal si
  odpoveď odvodzovať z reťazca slotu**, a to je jadro opravy: z `*pamat`
  sa dá odvodiť „zámok sa nezapamätá", ale nie „disketa neexistuje", a stará
  `SlotLockIsRemembered` odpovedala prvým na druhé.
- `SlotLockIsRemembered` → `SlotLockPersists`, a používa sa už len na to,
  ako sa riadok pomenuje: `— zamknutá` proti `— zamknutá dočasne`.

**Druhá vec, ktorá z toho vypadla.** Keď políčko ukazuje **skutočný** zámok
diskety v mechanike (a musí, inak by hovorilo o niečom inom než riadok nad
ním), musí ho vedieť aj zmeniť — inak by dialóg ukázal pravdu a po OK ju
nechal tak. Okno preto posiela `PostSetSlotWriteProtect` aj pre slot, ktorý
je práve v mechanike. So strážou: keď ten slot v tom istom sedení dialógu
prepíšeš inam, políčko je už o inej diskete, a worker porovnáva len čísla
slotov — zámok by sadol na tú, čo v mechanike zostala. Je to ten istý druh
tichého rozporu ako `assignedCurrent_` v 6.25.

**Pasca, ktorá pri tom vyšla najavo.** Pôvodná slučka v `IDC_SLOT_LOCK`
zaškrtávala každý slot, pre ktorý `Settings::SameDisk` povedala „tá istá
disketa". Dva neuložené sloty majú oba hodnotu `*pamat`, takže `SameDisk`
ich vyhlási za jednu disketu — a sú to dve rôzne diskety na dvoch rôznych
poličkách. Značka nie je meno, je to **neprítomnosť mena**. Slučka preto
beží len nad slotmi, ktoré menujú priečinok.

**Testom kryté.** `disk_test` má `StashKeepsTheLock`: zámok prežije
`Put`/`Take`, sloty sa nemiešajú, zámok sa dá nastaviť aj zrušiť na
poličke, a prázdny slot ani slot 0 sa zamknúť nedajú. `settings_test`
naďalej drží, že sa taký zámok **nezapamätá** — a komentár tam teraz
hovorí, že to nie je to isté ako „nedá sa nastaviť".

### 6.27 Šípky nekrúžili po prepínačoch v Nastaveniach — opravené

**Nahlásil majiteľ 31. 8. 2026:** v `Nastavenia` sa šípkami nedalo prejsť
medzi `Braillovská` a `Externá`, hoci v `Nová disketa` to ide.

Príčina nie je v kóde, je v `eureka.rc`. Skupina, po ktorej šípky krúžia,
začína prvkom s `WS_GROUP` a končí až **ďalším** prvkom s `WS_GROUP` — čo
je medzi tým, patrí dnu, aj keď to fokus prijať nevie. Za prepínačmi
v `IDD_SETTINGS` stál `GROUPBOX "Diagnostika"` bez `WS_GROUP`, takže bol
tretím členom skupiny, a krúženie o neho zakoplo.

Odmerané na hotovom EXE — dialógová šablóna sa vytiahne z resource,
`CreateDialogIndirectParamW` ju vyrobí skrytú a `IsDialogMessageW` dostane
ručne poskladaný `WM_KEYDOWN`. Nie je na to potrebná obrazovka ani myš.
Pred opravou: šípka dole z `Braillovskej` prišla na `Externú` a **tam
zostala stáť pri každom ďalšom stlačení**, šípka hore neurobila nič.
Po oprave sa fokus točí medzi oboma a `BM_GETCHECK` potvrdzuje, že sa
mení aj samotný výber, nie iba fokus.

`IDD_NEWDISK` bol v poriadku len náhodou: za jeho tromi prepínačmi
nasleduje `LTEXT` a **windres dáva `LTEXT` `WS_GROUP` implicitne**.
To je zdroj mýlky — rovnaká štruktúra v dvoch dialógoch, iné správanie,
a rozdiel nie je v `.rc` vidieť. Preto je `WS_GROUP` v `IDD_SETTINGS`
napísané výslovne aj s dôvodom.

Pri tej príležitosti aj `IDD_SLOTS`: zoznam, štyri tlačidlá a políčko
zámku boli **jedna skupina**, takže šípka z tlačidla prešla cez ostatné
tlačidlá, potom cez zámok a nakoniec do zoznamu, kde ešte aj prepla
vybraný slot. Teraz krúžia tlačidlá medzi sebou a zámok stojí sám.
`Tab` sa nemenilo — `WS_GROUP` naň nemá vplyv.

Nekryté testom, a zámerne: `run-tests.bat` drží dvanásť procesov a nič
z toho nie je GUI. Meranie je jednorazový skript typu sondy. Kto siahne
na skupiny v `.rc`, nech si ho spraví znovu — postup je hore v odseku.

### 6.28 Disketa nie je „v pamäti“, je **neuložená** — pomenované, a Ctrl+U je Uložiť ako

**Nahlásil majiteľ 31. 8. 2026** vetou, ktorá zrušila dvojročný slovník:
*„takže vlastne každá disketa je v pamäti. To je ešte zamotanejšie.“*
A hneď aj model, ktorý ho nahrádza: **disketa = dokument v editore.**
Dokument môže, ale nemusí mať meno; či je chránený proti zápisu, je
vlastnosť **jeho**, nie karty, na ktorej je otvorený.

**Mal pravdu, a doslova.** `Mount` prečíta priečinok **raz** a poskladá
z neho obraz 800 KiB (`BuildImage`); hosť potom celý čas píše len do toho
obrazu a späť to ide až vo `Flush`, keď radič dopísal. Priečinková disketa
teda s priečinkom **rovno neinteraguje** — je v pamäti presne tak ako tá
druhá. Rozdiel bol vždy len v dvoch veciach: má kam zapisovať, a má meno,
podľa ktorého sa dá zapamätať jej zámok. Slovo „RAM“ nepomenúvalo ani
jednu z nich, a práve preto sa dalo prečítať ako „nedá sa zamknúť“ (6.26).

**Kde analógia praská, a je to poctivé priznať.** Formátovanie: `FormatTrack`
na diskete s priečinkom nespraví nič (6.5). To je jediné miesto, kde meno
mení sémantiku samotného dokumentu, nie len to, kam sa ukladá — preto má
**vlastný predikát s vlastným dôvodom**, `formatting_erases()`, a nie
`!has_home()`. Druhá vec: `dirty_` nikdy neznamenalo „zmenené“, ale „dlhuje
niečo svojmu priečinku“, a disketa bez priečinka ten dlh nikdy nesplatí.
Otázka pri ukončení sa preto pýta na `StoredFiles() > 0`, teda „je neprázdna“.
Hlavička to teraz hovorí a zakazuje čítať `dirty_` ako používateľský stav.

**Čo sa premenovalo.** `Media` a `media()` zanikli. Zostalo `present()`
a pribudlo **`has_home()`** — priečinok, z ktorého sa disketa načíta a do
ktorého sa sama zapisuje. `folder_`/`folder()` → `home_`/`home()`,
`CreateRamDisk` → `CreateEmpty`, `DiskState::inMemory` → `unsaved`,
`DiskState::folder` → `home`, `SlotIsRam` → `SlotIsUnsaved`, `kSlotRam` →
`kSlotUnsaved`, `NewDiskDialog::Kind` na `kNewFolder`/`kUnsaved`/
`kUnformatted`, `IDC_NEW_RAM` → `IDC_NEW_UNSAVED`, `IDC_SLOT_NEWRAM` →
`IDC_SLOT_NEWUNSAVED` (čísla zostali).

**Značka v súbore s nastaveniami zostala `*pamat`** a musí zostať: je
zapísaná v súboroch, ktoré už existujú, a jej zmena by pri najbližšom štarte
vyprázdnila každý pamäťový slot — a ticho. Meno konštanty je pojem, hodnota
je jeho pravopis na disku.

**V reči pre používateľa: „v pamäti“ → „neuložená“.** Titulok, ponuka,
prepínače v `Nová disketa`, tlačidlo v slotoch, otázky pri výmene aj pri
ukončení, `--help` aj README. „Neuložená“ nie je len presnejšie, je to
**podmienené** slovo: hovorí, že cesta preč z toho stavu existuje. Presne
to, čo 6.25 žiadalo od popisky, ktorá raz prestrelila.

**A to slovo si vynútilo tretiu zmenu: `Ctrl+U` je odteraz Uložiť ako.**
Kým „uložiť do priečinka“ znamenalo len kópiu, titulok by hneď po uložení
stále hlásil „neuložená“ — teda by klamal v okamihu, keď používateľ práve
uložil. `VirtualDisk::SaveAs` preto priečinok **prevezme**: disketa doň
odvtedy zapisuje sama, dostane meno do titulku a `RememberDisk` ju zapíše
ako disketu na budúci štart. Jedno pravidlo pre obe diskety — disketa, čo
priečinok už mala, sa presťahuje, starý si ponechá svoje — lebo dve pravidlá
by od používateľa žiadali vedieť, v ktorom z nich práve je.

**Toto miesto v kóde už raz v editorových pojmoch myslelo a nikto si toho
nevšimol.** `main.cpp` pri ukončení ponúka `PickFolderToCreate`
s komentárom *„A name that does not exist yet is the point here“* — to je
doslova Uložiť ako nad nepomenovaným dokumentom. Bolo to jediné také miesto
v programe; teraz je to posledná šanca, nie jediná.

**Dve pasce v `SaveAs`, obe tiché.**

- **`imported_` treba prestavať.** Ten zoznam viaže obraz na priečinok
  a `ExportImage(writeBack=true)` podľa neho presúva zmazané súbory do
  `.eureka-trash`. Keby `SaveAs` priečinok prevzal a zoznam nechal prázdny,
  v deň uloženia by bolo všetko v poriadku — a disketa by odvtedy prestala
  rešpektovať mazanie, lebo write-back zahadzuje len súbory, o ktorých vie.
  `ExportImage` má preto parameter `adopted` a `SaveAs` ním `imported_`
  nahradí. Drží to `SavingAdoptsTheFolder`.
- **Formát.** Priečinok je filesystem, takže po prevzatí `formatted_.set()`.
  Nenaformátovaná disketa uložená do priečinka je odvtedy naformátovaná —
  je to jediná vec, ktorú ukladanie na médiu naozaj mení, a je napísaná
  v hlavičke aj krytá testom (`SavingAnUnsavedDisketteGivesItAHome`).

**Premenovanie zrazilo dve mnemoniky, a nahlásil to majiteľ.** `&Trvalá`
a `&Dočasná v pamäti` niesli `T` a `D`; ako `&Uložená` a `&Neuložená` si
vzali `U` a `N` — lenže `U` už mal `&Uložiť do slotu:` a `N` mal
`ne&naformátovaná`. V `IDD_NEWDISK` teda vznikli **dva** konflikty naraz
a `Alt+U` pritom sadol rovno na výber slotu, teda na popisku, ktorá sa
nemenila. Teraz je to `Ul&ožená` (O), `&Neuložená` (N),
`nena&formátovaná` (F) a `Alt+U` zostáva slotu.

**A ešte jedna vec, ktorú nahlásil majiteľ v ten istý deň: popiska zoznamu.**
Stálo v nej `Sloty — vkladá ich F11 a číslo s Ctrl:` a je to návod na klávesy
v mieste, ktoré má povedať, čo ten zoznam je. Tie skratky sú v ponuke Disketa,
v Pomocníkovi aj v README; tu sa len čítali nahlas pri každom otvorení dialógu.
Teraz je to `Sloty:`. Nebolo to z tejto zmeny — stálo to tam predtým — ale
pravidlo je odteraz v `CLAUDE.md` a platí pre celé GUI.

**Poučenie je metodické, nie o písmenách: premenovanie tlačidla je zmena
mnemoniky.** V `.rc` to vidieť nie je — ampersandy sú roztrúsené po
reťazcoch, ktoré sa lámu cez viac riadkov — a windres na kolíziu
neupozorní. Meria sa to tak ako `WS_GROUP` v 6.27, na **hotovom EXE**:
`FindResourceW(RT_DIALOG)` → `CreateDialogIndirectParamW` (vyrobí ho
skrytý, netreba obrazovku) → `EnumChildWindows` + `GetWindowTextW`,
a písmená za `&` sa zoskupia. Odmerané po oprave: `IDD_SETTINGS` 0,
`IDD_SLOTS` 0 (L, N, P, S, V, Z), `IDD_NEWDISK` 0 (F, N, O, P, R, U).
Skript je jednorazový, v scratchpade; do `run-tests.bat` nejde z toho
istého dôvodu ako meranie skupín — tých dvanásť procesov je bez GUI.

**Čo sa nezmenilo:** prepínač `--ram-disk` (je v skriptoch používateľov),
značka `*pamat` a číslovanie `IDC_*`.

Overené: `build.bat` bez varovania, dvanásť `PASS` (`disk_test` má
144 kontrol namiesto pôvodných 63), a všetky nové reťazce nájdené
v hotovom EXE v UTF-16 vrátane diakritiky — `neuložená`,
`Neuložená disketa`, `Sem &novú neuloženú`, `— zamknutá dočasne`,
`Odteraz je to jej priečinok a zapisuje sa doň sama.`
Staré (`disketa v pamäti`, `Disketa v pamäti`, `&Dočasná v pamäti`,
`Sem &novú v pamäti`, `&Trvalá — nový priečinok`) v ňom už nie sú.
Kontrolovaná bola aj negatívna vzorka, aby test naozaj meral.

**Čo z toho zostáva otvorené.** Nič z tejto témy, ale jedno pozorovanie
platí ďalej a je zovšeobecnením 6.26: **keď sa niečo nedá uložiť,
neznamená to, že sa to nedá nastaviť.** Oplatí sa hľadať ďalšie miesta,
kde je perzistencia zamenená za schopnosť.

### 6.29 Porty a masky majú mená z manuálu — `src/eureka_io.h`

Emulátor mal 302 šestnástkových literálov v `machine.cpp` a 36
v `emulator_thread.cpp`. Číslo portu, bit latchu, príkaz radiča a kód klávesu
vyzerali v kóde rovnako, a jediné, čo ich rozlišovalo, bol komentár nad nimi.
Teraz je z nich 27 a 4; zvyšok sú adresy v ROM, zmerané prahy a posuny masiek,
teda veci, ktoré meno z prameňa nemajú.

**Pravidlo, podľa ktorého sú mená vybrané.** Meno je to, ktoré používa jeho
prameň, prepísané do `kPascalCase` projektu — `vmsel_mask` → `hw::kVmselMask`,
`pol_disk` → `hw::kPolDisk`, `K_KEYPAD` → `hw::kKeyKeypad`. Nie krajšie
meno, ktoré by som vymyslel: meno z prameňa sa dá grepnúť v `eurekatech/`
a spor o to, čo bit robí, rozhodne Robotron, nie ten, kto písal kód. Vymyslené
meno nesie môj úsudok, a to je presne to, na čom sa tento projekt už dvakrát
popálil.

Prameň je pri každej konštante v komentári. Sú štyri:

- `IOREG.LIB` — interné registre HD64180 (`00h`–`3Fh`). Ich **bity** v ňom nie
  sú — príloha H výslovne odkazuje na Hitachi — takže mená bitov sú
  z datasheetu a v hlavičke sú tak označené.
- `IOPORT.LIB` — externé porty `80h`–`BFh` aj s maskami.
- `KB.H` — kódy klávesov Eureky.
- datasheet WD177x — príkazy a stavové bity radiča. V `eurekatech/` nie je,
  takže tie mená nesú odkaz na miesto v ROM, ktoré ich potvrdzuje.

**Jedna výnimka z toho pravidla: porty membránovej klávesnice.** Manuál si
v ich číslovaní protirečí — `IOPORT.LIB` počíta `bkb_row0 = 89h`,
`bkb_row2 = 8Ch`, kým text prílohy H tlačí tie isté popisy proti `$8C` a `$89`,
teda **row0 a row2 prehodené**. Rozpor **nie je nový**: `hardware-map.md` ho
už drží aj s dôkazom, ktorý ho rozhoduje v prospech `IOPORT.LIB` — ROM na
1D41F maskuje `89h` hodnotou `3Fh`, aby dostala šesť bodov, a na 1D206
maskuje `8Ch` hodnotou `0Fh`, aby ignorovala shift.

Preto sa tie tri porty **nevolajú** `kBkbRow0/1/2`, ale `hw::kBkbDots`,
`hw::kBkbFunction` a `hw::kBkbCursor` — pomenované podľa toho, čo nesú. Číslo
riadku by čitateľa poslalo do tej polovice manuálu, ktorá sa mýli. Je to jediné
miesto, kde hlavička meno z prameňa nepreberá, a hovorí prečo.

**Dve miesta, kde sa hodnoty zhodujú náhodou.** `kFdcStatusIndex` a
`kFdcStatusDrq` sú obe `02h`, `kFdcStatusTrack00` a `kFdcStatusLostData` obe
`04h`, `kFdcStatusSeekError` a `kFdcStatusNotFound` obe `10h` — WD177x
znamená tými bitmi niečo iné po príkaze typu I než po prenose dát. A
v dekodéri scancodov je `kCtrlFlag` = `04h` tá istá hodnota ako
`kFirstCharacter`, pričom jedno je príznak v tabuľke a druhé hranica znakov.
Obe dvojice majú dve mená zámerne. **Nezlučuj ich** — je to tá istá vec ako
`formatting_erases()` vedľa `has_home()` (6.28).

**`run-tests.bat` tento zásah nedrží a držať nemôže** — je mechanický, takže
dvanásť `PASS` by prišlo aj s prehodenou maskou. Drží ho
`tools/check_io_names.py`: prečíta hlavičku, pre každú konštantu s komentárom
menujúcim symbol z prameňa ho nájde v `IOPORT.LIB`, `IOREG.LIB` alebo `KB.H`
a porovná hodnoty. **107 overených, nula nezhôd**, 21 bez protajšku (WD177x
a vlastné konštanty). Odskúšané mutáciou: `kVmselMask` `0x40` → `0x20` zhodí
kontrolu a vráti nenulový návratový kód.

Overené aj: `build.bat` bez jediného varovania a dvanásť `PASS`.

**Doplnené 1. 9. 2026:** kontrola číta aj `SYSEQU.LIB` (kvôli `fdc_verify`,
6.30). So `IOPORT.LIB` a `IOREG.LIB` nemá ani jednu kolíziu, overené
porovnaním; teraz **108 overených, nula nezhôd**, 21 bez protajšku.
Odskúšané mutáciou aj na novom symbole: `kFdcVerify` `0x04` → `0x08` zhodí
kontrolu.

### 6.30 Mechanika mala na tri stavy jednu vetu — „vadný disk“

Majiteľ ohlásil z pamäti skutočného stroja, že nenaformátovaná disketa sa
ohlási otázkou, či ju naformátovať, prázdna mechanika slovami „disk není
založen“, a že **„vadný disk“ znel len na naozaj nečitateľnej diskete**.
Emulátor hovoril „vadný disk“ na všetko troje. Mal pravdu a ROM to dokladá
do posledného bitu.

**Kde v ROM tie tri vety vznikajú.**

- **„vadný disk“ je BDOS.** Na `1BB3D` volá BIOS READ (`C127`) alebo WRITE
  (`C12A`) a vetví sa na návratovom kóde: `0` je v poriadku, `2` → „disk je
  chráněn proti zápisu“, `3` → „disk není založen“, `4` → „slabá baterie“,
  a `JP DEB6h` na `1BB56` posiela **všetko ostatné** na „vadný disk“. Sú to
  presne kódy, ktoré `DEVICES.10` uvádza pri `fdc_ctl_read_track`
  (0 OK, 1 Disk faulty, 2 Write Protected, 3 No disk inserted, 4 Low battery).
- **„disk není naformátován. Chceš jej naformátovat?“ je `fdc_ctl_disk_test`.**
  Rutina na `197FB` (`DEVICE.H`: `FDC_CTL_DISK_TEST`, teda `BC=0900h` cez
  `C142`) vydá **jediný verify v celom firmvéri** — Seek s V, `14h` na
  `1980A` — a číta jeho stav troma spôsobmi: bity 3 a 4 (`AND 18h` na
  `19821`) znamenajú nečitateľnú stopu a stanú sa bitom 2, ktorý diskové
  funkcie na `149E7` prečítajú a položia tú otázku; bit 7 znamená, že radič
  príkaz vôbec nedokončil, a stane sa bitom 0, teda „no disk“.
- **„v jednotce není disk“ je `fdc_ctl_disk_in`** (`19828`): Seek a potom
  polling INDEX na porte `98h` s časovým limitom `5208h`. Túto cestu model
  mal správne už predtým.
- **Bit 7 je časový limit, nie stav radiča.** `EA00` (`19A00`) vydá príkaz
  a čaká 2000 pokusov na uvoľnenie BUSY (`19A16`); keď nepríde, vráti `80h`
  (`19A52`). Inak stav maskuje `5Dh`, takže bity 1 a 7 z radiča neprejdú —
  sú vyhradené pre batériu a pre tento časový limit.

**Čo bolo zlé v modeli.** Dve nezávislé veci, každá stačila sama:

1. `TypeOneStatus()` nevidel príkaz, takže vracal ten istý stav po Restore,
   Seeku aj po Seeku s verify. Verify teda vždy uspel a `fdc_ctl_disk_test`
   vrátil „všetko v poriadku“ pre prázdnu mechaniku aj pre nenaformátovanú
   disketu. Teraz dostane príkaz a na verify sa pýta média: naformátovaná
   stopa odpovie, nenaformátovaná dá **Seek Error**, prázdna mechanika
   **zostane BUSY** — bez dier v médiu nemá radič čo počítať, takže príkaz
   nedokončí a firmvér ho vytimeoutuje. `fdc_verify` (`100b`) je meno
   zo `SYSEQU.LIB`.
2. `InterceptBios` vracal pri každom zlyhaní `1`. Teraz vracia `3` bez
   diskety, `2` pri zápise na zamknutú a `1` inak (`DiskFailure`).

**Že to nie je podvrh, je odmerané.** S vypnutým obídením BIOS-u a s radičom,
ktorý bez média zostáva BUSY aj pri Read Sector, dôjde **pôvodný ovládač
v ROM** k „disk není založen“ sám. Obídenie teda hovorí to, čo by povedal
firmvér, len bez tých niekoľkých tisíc pollingov.

**Prečo napriek tomu Read Sector bez média RNF vracia ďalej.** Čakanie na
BUSY po Read Sector je na `19EF3` a **nemá časový limit** — je to `JR NZ` na
seba. Skutočný stroj sa tam nedostane (firmvér sa najprv pýta), ale cesta,
ktorá by sa tam dostala, by emulátor zavesila natvrdo. Nepresný stav, z
ktorého sa firmvér spamätá, je lacnejší než zaseknutý stroj. Rozdiel, ktorý
používateľ počuje, robí `DiskFailure`. **Verify je iné** a BUSY tam zostáva:
vydáva sa jediným miestom, a to cez `EA00`, ktorý limit má.

**Čo drží zásah.** Nový režim `hlaseni` v `integration_test`: tri prípady,
každý s vlastným bootom, a v každom sa okrem očakávanej vety kontroluje aj to,
že **nezaznelo „vadný disk“**. Odskúšané mutáciou — vypnutie verify zhodí dva
prípady, vrátenie kódu `3` na `1` zhodí tretí, takže obe opravy sú kryté
každá zvlášť. Sonda dostala tokeny `nova` a `vysun`; bez nich sa tie dve
médiá nedali vyrobiť.

**Čo zostáva otvorené.** `F8` (adresár) na nenaformátovanej diskete povie
„vadný disk“ ďalej, a je to zrejme správne: tá cesta sa nepýta
`fdc_ctl_disk_test`, ide rovno na BIOS, a nenaformátovaná stopa je z pohľadu
čítania nečitateľná stopa. Neoverené na skutočnom stroji — ak si to majiteľ
pamätá inak, je to prvé miesto, kam sa pozrieť. Neoverené je aj to, čo model
robí pri **zápise** na prázdnu mechaniku: `DiskFailure` vráti `3`, ale
kadiaľ tam firmvér ide a či to počuť, zmerané nie je.

#### Odmerané na skutočnom stroji 7. 9. 2026

Majiteľ s kamarátom prešli oba stavy média naprieč vstupnými bodmi. Toto
je odpoveď, proti ktorej sa model porovnáva — **odsek vyššie tým prestal
byť otvorený v prvej polovici**: `F8` na nenaformátovanej diskete naozaj
povie „vadný disk“ aj na kremíku, takže model je verný. Druhá polovica
(zápis na prázdnu mechaniku) otvorená zostáva, ale už proti známej
odpovedi, nie naslepo.

**Prázdna mechanika:**

- adresár disku (`F8`) — „disk není založen“
- diskové funkcie — „v jednotce není disk“
- textový procesor, čítanie — „disk není založen, žádný soubor nebyl načten“
- textový procesor, ukladanie — „disk není založen“ + „nebyl uložen“
- BASIC, načítanie — „disk není založen, chyba 21“
- BASIC, ukladanie — „disk není založen, chyba 21“
- formátovanie — najprv „mám formátovat disk ano nebo ne?“ a **až po
  odpovedi** „v jednotce není disk“

**Nenaformátovaná disketa** (skúšané DOSovou disketou; pre stroj je to to
isté, lebo formát nepozná):

- adresár disku (`F8`) — „vadný disk“
- diskové funkcie — otázka, či ju má naformátovať

Tri veci z toho, ktoré sa dajú ľahko urobiť zle a v modeli sa neoveria
samy: jeden stav média hovorí **rôzne vety podľa vstupného bodu**
(prázdna mechanika je raz „disk není založen“ a raz „v jednotce není
disk“); **formátovanie sa pýta skôr, než sa pozrie**, takže poradie je
otázka a až potom zistenie; a nenaformátovaná disketa má tiež dve rôzne
odpovede podľa toho, odkiaľ sa na ňu siahne.

**Neisté zostáva zamknutá disketa** — či „disk je chráněn proti zápisu“
znie rovnako zo všetkých vstupných bodov, alebo sa tiež líši. Majiteľ si
tým nie je istý (7. 9. 2026), takže sa to nesmie predpokladať.

#### Porovnané s modelom 17. 9. 2026 (ea4-2ll)

Model sedí so všetkými deviatimi riadkami tabuľky vyššie, vrátane poradia
pri formátovaní. Odmerané sondou a pribité režimom `hlaseni`, ktorý sa
rozšíril z troch prípadov na deväť. Nový režim nepribudol, testov je
naďalej trinásť. Tým je uzavretá aj **druhá polovica otvoreného odseku
vyššie**: zápis na prázdnu mechaniku z textového procesora aj z BASIC-u
povie „disk není založen“, ako na kremíku.

- **Ako sa na to siaha.** V textovom procesore otvorí `Shift+F1` výzvu
  „vlož souborový příkaz“, `r` číta a `s` ukladá, za tým meno a Enter.
  Stojí to v nápovede v ROM (`73752`). V BASIC-u `LOAD "X"` a `SAVE "X"`;
  na SAVE netreba program, prázdny dopadne rovnako.
- **Formátovanie sa naozaj pýta skôr, než sa pozrie.** Sonda s `trace`
  ukázala pred odpoveďou nula prístupov na porty radiča `98h`–`9Bh` a po
  `Y` 392. Test počúva 30 M inštrukcií pred odpoveďou a „v jednotce není
  disk“ v nich zaznieť nesmie; po `Y` ho model povie do 2 M.
- **Dve vety znejú inak, než si ich pamätá majiteľ, a ROM dáva za pravdu
  modelu.** Textový procesor pri ukladaní povie „X není uložen“, nie „nebyl
  uložen“: „nebyl“ je v celom dumpe len dvakrát, v „záznam nebyl přijat“
  (`62873`) a „žádný soubor nebyl přečten“ (`75821`), a pri čítaní teda
  zaznie „přečten“, nie „načten“. Test sa drží ROM.
- **Dve mutácie, každá zhodí iné prípady.** BIOS, ktorý na prázdnu
  mechaniku vráti `1` namiesto `3`, zhodí päť prípadov, ktoré idú cez neho
  (`F8`, oba prípady textového procesora aj BASIC-u). Verify, ktorý vždy
  uspeje, zhodí oba prípady diskových funkcií. Formátovanie nezhodí ani
  jedna, lebo prázdnu mechaniku zisťuje cez `fdc_ctl_disk_in`. Že kontrola
  poradia vie zazvoniť, je overené na teste. Model, ktorý sa pozrie pred
  otázkou, vyrobiť nevieme.
- Každá očakávaná veta zaznie do 5 M inštrukcií od klávesu, aj pri
  prázdnej mechanike. Test preto počúva, kým ju nepočuje, a potom ešte
  10 M. Režim tak trvá 21 s namiesto pôvodných 27 s, hoci prípadov je
  trikrát viac.

Otvorená zostáva len zamknutá disketa z predošlého odseku. Model to
zmerať vie (`+wp`), ale bez odpovede zo skutočného stroja nie je s čím
porovnávať. Eviduje to bead `ea4-4gw`.

### 6.31 Vypnutá Eureka zostáva v okne — celá spravená

**Uzavreté 9. 9. 2026, celé znenie v `HANDOFF-archiv.md`.** Vypnutie je
stav, nie koniec: okno zostáva otvorené, vlákno stroja beží ďalej a späť
ho zapne `Stroj` → `Zapnúť Eureku` (`Ctrl+P`) **teplo**, čiže s RAM,
hodinami aj ôsmimi bajtmi budíka, alebo `Reset` (`Ctrl+R`) studeno.
Deliaca čiara je v `EurekaMachine::Reset()` a `PowerOn()`, cesta cez
vlákno v `EmulatorThread` (`kPowerOn`), stav v ponuke a v titulku
v `main_window.cpp`. Drží to `integration_test … power` a odmerať sa dá
sondou tokenmi `vypni`, `zapni` a `studeno`. Hovoria o tom README aj
Pomocník.

V archíve má sekcia nadpis „dialóg preč, studený štart otvorený“ — taká
vznikla a text sa pri sťahovaní neupravuje. Že je studený štart zavretý,
hovoria jej dva vlastné dodatky z toho istého dňa.

Otvorené zostáva len prežitie **procesu**, a to nikdy do 6.31 nepatrilo:
je to 6.15 a epic `ea4-aip`.

**Doplnené 16. 9. 2026 (`ea4-6kz`): položka `Zapnúť Eureku` už
neexistuje, volá sa `Teplý reset` a platí v oboch stavoch.** Kým stroj
bežal, bola zošedená, a zošedenej položke akcelerátor `WM_COMMAND`
nepošle, takže `Ctrl+P` za behu nerobilo nič. Majiteľ ho chcel ako
východisko zo zamrznutia: na stroji to bol akord bod 3 + `F1` + šípka
hore, podržaný asi sekundu. Tie isté ID (`ID_MACHINE_POWERON`, `kPowerOn`)
teraz na bežiacom stroji volajú `PowerOn()` a navyše zahodia rozhovorenú
reč vo zvukovom zariadení (zatvorí a znovu otvorí sa ako pri `Reset`),
lebo z behu, na rozdiel od vypnutia, nie je dohraté. Zmerané sondou:

- `zapni` na bežiacom stroji povie len úvodné tóny `!%t154/50*5!%t230`,
  nie „inicializace eureky“, a skončí v hlavnom menu — z menu, z hodín,
  z editora aj 0,4 s po začatí čítania riadku. Text v editore prežije
  (`kA1 -> Ahoj Svete.`). Slová „A4 reset“ z `DEVICES.10` v tejto ROM nie
  sú; reťazec `reset` v nej chýba.
- **Akord ROM nečíta.** Bod 3 + `F1` + šípka hore držaný 3, 5 aj 10 s
  v menu nič nespraví, v hodinách tiež, v editore sa číta ako obyčajné
  klávesy. Žiadne čítanie `89h`/`8Ah`/`8Ch` naň netestuje a na `0066h` je
  text copyrightu, teda NMI sa nepoužíva. Záver, ktorý je **odvodený, nie
  zmeraný na hardvéri**: reset robil hardvér linkou RESET procesora, čiže
  presne `PowerOn()`.
- Pri tom sa odmerali dve veci o štarte: na `18042` sa firmvér pýta na
  akord **bez bodov, `F1`–`F4` a štyri šípky** (`8Ah=0Fh`, `8Ch=0Fh`)
  a pri ňom zmaže RAM (`1805E`), a **bod držaný počas štartu stroj vypne**
  (`180BB` → `18181`, točí sa na `18187`). Keby sa akord niekedy emuloval,
  reset musí prísť až po pustení klávesov.

### 6.32 Vypnutá Eureka sa budí sama — spravené

**11. 9. 2026, bead `ea4-b41`.** Kým beží emulátor, vypnutá Eureka sa zobudí na budík a diár
presne tak, ako to robil stroj. Majiteľ to odskúšal ručne na budíku aj na
diári.

#### Prečo je to model hardvéru, nie pohodlie

- `GLOSSARY.TXT`: „Turning the Eureka off doesn't turn off power to the RAM
  or the Real Time Clock.“
- `HARDWARE.1.txt` o hodinách: „triggers all chimes, alarms and diary
  functions. It is capable of turning the Eureka on at any arbitrary
  time/date.“
- `SYSJUMPS.11`, `.go_to_sleep`: „The Real Time Clock is programmed to wake
  up the Eureka when the next chime/alarm/diary message is due.“
- Prerušovacia linka RTC nejde na procesor, ale na spínač napájania (6.13
  v archíve, `hardware-map.md` pri budíku). Za behu sa budík hľadá
  pollovaním v heartbeate, čo potvrdzuje `PROG-A4.12`.

Emulátor túto polovicu nemal: `Step()` vypnutého stroja vráti `false`, takže
sa nevolalo ani `UpdateRtcEvents()` a hrana budíka vo vypnutom stroji nikdy
nevznikla.

#### Čo sa zmenilo

- `EurekaMachine::WakeOnAlarm()` — keď je stroj vypnutý a `rtc_mask` má
  bit 0, porovná hodiny s alarmovými registrami tou istou hranou ako
  `UpdateRtcEvents()`. Pri zhode zavolá `PowerOn()` a potom nastaví
  `rtc_status` na `81h`, lebo `PowerOn()` ho nuluje.
- Vlákno ho volá v hlavnej slučke raz za prechod, sonda pri každom tokene
  a hlási ho pri `cas:`.
- **`PowerOn()` už nenuluje `rtc_mask` ani `rtc_command`.** Sú v obvode
  hodín na nepretínanom napájaní, rovnako ako `rtcRam_`. Kým ich nuloval,
  budík odkliknutý klávesom zostal natrvalo vypnutý (`rtc_mask` = `00`,
  dátum sa neposunul). Zmerané sondou, majiteľ potvrdil, že po oprave budík
  prežije.
- Hostiteľské tóny napájania pri neodkliknutom budíku nezaznejú ani pri
  zobudení, ani pri uspatí — ohlasuje sa sama Eureka. Tichý režim zruší prvý
  kláves a každý príkaz napájania z ponuky.

#### Čo boot robí, zmerané

Cesta je `180C2`–`180D6` (disassemblované):

```
180C2  LD A,(0040h)   ; rtc_status odložený na 18011
180C5  RRCA
180C6  JR NC,180D2h   ; nie budík -> zapnutie rukou
180C8  CALL CFEBh     ; .service_alarm1
180CB  LD A,(C45Ah)
180CE  OR A
180CF  JP NZ,CFD9h    ; .go_to_sleep
180D2  CALL CFC4h     ; .schedule_alarm
180D5  XOR A
180D6  LD (C45Ah),A
```

Obsluha budíka teda skončí dvoma spôsobmi a rozhoduje značka `C45Ah`:

- **Nikto neodpovie.** Stroj zvoní, budík si sám presunie na ďalší výskyt
  (`den` `0B` → `0C`), značka zostane `FFh` a stroj **zase zaspí**. Zmerané
  sondou (`spin:`) a drží to `integration_test … budik`.
- **Kláves odpovie.** Značka sa vynuluje (`FFh` → `00h`), boot pokračuje na
  `180D2` a stroj **zostane v hlavnom menu**, ako po teplom zapnutí. Neskoršie
  vypnutie je potom obyčajné — akord alebo päť minút nečinnosti. Majiteľ to
  odskúšal na budíku aj na diári.

#### Pasce pri meraní

- Značky `?text` v sonde rozlišujú veľké a malé písmená a prepis je veľkými.
  `?dobr` sa nechytí nikdy, `?DOBR` áno. Kým sa to nevedelo, sonda bežala
  celý rozpočet a výsledky vyzerali ako „kláves budík vypne“; to vypnutie
  urobilo až päťminútové odpočítavadlo.
- `.` sa zastaví na tichu medzi zvoneniami, takže neodkliknutý budík sa ním
  dočkať nedá. Treba `spin:`.
- Vypínací akord funguje len z hlavného menu. Po nastavení budíka treba
  najprv `s01`.
- `Grind()` v integračnom teste zahadzuje zvuk v každom kroku. Rozkmit sa
  musí zbierať počas behu.

#### Otvorené

- **Odbíjanie hodín nie je zmerané** (`ea4-itd`). Ide rovnakými registrami
  a rovnakou cestou bootu, takže by sa malo správať ako neodkliknutý budík,
  ale nikto to neskúšal.
- Hostiteľ uspatý Windows preskočí čas medzi dvoma pollami a hrana sa
  nezachytí. Rovnakú vlastnosť má `UpdateRtcEvents()` za behu, takže to nie
  je nová trhlina; budík potom preplánuje firmvér pri najbližšom zapnutí
  (6.15).

### 6.33 Jadro proti manuálu Z180 — prerušenie po inštrukcii opravené, zvyšok otvorený

Prameň: Zilog **UM005004-0918** (tabuľky 38–47 a 50 čítané priamo v PDF)
a Hitachi **HD64180Z Hardware Manual**, oba mimo repozitára
v `C:\b\z180-docs`. Porovnané 12. 9. 2026.

#### Čo sedí

- **T-stavy všetkých zdokumentovaných inštrukcií** (tabuľky 38–47) sa
  zhodujú s `cyc_00`, `cyc_ddfd`, CB, DDCB aj `cyc_ed` na každom riadku.
  `SRL (HL)` = 3 v tabuľke 39 je preklep; Hitachi má v súhrne 13.
- **IN0, OUT0, TST, MLT:** operácia, príznaky aj horný bajt adresy `00h`.
  `IN`/`OUT (C)` dávajú na horný bajt B, `IN A,(m)` a `OUT (m),A` dávajú A.
- **Prerušenia:** `EI` aj `DI` odložia prijatie o inštrukciu, `RETN` obnoví
  IEF1, `LD A,I` a `LD A,R` dávajú IEF2 do P/V a vnútorné prerušenia idú cez
  I a IL bez ohľadu na IM.

#### Opravené: žiadosť o prerušenie sa rozhodovala pred inštrukciou

`Step` volal `ScheduleInterrupt()` **pred** `z80_step`. Žiadosť zostala
nastavená a jadro ju prijalo na konci inštrukcie, aj keď práve tá inštrukcia
zdroj zakázala. Z180 vzorkuje na **konci** inštrukcie (tabuľka 47,
poznámka 7).

Zmerané dočasným počítadlom v kópii sondy mimo repozitára, `sweep 4000000`:
zo 542 824 prerušení boli **3 falošné**, všetky od časovača 0 za
`OUT0 (TCR),A` na `0027C`. Rutina na `00277` ním časovač 0 zastavuje
a zakazuje jeho prerušenie, takže po zastavení prišla ešte jedna obsluha.

Teraz je `z80_step` rozdelený na `z80_execute` a `z80_process_interrupts`.
`Step` vykoná inštrukciu, posunie čas (`Advance`), rozhodne žiadosť nanovo
ako úroveň, nie západku, a až potom ju dá jadru. Časovač, ktorý dobehol
počas inštrukcie, sa tak prijme na jej konci, nie o inštrukciu neskôr.

Zmerané po oprave:

- To isté počítadlo: 546 081 prerušení, za `0027C` **žiadne**. Situácia, že
  by `0027C` bežala s čakajúcim a povoleným časovačom 0, nenastala ani raz:
  žiadosť sa prijme už na konci `AND EEh` na `0027A`, kde je prerušenie ešte
  povolené, tak ako na Z180.
- Tempo znelky, `seq 20000000 kC6 spin:2000000`: pred opravou 100 994 obehov
  `10E5D` na 37 012 prerušení (2,729), po nej 100 971 na 37 012 (2,728).
  Závery 6.10 platia ďalej.
  **Doplnené 24. 9. 2026:** tieto čísla platia pre stav **pred** čakacími
  stavmi `TMDR0L` (`4b43c76`, 20. 9. 2026, 6.10). Od nich je to **97 850 na
  37 343 (2,620)** a to je súčasné meradlo; zmerané pri `4b43c76^` aj pri
  `4b43c76` (viď dodatok k 6.43).

**Od 22. 9. 2026 to drží test** (`ea4-q6x`, viď 6.42). Dovtedy platilo, že
opravu nedrží nič a že by sa vrátenie chyby nepoznalo — to už neplatí. Platí
však naďalej, že **sledovať `0027C` za behu nestačí**: taký test prešiel aj
s vrátenou chybou, lebo tá kolízia potrebuje, aby časovač dobehol práve počas
`AND EEh`, a pri hraní znelky sa to nestane ani raz.
- Testy 16 zo 16 pred opravou aj po nej.

Test, ktorý by opravu držal, zatiaľ nie je (`ea4-q6x`).

#### Otvorené

V tejto ROM sa neprejaví nič z toho okrem TRAP pri cudzích programoch.

1. **TSTIO dáva na horný bajt adresy B namiesto `00h`** (`ea4-6dx`;
   `exec_opcode_ed`, `0x74`). Manuál to hovorí výslovne. V ROM je TSTIO dvakrát (`185FC`,
   `1E0A2`), vždy na `STAT0`/`STAT1`, kde `ReadPortInner` horný bajt
   nerozlišuje.
   **Opravené 22. 9. 2026, viď 6.40.** Posledná veta odvtedy neplatí z druhej
   strany: `ReadPortInner` horný bajt **rozlišuje** (6.39), takže `STAT0`
   s nenulovým `B` by už nevrátil stav registra, ale `FFh`. Chyba tým prestala
   byť neškodná ešte predtým, než sa opravila.
2. **Príznaky OTIM, OTDM, OTIMR a OTDMR sú neúplné** (`ea4-jp1`). Jadro nastaví len Z
   a N=1. Podľa tabuľky 46 nastavujú OTIM a OTDM aj S, H, P a C, opakovacie
   varianty dávajú S=0, H=0, P/V=1 a C=0, a N je vo všetkých štyroch
   najvyšší bit vyslaného bajtu. ROM má OTIMR šesťkrát a za žiadnym príznaky
   nečíta.
   **Sčasti spravené 22. 9. 2026, viď 6.41:** N aj pevné príznaky OTIMR
   a OTDMR sú doložené a zavedené. Otvorené zostáva **len** S, H, P/V a C pri
   OTIM a OTDM — tabuľka pri nich nehovorí, z čoho sa počítajú, takže to nie
   je práca, ktorá by čakala na čas, ale na prameň.
3. **N pri INI, IND, OUTI, OUTD a ich opakovacích variantoch** má byť
   najvyšší bit dát, jadro dáva 1 (`ea4-jp1`). **Spravené 22. 9. 2026, 6.41.**
4. **SLP sa správa ako HALT** (`ea4-ya6`). Pri IEF1=0 by Z180 povolený zdroj zobudil
   a pokračoval za SLP; jadro by spalo naveky. ROM SLP nemá.
5. **TRAP nie je emulovaný** (`ea4-cyq`). Z180 pri nedefinovanom kóde nastaví TRAP
   (pri treťom bajte aj UFO) v `ITC`, uloží PC a skočí na logickú `0000h`.
   Jadro kód vykoná ako Z80 alebo ho preskočí s hláškou na stderr. Firmvér
   s tým počíta: WBOOT BIOSu na `E6B3h` (fyzická `196B3`, druhá položka
   tabuľky na `1966B`) testuje `ITC`, bit zhodí a povie **„chybný operační
   kód“**.

   Zmerané s TRAP doplneným v kópii jadra a programom `TRAP.COM`, ktorý povie
   „pred“, vykoná `ED 77` a povie „po“ (33 bajtov: `11 15 01 0E 09 CD 05 00
   ED 77 11 1C 01 0E 09 CD 05 00 C3 00 00`, za nimi `pred\r\n$` a `po\r\n$`):

   - Bez TRAP stroj povie „pred po ahoj“. Majiteľ to potvrdil aj ručne.
   - S TRAP povie „pred ahoj“. Cesta `0000h` → `C103h` → `CF25h` je bežný
     koniec programu, `E6B3h` sa nedosiahne a bit TRAP zostane nastavený.
     Ani `RESET.COM` (BDOS 0) spustený potom k `E6B3h` nevedie.
   - `sweep` nevykoná ani jeden nedefinovaný kód, takže sa to týka len
     cudzích programov.

   Rozhodne test na skutočnom stroji (`ea4-r1c`).

   Rozhodnuté 18. 9. 2026: skutočná Eureka povedala **„pred“, potom „ahoj“**
   a vrátila sa do hlavného menu, „po“ nezaznelo a „chybný operační kód“
   tiež nie. HD64180 v Eureke kód `ED 77` teda zachytí a stroj ide tou
   istou cestou ako model v kópii jadra (`0000h` → `C103h` → `CF25h`).
   Dnešný emulátor („pred po ahoj“) sa od stroja líši a TRAP patrí
   doplniť tak, ako bol odskúšaný (`ea4-cyq`). Či na stroji zostane bit
   TRAP v `ITC` nastavený, tento test nerozhodol — počuť sa to nedá.

   Spravené 18. 9. 2026 (`ea4-cyq`): TRAP z kópie je prenesený do jadra bez
   dočasných počítadiel. Jadro ho robí len so zapnutým `z180_traps`, ktorý
   zapína stroj; holé jadro v `zex_test` ho má vypnuté, lebo ZEXDOC skúša
   aj tvary IXH/IXL, ktoré by Z180 zachytil (bod 8). Stroj prenesie TRAP
   a UFO do `ITC` hneď po inštrukcii, zápis do `ITC` TRAP len zhadzuje
   a UFO nemení (HD64180Z Hardware Manual, `ITC`). Sonda s `TRAP.COM`
   (`seq 15000000 kD6 TRAP~ . .`) teraz dáva „pred..ahoj“ ako stroj.
   Drží to režim `trap` v `integration_test`; s vypnutým `z180_traps`
   zlyhá na „pred..po..ahoj“ (overené mutáciou).
6. **CCF a príznak H** (`ea4-aze`). Oba manuály uvádzajú H=0, jadro kopíruje predošlé C
   ako Z80. Bez stroja sa to rozhodnúť nedá; prejaví sa to len pri DAA hneď
   po CCF.
7. **Vzorkovanie po `LD A,I` a `LD A,R`** (`ea4-1qi`). Podľa poznámky k tabuľke 41 masky
   R1 a Z po nich prerušenie nevzorkujú, jadro áno. ROM má `LD A,R` dvakrát
   (`0BB1E`, `183E7`), `LD A,I` ani raz.
8. **Bežné inštrukcie Z80 nie sú overené ZEXDOC** (`ea4-z8y`); na disku
   nie je.

   Už neplatí, 17. 9. 2026: overené. ZEXDOC (8 704 B, MD5
   `be3691b6b910d2c2e05e194cd529e45c`, z `anotherlin/z80emu`) leží mimo
   repozitára v `C:\b\z80-tests`. Púšťa ho `bin\zex_test.exe`
   (`tests/zex_test.cpp`) na holom jadre bez stroja: **všetkých 67 skupín
   OK**, „Tests complete“, 5 764 169 610 inštrukcií, 113 s. Prechádzajú
   aj skupiny s IXH/IXL/IYH/IYL, ktoré by Z180 zachytil ako TRAP (bod 5),
   a `<daa,cpl,scf,ccf>` — jadro tam počíta ako Z80, teda H po CCF podľa
   Z80 (bod 6 to nerozhoduje). Obal chybu chytí: s NEG pokazeným v kópii
   jadra (`subb(z, 0, z->a, z->cf)`) vypísal
   `neg ... ERROR **** crc expected:6a3c3bbd found:5330202b` a `FAIL`
   s kódom 1. V `run-tests.bat` nie je, lebo program nie je v repozitári.
9. `tools/z180dis.py` nepozná OTIM, OTDM, OTIMR ani OTDMR a vypisuje ich ako
   `db ED,93` (`ea4-6lc`).

### 6.34 Posuvníky hlasitosti a rýchlosti reči — spravené (`ea4-06x`)

Eureka mala dva posuvníky: ľavý menil rýchlosť reči, pravý hlasitosť.
Emulátor držal rýchlosť na pevných `80h` (`kRatePotLevel` v `machine.cpp`,
s poznámkou, že hodnota nie je nikde doložená) a hlasitosť nemal.

Manuál k tomu hovorí málo. `IOPORT.H` (príloha H) pomenúva „speech rate pot
(the left hand slider control)“ na komparátore `vm1` pri `vmsel` v jednotke
a nič viac. Hlasitosť v manuáli nie je vôbec — je to analógový potenciometer
za DAC a firmvér o ňom nevie.

#### Ako firmvér číta posuvník rýchlosti

Odčítané z kódu 14. 9. 2026 a na troch miestach (`004EB`, `001BB`, `00258`)
skontrolované priamo v bajtoch ROM.

- **Binárnym vyhľadávaním nie.** Vyhľadávanie OS na `192B5` sa volá len pre
  kanály 0, 1 a 3 (teplomer, voltmeter, batéria; `19C6D`, `1878B`).
- `004EB` nastaví `vmsel` (`C438h` bit 6, port `B0h`) na začiatku každej
  dávky reči (rutina `04C5`, volaná z `00337`, `004B0`, `0095F`).
- Obsluha prerušenia reči na `1D015` pošle vzorku na DAC a uloží ju do
  `C43Bh`. **Každú ôsmu vzorku** alofónu (`01A5`) aj pauzy (`0243`) porovná
  `001BB`–`001EE` bit `vm1` s touto vzorkou a odhad v `F833h` k nej posunie:
  keď je posuvník nad odhadom a odhad pod vzorkou, odhad sa zdvihne na
  vzorku, a naopak. Pri inicializácii je odhad `80h` (`00290`).
- Z odhadu skladá `01F0` → `0258` reload časovača:
  **`RLDR0L` = výška (`F802h`) − `F833h`/8**, `RLDR0H` = 0 (`00261`).
  Výška je predvolene `38h` = 56 (`C43Eh`), mení ju `!%P` (`03FF9`)
  a intonácia o ±2 (`03407`).
- Vzorkovacia frekvencia reči je teda 307 200 / (RLDR0 + 1) Hz a posuvník ju
  mení **ako rýchlosť pásky: reč je rýchlejšia a zároveň vyššia**. Výšku
  firmvér nekompenzuje. Pauzy (`!%W` aj medzi slovami) sa počítajú vo
  vzorkách, takže sa skracujú s rečou.
- Firmvér rozlišuje **32 polôh**. Pri výške 56 je to teoreticky 5 389 až
  11 815 Hz.
- Odhad nadobúda len hodnoty vzoriek, ktoré naozaj zazneli, takže krajné
  polohy obmedzuje rozkmit reči: pri `FFh` sa zastavil na `E0h`–`E7h`
  (`RLDR0` = `1Ch`), nie na `19h`. Na hardvéri je to tak isto.
- **Kliky** cez `.spchar` (`04010` → `03B03`) majú polovičnú váhu:
  `RLDR0` = výška/2 − `F833h`/16. **Tóny `!%T`** (`005DD`–`00676`),
  **melódie** (`0FB12`, `RLDR0` = `1Ah`) ani `19136` (`RLDR0` = `28h`) na
  posuvníku nezávisia.

Polarita komparátora (nastavený bit = DAC nad vstupom) sedí s binárnym
vyhľadávaním OS (`192BC`, `SUB D`) aj s meraním nižšie; na hardvéri
overená nie je.

#### Zmerané v emulátore

Veta „hlavní menu“ (F10), vždy 9 416 vzoriek na DAC, pred zavedením
posuvníka, podľa pevnej úrovne:

- `00h`: `RLDR0` = `38h`, 5 390 Hz, 1 747 ms
- `40h`: `RLDR0` = `31h`, 6 145 Hz, 1 532 ms
- `80h`: `RLDR0` = `28h`, 7 480 Hz, 1 259 ms
- `C0h`: `RLDR0` = `21h`, 9 036 Hz, 1 042 ms
- `FFh`: `RLDR0` = `1Ch`, 10 594 Hz, 889 ms

Po zavedení tokenom sondy: poloha 0 dáva zápisy `RLDR0L` `2Fh`–`33h`,
poloha 31 `1Ch`. Hlasitosť 0 dala 48 000 vzoriek s rozkmitom 0 až 0.

#### Dve vedľajšie zistenia

- **`80h` je presne úroveň ticha.** Pri rovnosti firmvér volá `01F0`
  a reštartuje časovač 0 každú ôsmu vzorku pauzy: 114 zápisov `RLDR0` na
  jednu vetu, pri iných úrovniach jeden. Odtiaľ je pravdepodobne väčšina
  z 13 041 prepisov v sekcii 3 — dohad, znova sa to nemeralo.
- **Úvodná veta môže byť pomalá.** `C43Bh` je po štarte `00h`, kým DAC už
  stojí inde, takže pri polohe pod `C0h` odhad spadne na `00h` (`RLDR0` =
  `38h`, `00264`). Je to správanie firmvéru, emulátor ho neopravuje.

#### Čo sa spravilo

- **`src/sliders.h`** je jediné miesto s rozsahmi. Rýchlosť má 32 polôh na
  úrovni poloha × 8 + 4, teda v strede kroku firmvéru; predvolená je 16
  (`84h`), ktorá dáva ten istý reload ako doterajších `80h`, ale nesedí na
  úrovni ticha. Hlasitosť má 21 polôh: vrch je doterajší plný výstup
  (špičky okolo 27 900 z 32 767, nad tým by sa orezávalo), predvolený stred
  je o 10 dB nižšie, nad stredom kroky po 1 dB, pod ním po 2 dB a poloha 0
  je ticho. Stred hlasitosti chcel majiteľ, aby sa dalo ísť oboma smermi.
- **Stroj:** `SetRatePot` (úroveň na stupnici DAC, ide do `ReadInputBuffer`)
  a `SetVolume` (zosilnenie na konci `RenderAudio`). `Reset` ani `PowerOn`
  ich nemenia.
- **Nastavenia:** `rychlost-reci=` a `hlasitost=`. Chýbajúci kľúč je stred,
  slovo sa preskočí, číslo mimo rozsahu sa pritiahne na koniec.
- **Okno:** ponuka `S&troj` (dovtedy `&Stroj`, bila sa so `&Súbor`) s
  položkami Rýchlejšia reč, Pomalšia reč, Hlasnejšie, Tichšie a Nastaviť
  posuvníky; hostiteľské skratky `Ctrl+šípka` vľavo a vpravo pre rýchlosť
  a `Alt+šípka` pre hlasitosť; dialóg `IDD_SLIDERS` s dvoma posuvníkmi
  Windows; každý krok sa hneď uloží. Na konci posuvníka krátky tón 1600 Hz.
- **`Alt` v hostiteľskej tabuľke je pre jednorazovku bezpečný** rovnako ako
  `Ctrl`: samotné stlačenie `Alt` príde do okna ako `WM_SYSKEYDOWN`
  a `HostKeepsKey` nastaví `passOnceUsed_`. Majiteľ vyskúšal, že pustenie
  `Alt` po `Shift+F11` a `Alt+šípke` ponuku neotvorí.
- **Doplnok NVDA 1.1.0** ohlási novú polohu („hlasitosť 12 z 20“, „rýchlosť
  17 z 32“ — rýchlosť od jednotky podľa želania majiteľa). Emulátor
  zverejňuje polohy vo vlastnostiach okna `EurekaA4.SpeechRate`
  a `EurekaA4.Volume`. Doplnok sleduje klávesy cez
  `inputCore.decide_executeGesture`, ktorý NVDA volá pred kontrolou spánku,
  a nie skriptom, lebo skript aplikačného modulu by v dialógoch vzal
  editačným poliam `Ctrl+šípku`. Po klávese sa pozrie o 30, 100 a 250 ms.
  Podrobne v `nvda-addon/README.md`.
- **Sonda:** tokeny `rychlost:N` a `hlasitost:N`. **`settings_test`:**
  13 nových kontrol.

#### Otvorené

1. **Kam siahal skutočný posuvník rýchlosti.** Celý rozsah `00h`–`FFh`
   a „vyššie napätie je rýchlejšie“ je dohad. Majiteľ si nepamätá, či
   najvyššia rýchlosť znela tak ako teraz. Rozhodne to nahrávka jednej vety
   pri oboch krajných polohách: z dĺžky vety sa dopočíta `RLDR0` a z neho
   úroveň. Mení sa len `sliders::RatePotLevel`.
2. **Hlásenie v NVDA** je v čase zápisu overené prekladom a kontrolou
   modulu, nie ešte naživo.
   **Uzavreté 14. 9. 2026:** majiteľ vyskúšal naživo, doplnok funguje, ako
   má.
3. **Test cez ROM** na to, že posuvník dôjde do firmvéru, neexistuje; je to
   overené len sondou.

### 6.35 Ťuknutie na Ctrl zastaví reč aj skladbu — spravené (`ea4-ooy`)

Používateľ NVDA ťukne na Ctrl, aby umlčal reč, a robí to bez rozmýšľania.
Nad oknom stroja NVDA spí, takže gesto bolo mŕtve. Na externej klávesnici
navyše chýba kláves, ktorý by reč zastavil a nič iné neurobil: shift ani
Ctrl nič nedoručia, a preto nič nezastavia (`hardware-map.md`, „Aj externá
klávesnica zastaví reč“). Membrána taký kláves má — shift — a je pripojená
aj vtedy, keď sa píše na externej klávesnici. Ťuknutie na Ctrl ho preto
v oboch režimoch krátko stlačí. Na hosťovi sa nič neberie, samotné Ctrl
na Eureke nerobí nič (zmerané, tá istá tabuľka).

#### Čo sa spravilo

- **Stroj:** `TapShift()` stlačí shift membrány (riadok `8Ch`, bit 6)
  a po `kShiftTapMs` = 40 ms hosťovského času ho sám pustí. Pulz sa ráta
  v cykloch od jedného okamihu, takže ho neskráti ani to, že vlákno púšťa
  hosťa po dávkach. `HoldShift` rozpracované ťuknutie ukončí.
- **Vlákno:** ťuknutie rozpoznáva `applyKey` v oboch režimoch. Ctrl musí ísť
  dole bez Shiftu a Altu, medzitým nesmie prísť žiadna iná udalosť klávesu
  a pustenie musí prísť do 350 ms (`kCtrlTapMs`). Pulz sa pošle až po
  preklade pustenia, lebo braillovský režim pri každej udalosti zosúlaďuje
  shift so skutočne drženým. V režime externej klávesnice každá iná udalosť
  klávesu pulz skráti, aby kláves napísaný hneď po ťuknutí nevyšiel
  s membránovým shiftom vedľa seba.
- **Vo vlákne, nie v okne, ako navrhoval bead.** Vlákno dostane presne tie
  klávesy, ktoré idú Eureke. Po `Shift+F11` a pri nachystanej jednorazovke
  okno Ctrl nepošle a hostiteľské skratky zje akcelerátorová tabuľka, takže
  podmienka `!released_ && !passOnce_` platí sama a netreba nový príkaz.
  AltGr je ľavý Ctrl s pravým Altom a Alt, ktorý príde medzi ne, ťuknutie
  zruší. Uvoľnenie klávesnice ide cez `kFocusLost`, ktorý ho zruší tiež.
- Hostiteľský tón nie je žiadny, spätná väzba je zastavená reč. Keď nič
  nehrá, ťuknutie nerobí nič, tak ako shift na stroji.

#### Zmerané 15. 9. 2026

- **Prehrávač melódií** číta `8Ch` na `10F13` každých 13,7 až 20,0 ms
  hosťovského času, typicky 13,8 ms (352 čítaní počas celej znelky). Tieň
  `B0F5h` nastaví pri štarte (`10ECC`–`10ECF`) a obnoví pri každom čítaní
  (`10F19`–`10F1B`), takže zastaví len **nový** bit — shift držaný od
  začiatku znelku nezastaví. Riadok `89h` na `10F1C` zastaví akýkoľvek bit.
- Pulz 20, 50, 80, 120, 200 aj 300 ms, každý v desiatich časoch medzi 1,0
  a 1,6 s po F7, zastavil znelku 10 z 10 (RMS 0); bez pulzu hrala (RMS
  5 252). Zvolených 40 ms sú dve čítania prehrávača a zároveň tri takty
  heartbeatu, rovnako ako `kMinPressMs`. Vzorková slučka reči číta riadky
  rýchlosťou DAC, tá pulz nemôže minúť.

#### Testy

- `kbd`, `CheckShiftTap`: ťuknutie zastaví vetu „kde som“ spustenú
  z membrány aj scancodom `44h` z externej klávesnice, a pol sekundy po ňom
  holý medzerník nevyjde ako Escape. Holý medzerník tam necháva `C638h` na
  `00h`, preto sa kontrola pýta len na `1Bh`. Overené mutáciou: pulz dlhý
  2 s ju zhodí.
- `hudba`: tretí beh s ťuknutím namiesto medzerníka.
- Druhá mutácia, pulz 0 ms, zhodí všetky tri: reč z membrány (786 910
  krokov s ťuknutím aj bez neho), reč z externej klávesnice (787 715) aj
  znelku (RMS 5 252). Tým je doložené aj to, že F10 zo scancodu `44h` reč
  naozaj spustí, takže kontrola externej klávesnice nie je prázdna.

#### Otvorené

- **Rozpoznanie ťuknutia test nekryje.** `applyKey` beží vo vlákne, ktoré
  testy nepoháňajú; ručné overenie v oboch režimoch je na majiteľovi.
  **Overené naživo 15. 9. 2026:** majiteľ ťuknutie vyskúšal v oboch
  režimoch a funguje.
  Reaguje subjektívne „lenivejšie“ ako shift, a to je očakávané: shift
  zastaví reč pri stlačení, ťuknutie až pri pustení Ctrl, lebo dovtedy sa
  nevie, či nejde o Ctrl s ďalším klávesom. Oneskorenie je teda dĺžka
  stlačenia, nie hranica 350 ms.

### 6.36 Externé porty sa dekódujú po blokoch — DAC aj na 8Ch

Spätná väzba od používateľov 16. 9. 2026: demo EUŠOU (HEMRNA Software,
1994, disketa `C:\b\diskety\eusou`) v emulátore nehrá a `OUT` z BASICu
„nereaguje“. Autor správy to pripisoval chýbajúcemu „podtaktovaniu“
procesora. Tá príčina to nie je, emulátor počíta čas po taktoch Z180 (6.10).

#### Príčina

`AUTOEXEC.COM` z dema posiela vzorky na **port `8Ch`**: slučka načíta bajt,
pripočíta `80h`, zapíše ho cez `OUT (8Ch),A` a počká cez `DJNZ`. Tá istá
adresa slúži aj na nastavenie pokojovej úrovne (`OR 8`, `OUT (8Ch),A`).
`IOPORT.LIB` delí priestor `80h`–`BFh` na bloky po ôsmich adresách
(„88-8F Digital to analog converter“) a `hardware-map.md` to tvrdila od
začiatku. `WritePort` však poznal len presnú adresu `88h`, takže zápis na
`8Ch` ticho zahodil. ROM používa vždy len prvú adresu bloku, preto sa to
dovtedy neprejavilo.

#### Čo sa zmenilo

- `hw::DecodedPort(port, write)` v `src/eureka_io.h` vráti pre bloky
  s jediným registrom (modemový, napájací a výstupný latch, vstupný buffer
  a vypínač) prvú adresu bloku. Pre `88h`–`8Fh` to robí len pri zápise,
  lebo čítanie vyberá riadky klávesnice jednotlivými bitmi. Hodiny
  (`90h`–`97h`) a radič (`98h`–`9Fh`) sú bez zmeny.
- `ReadPortInner` aj `WritePort` sa naň pýtajú. `io_` teda drží hodnotu
  pod prvou adresou bloku, a preto `debug_io(0x88)` v režime `dc` platí
  ďalej.

#### Test

`dc`, `CheckDacDecodesWholeBlock`: po sekunde ticha dostane každá adresa
`88h`–`8Fh` skok na plný rozsah a skok musí byť počuť. Overené mutáciou:
keď `DecodedPort` pre DAC vráti holý port, zlyhá všetkých sedem aliasov,
`88h` prejde. Stále je 16 riadkov `PASS`.

#### Otvorené

- Demo v sonde ešte **nebežalo**. Pred spustením kontroluje bajt `CFh`
  na logickej `CA00h` („Nepovolená manipulace“), čo je nejaká ochrana
  a nie je jasné, či ju model splní.
  **Overené naživo 16. 9. 2026:** majiteľ demo s opravou spustil a hrá.
  Kontrolu na `CA00h` teda model splní; čo presne ten bajt znamená,
  zostáva nezistené.
  **Neplatí (16. 9. 2026):** model ju sám nesplní. Na čistej kópii
  diskety demo v sonde povie „Nepovolená manipulace“; hralo len po cracku
  `EUSPATH.COM`, ktorý majiteľ v každom behu spúšťa. `CFh` je „O“
  s ôsmym bitom, viď ďalšiu podsekciu.
- Či sa `9Ch`–`9Fh` zrkadlí na `98h`–`9Bh` (WD177x má dve adresné
  linky), manuál nehovorí. Nechané bez zmeny.
- Príkaz `OUT` z BASICu: nevedno, na ktorý port ho autor správy skúšal.
- Druhá časť tej istej správy („zápis ôsmeho bitu kvôli ochranám“) sa
  pravdepodobne týka atribútov CP/M v ôsmom bite mena. `Trim`
  v `virtual_disk.cpp` ich pri zápise do priečinka zahadzuje. Zatiaľ
  neodmerané.
  **Odmerané 16. 9. 2026**, viď ďalšiu podsekciu (`ea4-iwb`).

#### Ochrana v mene súboru — EUŠOU a Sokoban (`ea4-iwb`)

Obe hry HEMRNA Software chránia disketu menom súboru v adresári, nie
obsahom. Cracky `EUSPATH.COM` a `SOKPATH.COM`
(`C:\b\diskety\cracks`, Turbo Pascal) meno len prepíšu. Ich účinok
vydrží do konca behu emulátora, lebo pri ďalšom načítaní priečinka sa
meno zostaví znovu z mena súboru na hostiteľovi.

Doložené disassemblom:

- `AUTOEXEC.COM` dema na `0320h`: `LD A,(CA00h)`, `CP CFh`, pri nezhode
  „Nepovolená manipulace“ a `JP 0000h`. `CFh` je „O“ (`4Fh`) s ôsmym
  bitom, teda druhý znak prípony `COM` s atribútom t2 (v CP/M SYS). Že
  `CA00h` je bajt t2 v kópii mena spúšťaného programu, je odhad podľa
  hodnoty; miesto v ROM, ktoré ho tam zapisuje, nie je vystopované.
- `EUSPATH.COM` na `21F3h` pripraví meno `41 55 54 4F 45 58 45 43 2E 43
  CF 4D` a súbor `AUTOEXEC.COM` naň premenuje.
- `SOKPATH.COM` na `21A9h` pripraví meno `37 E8 45 4D 52 4E 41 2E D5`
  („7“, malé „h“ s ôsmym bitom, „EMRNA“, „U“ s ôsmym bitom) a súbor
  vytvorí. Sokoban si to isté meno skladá za behu na `2100h` a otvára ho
  cez BDOS 15 z `1450h`; zo súboru nečíta nič (má 0 bajtov).

Odmerané v sonde s dočasným tokenom, ktorý po vložení diskety prepíše
bajty mena v adresári obrazu (každý beh na čerstvej kópii diskety):

- EUŠOU, bez úpravy: „Nepovolená manipulace“. S t2 = `CFh` v mene
  `AUTOEXEC.COM`: „Firma HEMRNA Software uvádí fantastické demo“, demo
  dohralo až po záverečnú melódiu, reproduktor dostal vzorky s rozkmitom
  −28 755 až 30 151. Ôsmy bit je celá ochrana dema.
- Sokoban, bez úpravy (meno `7HEMRNA.U`, ako ho dnes zostaví import):
  „Pozor, toto není originální disk“. Varianty mena:
  `37 E8 … D5` (presne ako crack) prijal, `37 C8 … D5` (veľké „H“
  s bitmi) odmietol, `37 68 … 55` (malé „h“ bez bitov) prijal.
- Z toho: **ROM pri hľadaní súboru rozlišuje veľké a malé písmená
  a ôsmy bit ignoruje.** Sokoban teda chráni malé písmeno, nie atribút.

Kde to emulátor stráca, dve nezávislé miesta:

- `Trim` v `src/virtual_disk.cpp` maskuje `7Fh`, takže meno zapísané do
  priečinka atribúty nemá (EUŠOU).
- `NamePart` v `src/cpm_disk.cpp` pri načítaní priečinka prevedie
  písmená na veľké (Sokoban). To zrušiť nejde: bežné súbory z Windows
  majú malé písmená a na diskete musia byť veľkými.

Otvorené — návrh riešenia, ešte nezačaté: postranný súbor `.disk`
v priečinku diskety s presnými jedenástimi bajtmi mena pre súbory,
ktorých meno sa do mena na hostiteľovi nezmestí (ôsmy bit alebo malé
písmeno). Vznikne len vtedy, keď taký súbor existuje. Celý 8 KB adresár
sa neukladá, lebo čísla blokov sa pri každom načítaní rátajú nanovo
a súbory v priečinku sa medzi behmi menia. Najcitlivejšie miesto je
kľúč `imported_` (dnes meno bez ôsmych bitov), na ktorom visí presun
zmazaných súborov do `.eureka-trash`. Sokoban z dnešného priečinka
(`7hEMRNA.U` sa načíta ako `7HEMRNA.U`) to samo neopraví, kým raz
neprejde crackom a disketa sa neuloží.
**Už neplatí „nezačaté“ (16. 9. 2026)** — urobené, viď nasledujúcu časť.
Súbor sa podľa rozhodnutia majiteľa volá `.eureka`, nie `.eureka-mena`.

#### Súbor `.eureka` — presné mená v priečinku diskety

- `src/virtual_disk.cpp`: riadok `meno súboru v priečinku`, tabulátor,
  22 šestnástkových znakov (bajty 1–11 položky adresára), UTF-8, LF.
  Zapisuje ho `ExportImage` (pri `Flush` aj `SaveAs`) pre každý súbor,
  ktorého meno má ôsmy bit alebo malé písmeno, **ešte pred skratkou pre
  nezmenený obsah** — EUSPATH mení len atribút. Zapisuje sa cez
  `.eureka-novy` a premenovanie, prepisuje sa len pri zmene a zmaže sa,
  keď ho netreba. Bajty sa berú z položky s najnižším extentom.
- `BuildImage` riadok použije podľa mena súboru v priečinku, len ak jeho
  meno ešte nedrží iný súbor; inak súbor dostane obyčajné meno. Riadok,
  ktorý sa nedá prečítať, sa preskočí; nečitateľný `.eureka` je chyba
  pripojenia. `ScanFolder` preskakuje všetko, čo začína `.eureka`.
- Kľúč `imported_` sa nezmenil: `DirectoryName` už predtým malé písmená
  zachovávala a ôsmy bit zahadzovala, čo je presne porovnanie ROM.
- Nová tichá chyba, ktorú to odkrylo: dve mená líšiace sa len veľkosťou
  písmen (po SOKPATH `7HEMRNA.U` a `7hEMRNA.U`) sa predtým zapísali do
  jedného súboru vo Windows. Teraz nový súbor dostane voľné meno s `~N`
  (`FreeLeaf`); rezervované sú vopred mená všetkých súborov, ktoré
  priečinok mal.
- Test: `ExactNamesSurviveTheFolder` a `ExactNamesFileIsForgiving`
  v `disk_test`. Overené mutáciou: bez zápisu riadku padne šesť kontrol,
  bez `FreeLeaf` päť. Stále 16 riadkov `PASS`.
- Naživo v sonde, na kópiách diskiet: EUŠOU → EUSPATH → uloženie → nový
  štart emulátora bez cracku → „Firma HEMRNA Software uvádí fantastické
  demo“; tá istá kópia bez `.eureka` → „Nepovolená manipulace“. Sokoban
  pred crackom „Pozor, toto není originální disk“, po SOKPATH, uložení a
  novom štarte „Logická hra Sokoban“; v priečinku pribudol `7hEMRNA~1.U`
  a `.eureka` s `37E8454D524E4120D52020`. Rovnaký výsledok vznikol aj
  v `C:\b\diskety` behom v okne.
- Pasca v sonde, na ktorú sa pri tom narazilo: token `mount:` disketu pred
  výmenou **neuloží** (`MountDisk` bez `FlushDisk`), hoci CLAUDE.md hovorí
  opak; ukladajú `folder`, `ram` a `slot1`/`slot2`. Meranie uloženia preto
  išlo cez `folder`. Stratu pred touto zmenou teda dokladá kód (`Trim`,
  `NamePart`), nie beh so zápisom.

### 6.37 Stopky si pamätajú čas štartu, nie uplynulý čas

Otázka majiteľa 18. 9. 2026: stopky spustené v emulátore bežia ďalej aj po
jeho zatvorení a novom spustení (každú sekundu „píp“). Ako to firmvér drží?
Manuál hovorí len toľko, že stopky merajú cez RTC (`HARDWARE.1`, riadok 68;
`SERVICE.3`, riadok 61). Premennú ani volanie pre ne neuvádza.

Ovládanie podľa majiteľa: F2 z hlavného menu, ďalšie F2 štart, ďalšie
zastaví a povie čas a „stop“, ďalšie „vynulováno“. F3 povie medzičas,
pri stojacich stopkách „0 vteřin“.

#### Zmerané

Stav je päť bajtov na `7C44E`–`7C452` (logicky `C44E`–`C452`), v bloku
trvalých dát `C43Ch`–`C508h`. Význam bajtov, adresy zápisov a pravidlo
o dátume sú v `hardware-map.md`, sekcia Stopky. Pri štarte sa ukladá
**absolútny čas z RTC bez dátumu**, pri hlásení sa odčíta.

Teplé vypnutie so štartom stopiek, Escape do menu, `vypni`, posunom
hodín, `zapni`, F2, F3, F2, F2 (stav zostal `FEh`):

- `cas:+2h`: „2 HODINY0 VTERIN27 SETIN.“, potom „STOP.“ a „VYNULOVANO.“.
  Čas vo vypnutom stroji sa **započíta**, rovnako ako na skutočnom stroji,
  kde RTC beží z batérie.
- `cas:+5h` cez polnoc (20:47 → 01:47): „5 HODIN0 VTERIN35 SETIN.“.
- `cas:+1d`: „0 VTERIN35 SETIN.“. Celý deň sa stratil. Je to vlastnosť
  firmvéru, emulátor ju nemá čo opravovať.

Príkazy sondy (F2 = `s3C`, F3 = `s3D`, Escape = `s01`):

```
diag_probe ROM DISK seq 15000000 . . s3C . . . s3C . . s01 . . vypni cas:+2h zapni . s3C . . . . s3D . . . . s3C . . . . s3C . . . .
```

Adresy sa hľadali dočasnými tokenmi sondy (snímka a rozdiel RAM, výpis
bajtov, sledovanie zápisov do rozsahu, záznam čítaní RTC). Tie sú len
v kópii mimo repozitára, `C:\b\eureka-a4-scratch\stopky\`.

#### Otvorené

- V prepise reči prišlo hlásenie času aj „STOP.“ o jedno F2 neskôr, než
  by zodpovedalo opisu majiteľa, a zápis nameraného času do RAM sa ukázal
  tiež až pri ďalšom F2. Či reč len mešká za klávesom, alebo kláves čaká
  vo fronte, nie je overené. Na emulátore v okne si to majiteľ nevšimol.
  Vstup do hodín to nie je: prvé F2 po zapnutí povie čas dňa („23 HODINY.“,
  „1 MINUTA.“), F3 medzičas, ďalšie F2 „2 HODINY0 VTERIN34 SETINY.“ a potom
  štyri čakania `.` ticho. „STOP.“ a hneď za ním „VYNULOVANO.“ prišli až po
  ďalšom F2. Zopakované 18. 9. 2026 s rovnakým výsledkom.
  **Uzavreté 18. 9. 2026:** v okne firmvér povie čas aj „stop“ pri tom
  istom F2, potvrdil majiteľ. Posun je len časovanie sondy a na stav
  stopiek ani na adresy vplyv nemá.

### 6.38 Praskanie pri hudbe — DAC sa čítal v okamihu, nie cez interval (`ea4-wr1`)

Majiteľ ohlásil 22. 9. 2026 praskanie pri prehrávaní melódií. Meralo sa na
`BACH.MEL` (896 B, mimo repozitára v `C:\b\xx`), načítanej do hudobného
editora z diskety a prehranej na mieste.

Cesta k nej je v sonde jeden riadok a stojí za to si ju zapísať, lebo ju
nikto neuhádne: `kC6` (F7, hudobný editor), `kD4` (Shift+F5, „nahrání
skladby z disku“), `bach~`, `kC2` (F3, prehrať). Firmvér odpovie „nahrávám
skladbu z disku“ a „oukej“.

#### Čo sa ukázalo nepravdivé

Prvá hypotéza bola podtečenie zvukovej fronty — vlákno emulátora spí 2 ms
na normálnej priorite, kým cieľ je 22 ms, a jedno oneskorenie plánovača by
stačilo. **Zmerané a nepravda.** Počas hudby: fronta najnižšie 16,4–16,9 ms,
nikdy pod 3 ms, najdlhší krok slučky 3,2–3,7 ms, zahodených blokov nula.
To, čo emulátor odovzdáva zvukovke, bolo porovnané so sondou a nemá oproti
nej žiadne diskontinuity navyše (`|diff|` p999 k rozkmitu 0,189 naživo proti
0,156–0,179 zo sondy). Real-time cesta je čistá a nič sa v nej neopravovalo.

Pozor na pascu v tom meraní: bežali v ňom chvíľu **dva emulátory naraz** a
písali do toho istého záznamu, takže prvé čísla ukazovali šestnásťnásobok
reálneho času. Kto to opakuje, nech si najprv overí, že proces je jeden.

#### Príčina

`RenderAudio` bral hodnotu DAC **v okamihu** výstupnej vzorky. Pri melódii
DAC kročí každých 540 cyklov (`RLDR0` = 26, 6.10), čo je pri 48 kHz **4,22
výstupnej vzorky** — nie celé číslo. Bodovým čítaním vyšiel jeden schod
štyri vzorky široký a ďalší päť, takže hrana schodu kmitala až o celú
vzorkovaciu periódu. Ten jitter nie je v stroji, je v spôsobe, akým sa
stroj odčítava, a prejaví sa ako šum položený pod signál — teda najhlasnejší
tam, kde je signál najhlasnejší. Preto sa počul ako praskanie hudby a nie
ako sykot.

Zmerané na syntetickom tóne 440 Hz cez ten istý 11 378 Hz hold, šum
v pásme 100 Hz – 8 kHz mimo harmonických:

| čítanie DAC | odstup šumu |
|---|---|
| bodové (pôvodné) | **43,3 dB** |
| integrované cez interval vzorky | **63,5 dB** |

Reč to trápilo menej iba náhodou: pri 7,5 kHz takte DAC je schod 6,4 vzorky,
takže tá istá chyba je menšia časť jedného kroku.

#### Oprava

Výstupná vzorka je úroveň DAC vážená počtom cyklov, počas ktorých na
výstupe naozaj bola (`audioAcc_`). **Nie je to aproximácia integrálu, je to
integrál**: medzi zápismi je signál konštantný, takže váženie cyklami
rekonštruuje presne tú vzorku, akú by hardvér podal ideálnemu 48 kHz
rekordéru. Stojí to jedno násobenie na inštrukciu.

Na skladbe, úsek 10.–20. sekunda: `|diff|` p99 klesol z 2816 na 2464, p999
z 4541 na 3953, teda o 13 % menej strmých prechodov pri rovnakom rozkmite.
Majiteľ aj druhý posluchač potvrdili, že je to **oveľa čistejšie**.

#### Čo sa pritom vylúčilo

Nové tokeny sondy (`wav:SUBOR`, `dac:N`) dali tri odpovede, ktoré by sa inak
hádali:

- **Prerušenie PRT0 sa nikdy nestratí.** 360 080 zápisov DAC a rozostup
  medzi nimi je vždy presne 540 cyklov, ani raz dvojnásobok. Pozor: merať sa
  musia **zápisy, nie zmeny hodnoty** — generátor tónov podá vzorku pri
  každom prerušení aj keď sa hodnota nepohla, takže sledovanie zmien ukáže
  falošné medzery (najprv ich takto ukázalo 15 311).
- **Bajt DAC nepretečie.** Najväčší skok hodnoty je 78; wrap by bol okolo
  250. Firmvér sčítava štyri hlasy po −32..+31, čo sa do bajtu práve vojde.
- **Rekonštrukčný filter sa počas hudby neprepína.** `filtersel` (`B0h`
  bit 4) hýbe len rečový modul (`0174h`/`0187h`); počas melódie stojí
  `B0h` na `6Ch`, teda na **otvorenom** filtre. Prepnutie by skokom zmenilo
  `b0` z 0,48 na 0,81 a lupnutie by z toho bolo, ale nedeje sa.

#### Test

`integration_test ROM DISK zvuk` (`CheckDacReconstruction`). Nepúšťa
firmvér vôbec: podá DAC 440 Hz sínus cez `debug_feed_dac` v takte melódie
a Goertzelom zmeria odstup šumu mimo harmonických. Prah je 55 dB, teda
desať decibelov rezervy na obe strany. **Overené mutáciou:** vrátenie
bodového vzorkovania dá 43,42 dB a test padne — pričom režim `hudba`
prejde aj tak, takže je to jediné, čo túto vlastnosť drží.
`ALL_MODES` má odvtedy pätnásť režimov a `run-tests.bat` dáva **osemnásť**
riadkov `PASS`, bez manuálu šestnásť.

#### Otvorené

Zostalo „sem tam malé lupnutie“ na hlasitých miestach, ktoré obaja
posluchači počujú aj po oprave. Zmerané o ňom je zatiaľ len to, čo je
vyššie — teda že to **nie je** vynechané prerušenie, pretečenie bajtu ani
prepnutý filter. Najpravdepodobnejšie sú to vrcholy pílového priebehu:
skok DAC o 78 je 30 % plného rozsahu naraz a hardvér ho urobí tiež.
Rozhodnúť to môže nahrávka skutočného stroja, nie ďalšia úvaha.

Druhá vec, ktorá je tesná a nie je dokázaná ako príčina: pri plnej
hlasitosti dosahuje `BACH.MEL` **30 073 z 32 767**, teda 0,76 dB pod
orezaním v `std::clamp`. Táto skladba neoreže ani raz, ale hlasnejšia
melódia by narazila a znelo by to presne ako praskanie. Majiteľ má
posuvník prakticky na maxime (rozkmit jeho živého behu sedí s rozkmitom
sondy pri zisku 1,0).

### 6.39 Vstavaný BASIC adresuje porty šestnásťbitovo — `OUT 50,240` je pravda (`ea4-dfd`, `ea4-vii`)

Kolujúce tvrdenie hovorí, že `OUT 50,240` v BASICu spomalí procesor. Preverené
22. 9. 2026 a **je pravdivé**, len nejde o takt: port 50 desiatkovo je `32h`,
teda `dcntl` (`IOREG.LIB:73`, DMA/WAIT Control Register), a `240` = `F0h`
nastaví jeho bity 7–6 (MWI1, MWI0) na tri čakacie stavy do **každého
pamäťového cyklu**. Kryštál beží ďalej, pribudne čakanie.

Firmvér tam zapisuje `38h` (fyz. `00012`: `3E 38` / `ED 39 32`), teda MWI = 0
a IWI = 3; to druhé dokladá aj `SYSEQU.LIB:19` slovom — `slowio equ true ;for
4 wait states on IO`.

**Prečo to vôbec môže fungovať.** Adresa portu je šestnásťbitová a interné
registre odpovedajú len pri nulovom hornom bajte. Pri `OUT (n),A` ide do
horného bajtu akumulátor, takže s hodnotou 240 by adresa bola `F032h` a
netrafila by nič. Vstavaný BASIC to však robí druhou cestou — obsluha príkazu
`OUT` je na fyz. `0B5C1` (položka 17 v tabuľke skokov na `0A333`, dispečer
`0A2C8`):

```
CD 96 DA    CALL DA96h        ; číslo portu do DE
D5          PUSH DE
CD A6 D2 2C SYNCHR ','
C1          POP BC            ; celé 16-bitové DE do BC
C5          PUSH BC
CD EB E5    CALL E5EBh        ; GETBYT — hodnota do A
C1          POP BC
ED 79       OUT (C),A
```

Že je port naozaj šestnásťbitový a neoreže sa, dokladá GETBYT na `0B5EB`:
`LD A,D / OR A / JP NZ,DAB2h` — na 0–255 sa stráži **hodnota**, nie port.
`INP` je to isté (`0B5B7`: `LD B,D / LD C,E / IN A,(C)`), takže z BASICu sa
interné registre dajú aj čítať. Logická adresa = fyzická + `3000h`; doložené
tým, že `D2A6h` je doslovný MS-BASIC `SYNCHR` a `D2AEh` `CHRGET`.

Overené aj meraním, nezávisle od rozboru: s dočasnou inštrumentáciou, ktorá
horný bajt nezahadzuje, dá `OUT 50,240` v BASICu `port=0032h value=F0h` na PC
tesne za `OUT (C),A`, a `OUT 306,240` dá `port=0132h`. Do BASICu sa v sonde
vchádza scancodom `s40` (F6).

**Čo z toho bolo v emulátore zle, a je spravené.** `WritePort` aj `ReadPort`
brali z portu iba dolný bajt, takže interné registre u nás odpovedali aj na
`0132h`. Teraz o tom rozhodujú `hw::IsInternalRegister` a `hw::IsUndecodedIo`
v `src/eureka_io.h`; nedekódovaný zápis sa zahodí (ale **do trace sa zapíše**,
lebo pokus je práve preto zaujímavý) a čítanie vráti `hw::kUndecodedIoRead`.
`ChargeIoWaits` sa zosúladilo — o tom, či je cyklus interný, rozhoduje celá
adresa, takže `0132h` je externý cyklus a čakacie stavy dostane.

`kUndecodedIoRead` je `FFh` a je to **voľba, nie meranie**: nikto nezmeral, čo
na nepoháňanej zbernici skutočná Eureka vracia. V hlavičke to tak stojí
napísané a citovať to ako dôkaz sa nesmie.

Drží to `CheckInternalRegistersNeedZeroHighByte` v režime `dc`
(`integration_test`), a drží obe strany: že `0132h` neodpovie, aj že `0032h`
odpovedať musí. Overené dvoma mutáciami — vypnutý predikát zhodí prvé dve
kontroly, predikát rozšírený aj na nulový horný bajt zhodí tretiu.

**Čo zostáva otvorené.** MWI sa ďalej **nemodeluje** (`ea4-vii`), takže
`OUT 50,240` u nás nespomalí nič; tabuľky v `z80.c` nesú celkové T-stavy, nie
počet pamäťových cyklov na inštrukciu, takže poctivé domodelovanie nie je
drobnosť. Kým firmvér drží MWI na nule, nemá to dopad na časovanie ani na
zvuk. Otvorené je aj `ea4-6dx` — jadro dáva pri `TSTIO` na horný bajt `B`
namiesto `00h`, čo je tá istá minca z druhej strany, a nedotklo sa ho to.
A neoverené zostáva, čo na skutočnom stroji spôsobí, že `F0h` vynuluje bit 3
(DMS1), ktorý firmvér nastaví a ktorý sa týka snímania DREQ pre kanál 1, teda
cesty k mechanike; bitové polia `dcntl` manuál Robotronu nepopisuje.

### 6.40 TSTIO dávalo na horný bajt adresy B — opravené (`ea4-6dx`)

Pokračovanie 6.39 a uzavretie bodu 1 zo 6.33. `TSTIO` dáva na A8–A15 `00h`,
rovnako ako `IN0`, `OUT0` a blokové `OTIM`/`OTDM` (UM005004, tabuľka 46);
`B` na horný bajt dávajú len `IN`/`OUT (C)`. Jadro volalo `port_in` s celým
`BC`. Opravené v `exec_opcode_ed`, `0x74` — číta sa z `z->c`.

**Prečo to muselo prísť hneď za 6.39.** Kým `ReadPortInner` horný bajt
zahadzoval, bola to neškodná nepresnosť: oba `TSTIO` v ROM (`185FC`, `1E0A2`)
siahajú na `STAT0`/`STAT1`, a tie sú pod `40h`, takže sa trafili tak či onak.
Po 6.39 už nie — nenulové `B` by adresu poslalo tam, kde nikto neodpovedá,
a `TSTIO` by namiesto stavu ASCI dostalo `FFh`. Cena tej chyby stúpla tou
istou zmenou, ktorá opravila dekódovanie.

**Meranie, ktoré rozhodlo, že to netreba riešiť ako regresiu.** S dočasnou
inštrumentáciou na `IsUndecodedIo` prešla celá sada a nedekódovanú adresu
trafili presne dva prístupy — oba z vlastného testu 6.39, žiadny z firmvéru.
Druhá inštrumentácia, priamo na `TSTIO`, ukázala prečo: v celej sade sa
`TSTIO` **nevykoná ani raz**. Tie dve miesta v ROM testy nenavštívia.

Z toho plynie, že opravu nemôže držať ROM, a drží ju preto test na **holom
jadre**: `CheckZ180IoAddressHighByte` v režime `dc` postaví `z80` s ôsmimi
bajtmi pamäte a portom, ktorý si pamätá len adresu, nastaví `B` na `E1h`
a pustí jednu inštrukciu. Šesť prípadov — `TSTIO`, `IN0`, `OUT0` a `OTIM`
musia siahnuť na `0004h`, `IN A,(C)` a `OUT (C),A` na `E104h`. Tie dva
posledné sú kontrola, nie ozdoba: bez nich by test prešiel aj na jadre, ktoré
by horný bajt nulovalo vždy. Overené dvoma mutáciami — `TSTIO` vrátené na
`get_bc` zhodí prvý prípad, `in_r_c` prepnuté na `z->c` zhodí piaty.

`zex_test` (ZEXDOC) po zásahu do jadra prešiel: `instructions=5764169610
finished=1 errors=0`.

Zvyšok 6.33 sa nemenil — body 2 až 4 (`ea4-jp1`, `ea4-ya6`) zostávajú otvorené.

### 6.41 Príznak N blokového I/O je najvyšší bit dát, nie jednotka (`ea4-jp1`)

Uzavretie bodu 3 a väčšiny bodu 2 zo 6.33. Prameň prečítaný priamo v PDF
22. 9. 2026: **UM005004, tabuľka 46 „I/O Instructions"**, manuálové strany
231–234 (PDF 245–248), legenda symbolov je tabuľka 36 na strane 209
(PDF 223). Hitachi HD64180Z 4th Ed. (príloha A, s. 159–160) sa s ňou zhoduje
znak po znaku.

Legenda: `·` neovplyvnený, `↑` ovplyvnený, `x` **nedefinovaný**, `S` nastavený
na 1, `R` vynulovaný, `P` parita, `V` pretečenie. Dve poznámky pod tabuľkou
(s. 234) znejú doslova: **(5)** `Z = 1 : Br-1 = 0 / Z = 0 : Br-1 ≠ 0` a
**(6)** `N = 1 : MSB of Data = 1 / N = 0 : MSB of Data = 0`.

Čo z toho platí:

- `INI`, `IND`, `OUTI`, `OUTD`: S `x`, Z `↑`(5), H `x`, P/V `x`, N `↑`(6), C `x`.
- `INIR`, `INDR`, `OTIR`, `OTDR`: to isté, len Z je pevne `S` (1).
- `OTIM`, `OTDM`: S `↑`, Z `↑`(5), H `↑`, P/V `P`, N `↑`(6), C `↑`.
- `OTIMR`, `OTDMR`: S `R`, Z `S`, H `R`, P/V `S`, C `R`, a N `↑`(6).

**Zavedené.** `ini` aj `outi` berú N z najvyššieho bitu prenášaného bajtu
namiesto konštantnej jednotky; opakovacie varianty ho dedia, lebo sú nad nimi
postavené. `z180_otim` to isté, a pri `repeat` nasadí pevnú pätici
S=0, Z=1, H=0, P/V=1, C=0.

**Čo sa zámerne nezaviedlo.** Pri `OTIM` a `OTDM` je pri S, H a C len `↑`
a pri P/V `P`, ale **z čoho sa počítajú, nestojí nikde** — ani v tabuľke, ani
v texte na s. 174, a Hitachi mlčí rovnako. Zostali preto nedotknuté. To isté
z druhej strany pri `INI`/`OUTI`: tam sú S, H, P/V a C výslovne `x`, teda
nedefinované, takže doplniť im nedokumentované pravidlo Z80 by znamenalo
tvrdiť za Zilog niečo, čo Zilog netvrdí. Nepriama indícia, **nie** tvrdenie
manuálu: pevná pätica OTIMR je presne to, čo by vyšlo z `B-1 = 0`.

**Držať to nemá čo iné.** ROM má OTIMR šesťkrát a za žiadnym príznaky nečíta,
`INI` ani `OUTI` v žiadnom teste nebežia, a `zex_test` je slepý úplne —
ZEXDOC I/O inštrukcie vôbec neobsahuje. Drží to preto `CheckBlockIoFlags`
v režime `dc`, opäť na holom jadre: šesť prípadov na N a Z (`80h` proti `7Fh`,
B rovné 1 proti 2), samostatná kontrola pevnej pätice OTIMR — a tretia, ktorá
**pribíja aj to rozhodnutie nevymýšľať**: `OUTI` nesmie siahnuť na S, H, P/V
ani C. Keby ich niekto neskôr doplnil, tá kontrola ho pošle najprv prečítať
komentár v `z80.c`.

Overené tromi mutáciami, každá zazvonila na svojej vetve a inde nie:
`N = 1` v `ini`, `P/V = 0` v pevnej pätici, a dotyk na `H` v `outi`.
`zex_test` po zásahu do jadra: `errors=0`.

### 6.42 Vzorkovanie prerušenia na konci inštrukcie konečne niečo drží (`ea4-q6x`)

Oprava zo 6.33 (žiadosť o prerušenie sa rozhoduje po inštrukcii, nie pred ňou)
bola doložená len meraním dočasným počítadlom. Teraz ju drží
`CheckInterruptSampledAtEndOfInstruction` v režime `dc`.

**Prvý pokus o ten test bol zlý a stojí za to vedieť prečo.** Sledoval, či
firmvér po `OUT0 (TCR),A` na `0027C` pokračuje na `RET` na `0027F`. Cez znelku
sa tam stroj dostane trikrát a kontrola prechádzala — lenže **prechádzala aj
s vrátenou chybou**. Aby kolízia nastala, musí časovač 0 dobehnúť práve počas
`AND EEh` na `0027A`, a to sa pri hraní nestane ani raz; pôvodné meranie malo
3 prípady na 542 824 prerušení. Taká vec sa nedá vzorkovať čakaním — test
postavený na nej by chybu prestal chytať pri hocijakom posune v časovaní,
a **ticho**.

**Situácia sa preto stavia.** Do RAM sa položí jedna inštrukcia s prefixom
`ED`, časovač 0 sa nastaví ako čakajúci a povolený a do `A` sa dá `EEh` —
hodnota, ktorú tam nechá firmvérov vlastný `AND` na `0027A` a ktorá zhodí
`TDE0` aj `TIE0`. Potom jeden krok:

- `OUT0 (TCR),A` (`ED 39 10`) prerušenie dostať **nesmie** — je to práve tá
  inštrukcia, ktorá ho zakazuje.
- `IN0 A,(TCR)` (`ED 38 10`) — rovnaký stav, rovnaká dĺžka, ale nezakazuje
  nič — ho dostať **musí**.

Druhá polovica nie je ozdoba: bez nej by stroj, ktorý prerušenie neprijme
nikdy, prešiel prvou. A medzi polovicami sa `IFF1` nasadzuje späť, lebo
prijatie prerušenia ho zhadzuje a druhá kontrola by potom hlásila „scenár nie
je nabitý“ o stave, ktorý pokazila prvá.

**Prečo cez RAM a nie cez ROM.** S mapovaním, ktoré má firmvér za behu, je
logická `027Ch` fyzická `7027C` — rutina na `00277` sa v tom stave nedá
zavolať vôbec. Zmerané, nie odhadnuté; test si to overuje aj sám tým, že po
zápise prečíta bajt späť, takže ak by `8000h` prestala byť RAM, povie to
namiesto toho, aby meral nezmysel.

Pribudli kvôli tomu ladiace háčiky v `machine.h`: `debug_set_pc`,
`debug_set_a`, `debug_iff1`, `debug_set_iff1`, `debug_make_timer0_pending`
a `debug_poke`. Ten posledný píše cez MMU tak, ako by písal hosť.
`debug_make_timer0_pending` nastaví **oboje** — vlastnú evidenciu časovača aj
`TIF0` v `TCR` — lebo stav s jedným bez druhého stroj dosiahnuť nevie.

**Overené mutáciou:** vrátené poradie (`ScheduleInterrupt()` pred
`z80_execute`) zhodí presne prvú polovicu a druhú nechá prejsť —
`prerusenie prislo za OUT0 (TCR),A: PC=cc12 namiesto 8003`.

Kontrolnú polovicu mutáciou v kóde overiť **nejde** a je to vlastnosť, nie
medzera: každý zásah, ktorý vypne prerušenie časovača 0, rozbije boot, lebo je
to srdcový tep firmvéru — a zazvoní skôr poistka o RAM. Že tá polovica vie
zlyhať, je doložené inak: kým `IFF1` medzi polovicami ešte nenasadzovala,
ohlásila sa sama.

### 6.43 MWI sa modeluje — `OUT 50,240` už stroj naozaj spomalí (`ea4-vii`)

Dokončenie 6.39. Tvrdenie bolo overené a cesta k DCNTL opravená, ale samotný
účinok chýbal: `OUT 50,240` z BASICu zapísalo `F0h` a nestalo sa nič.

**Odhad, ktorý bol v beade, bol pesimistický a mýlil sa.** Hovoril, že poctivé
domodelovanie naráža na to, že tabuľky v `z80.c` nesú celkové T-stavy, nie
počet pamäťových cyklov na inštrukciu. To je pravda o tabuľkách, ale nie
o jadre: **každý** pamäťový prístup ide cez `rb` alebo `wb`, a `z->read_byte`
so `z->write_byte` sa nikde inde nevolajú. Načítanie operačného kódu tiež
(`nextb` → `rb`), čo je presne to, čo Z180 robí — MWI sa vkladá aj do cyklu M1.

Zavedené: `z80` má pole `mem_wait`, `rb` a `wb` ho pripočítajú k `cyc`,
a `WritePort` ho pri zápise do DCNTL naplní z bitov 7–6 (`hw::kDcntlMwi`).
`rw` a `ww` sa pri tej príležitosti prepísali cez `rb`/`wb`; dovtedy volali
`read_byte` priamo, a to v poradí, ktoré C nemá definované.

**Pri MWI = 0 je to nulový zásah, a to je zmerané, nie odhadnuté.** Tempo
znelky, `seq 20000000 kC6 spin:2000000`: so zavedeným účtovaním
**97 850** obehov `10E5D` na **37 343** prerušení, s dočasne vyradeným
účtovaním **97 850 na 37 343** — do posledného obehu to isté. Firmvér drží
MWI na nule, takže sa pripočítava nula.

Drží to `CheckMemoryWaitStates` v režime `dc`: `NOP`, `LD A,(HL)` a
`LD (HL),A` v RAM, každá raz s MWI = 0 a raz s MWI = 3, a rozdiel musí byť
tri T-stavy **na pamäťový prístup** — teda 3, 6 a 6. Tri rôzne počty prístupov
sú tam zámerne: implementácia, ktorá by penalizovala raz za inštrukciu, prvý
prípad prejde a na ďalších dvoch spadne. Overené dvoma mutáciami — vyradené
účtovanie zhodí všetky tri, vyradené len pri zápise zhodí presne `LD (HL),A`.

`zex_test` po zásahu do jadra: `errors=0`.

**Pozorovanie mimo rozsahu tejto zmeny,** zapísané preto, aby sa naň nezabudlo:
číslo tempa znelky vyššie (97 850 / 37 343) nesedí s tým, čo o ňom hovorí 6.33
z 12. 9. 2026 (100 971 / 37 012). Meraním vyššie je doložené, že to **nespôsobilo
účtovanie MWI** — rozdiel je rovnaký s ním aj bez neho. Prišlo teda niekedy
medzi 12. 9. a 22. 9., a kandidátmi sú zásahy 6.34 až 6.38. Testy `hudba`
aj `zvuk` prechádzajú, takže to nie je zlomené; ale 6.10 stavia na tempe znelky
ako na meradle, takže by sa to malo dohľadať.

**Vysvetlené 24. 9. 2026 (`ea4-9dt` zavretý): nie je to regresia, je to
kalibrácia tempa.** Kandidáti vyššie boli určení zle — medzi 12. 9. a 22. 9.
prišli aj tri čakacie stavy `TMDR0L` (`4b43c76`, 20. 9. 2026), ktoré podľa
6.10 posunuli znelku zo 3,9 % rýchlejšej na 0,2 % od nahrávky skutočného
stroja. Menej obehov čakacej slučky na prerušenie je presne to spomalenie,
a viac prerušení na 20 miliónov inštrukcií je ten istý dôvod z druhej strany.
Zmerané tou istou sekvenciou:

- `4b43c76^`: 100 891 na 37 014 (2,726).
- `4b43c76`: 97 850 na 37 343 (2,620) — pokles o 3,9 %.
- `c3fc9db` (HEAD 24. 9.): 97 850 na 37 343, do obehu to isté.

Zvyšných 80 obehov medzi číslom 6.33 (100 971) a `4b43c76^` je 0,08 %
a pochádza zo zásahov medzi 12. a 20. 9.; neskúmalo sa to ďalej.

### 6.44 Firmvér pamäť na 40000h–6FFFFh nepoužíva — RAM4B testuje anglické rozloženie

Podnet: `RAM4B.COM` (128 B, mimo repozitára v `C:\b\diskety\com`), poslaný
bez vysvetlenia. V emulátore povie „Konec“ a skončí do hlavného menu.
Rozobraté 23. 9. 2026: je to **test osadenia čipov RAM**, nie vypínanie.
Cez `CBR` = `35h`, `3Dh`, `45h`, `4Dh` zapíše „Dobrý večer“ na fyzické
`42000h`, `4A000h`, `52000h`, `5A000h`, teda po jednej adrese do každého
32 KB čipu, a späť ho prečíta do štyroch rutín na `8000h`–`8300h`
(`CALL F091h` + text). Potom vráti `CBR` na `FDh`, povie „Konec“, rutiny
zavolá a skončí cez `JP 0`. Podľa človeka, ktorý program poslal, povie
na plne osadenom stroji štyrikrát „Dobrý večer“ a bez „banky 4“ dvakrát.

Adresy sú rozloženie **Advanced English Eureky** z `MEMMAP.E`: `RAM 2,3`
na `40000h`, `RAM 0,1` na `50000h`, čipy po 32 KB (štandard 2 × 32 KB,
Advanced 4 × 32 KB, `HARDWARE.1`). Náš model má RAM len na `70000h`
a pod ňou zápisy zahadzuje, čítanie vráti nuly — rutiny nepovedia nič.

**Česká ROM do 40000h–6FFFFh nesiaha.** Zmerané sondou s dočasným
počítaním každého prístupu CPU aj DMA do toho pásma a každého zápisu do
BBR a CBR: boot, `sweep` všetkých F-klávesov aj so Shiftom, hudobný editor
(F7, načítanie a prehranie `BACH.MEL`, všetky F-klávesy v ňom, písanie nôt)
a `READ.COM` z diskety — nula prístupov. Staticky tomu zodpovedá, že DMA
nesie len banky 00h, 01h a 07h (hardware-map, tabuľka `.dma0_move`), BBR
pri reči siaha najvyššie po `3FFFFh` a test RAM ani príznak rozšírenej
pamäte v ROM nie je — sebakontrola na `1D9B8` je kontrolný súčet ROM.
`SYSEQU.LIB` pozná len `ram0_page 70h` a `ram1_page 78h`. Domnienka, že by
rozšírená RAM pomohla hudobnému editoru, je tým vyvrátená.

**Otvorené, a z ROM nerozhodnuteľné:** či sa `5xxxxh` na skutočnom stroji
zrkadlí na `7xxxxh` (nedekódovaný A17). Sedelo by to s tým, že `MEMMAP.E`
dáva štandardnému anglickému stroju RAM na `50000h` a `SYSEQU.LIB` mu
dáva `ram0_page 70h`. Rozhodne to RAM4B na štandardnom českom stroji:
dvakrát „Dobrý večer“ znamená zrkadlo a oplatí sa ho modelovať pre
programy písané pre anglickú Eureku; samotné „Konec“ znamená, že model
sa správa ako stroj. Neoverené je aj to, čo vráti čítanie z prázdneho
pásma — nuly v modeli sú voľba, nie meranie.

### 6.45 Obnovovanie DRAM sa modeluje — `OUT 54,252` stroj spomalí (`ea4-e2m`)

Podnet v tej istej správe ako 6.44: na skutočnom stroji z BASICu
`OUT 50,240` funguje (6.43) a „ešte nejde `OUT 54,252` a `OUT 62,1`“.

Port 54 je `36h`, `rcr` (`IOREG.LIB:82`). Bity sú v manuáli HD64180 (kap.
2.8) aj v UM005004 zhodne: `REFE` (7) zapína obnovovanie, `REFW` (6) robí
obnovovací cyklus trojtaktový namiesto dvojtaktového, `CYC1–0` dávajú
interval 10, 20, 40 alebo 80 taktov. Po resete je `FCh` — zapnuté, tri
takty, interval desať. Firmvér na `00018` zapíše `00h`, šesť inštrukcií po
resete. `252` = `FCh`, takže `OUT 54,252` je presne hodnota po resete.
Pamäť Eureky je statická (`HARDWARE.1`), obnovovanie jej teda len berie čas.

**Čo je spravené.** `EurekaMachine::RefreshCycles` pripočíta ku každej
inštrukcii (aj k prijatiu prerušenia) obnovovacie takty a `Step` ich pridá
do `cpu_.cyc` skôr, než ich dostane `Advance`. `PowerOn` nastaví `rcr` na
`FCh`, ako reset — prvých šesť inštrukcií teda beží s obnovovaním.

**Voľba, ktorú manuál číslom nerozhodne.** Časovač obnovovania sa tu počíta
v skutočnom čase, obnovovacie takty **v ňom**: pri `FCh` sú to 3 z každých
10, program dostane 7 a beží na **70 %**. Druhé čítanie — 10 taktov
programu, potom 3 obnovovania — by dalo **77 %**. Za prvé hovorí to, že
manuál volá cykly „asynchronous ... independent of CPU program execution“
a že časovač beží ďalej aj pri uvoľnenej zbernici. Rozhodnúť to vie
meranie na skutočnom stroji: tá istá melódia alebo slučka v BASICu s
`OUT 54,252` a bez neho — pomer dĺžok 1,43 znamená model, 1,30 druhé čítanie.

Obnovovanie sa pripočítava po inštrukciách, nie po strojových cykloch, a
nerozlišuje SLEEP; celkový čas to nemení, len posúva takty v rámci
inštrukcie. Pri vypnutom `REFE` (firmvér) je to nulový zásah — všetkých
osemnásť testov prechádza bez zmeny.

Drží to `CheckRefreshCycles` v režime `dc`: tisíc `NOP` pri `FCh`, `83h`
a `7Ch` (všetky bity okrem `REFE`), prírastok musí byť P·3/7, P·2/78 a nula.
Overené dvoma mutáciami — interval rátaný len z taktov programu zhodí `FCh`
(1200 namiesto 1714), ignorovaný `REFE` zhodí `7Ch`.

**Otvorené:** `OUT 62,1`. Port `3Eh` v `IOREG.LIB` nie je a na HD64180 tam
register nie je; na Z80180 je tam `OMCR`, ktorý o rýchlosti nerozhoduje.
Veta „tá šestka zrýchľuje“ nie je jasná — treba sa spýtať.

### 6.46 CHESS.COM — rýchlosť emulátora proti spomienke na skutočný stroj

Podnet: `CHESS.COM` (34 816 B, mimo repozitára v `C:\b\diskety\com`).
Rozobraté 23. 9. 2026. Je to čistý CP/M program „MYCHESS“: bez `IN`/`OUT`,
bez priamych volaní BIOS, z BDOS volá funkcie 0, 1, 2, 5, 9, 0Bh, 0Fh a 1Ah
a ďalšie cez `855Bh` (súbory). Hardvér teda nemeria, meria len **rýchlosť
CPU**, a práve preto sa naň dá spoľahnúť.

Čo je v ňom dôležité pre meranie:

- **Knižnica otvorení** na `6630h`–`6B23h`, 316 štvorbajtových položiek
  (odkiaľ, kam, príznak, `FFh` alebo odkaz na pokračovanie; pole je
  `stĺpec·16 + riadok`). Vstup do stromu vyberá `LD A,R` na `156Ch` z piatich.
  Ťahy a2-a3 a a2-a4 v nej nie sú, takže po nich program počíta hneď.
- Konfiguračný bajt `6B27h` = 0 volí riadkový výstup (VEĽKÉ písmená,
  šachovnica s okrajom), nie obrazovkový; `6B26h` = 0 vypína zvonček.
- Otázky: `HOW MANY PLIES OF LOOK AHEAD?`, potom `DISPLAY BEST VARIATION?`
  (pri úrovni 1 nepríde), potom `DO YOU WANT WHITE?`. Na meno sa nepýta.
  Ťahy sa píšu `a2 a3`, `go` potiahne za stranu na ťahu.

**Meranie** tokenom `@TEXT` sondy (pribudol pre toto): od Enteru za ťahom
po riadok `MY MOVE`, pri 6,144 MHz, v zátvorke čas v samotnom programe.
Po tom riadku ešte asi 3 s trvá, kým stroj ťah dohovorí. Odpovede programu
boli na všetkých úrovniach rovnaké (E7-E5, G8-F6, F8-C5).

- Úroveň 1: a2 a3 5,67 s (2,28), h2 h3 5,25 s (1,98), b2 b3 5,93 s (2,96).
- Úroveň 2: 7,27 s (3,78), 5,24 s (1,98), 7,35 s (4,30).
- Úroveň 3: 22,96 s (18,43), 23,59 s (19,18), 25,14 s (20,98).
- Úroveň 4: 115,19 s (104,80), 125,70 s (114,40), 126,47 s (116,05).
- Úroveň 5: 167,48 s (153,82), len po a2 a3.

Majiteľ si zo skutočného stroja pamätá úroveň 2 „asi 8 s“ a úroveň 3 „asi
20 s“; pri 4 nemal trpezlivosť. Emulátor je teda **v správnom rádovom
pásme, na úrovni 3 skôr o trochu pomalší než stroj**, nie rýchlejší.
Ako overenie čakacích taktov (6.43, 6.45) to nestačí — je to spomienka,
pozícia nie je známa a nevie sa, či sa stopovalo po riadok alebo po
dohovorenie. Počítadlo cyklov v tomto meraní nesie štyri čakacie takty na
externý I/O (`DCNTL` = `38h`) a takt navyše na M1; pamäť je bez čakania
a obnovovanie vypnuté, ako ich nechá firmvér.

Asi 3,3 s z každého čísla nie je šach: je to ozvena riadku „01 a2 a3“,
ktorú hovorí ROM (stránky `00100h`, `00200h`), a BDOS. Na nízkych úrovniach
je to väčšina času, takže práve tam by sa prejavilo, keby stroj hovoril
ozvenu inak dlho.

**Otvorené, program sám, nie model:** po h2 h3 trvá úroveň 1 aj 2 takmer
na cyklus rovnako a úroveň 5 len 1,5-krát dlhšie než 4 (inak je krok 5- až
6-násobný). Príčina nehľadaná.

**Pre skutočný stroj** (bead `ea4-95h`): stopky na úrovni 3 po a2 a3
z úvodnej pozície, od Enteru po vypísanie ťahu. Emulátor tam dáva 22,96 s,
z toho 18,43 s v programe; sekvencia je v `tests/README.md` pri `@TEXT`.

### 6.47 Ochrana proti zápisu: ROM sa pýta stavového bitu, radič zápis nedostane (`ea4-62u`)

Pri prenose kontroly `wp` na `EurekaSession` (eureka.md, krok 3) sa ukázalo,
že vetvu modelu radiča, ktorá na chránenej diskete odmietne **Write Track**,
sa dá vypnúť a všetkých 19 režimov prejde. Otázka bola, či je to diera
v testoch, alebo vetva, na ktorú ROM nikdy nedôjde. Je to to druhé.
Zmerané 25. 9. 2026 sondou s `trace` (pracovné poznámky
`build\wsector\progress.md` a `build\wcopy\progress.md`, mimo gitu).

- **Formátovanie** (`Shift+F8`) pozrie bit 6 stavu po príkaze typu I
  a Write Track na chránenej diskete nevydá.
- **Ukladanie súboru** (BASIC `SAVE`, textový procesor `Shift+F1` `s`) ide
  cez BIOS 14 a ten obsluhuje `InterceptBios` cez `VirtualDisk::WriteRecord`
  — porty `98h`–`9Bh` sa nedotknú vôbec, s ochranou ani bez nej. Ochranu tu
  drží `WriteRecord` a `DiskFailure(true)`; či ju drží aj nejaký test, sa
  nezisťovalo.
- **Kopírovanie** — `F4` (jeden súbor) aj `Shift+F4` (hromadné, po
  `Shift+F3`) — číta aj zapisuje cez BIOS. Radič dostane len Restore, Seek
  s overením pri vstupe do diskových funkcií a Seek `10h` pri každej
  kontrole diskety, po ktorom sa číta stav (`46h` chránená, `06h` nie).
  Chránený cieľ odmietnu obe vopred: „cílový disk je chráněn proti zápisu“,
  cieľ nezmenený. Nechránený **zdroj** odmietne len `Shift+F4` („zdrojový
  disk není chráněn proti zápisu“); `F4` súbor skopíruje. **Majiteľ
  potvrdil, že tak je to aj na stroji** — jeden súbor sa o zámok nestará,
  hromadné kopírovanie áno.
- **Ovládač ROM sám vopred nekontroluje.** Keď sa zachytávanie BIOS
  dočasne vypne, ROM na chránenú disketu vydá Write Sector (`A0h`) aj po
  Seeku so stavom `46h`, prečíta odmietnutie (`40h`) a po štyroch pokusoch
  to vzdá. Vetva Write Sector v modeli teda nie je zbytočná — len na ňu pri
  zapnutom `InterceptBios` nič bežné nedôjde.

Záver: zámok, ako ho ROM pozná, je **stavový bit po Seeku**, a ten drží
režim `wp` (mutácia bitu zhodí odmietnutie formátovania). Vetvy Write Sector
a Write Track sú vernosť k 1770 pre kód, ktorý radič ovláda sám, a test
ich nemá ako dosiahnuť bez obídenia BIOS. Pri oboch to stojí v komentári
v `machine.cpp`.

Jedna tichá vec, ak by vetva Write Sector niekedy vypadla: obraz by
neutrpel, lebo `VirtualDisk::WritePhysicalSector` na chránenej diskete
vráti `false`, ale `WriteFdcData` tú hodnotu zahodí a radič ohlási úspech.
Stroj by potom povedal „hotovo“ (BASIC) alebo „Y je uložen“ (textový
procesor) a súbor by nevznikol.

Pre otvorenú otázku zo 6.30 (`ea4-4gw`, či veta o zamknutej diskete znie
zo všetkých vstupných bodov rovnako): model hovorí pri `SAVE` v BASIC-u aj
v textovom procesore „disk chráněn proti zápisu“ (textový procesor potom
„Y není uložen“), pri formátovaní „disk je chráněn proti zápisu“ a pri
kopírovaní „cílový disk je chráněn proti zápisu“. So strojom to porovnané
nie je.

Príkazy (bash, čerstvý priečinok s dvoma súbormi v `slot1`):

- `diag_probe ROM DISK seq 60000000 slot1 trace kD5 . k82 k82 kD2 . kD3 . y "?cílový disk" slot2 +wp stav kC0 "?zápisu" . stav`
  — chránený cieľ pri hromadnom kopírovaní.
- `diag_probe ROM DISK seq 60000000 slot1 -wp trace kD5 . k82 k82 kD2 . kD3 . y "?disk" . . stav`
  — nechránený zdroj.
- Výmenu diskety pri kopírovaní potvrdzuje **F1** (`kC0`); Enter, Escape
  ani `y` na výzvu nereagujú. `y` poslané počas reči sa stratí, spoľahlivé
  je `. y`. Záznamník udalostí (512 položiek) celé kopírovanie neudrží —
  zápisy rýchlosti reči do `0Eh`/`0Fh` ho prepláchnu; na meranie treba
  dočasne zväčšiť `kRingSize` v `src/diagnostics.h`.

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
| `check_io_names.py` | overí `src/eureka_io.h` proti `IOPORT.LIB`, `IOREG.LIB`, `SYSEQU.LIB` a `KB.H` |

Príklad:

```
set A4ROM=C:\b\a4rom.dmp
python tools\dis.py 19837:19880
python tools\strings_kam.py 8 0xd000 0xe000
```

---

## 8. Ako pokračovať

Poradie podľa pomeru prínos/námaha:

**Hotové 26. 8. 2026: default zrušený** (6.11), a s ním parkovanie,
zachytávanie konzolového vstupu aj `RenderIdle`. Malo to riešiť tri hlásené
chyby — hladujúce funkčné klávesy, ukladanie v BASICu a medzerník, ktorý
nezastaví hudbu. Deväť testov prechádza a majiteľ to odskúšal aj ručne.

**Hotové 27. 8. 2026: GUI** (6.18) — okno, ponuka, akcelerátory, vlastné
vlákno emulátora a dva skutočné dialógy. Deväť testov ďalej prechádza.
Výmena diskety za behu a sloty rýchlej voľby pribudli 28. 8. 2026 (6.22);
z pôvodného zoznamu zostáva len to, ktoré prepínače dialógu `Nastavenia`
majú beh prežiť. Podrobnosti sú v `HANDOFF-archiv.md` 6.18, otvorené kusy
v 6.18 tu.

**Hotové 27. 8. 2026: doplnok pre NVDA** (`nvda-addon/`) — čítačka spí nad
oknom stroja a len nad ním, takže ponuka a dialógy sa čítajú ďalej.
Odskúšané v ostrom používaní; doplnok sa od začiatku používa aj ladí
a funguje, ako má (potvrdené 7. 9. 2026). Rozbor je na konci 6.18. Pozor
len na vývojovú slučku: beží zo scratchpadu, takže zmena v repozitári sa
neprejaví, kým sa modul nenakopíruje (`build-addon.bat scratchpad`)
a nenačíta znovu (`NVDA+Ctrl+F3`).

**Hotové 14. 9. 2026: posuvníky hlasitosti a rýchlosti reči** (6.34,
`ea4-06x`) — model, nastavenia, ponuka, skratky, dialóg, tón na konci
posuvníka a hlásenie v doplnku NVDA. Otvorené zostáva hlavne to, kam siahal
skutočný posuvník rýchlosti (6.34, Otvorené).

1. **Sonda na klávesnicu** (6.18, koniec) — dva krátke `.COM` programy
   cez `dev_kb` a `con_ctl_getkey`, ktoré ohlásia prijatý kód. Bez ich dát
   sa nedá rozhodnúť, ktoré medzerníkové akordy sú voľné na ovládanie
   emulátora, lebo Space+body456 je Eurekin vlastný prefix Ctrl. Krátke a
   odblokuje to väčšiu vec. Pozor aj naďalej na to, že ROM na exterke
   Ctrl+písmeno rešpektuje (`1DE0E`), takže každá ďalšia taká skratka
   berie hosťovi jeden riadiaci znak.
2. **Hudba hrá o 14,5 % pomalšie** (6.10). Zmerané; hľadá sa okolo dvoch
   percent zle započítaných cyklov v obsluhe generátora tónov. Pozor na
   páku 8 : 1 — tempo reaguje osemkrát citlivejšie než cena obsluhy.
   **Doplnené 12. 9. 2026:** model má doklad od výrobcu — Z180 plus jeden
   T-stav na cyklus M1 (`KEYSCAN.MAC`, `rtc_ctl_wait_de_ms` na `19CD9`)
   trafí znelku bez kalibrácie a model čakania na ROM padá na pulznej
   voľbe. Podrobne v dodatku k 6.10; ďalší krok je zaviesť ho do jadra.
   **Zavedené 12. 9. 2026:** znelka je zo 14,5 % pomalšie na asi 3,7 %
   rýchlejšie, vypočuté a prijaté. Zostáva rozhodnúť o čakacích stavoch
   `TMDR0L`, až keď to vypočujú aj iní.
   **Uzavreté 20. 9. 2026:** druhá, nezávislá nahrávka skutočného stroja
   dala `TMDR0L` = 3 a znelka sedí na **0,2 %**; reč sa nepohla (0,03 %).
   Tento bod je hotový, bead `ea4-9an` zavretý.
3. **Umŕtvená klávesnica po čase** (6.9) — čaká na postup na
   reprodukciu; bez neho je to beh naslepo. Prvá stopa, ktorá sa dá
   sledovať bez neho, sú `C598h`/`C599h` (viď 6.9). Pozor: kým nebol
   modelovaný vypínací strob, päť minút nečinnosti stroj potichu zabilo,
   takže časť starších pozorovaní „po čase prestane reagovať" môže byť
   práve toto. **Časť z toho už mohla spadnúť s GUI:** `WM_KILLFOCUS` teraz
   pustí všetko deterministicky a `ForgetStaleArrows` je len poistka.
   Skúsiť to znovu skôr, než sa bude hľadať inde.
4. **Zachovanie RAM medzi behmi** (6.15) — rozhodnuté, nespravené.
   Vypnutie je hotové a `C45Ah` už nesie značku, ktorú na to ROM sama
   používa. Oplatí sa rozhodnúť naraz s uchovaním nastavení (6.18).
   Od 9. 9. 2026 k tomu vedie kratšia cesta: vypnutý stroj zostáva
   v okne aj s RAM (6.31), takže **teplé zapnutie** sa dá odmerať bez
   toho, aby čokoľvek prežilo proces (`ea4-aip.1`).
   **Ten krok je hotový ešte v ten deň** — `EurekaMachine::PowerOn()`,
   tokeny sondy `vypni`/`zapni`/`studeno` a `Stroj` → `Zapnúť Eureku`
   v ponuke; odmerané je, že firmvér nadviaže.
   **Doplnené 10. 9. 2026: prežitie procesu je hotové, epic `ea4-aip`
   uzavretý.** `SaveSnapshot`/`LoadSnapshot` so snímkou do `pamat.bin`,
   MD5 ROM v hlavičke, osem bajtov `rtcRam_` v nej, zmeškaný budík
   odmeraný (`ea4-oti`) a prepínač „Pamäť" v Nastaveniach (`ea4-dh1`,
   kľúč `zachovat-ram`). Podrobne v 6.15. Otvorené zostáva len doloženie
   relogovania po ceste vypnutie–zapnutie (`ea4-rwe`).
5. **Správa diskiet** (6.22) — **celá spravená** 9. 9. 2026 a stiahnutá
   do archívu; čo sa kedy urobilo, je tam. Otvorené z nej zostalo len
   doloženie: že sa EurekaDOS po výmene preloguje sám, je odmerané
   (koniec 6.17), zatiaľ ale len ručne (`ea4-rwe`), a chýba test cez ROM
   na hromadné kopírovanie, ktoré je najtvrdšia skúška celej správy
   diskiet (6.24, `ea4-442`).
6. **Prerenderovať `audio/`** — **hotové 10. 9. 2026 (`ea4-upc`)**:
   `tools/speech_wav.py` renderuje dáta reči pri 7493 Hz (φ/(20·41),
   RLDR0=`28h`) a `tools/melodies.py` prešiel na 11 378 Hz (φ/540, 6.10)
   namiesto hádaného 22050. Podrobne v 6.1 „Čo zostáva“.
7. **Formáty súborov z `FILE-FMT.D`** — telefónny zoznam, diár, melódie,
   databáza a texty sa dajú konvertovať do a z hostiteľských formátov.
   Doteraz to nešlo, lebo formáty neboli známe.
8. **Zahodené zápisy na 1C1FA** (6.2) — krátke, ale treba disassemblovať
   cestu adresára disku.
9. **Sériová relácia** — odblokuje `B0h` bit 7, `A8h` bity 2 a 5 a
    rozhodne otázku 6.3.

### Čím sa dá testovať

Manuálová disketa priniesla dobové binárky. `tests/integration_test.cpp`
v režime `com` očakáva `READ.COM` v priečinku disku — je
v `eurekatech/TECHMAN1/READ.COM`. Obidva integračné testy prechádzajú:

```
integration_test ROM DISK_FOLDER bas   -> PASS
integration_test ROM DISK_FOLDER com   -> PASS (196 BIOS čítaní)
integration_test ROM DISK_FOLDER kbd   -> PASS (klávesy, braille, PC)
integration_test ROM DISK_FOLDER power -> PASS (vypnutie štyrmi kurzormi)
integration_test ROM DISK_FOLDER dc    -> PASS (výstup po reči sadne na ticho, DAC na 88h–8Fh)
integration_test ROM DISK_FOLDER rtc   -> PASS (budík sa nastaví a zazvoní)
integration_test ROM DISK_FOLDER hudba -> PASS (medzerník zastaví znelku)
integration_test ROM DISK_FOLDER format-> PASS (Shift+F8 naformátuje prázdnu)
integration_test ROM DISK_FOLDER wp    -> PASS (zámok číta, zápis odmietne)
integration_test ROM DISK_FOLDER hlaseni -> PASS (tri stavy mechaniky, tri vety)
integration_test ROM DISK_FOLDER snimka -> PASS (RAM do súboru a späť, tri odmietnutia)
integration_test ROM DISK_FOLDER akord -> PASS (postupné skladanie akordu bodmi)
integration_test ROM DISK_FOLDER budik -> PASS (budík zobudí vypnutý stroj a uspí ho)
integration_test ROM DISK_FOLDER trap  -> PASS (TRAP.COM povie „pred ahoj“, nie „po“)
integration_test ROM DISK_FOLDER zvuk  -> PASS (rekonštrukcia DAC, odstup šumu nad 55 dB)
disk_test                              -> PASS (214 kontrol, bez ROM)
codec_test                             -> PASS (bez ROM)
settings_test                          -> PASS (75 kontrol, bez ROM)
```

Všetkých **osemnásť** naraz spustí `run-tests.bat`: paralelne, s jedným
súhrnom na konci a nenulovým návratovým kódom, keď čokoľvek zlyhá. Priečinok
diskety si pripraví sám, takže ručne netreba nič.

Ten počet je jediné miesto, kde sa tento zoznam dá overiť zvonka, a preto tu
stojí číslom: keď režim pribudne do `MODES` v `Makefile` a sem nie, rozdiel
nevidno inak než spočítaním riadkov `PASS`. Presne to sa aj stalo — `wp`
tu chýbal a text hovoril „jedenásť“, kým `run-tests.bat` už dávno púšťal
dvanásť procesov. Trinásty je `hlaseni` (6.30), štrnásty `snimka` (6.15),
pätnásty `akord` (ea4-v1j, 11. 9. 2026) a šestnásty `budik` (6.32, v ten
istý deň). Sedemnásty je `trap` (6.33 bod 5, 18. 9. 2026), osemnásty `zvuk`
(6.38, 22. 9. 2026).

Pozor: `com` potrebuje `READ.COM` v priečinku disku a bez neho zlyhá.
Netreba ho hľadať — je v `eurekatech/TECHMAN1/READ.COM`, a `run-tests.bat`
si ho kopíruje sám.

**Doplnené 20. 9. 2026 (`ea4-fo1`):** priečinok je mimo repozitára, takže
`run-tests.bat` ho hľadá cez `%EUREKATECH%` (inak `C:\b\eurekatech`). Keď
ho nenájde, vynechá `com` **aj `wp`** a `PASS` je šestnásť. Že ten súbor
potrebujú dva režimy a nie jeden, sa ukázalo až meraním: `wp` ním overuje
čítanie z chránenej diskety (`CheckProtectedDiskStillReads`) a bez neho
padne na `citanie=chyba`.

`TP.COM` a `TPS.COM` (Turbo Pascal) sú zatiaľ nevyskúšané a boli by
podstatne tvrdším testom EurekaDOS.

**Už neplatí (17. 9. 2026, `ea4-1s4`):** oba sú vyskúšané ručne v emulátore
a prešli. Majiteľ nimi preložil program s 996 riadkami, a to **na disketu**,
nie do pamäte, takže kompilátor výsledný `.COM` naozaj zapísal. Disketa mala
domov (priečinok) a preložený program sa spustil a beží správne. Oproti
`READ.COM` pribudol zápis nového súboru cez EurekaDOS a spustenie
programu, ktorý nevznikol na dobovej diskete.

Čo to **nerozhodlo**: `ea4-ary` (číslovanie logických sektorov, 6.12).
Chyba sa neprejavila, ale to dokladá len toľko, že tieto dva programy
na ňu nenarazili — nie že model čísluje správne. Automatický test z toho
nie je; ručné meranie sa pri zmene diskovej vrstvy musí zopakovať ručne.
