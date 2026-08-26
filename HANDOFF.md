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
| F2 | hodiny a kalendár | Shift+F2 | (mlčí) |
| F3 | kalkulátor | Shift+F3 | teplomer |
| F4 | komunikácia | Shift+F4 | voltmeter |
| F5 | telefónny zoznam | Shift+F5 | databáza |
| F6 | BASIC | Shift+F6 | diskové funkcie |
| F7 | hudobný editor | Shift+F7 | spustiť program z disku |
| F8 | adresár disku | Shift+F8 | formátovať disk |
| F9 | režim | Shift+F9 | stav batérie |
| F10 | kde som | Shift+F10 | sebekontrola |

F2 sa pri otvorení neohlási — že to sú hodiny a kalendár, povie až F10.
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

Toľko, koľko mal stroj klávesníc. Prepína sa medzi nimi `Ctrl+K`:

| režim | čo je pod rukami |
|---|---|
| **externá klávesnica PC** (štartový) | všetko ide scancodmi po sériovom porte, ROM si prekladá sama |
| **braillovská klávesnica** | dvadsať klávesov: body na `F D S J K L`, medzerník, funkčné, kurzory, shift |

**Default zanikol 26. 8. 2026** (6.11). Nebol to režim stroja, bola to
obchádzka: text z neho išiel skratkou do fronty ROM, čo skutočná Eureka
nevedela urobiť, a keď sa zrušilo parkovanie, prestal fungovať aj tak.
Štartuje sa v **exterke**, teda v stave „Eureka s pripojenou klávesnicou".

Cena, ktorú 6.11 za to priznávala — že sa nebude dať napísať
`ľ ĺ ŕ ô ä Ľ` — **žiadna cena nebola**. Preverené pred zrušením, ako si
tá poznámka pýtala: v Kamenických majú tie znaky kódy `8C 8D AA 93 84 9C`,
teda všetky nad `7Fh`, a staré `QueueKey` posielalo každý taký bajt do
`PressMembraneKey`. Napísať sa nedali nikdy — stlačili sa ako akord.
Napísať `ä` znamenalo šípku vľavo a napísať `Á` (`8Fh`) štyri kurzory
naraz, teda vypnutie stroja.

`Ctrl+K` a nie `Ctrl+Shift+K` je rozhodnutie majiteľa, spravené s vedomím,
že ROM na exterke Ctrl+písmeno **rešpektuje**: dekodér ho na `1DE0E`
maskuje cez `AND 1Fh` na riadiaci znak, takže hosťovi tým v tom režime
zaniká `0Bh`. Zmerané, že sa písmeno nenapíše: `a`, `b`, Ctrl+K, `c`, `d`
dá riadok „abcd". Žiadna aplikácia ROM na to viditeľne nereaguje.

### Braillovská klávesnica

`Ctrl+K` prepne písanie na šesť bodových klávesov: `F D S` sú
body 1, 2, 3 a `J K L` body 4, 5, 6, medzerník zostáva medzerníkom.
**Písmená sa v tomto režime nepíšu** — kto v ňom chce písať, píše bodmi,
tak ako na stroji. Stlačené `a` neurobí nič a pri `--diag` to trasovanie
klávesov povie, lebo inak je „nič" na nerozoznanie od „kláves neprišiel".
S textom padlo aj Ctrl+písmeno, a nie je to strata: na tejto klávesnici
riadiaci kláves nie je, riadiace znaky sa na nej robia akordom.
Akord sa zbiera, kým sú prsty dole, a vydá sa naraz pri pustení
posledného — to je perkinsovské správanie a `ReleaseKey` ho umožňuje.

**Shift patrí do akordu, nie vedľa neho** (doplnené 26. 8. 2026).
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

Emulátor **neprekladá nič**. Pošle šesť bitov na riadok `89h`, shift na
`8Ch` a znak si nájde ROM sama (`1D79C`), takže fungujú všetky tri
tabuľky vrátane číselného režimu aj akordy s medzerníkom, o ktorých
nevieme. Doložené: štyri akordy napíšu v textovom procesore „Ahoj"
a `Home` to prečíta späť — presne to robí `integration_test … kbd`.

**Kurzory sa v tomto režime chordujú tiež** a je to tak zámerne: kto píše
na braillovej klávesnici, čaká, že sa aj kurzory budú správať ako na
Eureke. Nie je to zvláštnosť tohto režimu — chordovanie je zapnuté všade
okrem exterky, kde sa scancody prekladajú po jednom a chordovať sa
nedajú (viď „Vypínanie stroja").

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

Na klávesnici PC je to **ľavý Alt + šípka hore** a overené je to
zmeraním, nie odvodením: ROM na `1DD9E` prisadí k šípke bit 5, keď je
v `C670h` bit 3 (ľavý Alt, `DF05[38h]` = `F8h`), takže z `81h` vznikne
to isté `A1h`, ktoré robí medzerníkový akord. Odoslané ako `38 E0 48
E0 C8 B8` stroj povie „Ahoj Svete.".

Pozor na to pri testoch: `tests/README.md` roky tvrdil, že `kbd` si
nechá „prečítať riadok". Nechá si prečítať **slovo**. Test tým neprešiel
omylom — píše jedno slovo, takže je to to isté — ale popis bol nesprávny
a je opravený.

### Klávesnica IBM PC na sériovom porte

`Ctrl+K` prepne hostiteľskú klávesnicu na tú, ktorá sa k Eureke
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

V režime **externej klávesnice PC** sa stroj vypnúť nedá, a je to tak
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

### 6.1 Zvuk — rekonštrukčný filter aj latencia hotové

**Hotové.** `RenderAudio()` bol zero-order hold bez filtrácie, takže
všetko nad polovicou vzorkovacej frekvencie DAC sa zrkadlilo ako
aliasing. Skutočný stroj má za prevodníkom analógový filter — je to
práve to, čo prepína `filtersel_mask` (`B0h` bit 4) — takže jeho
doplnenie je modelovanie hardvéru, nie kozmetika.

Rečový engine ten prepínač ovláda vedome: rutiny `00162` (nastaví bit)
a `00175` (zhodí ho) volajú pri každom prechode ustaľovaciu rutinu
`0234h`. Volajú sa z `0051E`, `005CE` a `03B50` / `03B0B`.

Emulátor má **jednopólový filter, 5 kHz pre reč a 10 kHz otvorený**
(pomer 2:1 zodpovedá tomu, že efekty bežia na dvojnásobnej frekvencii).

Za ním je od 26. 8. 2026 ešte **väzobný kondenzátor, hornopriepusť
30 Hz** — bez neho zostávala na výstupu pokojová hodnota DAC ako stojatá
jednosmerná zložka a lupalo to. Viď 6.16; toho, čo je v tejto sekcii,
sa to nedotklo, merateľne vôbec.

#### Ako sa tá hodnota vybrala

**Manuál medznú frekvenciu neuvádza** — prehľadané kapitoly o hardvéri,
servisná aj kapitola o reči. Číslo teda nie je doložené a vybralo sa
počúvaním, ktoré robil majiteľ skutočného stroja. Postupne sa skúšali:

| variant | 3,4–5 kHz | 5–8 kHz | nad 8 kHz | ako to poslucháč opísal |
|---|---|---|---|---|
| rad 4, 3400 Hz | −5,4 dB | −7,8 dB | −19,1 dB | zreteľne tlmené „c" a „s", basovejšie a plnšie |
| rad 2, 3400 Hz | −4,3 dB | −6,9 dB | −13,5 dB | — |
| rad 2, 4200 Hz | −2,8 dB | −5,0 dB | −10,4 dB | — |
| 1 pól, 3400 Hz | −3,5 dB | −4,9 dB | −7,3 dB | — |
| **1 pól, 5000 Hz** | **−2,0 dB** | **−3,0 dB** | **−4,9 dB** | veľmi jemný rozdiel, sykavky miernučko oslabené |

Prvý pokus bol rad 4 na 3400 Hz a bol **zle**. Poslucháč to opísal ako
tlmenejšie „c" a „s" a basovejší, plnší zvuk — a mal pravdu: sykavky
majú väčšinu energie práve v 3,4–8 kHz. Štvrtý rád bol navyše výmysel,
nie model hardvéru; prenosný stroj z roku 1989 mal realisticky jeden
odpor a jeden kondenzátor.

Tie dva slovné popisy sú kalibrácia tabuľky, nie dojmy. Sedia na čísla
aj vo veľkosti: pri útlme 5–8 dB je zmena zreteľná, pri 2–3 dB je na
hranici postrehnuteľnosti. Kto bude hodnotu meniť, vie podľa toho
odhadnúť, čo ešte bude počuť.

Poslucháč používa kochleárne implantáty, takže v pásme sykaviek počuje
inak než bežné ucho. Voľbu to nediskvalifikovalo — popisy sedeli na
meranie v smere aj vo veľkosti — a **druhý, nezávislý poslucháč
s bežným sluchom výsledok potvrdil**. Jednopólový 5 kHz teda prešiel
dvoma rôznymi sluchmi, čo je pri tomto parametri asi maximum
dosiahnuteľnej istoty: doložený údaj neexistuje a skutočné stroje sa
medzi sebou líšili.

Poznámka pre toho, kto to bude meniť: jednopólový 5 kHz je zámerne
**mierny**. Nehľadalo sa „čisté", ale vierohodné. Poslucháč tiež
spomenul, že rôzne Eureky zneli rôzne — čo sedí, lebo tolerancia
súčiastok posúva medznú frekvenciu o desiatky percent. Jedna správna
hodnota teda neexistuje a toto je odhad stredu.

#### Ako sa to meria

`audio/porovnanie/` (mimo gitu) drží nahrávky a skript na porovnanie.
Pozor na dve pasce, na ktoré sa dá naletieť:

1. Rozdiel `bez filtra − s filtrom` obsahuje aj **fázové oneskorenie**
   filtra. Bez kompenzácie to vyzerá, akoby filter rezal do reči: 78 %
   odobraného pod 3,4 kHz. Po kompenzácii šiestich vzoriek (0,12 ms) je
   pod 2 kHz len 1 % a nad 3,4 kHz 92,8 %.
2. Filter sa nedá vierohodne napodobniť offline v Pythone, lebo firmvér
   ho prepína za behu. Statický model sedel len na 19 dB.

#### Latencia — zmeraná, opravená, a nebola to tá, čo tu stála napísaná

Predchádzajúca verzia tohto dokumentu tvrdila **„latencia až 240 ms"**.
Bolo to **nesprávne**. Tých 240 ms bol teoretický strop starého throttlu
(12 blokov po 20 ms) a meranie ukázalo, že sa nikdy nedosiahol — v
ustálenom stave boli v zariadení štyri bloky. Skutočná latencia počas
reči bola **22,6 ms**. Číslo v dokumente vzniklo prečítaním konštanty v
zdrojáku namiesto merania.

Čo meranie našlo namiesto toho, boli tri iné veci — a najhoršia z nich
nebola zvuková:

1. **Hlavná slučka bežala 63 Hz, nie 500 Hz.** `std::this_thread::
   sleep_for(2ms)` na mingw počká vždy celý 15,6 ms systémový tik, nech
   ho žiadaš o čokoľvek, a `timeBeginPeriod(1)` na tom **nič nezmení** —
   opraví len `::Sleep()`. Klávesnica sa teda snímala každých 16 ms a
   procesor bežal v 16 ms dávkach. Náprava je časovač s vlajkou
   `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, ktorý spí presne a nedvíha
   rozlíšenie časovača celému systému.
2. **Nikto nikdy nevyprázdnil štartový prechodový jav.** Producent aj
   spotrebiteľ bežia presne reálnym časom, takže hĺbka fronty zostane
   navždy tam, kam ju odložil rozbeh zvukového zariadenia.
3. **Zaparkovaný procesor neprodukoval nič**, takže prúd medzi vetami
   vysychal. Zmerané: rozbeh vyschnutého prúdu stojí **19 ms**, kým
   udržiavaný odpovie za 2–9 ms.

Ku každej z nich pozor — **samostatne dve z nich latenciu zhoršia**.
Zmerané na skutočnom stroji, priemer počas reči:

| konfigurácia | slučka | priemer | najhorší prípad |
|---|---|---|---|
| pôvodná | 63 Hz | 22,6 ms | 13,4 ms |
| len presný časovač | 352 Hz | 36,7 ms | 19,0 ms |
| len renderovanie pri parkovaní | 63 Hz | 41,2 ms | 19,3 ms |
| **všetky tri spolu** | **352 Hz** | **23,0 ms** | **16,4 ms** |

Rýchlejšia slučka totiž podáva bloky častejšie, takže fronta narastie;
a udržiavaný prúd nemá kedy zabudnúť na prechodový jav. Až regulátor
robí obe bezpečnými.

#### Čo to teda robí teraz

- Emulovaný takt sa **prispôsobuje hĺbke fronty** (`kTargetLatencyMs`
  v `main.cpp`), najviac o 2 %. Je to správna páka: výstup DAC zostáva
  súvislý, na rozdiel od zahadzovania či opakovania vzoriek, a 2 %
  zo 6,144 MHz je tretina poltónu.
- Cieľ je **22 ms**, vybraný meraním: je to najnižšia hodnota, ktorú
  regulátor ešte drží s odchýlkou 2 ms. Pod 20 ms je dnom vlastné
  bufferovanie ovládača, regulátor doň tlačí a namiesto priblíženia
  prestreľuje — rozptyl nameraný 10 až 48 ms.
- Bloky sú **5 ms** namiesto 20 a strop fronty je **v milisekundách,
  nie v blokoch**. V blokoch to potichu znamenalo 240 ms pri 20 ms
  bloku a tlčenie na 30 ms, keby sa bloky niekedy zmenšili.
- Pri prekročení stropu sa prebytok **zahodí**, nevyčká. Pôvodný
  `Sleep(1)` v cykle zmrazil celú hlavnú slučku aj s klávesnicou.
- `EurekaMachine::RenderIdle()` dorenderúval cykly, ktoré zaparkovaný
  procesor neodbehol. **So zrušením defaultu (6.11) zanikol**, lebo zanikla
  aj medzera, ktorú lepil: procesor beží stále a čakaciu slučku ROM točí
  naozaj, tak ako na stroji. Meranie, ktoré ho vtedy obhájilo, platí ďalej
  — držaná hodnota je stred stupnice, jedna sekunda nečinnosti má priemer
  0,0 a rozsah 0 až 29, takže sa do výstupu netlačí jednosmerná zložka
  a nástup reči neluplne.

Čo z toho plynie pre ďalšieho: **latencia sa prakticky nezmenila**
(22,6 → 23,0 ms). Získalo sa, že je odteraz **zvolená a stabilná**
namiesto náhodnej, že prúd sa nerozbieha nanovo pred každou vetou,
a že slučka beží 5,6-krát rýchlejšie. Ak niekto hľadá zdroj problémov
v emulátore, **vo veľkosti zvukového buffera nie je** — počas reči sa
ani stará verzia nikdy nepriblížila k vyschnutiu bližšie než na 13 ms.
Podozrivejšia je tá 63 Hz slučka, lebo kvantovala časovanie klávesnice
zo sekcie 5 (stlačenie 120 ms, pustenie 80 ms, ALT-akord 80+120 ms) po
16 ms krokoch.

Merané sondou, ktorá poháňa skutočný `EurekaMachine` cez skutočný
`AudioPlayer` tou istou slučkou ako `main.cpp`, len s vynulovanými
vzorkami; hĺbka fronty sa pýta `waveOutGetPosition`, nie účtovníctva
blokov, lebo len tá vidí aj bufferovanie ovládača.

#### Čo zostáva

- **WAV-y v `audio/`** sú stále v štyroch hádaných frekvenciách;
  prerenderovať podľa známych ~7,5 kHz.
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

### 6.5 Formátovanie nemaže hostiteľský priečinok

Emulátor formátovanie dokončí a ohlási úspech, ale obsah stopy zahodí:
virtuálny disk je hostiteľský priečinok a mazať používateľove súbory
preto, že emulovaný stroj formátoval, nie je rozhodnutie tohto modelu.
Ak by sa to niekedy malo zmeniť, musí to byť vedomé a s potvrdením.

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

#### Medzerník: vyriešené, je to znovu default režim

**Zmerané 26. 8. 2026.** Hudobný editor (`F7`) prehrá po otvorení znelku,
ktorá trvá asi **6,3 s** hosťovského času a potom skončí sama. Meria sa
efektívna hodnota výstupu v okne 2,5–3,0 s po stlačení `F7`, teda hlboko
vnútri znelky, a kláves sa posiela v prvej sekunde:

| kadiaľ kláves ide | čo sa stlačí | RMS v okne | zastaví? |
|---|---|---|---|
| `PressBraille` → riadok `89h` | medzerník sám | **0** | áno |
| `PressMembraneKey` → riadok `8Ch` | šípka hore | **0** | áno |
| `PressMembraneKey` → riadok `8Ah` | `F4` | 5000 | nie |
| vlastná fronta ROM (`C67B`) | medzerník v defaulte | 5028 | nie |
| vlastná fronta ROM (`C67B`) | písmeno v defaulte | 5000 | nie |
| sériová klávesnica PC (scan `39h`) | medzerník | 5000 | nie |

Prečo to tak je, je v ROM čierne na bielom. Prehrávacia slučka sa raz za
takt pozrie na klávesnicu takto:

```
10F10  LD HL,B0F5h
10F13  IN A,(8Ch)      ; kurzory a shift
10F16  AND (HL)        ; proti uloženému (invertovanému) stavu
10F17  JR NZ,10F94h    ; nový kurzorový kláves -> koniec
10F1C  IN A,(89h)      ; body a medzerník
10F1E  OR A
10F1F  JR NZ,10F94h    ; čokoľvek na riadku 0 -> koniec
```

Sú to **priame čítania portov**, nie dotaz do fronty. Medzerník má
v `10F94` dokonca vlastnú vetvu: `CP 80h` rozozná `89h` = `80h`, teda
medzerník bez bodov, a podľa toho sa v `10F6E` rozhodne, či sa pozícia
v skladbe uloží (`LD (B0A9h),HL`) alebo nie. **ROM medzerník čaká.**

Takže emulátor ho nedoručí, nie že by ho ROM nečítala. V default režime
ide písmeno aj medzerník do **vlastnej fronty ROM na `C67B`**
(`QueueKey` → `firmwareKeys_` → `InjectFirmwareKey`), a na `89h` sa
neobjaví nikdy. To je **tá istá skratka, ktorú ruší 6.11** — tretí
príznak jednej príčiny, vedľa hladujúcich funkčných klávesov a ukladania
v BASICu.

Dve veci, ktoré z toho merania plynú a nie sú chyba:

- **Riadok funkčných klávesov `8Ah` sa v slučke nečíta vôbec**, takže
  `F1`–`F8` znelku neukončia ani na skutočnom stroji. Tvrdenie manuálu,
  že skladbu ukončí „ľubovoľný kláves", platí pre riadky `89h` a `8Ch`,
  nie pre funkčné.
- **Externá klávesnica PC znelku tiež nezastaví**, a to je verné.
  Sériová klávesnica ide cez dekodér ROM do tej istej fronty `C67B`;
  žiadny jej kláves sa na membránových portoch neobjaví. Na skutočnom
  stroji to teda nešlo tiež.

Ostáva teda jediný správny spôsob, a je to ten, ktorý má stroj: kláves
na braillovej klávesnici — hociktorý bod, medzerník alebo kurzor.

Po zrušení defaultu (26. 8. 2026) to platí v oboch režimoch, ktoré zostali:
v braillovskom ide medzerník na `89h` priamo, na exterke ho tam dostane
dekodér ROM. Znelku zastaví.

Drží to `integration_test ROM DISK hudba`. Beží dvakrát to isté okno,
raz s medzerníkom a raz bez ničoho: bez druhého behu by test prešiel aj
na znelke, ktorá dohrala sama. Odskúšané mutáciou — keď sa medzerník
pošle do fronty ROM namiesto na riadok, test spadne s RMS 5028.

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

### 6.10 Hudba v emulátore hrá o 14,5 % pomalšie

**Zmerané 25. 8. 2026** proti nahrávke skutočnej Eureky (mobil, majiteľ
stroja). Emulátor hrá **1,145-krát pomalšie** a **výšku má správnu na
0,006 %**. Tým je rozhodnutá otvorená otázka, ktorá tu stála predtým:
dĺžky nôt sa nepočítajú z rýchlosti DAC.

#### Ako sa to meralo

Kópia sondy v scratchpade poháňa skutočný `EurekaMachine` tou istou
slučkou ako `main.cpp`, ale **bez regulátora latencie**, takže hosťovské
hodiny idú presne 6,144 MHz; všetko z DAC ide cez `TakeAudio()` do WAV.
Kontrola sedí na vzorku: 25,001 s hosťovského času = 1 200 047 vzoriek
pri 48 kHz. Do tej istej kópie sa pridal histogram PC za zvolené okno
hosťovského času — bez neho by sa mechanizmus hľadal odvodzovaním.

Noty sa z oboch nahrávok vytiahli jedným skriptom: FFT v 40 ms okne
s posunom 10 ms, doplnenie núl na osemnásobok a parabolická interpolácia
vrcholu (bez nej sa 18 centov v 25 Hz koši nedá vidieť), potom zlúčenie
susedných rámcov s rovnakým poltónom. Že ide o tú istú znelku, potvrdzuje
zhodná sedemtónová postupnosť E5 C5 D5 A#4 C5 A4 A#4 aj korelácia obálok
nábehov, ktorá o notách nevie nič.

#### Čísla

| meranie | skutočný stroj | emulátor | pomer |
|---|---|---|---|
| úvodný tón -> E5 | 1,158 s | 1,320 s | 1,140 |
| úvodný tón -> záverečné F5 | 3,762 s | 4,310 s | 1,146 |
| rozostup tónov v behu | 236,2 ms | 272,9 ms | 1,155 |
| korelácia obálok nábehov | — | — | 1,141 |

Výška: skutočný stroj −18,4 centa, emulátor −18,5 centa oproti A = 440 Hz.

Tá odchýlka **nie je vlastnosť skutočného stroja** — je to aritmetika
v ROM, lebo emulátor s presne 6,144 MHz vyrobí tú istú. (Skorší výklad,
že skutočnej Eureke ide kryštál o 1 % pomalšie, bol nesprávny.)

A práve tá zhoda je dôkaz, kde chyba **nie je**: výšku určuje prerušenie
od časovača, takže keby jeho frekvencia bola vedľa o 2 %, výška by sa
posunula o 35 centov. Namerané je 0,1 centa. **Časovač je správne na
0,06 % a tempo je napriek tomu o 14,5 % vedľa.**

#### Kde tempo vzniká

Dĺžku noty odmeriava **prázdna čakacia slučka v ROM na 10E5D**, žiadny
časovač:

```
10E5A  LD HL,(B0B0h)
10E5D  DEC HL
10E5E  LD A,H
10E5F  OR L
10E60  JR NZ,10E5Dh
10E62  RET
```

Rozdelenie procesora počas tónu (histogram PC, okno 0,2 s):

- **11 378-krát za sekundu** beží obsluha generátora tónov v RAM na
  `7B01D`–`7B051`. To je φ / (20 × 27), teda RLDR0 = `26` — pre generátor
  tónov, nie tých 7,5 kHz, ktoré patria reči.
- Čakacia slučka urobí **26 400 obehov za sekundu**. Pri 26 T-stavoch
  (časovanie Z80) je to **11,2 %** cyklov; zvyšných **88,8 %** spotrebuje
  obsluha.

Z toho plynie páka, ktorá je na tomto celá podstatná. Na jedno prerušenie
pripadá **540 T-stavov**, z toho obsluha zoberie asi 480 a na slučku
zostane 60. Slučka teda dostáva **iba zvyšok**, a preto sa **chyba 1 %
v cene obsluhy premietne do 8 % v tempe**.

#### Obsluha generátora tónov, presne

Vytiahnutá z RAM trasovaním (logická `B000`–`B051`). Je to štvorhlasý
fázový akumulátor: pre každý hlas `HL += BC`, index do 256-bajtovej
tabuľky priebehu je horný bajt `HL`, vzorky sa sčítajú a idú na DAC.

```
B000  EXX / EX AF,AF'         B046  OUT (88h),A
B002  LD DE,(B0A5h)           B048  EXX
B006  4x { LD HL,(fáza)       B049  IN0 A,(ITC)
           LD BC,(prírastok)  B04C  IN0 A,(TMDR0L)
           ADD HL,BC          B04F  EX AF,AF'
           LD (fáza),HL       B050  EI
           LD L,H / LD H,0    B051  RET
           ADD HL,DE
           ADD A,(HL) }
```

Zmerané v emulátore (nie odvodené):

| veličina | hodnota |
|---|---|
| perióda prerušenia | **540,00 T-stavu** (φ/540 = 11 377,8 Hz) |
| jeden obeh čakacej slučky | **26 T-stavov** |
| obehov slučky na prerušenie | **2,286** = 59,43 T |
| obsluha vrátane prijatia prerušenia | **480,58 T = 89,0 %** |

Aby tempo sedelo, musí slučka stihnúť 2,286 × 1,145 = **2,617 obehu**,
teda obsluha smie stáť **472 T namiesto 480,6** — o **1,8 % menej**.
Rovnomerne rozložené je to zrýchlenie vykonávania o **1,6 %**.

#### Rozpočet 540 T-stavov, presne

Zmerané priamo, nie dopočítané. Telo obsluhy `B000`–`B050` stojí
**443,00 T-stavu** (zhoduje sa s ručným súčtom podľa tabuliek v `z80.c`),
`RET` 10, prijatie prerušenia v IM 2 pridáva 19.

| položka | kde beží | T-stavov na prerušenie | podiel inštrukcií |
|---|---|---|---|
| obsluha + prijatie | RAM | 472,0 | 80,6 % |
| čakacia slučka `10E5D` | ROM | 59,4 (2,286 × 26) | 17,6 % |
| prehrávač melódie `10C9x`–`10FCx` | ROM | 9,0 | 1,9 % |
| **spolu** | | **540,0** | |

Aby tempo sedelo, musí slučka dostať 68,0 T namiesto 59,4 — teda všetko
ostatné musí zlacnieť o **8,6 T-stavu, čo je 1,8 %**.

#### Prečo časovanie Z180 nie je tá oprava (s prameňom)

Časovanie je z **Zilog Z8018x Family MPU User Manual UM005004**, tabuľka
Instruction Summary a kapitola o zbernicových cykloch. Základné pravidlo:
zbernicový strojový cyklus má **3 takty**, vnútorný **1**; Z80 má načítanie
operačného kódu za 4.

| inštrukcia | Z80 (dnes v jadre) | Z180 (manuál) |
|---|---|---|
| `ADD HL,ww` | 11 | **7** |
| `DEC ww` | 6 | **4** |
| `JR cc,j` splnené | 12 | **8** |
| `LD ww,(mn)` (`ED 4B`) | 20 | **18** |
| `LD HL,(mn)` | 16 | **15** |
| `LD g,m` | 7 | **6** |
| `ADD A,(HL)` | 7 | **6** |
| `EXX` | 4 | **3** |
| `RET` | 10 | **9** |

Po dosadení: telo obsluhy klesne zo 443 na **385**, `RET` na 9, obeh
slučky z 26 na **20**. Aj keď sa pripočítajú tri čakacie stavy pre I/O,
ktoré si firmvér objednáva sám (`DCNTL = 38h` na `00012`: **0 pre pamäť,
3 pre I/O**, tri I/O prístupy v obsluhe = +9 T), vyjde obsluha okolo
**422 T** a slučka by dostala vyše 100 T. Znelka by hrala **asi
dvojnásobne rýchlejšie než skutočný stroj**.

#### Čo z toho zostáva ako model

Aby čísla sedeli, musí skutočný stroj niekde stratiť okolo 40 T-stavov na
prerušenie. Jediné miesto, kde sa môžu stratiť a firmvér o nich nevie, sú
**čakacie stavy z vývodu WAIT**. Dve možnosti a ich dôsledky:

- **Rovnomerne na každý prístup do pamäte:** vychádza **0,44** čakacieho
  stavu na prístup. To hardvér nerobí, takže tento model je vylúčený.
- **Len na ROM, RAM bez čakania:** vychádza **asi 3,6**. To hardvér robiť
  môže, a hlavne to sedí s tým, čo firmvér sám robí — **obsluhu si kopíruje
  do RAM**, hoci ju má v ROM na `0FB6D`. Nikto nekopíruje kód do RAM pre
  zábavu; robí sa to práve vtedy, keď je ROM na obsluhu prerušenia
  11 378-krát za sekundu príliš pomalá.

Je to však **jedna rovnica na jednu neznámu, dopasovaná na jedno meranie**.
Kým nie je druhé, nezávislé meranie, je to hypotéza, nie výsledok.

#### Kadiaľ ísť

1. **Netreba meniť tabuľky samotné.** Dnešné Z80 hodnoty trafia skutočný
   stroj na 1,8 %; samotné Z180 by minuli o 100 %. Zmena tabuliek bez
   čakacích stavov by emulátor **zhoršila**, nie zlepšila.
2. Zaviesť oboje naraz: časovanie Z180 **a** počet čakacích stavov zvlášť
   pre ROM a pre RAM. Počet pre ROM nakalibrovať na znelku; ak vyjde blízko
   celého čísla, je to model hardvéru, ak nie, je to prispôsobenie a treba
   to tak napísať.
3. Nájsť druhé nezávislé meranie, ktoré ten model overí. Ideálne niečo,
   čo beží celé z ROM a čoho dĺžku vidieť na nahrávke.
4. Pozor, taký zásah sa dotkne **všetkého** — disku, reči, klávesnice.
   Reč beží na časovači a nemala by sa hnúť; treba to overiť, nie
   predpokladať.

#### Vedľajší nález: jednosmerná zložka v hudobnom editore

Keď editor dohrá a čaká na kláves, ROM nechá DAC na hodnote **65**
namiesto stredných 128. Výstup ju drží ďalej, takže má
konštantnú jednosmernú zložku −49 % rozsahu — overené, od 10. do 25.
sekundy je každá vzorka presne −16128. Skutočný stroj to nemá kam pustiť,
v emulátore to lupne pri nábehu a zoberie polovicu odstupu od orezania
všetkému, čo príde potom. Overenie zo sekcie 6.1, že držaná hodnota je
stred stupnice, platí **pre parkovanie po reči, nie po hudbe**.

Doplnené 26. 8. 2026: nie je to zvláštnosť editora. Rovnaké to je po
každej aplikácii, len s inou hodnotou, a príčina je jedna — chýbajúca
hornopriepusť. **Opravené**, viď 6.16 — držaná hodnota už na výstup
neprejde a tento odsek platí len ako záznam, čo sa dialo predtým.

### 6.11 Funkčné klávesy hladujú: emulátor odreže ROM od jej vlastnej fronty

Hlásené z používania: v BASICu `F1` (run), `F2` (list) aj `F5` (save)
**neurobia nič**, kým sa nestlačí ďalší kláves. Pri `F5` sa navyše
„odošle blbosť" a príde syntaktická chyba. Diagnóza je hotová
a reprodukovateľná na povel; oprava nie.

#### Čo sa deje

Funkčný kláves **nedoručuje kód** — natlačí text do **vlastnej fronty
ROM** a tá si ho potom číta cez BIOS ako každé iné písanie. Manuál to
pomenúva v `SYSRAM.A`: `fk_table` (`$C622`) je tabuľka textu,
`fk_echo` (`$C624`) tabuľka toho, čo sa pri klávese povie,
`qhelp_ptr` (`$C628`) rýchla nápoveda. To „běží" pri `F1` je teda
**echo, nie hlásenie stavu**.

Emulátor zachytáva BIOS console input (funkcia 3) a odpovedá naň
z hostiteľskej fronty; keď je prázdna, zaparkuje procesor. ROM sa tak
k vlastnému textu nikdy nedostane. Makro zamrzne po prvom znaku, ktorý
už stihla vyhodiť na konzolu.

Zmerané na `F5`:

- `C62B` prejde z `00` na `C4` (kód klávesu), `C62C/C62D` na ukazovateľ
  `D1E4`
- stroj povie „uložit? vlož jméno souboru" a **zastane**
- po príchode ľubovoľného **hardvérového** klávesu (`HardwareInputBusy()`
  prestane zachytávať) ukazovateľ prebehne `D1E5`…`D1E9`, na konzolu
  vypadne `s`, `a`, `v`, `e`, `"` a `C62B` sa vráti na `00`

Makro `F5` je teda `save"`, makro `F1` je `run`. „Odošle blbosť" je ten
istý jav: `save"` visí nedopísané, meno sa napíše medzitým a riadok sa
poskladá naopak.

#### Predikát, ktorý na to sedí

„Má ROM ešte vlastný vstup?" — nie „kto práve číta". Tri zložky:
fronta emulátora nie je prázdna, **fronta ROM** má nevyzdvihnutú položku
(indexy `C679`/`C67A`, tie isté, cez ktoré píše `InjectFirmwareKey`),
alebo je rozpísané makro (`C62B` nenulové). Odskúšané: keď sa podľa toho
ROM pustí dnu, `F1` prejde celé — „bezi..ahoj...hotovo", nič useknuté.

#### Prečo to samo o sebe nestačí

`19B41`–`19B6B` **nie je čakacia slučka, je to hlavný rozdeľovač
udalostí**: prejde päťpoložkovú tabuľku (obsluhy na `EBDD`, podmienky na
`EBAD`), na každý nastavený bit zavolá `EB64h` a keď niektorá ohlási
prácu, skočí do nej cez `JP (HL)`; inak `JR 19B41h` donekonečna.
ROM sa teda ku dverám BIOS-u **nevracia** — v tom rozdeľovači aj beží,
program sa vykonal zvnútra neho. Parkovať sa preto musí **vnútri**.
S opravou samotnou padne parkovanie z 83 % na 0 % a emulátor páli
procesor natrvalo.

Dva slepé konce, aby sa neopakovali:

- **„Zaparkuj, keď makro dopísalo"** zmrazí stroj tam, kde práve je,
  teda uprostred programu. Počuť to ako useknutú reč.
- **Všeobecný detektor točenia na mieste** (PC v okne 64 bajtov) rozbíjajú
  prerušenia — PC skáče do obsluhy a okno sa resetuje. Bez podmienenia
  zamrzol už štart stroja.

#### Prepletené s parkovaním, a záleží na poradí

`Step()` odchádza pri zaparkovaní **skôr** než `ScheduleInterrupt()`
a `Advance()`, a `RenderIdle()` renderuje len zvuk. Zaparkovaný stroj
teda **stojí celý**: netikajú časovače, nebeží heartbeat na 75 Hz,
nevystrelí prerušenie. Zmerané: po `F1` (záznamník) je z dvadsiatich
sekúnd 15,4 zamrznutých, v nečinnosti 83 %. Na hodinách to nevidno, lebo
RTC číta **hostiteľský** čas.

Ak je medzi tými piatimi obsluhami rozdeľovača aj pumpa reči, potom
zaparkovať v ňom znamená useknúť reč. Preto:

1. **Najprv** zariadiť, aby zaparkovaný čas tikal — časovače, prerušenia,
   DAC. Skutočná Eureka v rozdeľovači točí a heartbeat jej beží.
2. Až potom je parkovanie vnútri rozdeľovača bezpečné.
3. A až potom drží oprava makier bez pálenia procesora.

#### Overené 25. 8. 2026: zrušenie parkovania to rieši

Skúšané tak, že sa BIOS-ové čítanie konzoly prestalo zachytávať úplne —
ROM na kláves čaká točením vo vlastnom rozdeľovači, ako na kremíku.

- V sonde prejde `F1` v BASICu celé: „bezi..ahoj...hotovo", nič useknuté.
- Majiteľ odskúšal ručne v režime **externej klávesnice** a fungovalo
  `run`, `load`, `save`, `F1`, `F2`, `F5` aj `Shift+F5` — teda aj ukladanie,
  ktoré predtým nešlo vôbec.
- `integration_test` prejde vo všetkých troch režimoch, keď sa v ňom
  podávanie klávesov prepne z „stroj zaparkoval" na „stroj pol sekundy nič
  nevypísal". To isté platí pre `RunUntilPrompt` v sonde.
- Znelka `F7` je časovo aj výškovo nezmenená (rozostup 270 ms, −18/−19
  centov), takže sa zvuku netýka.

Cena je hostiteľský procesor: 30 s hosťovského času v nečinnosti stálo
**1,12 s s parkovaním a 4,15 s bez neho**, teda 3,7 % oproti 13,8 % jadra.

**Prvý krok je už v strome.** Zachytávanie console input odpovedá len na to,
čo hostiteľ naozaj napísal, inak nechá čakať ROM; `biosWaiting_` a parkovanie
sú preč a testy aj sonda podávajú klávesy podľa ticha. Kým nezanikne default,
bolo treba emulátor spúšťať s **`--pc`**; dnes je exterka štartový režim.

#### Prečo to samo o sebe nestačí: default režim

Zrušenie parkovania **rozbije režim default**. Text v ňom išiel do
hostiteľskej fronty `keys_` a vyzdvihovalo ho práve zachytené čítanie
konzoly; keď sa ROM prestane vracať ku dverám BIOS-u, je z tej fronty slepá
schránka a v defaulte prestane fungovať všetko. Braille a exterka idú cez
hardvér, tých sa to netýka.

Pokus posielať aj default do fronty ROM (`C67B`) neuspel: vstup sa dostal
dnu, ale hromadný text sa rozsypal a BASIC hlásil `chyba 26`. Nepomohlo ani
spomalenie vkladania pod tempo heartbeatu.

#### Hotové 26. 8. 2026: default zrušený

Nie opraviť skratku, ale odstrániť ju. Spravené celé:

- `main.cpp` — dva režimy namiesto troch, štartuje sa v exterke, prepína
  `Ctrl+K` (jedna skratka namiesto `Ctrl+Shift+B` a `Ctrl+Shift+E`; voľbu
  aj s jej cenou rozhodol majiteľ, viď sekciu 5). Braillovský režim už
  neprepúšťa text a pri `--diag` to trasovanie klávesov povie. Zanikol aj
  `RenderIdle` v hlavnej slučke — parkovanie, ktoré ním bolo treba
  zaplátať, tam už nie je.
- `machine.cpp` — preč so zachytávaním konzolového vstupu (funkcie 2 a 3),
  s `keys_`, `firmwareKeys_`, `keyboardInitialized_`, `InjectFirmwareKey`,
  `HardwareInputBusy`, `NoteHardwareInput` aj `RenderIdle`. `QueueKey`
  berie už len kódy klávesov (bit 7), text ide výhradne cez `QueueText`.
- testy — `QueueText` píše scancodmi, tabuľka je obrátená z ROM (viď nižšie).
  Escape v sonde sa píše ako znak, nie ako kód klávesu.

Deväť testov prechádza. `--help` aj úvodná hláška hovoria o dvoch režimoch.
**Majiteľ to 26. 8. 2026 odskúšal ručne a funguje** — to je tá časť, na
ktorú testy nesiahajú, lebo `main.cpp` nepokrývajú.

Cena, ktorú táto sekcia priznávala — že sa nebude dať napísať
`ľ ĺ ŕ ô ä Ľ` — **žiadna cena nebola**. Preverené pred zrušením: tie znaky
majú v Kamenických kódy nad `7Fh` a staré `QueueKey` posielalo každý taký
bajt do `PressMembraneKey`, takže sa nikdy nenapísali, len stlačili ako
akord. Podrobne v sekcii 5 a v uzavretej poznámke „znaky nad 7Fh".

#### A čím sa pri tom stane braillovský režim

Vyvstalo to 26. 8. 2026 pri chordovaní kurzorov a patrilo to do tej istej
práce, lebo braillovský režim bol dovtedy doslova „default plus body".
So zánikom defaultu bolo treba rozhodnúť, čo z toho ostatného v ňom
zostane. Rozhodnuté a spravené:

Braillova klávesnica Eureky má **presne dvadsať klávesov** (`IOPORT.H`):
šesť bodov a medzerník na riadku `89h`, osem funkčných na `8Ah`, štyri
kurzorové a shift na `8Ch`. Nič iné na nej nie je.

Čo tým režimom prechádzalo a ako to obstálo:

| čo | ide kam | je to na stroji? |
|---|---|---|
| body `F D S J K L` a medzerník | riadok `89h` ako akord | áno |
| `F1`–`F10` vrátane Shift | riadok `8Ah`, F9/F10 ako akordy | áno |
| kurzory vrátane akordov | riadok `8Ch` ako bitová množina | áno |
| `Home` `End` `PgUp` `PgDn` `Insert` `Delete` | riadok `8Ch` | áno — sú to **kurzorové akordy**, Home je hore plus vľavo |
| písmená, číslice, interpunkcia | vlastná fronta ROM (`C67B`) | **nie** |

Prvé štyri riadky nie sú obchádzka: každý z nich sa premietne na kláves
alebo akord, ktorý stroj naozaj má, a `Home` až `Delete` sú len pohodlný
názov pre kurzorový akord. Tie zostávajú.

Posledný riadok obchádzka **bola** — presne tá skratka, ktorú táto sekcia
zrušila. Text sa teda ignoruje: braillovský režim už neprepúšťa písmená.
Kto chce v ňom písať, píše body, tak ako na stroji.

Dve veci, ktoré pri tom nezabudnúť:

- Ignorovanie musí byť **ignorovanie, nie tichý nezmysel**. Stlačené `a`
  nemá urobiť nič — a keďže na tomto stroji je „nič" na nerozoznanie od
  „kláves neprišiel", nech to aspoň pri `--diag` povie trasovanie
  klávesov, ktoré tam už je.
- S textom padlo aj `Ctrl`+písmeno, lebo `BrailleBit` sa pri stlačenom
  `Ctrl` preskakuje. Nie je to strata: `Ctrl+C`, ktorý hlavné menu testuje
  na 18149, sa na skutočnej klávesnici vyrába akordom, nie ovládacím
  klávesom. Navyše `Ctrl+K` je odteraz prepínač režimu emulátora.

#### Hotové 26. 8. 2026: QueueText píše scancodmi

Prvý krok rozsahu je spravený. `QueueText` už nesype znaky do fronty ROM;
píše ich na emulovanej klávesnici PC, teda tou istou cestou, ktorou od
zrušenia defaultu chodí všetko ostatné.

Tabuľka sa **nikde nepíše ručne**. `BuildKeyboardLayout` ju pri načítaní
obrátí z obrazu: prejde všetky scancody krát tri modifikátory a pre každý
sa spýta `TranslatedScanCode`, čo by z toho ROM urobila. Tá ide po
vetvách dekodéra na `1DD63`, nie len po indexe do tabuľky, lebo vo
vetvách je polovica rozloženia:

- `1DDCB` vyberá tabuľku podľa `C670h` — `DF05`, `DF5E` pri shifte
  (bity 0–1), `DF98` pri pravom Alte (bit 4);
- od scancodu `3Ah` vyššie sa modifikátor **vôbec nepozerá** (`1DD68`),
  preto numerická klávesnica píše to isté so shiftom aj bez neho;
- `56h` je jediný kláves, ktorého zhiftovaný znak dekodér **počíta**
  namiesto hľadania (`1DD8C`), a preto jediné miesto, kde sú `<` a `>`;
- hodnoty od `F0h` sú samotné modifikátory (`1DDE6`), `01h`–`03h` sú
  mŕtve klávesy (`1DDED`) a od `80h` nad `3Ah` sú kódy Eureky, nie znaky.

Kde sedí shift a pravý Alt sa tiež **hľadá**, nie predpokladá: shift je
ten kláves, ktorého hodnota v `DF05` je `F1h`, a pravý Alt sa nájde
v zozname dvojíc na `DFD6` podľa výsledku `10h`. Kópia tabuliek
v zdrojáku by bola aj druhý názor navyše, aj kus ROM v repozitári.

Čo sa tým dá napísať — **111 znakov**:

- celá tlačiteľná ASCII, všetkých 95, vrátane `@ # $ ~ ^ & * { } [ ] ' \``
  cez pravý Alt a `< >` z klávesu `56h`;
- `08h` (backspace), `09h` (tab), `0Dh` (Enter) a `1Bh` (Escape);
- dvanásť českých písmen `é č ě ž ů ý á í ú ň š ř`.

To je presne to, čo 6.11 sľubovala: `ľ ĺ ŕ ô ä Ľ` medzi nimi nie sú,
lebo v tabuľkách nie sú. Ani `0Ah` tam nie je — nový riadok nie je kláves.

Overené cez ROM, nie proti vlastnej tabuľke: v BASICu sa napíše reťazec
a porovná sa s tým, čo firmvér vypľul na konzolu. Sedia všetky štyri
skupiny interpunkcie, obe písmenkové aj číslice, a dvanásť českých písmen
sa vráti bajt na bajt (`82 87 88 91 96 98 A0 A1 A3 A4 A8 A9`).

Znak, ktorý na klávesnici nie je, sa **neprepíše na nič podobné**:
`QueueText` nenapíše z reťazca nič, vráti `false` a povie ktorý bajt to
bol. Celý riadok alebo nič — polovica príkazu v stroji by test zhodila
o tri kroky ďalej a na inom mieste. `integration_test` aj `diag_probe`
to vypíšu a skončia.

Deväť testov prechádza aj po zmene.

#### Parkovanie: znovu overené 26. 8. 2026

Tvrdenie „už v strome" platilo. `biosWaiting_` v strome nebol,
`InterceptBios` nemal ani jednu vetvu, ktorá by vrátila `false`, a `Step()`
vracia `false` len po vypnutí stroja. Z toho plynulo, že `RenderIdle` je už
mŕtvy kód: `blocked` v hlavnej slučke `main.cpp` nastal iba po vypnutí,
a to sa zo slučky rovno vyskakuje. Zanikol spolu s defaultom.

#### Nedoriešené vedľa toho

Sonda prestala stíhať formátovanie. `RunUntilPrompt` sa dnes vracia pol
sekundy po tom, čo konzola stíchne, a formát je dlhé ticho, takže
`seq 15000000 kD7 Y Y` z `tests/README.md` skončí po necelých štyroch
miliónoch inštrukcií, ešte pred ním. **Nie je to chyba modelu**: keď sa
tá istá sekvencia vyklepe a stroj sa nechá bežať ďalej, po 206 miliónoch
inštrukcií povie „formátování skončeno" ako predtým. Chýba sonde spôsob,
ako povedať „a teraz už len bež" — tokenu na čakanie.

Meno súboru pri `SAVE` sa do jednoriadkového editora nedostane: pri
parkovaní je fronta prázdna a procesor stojí na `D874`, teda znaky mizú
inou cestou než cez makrá. Môže to byť aj artefakt sondy — tá podáva
klávesy bez pustenia a bez ľudského tempa, na rozdiel od `main.cpp`.

### 6.12 Kapacita diskety je z manuálu, nie z odhadu

Otázka znela, či `VirtualDisk` správne posúdi, že sa hostiteľský
priečinok na disketu zmestí. Odpoveď dal DPB, ktorý firmvér vydáva na
službu BDOS 31 (`eurekatech/TECHMAN1/BDOS.8`):

| pole | hodnota | dôsledok |
|---|---|---|
| BSH / BLM | 4 / 15 | blok má 2048 B |
| DSM | 399 | 400 blokov spolu |
| DRM | 255 | 256 položiek adresára |
| ALL | `11110000b` `00000000b` | prvé štyri bloky drží adresár (8 KiB) |
| EXM | 0 | jedna položka = jeden extent = 8 blokov = 128 záznamov |
| OFF | 0 | žiadna stopa nepatrí operačnému systému |
| SPT | 40 | 40 záznamov po 128 B na stopu |

Voľných teda zostáva **396 blokov = 792 KiB**, čo `BIOS.9` potvrdzuje
inými slovami: „800 K total, 8 K directory, leaving 792K available for
data storage". `DISKFREE.MAC` to potvrdzuje tretí raz — alokačný vektor
má 50 bajtov, teda 400 bitov.

Konštanty v `virtual_disk.cpp` tomu odpovedali presne a aritmetika
kontroly tiež: bloky na súbor sú `ceil(veľkosť / 2048)` (blok v CP/M
patrí vždy len jednému súboru) a položky adresára `ceil(záznamy / 128)`,
najmenej jedna. Odmerané sondou: 396 blokov sa pripojí, 398 nie; 256
položiek sa pripojí, 257 nie; export z úplne plnej diskety je bajt na
bajt zhodný, takže blok 399 sa naozaj používa.

Chyby boli inde — v tichých stratách okolo tej kontroly:

1. **Nečitateľný súbor sa preskočil bez slova** (`if (!input) continue;`).
   Overené zákazom čítania cez `icacls`: disketa sa pripojila a súbor na
   nej jednoducho nebol. Na stroji to vyzerá presne ako stratený súbor.
2. **Kontrola a import sa nezhodli na množine súborov.** Súbor, ktorému
   zlyhalo `file_size`, sa do súčtu nezarátal, ale alokoval sa. Prejsť
   mohla predbežná kontrola a spadnúť až tá strohá v cykle, bez čísel.
3. **Prehľadávanie priečinka sa dalo ticho useknúť.** `if (ec) break`
   zachytávalo len chybu konštruktora iterátora, ktorá sa nikdy
   nevyhodnotila, a chyba pri posune iterátora v range-for by vyhodila
   nezachytenú výnimku.
4. **Podpriečinky mizli bez slova.** CP/M ich nepozná, ale používateľ to
   musí počuť.

Opravené: prehľadávanie je `ScanFolder` s `increment(ec)`, každé
zlyhanie je pomenovaná chyba, podpriečinky zbiera
`VirtualDisk::skipped_entries()` a `main.cpp` ich vypíše. Hlásenie
o kapacite hovorí prebytok v blokoch aj v KiB a menuje tri najväčšie
súbory; číslovky sa ohýbajú (1 blok, 2 bloky, 5 blokov), lebo to znie
čítač obrazovky.

Regresiu drží `tests/disk_test.cpp` (18 kontrol). Testovacie priečinky
si generuje sám v `%TEMP%` — skutočný diskový priečinok je pohyblivý
cieľ a test, ktorý ho číta, meria to, čo tam práve niekto nechal.
Overené mutáciami: kontrola o dva bloky štedrejšia aj návrat tichého
`continue` test zhodí.

Otvorené, mimo tejto opravy: `BIOS.9` v úvode kapitoly píše, že logické
sektory sú číslované **1 až 40**, kým `VirtualDisk::ReadRecord` berie
0 až 39. Pre BDOS je nula správne — inak by test `com` nemohol prejsť —
ale program, ktorý si volá `bios_setsec` sám, by mal u nás všetko
posunuté o jeden záznam a sektor 40 by skončil chybou.

### 6.13 Časové funkcie — vyriešené

Hlásené 26. 8. 2026: nefungujú funkcie viazané na čas — **budík,
odbíjanie hodín, diár a automatické vypnutie pri nečinnosti**.

**Jedna zo štyroch je vyriešená a bola inde, než sa hľadalo.**
Automatické vypnutie po nečinnosti sa 26. 8. 2026 zmeralo: ROM ho
vyvolá **načas a správne**. Odpočítavadlo `C66Ch` dobehne na nulu, 30
sekúnd vopred zaznie výstražná znelka a firmvér vydá vypínací strob.
Nefungoval až posledný krok — port `B8h` bol v emulátore prázdny.
Zmerané sondou v scratchpade: stroj sa vypol po **303,66 s** hosťovského
času bez jediného klávesu. Opravené, viď „Vypínanie stroja" v sekcii 5.

Z toho plynú dve veci pre zvyšné tri:

1. **Domnienka o parkovaní zo sekcie 6.11 bola správna.** Kým sa
   parkovalo, heartbeat pri čakaní na kláves stál a `C66Ch` neodpočítaval
   vôbec. Zrušenie parkovania to rozbehlo — a keďže strob nebol
   modelovaný, urobilo to z ticha regresiu: emulátor po piatich minútach
   nečinnosti potichu zomrel. Ak budík, odbíjanie alebo diár závisia od
   heartbeatu rovnako, môžu byť **už opravené** a treba to overiť skôr,
   než sa hľadá nová príčina.
2. **Nebola to jedna príčina so zvyškom.** Vypnutie po nečinnosti nešlo
   cez porovnávanie RTC s uloženým časom, ale cez obyčajné odpočítavadlo
   v heartbeate. Budík a diár idú inou cestou — a tá je nižšie.

Overené 26. 8. 2026 aj z druhej strany: majiteľ výstražnú znelku
tridsať sekúnd pred vypnutím **počul**, takže celá cesta cez `C66Ch`
funguje od začiatku do konca. Nezhoda zostáva jediná a je otvorená:
majiteľ má dojem, že stroj pri tom päťminútovom vypnutí povie **„konec"**.
Meranie hovorí opak — za celých 320 sekúnd prešla do rečového engine iba
tá znelka a nič viac, a v ROM je „konec" viazané výhradne na vetvu
`CP 8Fh` na 18154. Ak sa dojem potvrdí, chýba v tej ceste ešte niečo.

#### Zvyšné tri: RTC sa nemodeloval — opravené 26. 8. 2026

Bol to jeden spoločný mechanizmus, nie tri chyby. Emulátor mal na
`rtc_status` `case 0x0290: return 0;`, takže poll v heartbeate čítal
donekonečna nulu a budík, odbíjanie ani diár nemali ako nastať.
Doložené meraním: pri boote a potom stále dokola `čítanie portu 0290h
-> 00h` z `PC CF63h`.

**Opravená bola aj premisa.** Nie je to prerušenie. RTC svojou
prerušovacou linkou na procesor vôbec nesiaha:

- ROM nikdy nenastaví `ITE1` ani `ITE2` — jediný zápis do `ITC`
  (`196BE`) zhodí bit 7 (TRAP) a nič viac;
- v celej ROM nie je ani jedna inštrukcia `IM`;
- vektory `INT1` a `INT2` (obraz tabuľky na `1D000`, kopíruje sa na
  `CC00` a odtiaľ na `C180`) ukazujú obidva na holý pahýľ `EI; RET`
  na `CC37`.

Linka ide na spínač napájania. Preto studený štart číta `rtc_status`
hneď na `18000`, odloží ho do `0040h` a na `180C2` z neho vyvolá
`.service_alarm1` — **budík zapne vypnutý stroj**. Kým stroj beží,
budík sa nachádza **pollovaním**: heartbeat na `PRT1` prepadne na konci
každého tiku do `.service_alarm` (`1D0FB`), a to je to čítanie na
`CF61`.

##### Mapa mechanizmu

Systémová skoková tabuľka je v RAM na `CFA0` (`jump_table equ
system_addr - 32*3`, `SYSEQU.LIB`), obraz má ROM na `1D3A0`. Z nej:

| položka | adresa | čo robí |
|---|---|---|
| `.schedule_alarm` | `CFC4` → `CF79` → `D95C` | naplánuje najbližšiu udalosť a vybaví premeškané |
| `.service_alarm` | `CFD3` → `CF54` → `D442` | pozrie `rtc_status`, a ak je bit 0, obslúži budík |
| `.service_alarm1` | `CFEB` → `CF65` | to isté, ale bez pohľadu na `rtc_status` |

Celý blok `CC00`–`CFFF` je kód bežiaci v RAM a je to kópia ROM
`1D000`–`1D400` (posun `10400h`); modul budíka a diára je fyzicky
`0D000`–`0FFFF` a mapuje sa na logické `D000` pri `CBR=0`. Preto
`.service_alarm` nemá v ROM ani jediné `CALL` — volá sa cez RAM.

##### Alarmové registre

`0DA5B` ich plní z šesťbajtovej štruktúry v poradí rok, mesiac, deň,
hodina, minúta, sekunda, a do polí, ktoré sa porovnávať nemajú, zapíše
**`80h`**. Registre sú binárne a nikdy nepresiahnu 99, takže bit 7 sa
s nijakou platnou hodnotou nebije. Stotiny a deň v týždni sú „ľubovoľné"
vždy, sekundy podľa `C43Fh`. `C43Fh` zároveň hovorí, o aký druh udalosti
ide (`1`, `2`, `4` — vetvy na `D454`, `D466`, `D46F`), a `0DA98` budík
ozbrojí (`rtc_mask` = `01h`, `rtc_command` = `1Ch`), `0DAA8` ho zruší.

##### Čo je v emulátore

`machine.cpp` má teraz masku, stavový register s nulovaním pri čítaní,
periodické udalosti (stotina až deň) a komparátor budíka s „ľubovoľnými"
poľami. Dve veci stoja za zapamätanie:

1. **Budík je hrana, nie úroveň.** Zhoda platí celú minútu; keby sa bit
   nastavoval podľa úrovne, heartbeat by ho prečítaním zhodil a hneď by
   naskočil späť, a ten istý budík by zvonil dovtedy, kým minúta neprejde.
2. **Zápis do `190h`–`197h` prepočíta komparátor bez vyvolania udalosti.**
   Firmvér plní osem registrov po jednom a nedopísaná zostava sa vie
   trafiť náhodou; keby sa to počítalo za zhodu, zjedlo by to hranu, na
   ktorej stojí skutočný budík.

Zrušený je aj starý aliasing: `290h`/`291h` už nepadajú do `io_[90h]`
a `io_[91h]`, teda do tých istých buniek ako hodinové registre.

##### Overené

Majiteľ 26. 8. 2026 ručne: **budík aj diár fungujú**, a dialóg „vlož
čas buzení" je počuť. (Sonda ho nezachytila, ale to je jej vlastná
slepota, nie chyba stroja — reč aplikácií cez `0103` neprechádza celá.) Automaticky
`integration_test ROM DISK_FOLDER rtc` — nastaví budík v aplikácii
hodín, skontroluje alarmové registre a masku, potom posunie hodiny do
tej minúty a čaká, kým firmvér budík obslúži. Že ho obslúžil, sa pozná
po tom, že `.schedule_alarm` prepísal alarmové registre na ďalší deň;
nič iné v ROM ich samo od seba nemení.

Aby test nemusel čakať dve minúty v reálnom čase, pribudlo
`EurekaMachine::SetRtcOffset()` — hodiny stroja sú hostiteľské, takže
sa posunú ony, nie test. Nič iné než testy to nenastavuje.

##### Vedľajšie zistenia z tejto práce

- Klávesnica ROM je **česká QWERTZ** (`DF05`): horný rad bez shiftu dáva
  písmená s diakritikou, číslice si žiadajú shift. Bez neho príde do
  dialógu budíka namiesto „10 33" reťazec „+ě šš" a stroj len pípne.
- Parser času na `E3BC` chce medzi hodinou a minútou oddeľovač, a musí
  byť **pod `'0'`**. `E40D` je `CP 30h / RET NC` — preskočí len znaky
  menšie než `30h`, takže medzera, bodka, čiarka či pomlčka prejdú.
  **Dvojbodka neprejde**: `3Ah` je nad `'0'`, takže sa `E40D` na nej
  zastaví, pošle ju do testu číslice a `CP 3Ah / JR NC` na `E3DE` ju
  zhodí do chyby. Overené aj z druhej strany — majiteľ skúšal „11:00"
  a stroj ho odmietol. „1033" zlyhá tiež, `E42E` číta číslice hltavo
  a vyjde mu 1033. Sekundy sú nepovinné.

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

### 6.16 Lupanie: chýbal väzobný kondenzátor — opravené

**Zmerané a opravené 26. 8. 2026.** Hlásené z ostrého používania:
pri zvýšenej hlasitosti počuť počas nečinnosti **nepravidelné lupanie**.

Podstatná časť diagnózy je v tom, kedy sa to *nedeje*: po čerstvom boote
je výstup **absolútne digitálne ticho**, tridsať sekúnd bez jedinej
nenulovej vzorky. Objaví sa to až keď stroj predtým prehovorí — a práve
to bola stopa, bez ktorej sa to nedalo nájsť.

DAC totiž drží **poslednú vzorku reči** a tá je tam, kde ju veta náhodou
nechala. Zmerané v pokoji po návrate z aplikácie:

| po klávese | DAC v pokoji | výstupná vzorka |
|---|---|---|
| F1 záznamník | 128 | 0 |
| F3 kalkulátor | 131 | +768 |
| F7 hudobný editor | 111 | −4352 |
| F10 kde som | 135 | +1792 |

V jednom behu bolo 28 sekúnd po sebe presne +2560 na každej vzorke.

`RenderAudio` má dolnopriepustný rekonštrukčný filter (6.1), ale **žiadnu
hornopriepusť**. Skutočný stroj má za výstupom väzobný kondenzátor —
každý zosilňovač ho má, inak by reproduktor trvale ťahal jednosmerný
prúd — a ten jednosmernú zložku odstráni a každý skok nechá odznieť.
U nás prejde rovno na výstup.

Lupanie z toho vzniká tak, že konštantná jednosmerná zložka je sama osebe
nepočuteľná, ale len čo sa zvukový prúd na okamih preruší alebo dobehne,
výstup skočí z tej hodnoty na nulu a späť. Tie prerušenia sú nepravidelné,
a tak je aj lupanie.

Vysvetľuje to zároveň vedľajší nález zo 6.10 (hudobný editor nechá DAC
na 65): nie je to zvláštnosť editora, je to všeobecná vlastnosť a editor
bol len najkrikľavejší prípad.

#### Oprava

Jednopólová hornopriepusť **30 Hz** za rekonštrukčným filtrom
(`kCouplingFilter` v `machine.cpp`), teda **modelovanie hardvéru, nie
kozmetika** — ten istý argument, akým sa do modelu dostala dolnopriepusť.

Prečo práve 30 Hz: fundamentál syntetizátora je okolo **174 Hz** (znelé
úseky držia periódu 41–45 vzoriek pri asi 7,5 kHz), takže filter sedí
takmer tri oktávy pod čímkoľvek, čo reč nesie, a berie jej 0,1 dB. Na
rozdiel od medznej frekvencie dolnopriepusti tu **nie je čo kalibrovať
počúvaním**: hodnota musí byť len dosť nízko, aby nechala hlas na pokoji,
a dosť vysoko, aby skok promptne odzvonil — časová konštanta je
1/(2π·f), teda 5,3 ms. Posúvať nadol, ak by reč znela chudobne; nie
vtedy, keď niečo lupne.

Zmerané na tej istej vete (F3, kalkulátor), bez filtra a s ním:

| veličina | bez filtra | s filtrom |
|---|---|---|
| RMS počas reči | 6281,4 | 6280,9 (**−0,00 dB**) |
| jednosmerná zložka počas reči | 37,4 | −0,4 |
| pol sekundy ticha po vete | konštantne +2560 | presne 0 |

Reč sa teda nezmenila merateľne vôbec a zmizla len tá zložka, ktorá tam
nemala čo robiť. Najväčší rozdiel jednej vzorky je 1683 (5,1 % rozsahu)
a je to práve odobratie stojatého odstupu, nie skreslenie hlasu.

**Potvrdené sluchom** (majiteľ, 26. 8. 2026): lupanie zmizlo **a reč znie
rovnako**. To je to podstatné, čo meranie samo povedať nevie — jednosmerná
zložka je na grafe zjavná, ale či ju bolo počuť, rozhoduje ucho.

Druhá polovica toho potvrdenia je kalibrácia pre každého, kto by tú
frekvenciu chcel meniť: pri 30 Hz **nie je hlas počuteľne dotknutý**.
Kto ju posunie nahor a bude sa pýtať, odkiaľ vie, že už zobral basy, má
tu záchytný bod.

Drží to `integration_test ROM DISK dc`: prejde štyri aplikácie, po každej
nechá dosek deväť sekúnd hosťovského času a overí, že posledná polsekunda
je v pásme ±64, teda štvrtine najmenšieho kroku, aký DAC vie urobiť. Ak
by žiadna z tých aplikácií nenechala DAC mimo stredu, test to **ohlási
a spadne** namiesto toho, aby prešiel bez toho, že by čokoľvek overil.
Odskúšané mutáciou: pri obídení filtra spadne na troch zo štyroch
aplikácií (po `F1` DAC zhodou okolností spočinie presne na 128, a práve
preto ich test skúša viac).

### 6.17 Výmena diskety za behu — EurekaDOS sa preloguje sám

**Rozhodnuté manuálom, 26. 8. 2026.** Na skutočnom stroji bola výmena
diskety bezstarostná vec: vytiahnuť, vložiť, pokračovať. Nie je to
zhoda okolností a **nie je to správanie stock CP/M 2.2**, kde po výmene
príde „BDOS err: R/O", kým sa drive neprihlási znovu.

`BDOS.8`, služba 13 (`bdos_reset`), to hovorí priamo:

> Forces BDOS to re-log the disk drive. **As drive logging is
> automatically performed in EurekaDOS**, this service is provided for
> compatibility.

Rovnako služba 28 (`bdos_protect`): stav read-only trvá „until a BDOS
reset is performed, **or the disk is changed**". Výmena diskety je pre
ten operačný systém prvotriedna udalosť.

**Ako ju zbadá, je dátovo, nie hardvérovo.** DPB v `BDOS.8` deklaruje
`CKS = 256/4`, teda 64 kontrolných súčtov pre 256 položiek adresára —
„an 8-bit checksum is maintained for each record in the directory *so
that disk changes can be detected*". Preto stroj nemá a nepotrebuje
žiadny signál „disketa vymenená": na `A8h` sú modem, komparátory
a batéria (viď mapu hardvéru), radič ponúka len `98h` bit 1 INDEX
a bit 7 not ready.

Pre emulátor z toho plynie, že mid-run výmena je malá práca a **nie je
to zásah do modelu stroja**:

- `VirtualDisk::Mount` stavia obraz z priečinka nanovo, a keďže sa volá
  medzi dvoma `Step()`, z pohľadu hosťa je výmena okamžitá. ROM potom
  nájde iný adresár, kontrolné súčty nesedia a preloguje sa sama.
- Ustrážiť treba len hostiteľskú stranu, a tá je hotová: nevymieňať,
  kým beží `fdcWriting_`, a starý obraz predtým zapísať späť
  (`DiskSettled`, `FlushDisk`).

Zatiaľ **nespravené a neodskúšané** — cesta na výmenu za behu v kóde
neexistuje, takže vyššie uvedené je z manuálu a z kódu, nie z merania.

### 6.18 GUI — otvorená otázka

**Zámer, 26. 8. 2026.** Emulátor dostane vlastné okno (Win32). Nie je to
kvôli jednej klávese; ide o ovládanie emulátora ako takého: nastavenia,
pohodlnejšie prepínanie diskiet, obľúbené diskety a čo príde neskôr.

Čo je už rozhodnuté alebo zistené, aby sa to znovu neodvodzovalo:

- **Virtuálnu obrazovku hostiteľ robiť nemusí.** Eureka ju má ako
  vlastné výstupné zariadenie a zapína si ju tam, kde to dáva zmysel —
  napríklad v BASICu. `cono_list` (`C90A`, `SYSRAM.A`) je bitová maska
  „Speech / Serial Port / Virtual Screen / Printer".
- **Hostiteľská konzola nie je tá obrazovka.** Je to odbočka z BIOS
  funkcie 4, zachytená na stube (`machine.cpp`), a ROM si znak potom aj
  tak pošle svojim zariadeniam. Keď prestane byť hlavným povrchom,
  konštrukčne sa nestratí nič a pre testy a `--diag` zostáva užitočná.
- **Prístupnosť:** chróm okna — menu, výber diskety, obľúbené,
  nastavenia — nech sú **štandardné Win32 prvky**. Štandardné menu,
  listbox a common dialog číta NVDA zadarmo; owner-draw je presne to,
  čo čítačky rozbíja.
- **Výmena diskety za behu je vyriešená vecne** (6.17): stroj sa
  preloguje sám, hostiteľ len nesmie vymieňať uprostred zápisu.

Čo GUI prinesie na klávesnici, a konzola to dať nevie:

- `lParam` bit 30 je „kláves už bol dole", teda **explicitné
  autorepeat**. Padla by tým heuristika `SameKey` v `PressMembraneKey`,
  ktorá dnes háda, či je opakovanie nové stlačenie.
- `WM_KILLFOCUS` dovolí pri strate zamerania **deterministicky pustiť
  všetko**. Dnes to rieši `ForgetStaleArrows` dvojsekundovým limitom
  a je to presne ten druh veci, ktorý potichu zožerie kurzorový kláves —
  nie je vylúčené, že s tým súvisí „umŕtvená klávesnica po čase" (6.9).
- Okno môže držať **skutočnú bitovú mapu dvadsiatich klávesov**, takže
  jeden membránový rámec je priamo stav klávesnice, nie skladačka
  z udalostí.

**Poradie oproti 6.11 je rozhodnuté: 6.11 najprv, a v konzole.** Obe
prepisujú tú istú časť `main.cpp`, takže otázka stála. Rozhodli tri
veci:

- Za 6.11 sú **tri hlásené chyby z ostrého používania** — funkčné
  klávesy, ukladanie v BASICu a medzerník v hudbe (6.9). GUI je dlhá
  práca a nie je dôvod, aby tie tri čakali za ňou.
- Väčšina 6.11 je v `machine.cpp` (zachytávanie konzolového vstupu,
  `keys_`, `keyboardInitialized_`) a s hostiteľským oknom nemá nič
  spoločné. Treba to tak či tak.
- Časť v `main.cpp` je **mazanie, nie stavanie**. Zmazať default stojí
  skoro nič, aj keď to GUI neskôr prepíše; opačné poradie by tú skratku
  prenieslo do nového okna a rušilo ju tam druhýkrát.

GUI teda začína nad dvojrežimovou klávesnicou, nie nad trojrežimovou.

Otvorené a treba rozhodnúť:

- **Ktoré nastavenia** a kde sa uchovajú. Dnešné prepínače sú prepínače
  príkazového riadka (`--pc`, `--disk`, `--diag`); časť z nich sa stane
  položkou v okne a časť by mala prežiť medzi behmi — a to je tá istá
  otázka ako uchovanie RAM (6.15), takže sa oplatí rozhodnúť naraz.
- **Čo s konzolou.** Buď zostane vedľa okna, alebo výstup pôjde do
  editačného prvku len na čítanie. Editačný prvok má tú výhodu, že
  v ňom NVDA vie prehliadať kurzorom; konzola má tú, že už funguje.

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

**Hotové 26. 8. 2026: default zrušený** (6.11), a s ním parkovanie,
zachytávanie konzolového vstupu aj `RenderIdle`. Malo to riešiť tri hlásené
chyby — hladujúce funkčné klávesy, ukladanie v BASICu a medzerník, ktorý
nezastaví hudbu. Deväť testov prechádza a majiteľ to odskúšal aj ručne.

1. **GUI** (6.18) — vlastné okno, nastavenia, prepínanie a obľúbené
   diskety. Najväčšia položka tohto zoznamu. Dedí `main.cpp` už bez tej
   skratky, tak ako bolo zamýšľané: prepínanie režimu je dnes jediné
   `Ctrl+K` a rodina `Ctrl+*` pre GUI je rozbehnutá. Pozor pri nej na to,
   že ROM na exterke Ctrl+písmeno rešpektuje (`1DE0E`), takže každá ďalšia
   taká skratka berie hosťovi jeden riadiaci znak.
2. **Hudba hrá o 14,5 % pomalšie** (6.10). Zmerané; hľadá sa okolo dvoch
   percent zle započítaných cyklov v obsluhe generátora tónov. Pozor na
   páku 8 : 1 — tempo reaguje osemkrát citlivejšie než cena obsluhy.
3. **Umŕtvená klávesnica po čase** (6.9) — čaká na postup na
   reprodukciu; bez neho je to beh naslepo. Prvá stopa, ktorá sa dá
   sledovať bez neho, sú `C598h`/`C599h` (viď 6.9). Pozor: kým nebol
   modelovaný vypínací strob, päť minút nečinnosti stroj potichu zabilo,
   takže časť starších pozorovaní „po čase prestane reagovať" môže byť
   práve toto. Časť z toho môže spadnúť aj s GUI — `WM_KILLFOCUS`
   nahradí dvojsekundový limit v `ForgetStaleArrows`.
4. **Zachovanie RAM medzi behmi** (6.15) — rozhodnuté, nespravené.
   Vypnutie je hotové a `C45Ah` už nesie značku, ktorú na to ROM sama
   používa. Oplatí sa rozhodnúť naraz s uchovaním nastavení (6.18).
5. **Výmena diskety za behu** (6.17) — vecne vyriešené, EurekaDOS sa
   preloguje sám. Zostáva cesta v kóde a stráženie, aby sa nevymieňalo
   uprostred zápisu.
6. **Prerenderovať `audio/`** na správnu frekvenciu namiesto štyroch
   hádaných; DAC beží asi 7,5 kHz (`tools/melodies.py` a export dát reči).
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
integration_test ROM DISK_FOLDER dc    -> PASS (výstup po reči sadne na ticho)
integration_test ROM DISK_FOLDER rtc   -> PASS (budík sa nastaví a zazvoní)
integration_test ROM DISK_FOLDER hudba -> PASS (medzerník zastaví znelku)
disk_test                              -> PASS (18 kontrol, bez ROM)
codec_test                             -> PASS (bez ROM)
```

Všetkých deväť naraz spustí `run-tests.bat`: paralelne, s jedným súhrnom
na konci a nenulovým návratovým kódom, keď čokoľvek zlyhá. Priečinok
diskety si pripraví sám, takže ručne netreba nič.

Pozor: `com` potrebuje `READ.COM` v priečinku disku a bez neho zlyhá.
Netreba ho hľadať — je v `eurekatech/TECHMAN1/READ.COM`, a `run-tests.bat`
si ho kopíruje sám.

`TP.COM` a `TPS.COM` (Turbo Pascal) sú zatiaľ nevyskúšané a boli by
podstatne tvrdším testom EurekaDOS.
