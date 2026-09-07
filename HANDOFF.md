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

Toľko, koľko mal stroj klávesníc. Prepína sa medzi nimi `F11`, `Ctrl+K`:

| režim | čo je pod rukami |
|---|---|
| **externá klávesnica** (štartový) | všetko ide scancodmi po sériovom porte, ROM si prekladá sama |
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
krížikom uprostred zápisu na disketu je na skutočnom stroji vybratie
batérie — a po ňom prišla presne „inicializace eureky". Neuložiť snímku
v tom prípade teda nie je diera, je to vernosť hardvéru.

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

### 6.22 Správa diskiet — návrh, sčasti spravené

**Rozhodnuté 28. 8. 2026 s majiteľom.** Návrh stojí na tom, čo už
doložené je (6.6 nepovinná disketa, 6.12 kapacita z DPB, 6.17 výmena za
behu). Miesta, ktoré merať treba, sú v texte označené.

**Stav k 28. 8. 2026:** hotový je celý prvý kus poradia prác na konci
tejto sekcie — perzistencia, výmena za behu, ponuka `Disketa` aj sloty
rýchlej voľby pod `Ctrl+0` až `Ctrl+9`. Výmenu dvoch diskiet za behu
odskúšal majiteľ v ten istý deň a prebehla správne, čím sa doložila aj
6.17. Hotový je aj **dialóg `Nová disketa` vrátane nenaformátovaného
média** — čo sa pri jeho meraní zistilo o formátovaní, je v 6.5. Zámok
proti zápisu a rozdeľovač kolekcie **napísané nie sú**; všetko nižšie
o nich platí ako návrh.

**Slot unesie aj disketu v pamäti** (doplnené po pripomienke majiteľa
28. 8. 2026). Pôvodne bol slot len cesta k priečinku, takže disketa
v pamäti sa doň nedala priradiť — ani pri vytvorení, ani neskôr, čo
znamenalo, že rozmyslieť si to už nešlo. Slot preto nesie buď cestu,
alebo značku `*pamat`; cesta na Windows nikdy nezačína `*`, takže sa to
nemá ako pomýliť a súbor zostáva čitateľný. `SlotIsRam` berie ako značku
**čokoľvek, čo začína hviezdičkou** — nie zo šírky, ale preto, že
v súbore po staršej verzii môže stáť `*pamat-nenaformatovana` a bez toho
by sa taká hodnota vzala ako cesta a emulátor by ju skúšal pripojiť.

Podstatné je, čo taký slot **robí**: nevracia tú disketu, lebo tá po
ukončení emulátora nikde nie je — **vyrobí novú prázdnu**. Preto sa
všade volá „nová prázdna v pamäti“. Slot, ktorý by sľuboval návrat
a ticho podal prázdnu disketu, by bol horší než žiadny.

**Toto už neplatí, viď 6.24.** Slot disketu v pamäti drží (`DiskStash`),
takže druhé vloženie vráti tú istú so všetkým, čo na ňu Eureka zapísala;
prázdnu vyrobí len prvé vloženie nepoužitého slotu. Meno preto od
29. 8. 2026 znie len **„Disketa v pamäti“** — pôvodné „nová prázdna“
klamalo od prvého uloženého súboru. Čo z neho zostalo v platnosti, je
druhá polovica: cez ukončenie emulátora sa taká disketa neprenesie, a preto
sa naň `main.cpp` pri ukončení pýta.

**Značka pre nenaformátovanú disketu bola a je zrušená** (majiteľ,
28. 8. 2026). Slot menovaný „nenaformátovaná“ by taký zostal aj potom,
ako ju používateľ naformátuje — popisoval by stav, ktorý trvá po prvé
`Shift+F8`. V dialógu `Nová disketa` je preto pri tej voľbe výber slotu
zošedený.

**A rovnaké pravidlo platí pre titulok: disketa v pamäti je disketa
v pamäti.** Titulok kedysi hovoril „v pamäti, nenaformátovaná“ a vo
vlákne stálo sledovanie `has_format`, ktoré ten stav strážilo a hlásilo
oknu. Majiteľ to zamietol a s popisom odišiel celý aparát — sledovanie,
druhá notifikácia aj príznak `swapped` v `DiskChange`. Či je disketa
naformátovaná, je vec, ktorú používateľ práve mení, nie to, čo tá
disketa je. `VirtualDisk::has_format` zostáva v modeli (drží ho režim
`format`), do rozhrania sa nedostane.

Mená slotov počíta `SlotDisplayName` v `settings.cpp` a robí to na
jednom mieste zámerne: predtým sa to počítalo zvlášť v ponuke a zvlášť
v dialógu, a to sú dve miesta, ktoré sa rozídu.

Jedna vec sa oproti návrhu nižšie zmenila, a zmenila sa správne:
**prázdny slot nie je zošedený.** Zošediť ho by znamenalo, že `Ctrl+3`
z akcelerátorovej tabuľky — ktorá stav ponuky nepozná a `WM_COMMAND`
pošle tak či tak — je jediná vec v tejto ponuke, ktorá odpovie tichom.
Prázdny slot preto zostáva prístupný a povie dialógom, kde sa napĺňa.

Zámerom je prestať sa na disketu pozerať ako na jeden priečinok zadaný
pri štarte. Vzniknú tri veci: rýchla voľba diskety pod `Ctrl+číslo`,
dialóg na vytvorenie novej diskety a rozdeľovač veľkej kolekcie na
diskety.

#### Ponuka `Disketa` a rýchla voľba

**Mená položiek aj slovník ponuky sa zmenili 30. 8. 2026, viď 6.25.**
Nižšie stojí návrh v pôvodnom znení; čo z neho platí, je štruktúra, nie
texty.

`Uložiť disketu do priečinka…` sa presunie zo `Súboru` do novej ponuky
`Disketa`, kde bude aj `Vložiť z priečinka…`, `Nová disketa…`,
`Vysunúť`, deväť slotov rýchlej voľby, `Spravovať rýchlu voľbu…`
a `Rozdeliť kolekciu na diskety…`. V `Súbore` zostane `Skončiť`.

`Ctrl+1` až `Ctrl+9` a `Ctrl+0` (vysunúť) patria do
**`IDR_ACCELERATORS_HOST`**, teda platia až po `F11` alebo `Shift+F11`.
Reálne stlačenie je „`F11`, `Ctrl+3`" a to nie je ústupok: znamená to, že
rýchla voľba nestojí hosťa ani jeden kláves. Desať trvalých akcelerátorov
by bolo desať klávesov odobratých Eureke bez merania, či ich používa.
Kto by to chcel skrátiť, musí najprv sondou zistiť, čo `Ctrl+číslica`
na stroji robí — dovtedy je druhá cesta `F12 → D → 3` rovnako dlhá
a čítačka ju číta.

**Sloty sú explicitné a stabilné.** Zvažované a zamietnuté bolo plniť ich
automaticky z histórie „naposledy použité": slot, ktorý sa mení pod
prstom, je horší než žiadny. Keď je `Ctrl+3` dnes slovník, musí byť
slovník aj o mesiac.

Sloty musia byť v ponuke **vypísané menom**, nielen dostupné pod
skratkou; prázdny slot je zošedený. Skratka, ktorú nemá kde nájsť, je
skratka, ktorú nemá.

#### Perzistencia — prvý stav, ktorý prežije beh

Emulátor si dodnes nepamätá medzi behmi nič (viď otvorené v 6.18). Rýchla
voľba to otvára ako prvá.

- **Ak vedľa EXE existuje priečinok `config`, ide to tam**, inak do
  `%APPDATA%\EurekaA4\`. Priečinok, nie súbor: je to vedomý úkon, ktorý
  nevznikne omylom pri rozbalení archívu, a dá prenosný režim na USB
  zadarmo. Pri vývoji je to `bin\config`, a `bin\` je v `.gitignore`.
- **`config` sa sám nevytvára.** Automaticky vyrobený by prenosný režim
  zapol nechtiac a natrvalo.
- Formát je obyčajný `kľúč=hodnota`, UTF-8, `#` komentár, vlastný parser.
  **Nie `GetPrivateProfileString`:** `WritePrivateProfileStringW` zapisuje
  do súboru bez UTF-16 BOM v ANSI, takže cesta s diakritikou
  (`C:\Diskety\Príbehy`) sa pri prvom uložení poškodí, a **potichu**.
- Chýbajúci alebo nezrozumiteľný súbor znamená defaulty a **žiadny
  dialóg** (6.19). Slot, ktorý ukazuje na priečinok, čo zmizol, sa
  **nemaže**; dialóg príde až pri pokuse ho vložiť.
- Keď `config` existuje, ale nedá sa doň zapísať (USB v režime len na
  čítanie), **hlási sa to v okamihu úkonu** — keď používateľ pridá slot
  a ten sa neuloží — nie pri štarte. Inak by sloty ticho mizli medzi
  behmi, a hlásenie pri štarte je otravovanie (6.19, 6.20).

**Štart bez `--disk` vloží naposledy použitú disketu**, takže štart je bez
dialógu. Pamätá sa len priečinková disketa; RAM disketa nemá čo obnoviť.
Keď priečinok medzitým zmizol, štartuje sa s prázdnou mechanikou a dialóg
sa spýta, či si ho má emulátor pamätať aj naďalej — zmazať záznam sám by
bola strata a nezmazať ho nikdy by znamenalo ten istý dialóg pri každom
štarte.

#### Výmena za behu

Vecná stránka je hotová v 6.17: stroj sa preloguje sám. Ostáva hostiteľ.

Nové príkazy vo fronte (`PostMountDisk`, `PostEjectDisk`) — okno sa stroja
nedotýka ani tu. Worker točí `Step()`, kým `DiskSettled()` nie je `true`,
s tvrdým stropom rádovo dvoch miliónov cyklov; keď to nestihne, výmenu
**odmietne s hláškou** a nevymení nasilu. Potom `FlushDisk()` starého
obrazu, `Mount()` nového a `WM_EMU_DISK_CHANGED` späť do okna.

Tri veci, ktoré sa pri tom nesmú prehliadnuť:

- **`diskName_` a `diskDescription_` v `MainWindow` sú dnes konštanty
  z konštruktora.** Musia sa stať meniteľnými, inak titulok po výmene
  klame — a titulok je to, čo `NVDA+T` prečíta.
- ~~**Vysunutie RAM diskety so súbormi sa musí spýtať hneď.**~~
  **Neplatí od 28. 8. 2026 — zamietol to majiteľ a má pravdu.** Bolo to
  napísané a hneď zrušené: otázka pred každou zmenou média zavadzia
  presne tam, kde je výmena bežná práca — vložiť inú disketu, aby bolo
  odkiaľ kopírovať, je hlavný dôvod, prečo si niekto vôbec drží
  odkladaciu disketu v pamäti. Pýta sa preto **len pri ukončení**
  (`main.cpp`), čo je posledná chvíľa, keď to má zmysel.
  Nie je to tichá strata: titulok hovorí „v pamäti“ celý čas, takže to,
  čo by sa zahodilo, je na obrazovke aj pod `NVDA+T` **skôr**, než
  niekto siahne na `Ctrl+I`. A uložiť sa dá kedykoľvek cez `Ctrl+U`.
  Poučenie je všeobecnejšie než tento dialóg: **stráženie, ktoré stojí
  v ceste bežnému úkonu, je horšie než riziko, pred ktorým stráži** —
  najmä keď stav aj tak stojí v titulku.
- **Výmenu musí byť počuť.** Platí tu pravidlo „režim, ktorý nepočuť, je
  chyba": `Beep()` hostiteľa, dvojtón nahor = vložená, jeden nízky =
  vysunutá, odlišné od klávesnicových dvojíc.

#### Dialóg `Nová disketa` a nenaformátované médium

Jeden dialóg zo šablóny, **nie viacstranový sprievodca**: jedna rola,
jedno poradie Tab, jedno Enter sa čítačke číta lepšie než tri stránky
`PropertySheet`. Prepínače: z existujúceho priečinka, nový prázdny
priečinok, prázdna v pamäti naformátovaná, prázdna v pamäti
nenaformátovaná. Plus voľba „pridať do rýchlej voľby ako slot …".

**Prepínače už takto nevyzerajú, viď 6.25.** „Z existujúceho priečinka“
zrušené ako duplikát `Ctrl+I`, zvyšné tri premenované na trvalá /
dočasná v pamäti / dočasná v pamäti nenaformátovaná. Voľba slotu
zostala.

Nenaformátovaná disketa je lacná, lebo model už vie povedať „tento sektor
tu nie je" — `StartFdcCommand` vracia `0x10` (Record Not Found):

- `VirtualDisk` dostane bitovú mapu naformátovaných stôp, indexovanú
  `cylinder * 2 + side`. Priečinok aj naformátovaná RAM ju majú plnú.
- `ReadPhysicalSector` a `WritePhysicalSector` na nenaformátovanej stope
  vrátia `false`, z čoho `machine.cpp` už dnes urobí RNF. Eureka ohlási
  „vadný disk", presne ako skutočný stroj.
- **Write Track (`F0h`) stopu naformátuje** — nastaví bit a vyplní ju
  `E5h` — ale **len pre médium v pamäti**. Pri hostiteľskom priečinku
  zostáva správanie z 6.5: obraz stopy sa zahadzuje, lebo mazať
  používateľove súbory nie je vec modelu.

Tým `Shift+F8` na RAM diskete konečne niečo naozaj robí a nenaformátovaná
disketa sa formátovaním stane obyčajnou, takže ju na konci ide uložiť do
priečinka. Zapadá to bez výnimky.

#### Zámok proti zápisu — model hotový 29. 8. 2026, hostiteľská strana nie

Disketa má byť uzamykateľná: prepínacia položka v ponuke `Disketa` aj
vlastnosť slotu, default odomknutá. Chráni rozdelenú kolekciu pred
prepísaním zo stroja.

**Model je hotový a firmvér ho prijal.** Bola to jedna z tých vecí, kde sa
odvodenie neoplatilo — manuál to má napísané a rozhodol to za pár minút:

- `DEVICES.10` má bit 6 ako „Write protected" v odpovedi
  `fdc_ctl_chkdsk` aj `fdc_ctl_diskin`, a pre `fdc_ctl_write_track` uvádza
  návratový stav 2, „Disk Write Protected".
- `SYSEQU.LIB` má `write_protect_mask = 01000000b` a — čo je dôležitejšie —
  `write_error_mask = 11011110b` s bitom 6, kým `read_error_mask =
  10011110b` bez neho. **Ochrana teda zastavuje zápis a čítanie nechá
  bežať; nie je to dohad, je to maska.**
- `ERRNO.LIB` má `EDISKWP = 28`, „Disk write protected", a v ROM sú hlášky
  na to naviazané: `13D5E` „disk je chráněn proti zápisu" pri formátovaní,
  `14212` a `14269` pri hromadnom kopírovaní a rovnaká veta v tabuľke chýb
  BDOS na `1BEF8`.

V modeli je to `VirtualDisk::set_write_protected`, ktoré `TypeOneStatus`
premieta do bitu 6 a ktoré odmieta Write Sector aj Write Track statusom
`40h` — pred zápisom, ako to robí 1770. `WritePhysicalSector` a
`WriteRecord` ho odmietajú ešte raz, lebo BIOS stub ide do obrazu mimo
radiča.

Zámok patrí médiu, nie mechanike: `Mount`, `Eject` aj `CreateRamDisk` ho
nulujú. Zámok, ktorý prežije disketu, pre ktorú bol nastavený, by
**potichu** chránil tú nasledujúcu.

Odmerané na bežiacej ROM (`integration_test ROM DISK wp`): chránená
disketa sa **číta normálne** — `READ.COM` sa z nej spustí a BIOS z nej
prečíta plných 196 záznamov — a formátovanie odmietne slovami „disk je
chráněn proti zápisu"; po zrušení zámku tie isté klávesy prejdú. Poistka
overená mutáciou: keď `TypeOneStatus` bit 6 nedá, stroj ponúkne
preformátovanie a režim `wp` spadne.

**Kedy presne v dialógu tá hláška padne, test zámerne nepribíja.** ROM sa
najprv pýta „mám formátovat disk" a na diskete s formátom aj „disk je už
naformátován, přeformátovat"; odmietnutie príde na konci tej rady, po
oboch potvrdeniach. Meranie, ktoré tvrdilo inak, bolo meranie sondy, nie
stroja: dve „y" poslané tesne za sebou zahryznú druhú otázku skôr, než ju
stroj vysloví, a `k00` poslané ako výplň čakania je odpoveď na otázku,
ktorá ešte nezaznela. Sonda má na čakanie token `.`.

**Hostiteľská strana je hotová v ten istý deň.** Prepínacia položka
`Disketa → Zamknúť proti zápisu`, `F11`, `Ctrl+Z` v tabuľke
`IDR_ACCELERATORS_HOST`, stav v titulku („disketa: testdisk, zamknutá“)
a vlastný tón — nízke dlhé pípnutie zamkne, vysoké odomkne. Tretí tvar
zámerne: dvojica tónov už znamená klávesnicu a dvojica krátkych disketu,
takže zámok nesmie znieť ako ani jedno z nich.

Vo `VirtualDisk` sa zámok pri vložení nuluje, takže hostiteľ ho podáva
**spolu s pripojením** (`PostMountDisk(folder, protect)`), nie zvlášť po
ňom — inak by disketa bola na okamih zapisovateľná presne v tom okne,
ktoré má zámok chrániť.

**Zámok je trvalý a patrí diskete, nie relácii.** Prvá verzia ho viazala
na slot rýchlej voľby a rušila pri každom vložení; nahlásil to majiteľ
29. 8. 2026 na skutočnom postupe, ktorý to zabije: **hromadné kopírovanie
v diskových funkciách žiada chránený zdroj** („zdrojový disk není chráněn
proti zápisu“, `14269`) a potom strieda zdroj s cieľom — nakopíruje, koľko
sa zmestí, vypýta si cieľovú disketu, potom zase zdrojovú. Zámok, ktorý
zmizne pri výmene, tú prácu zastaví na druhom kroku.

Preto je zámok v nastaveniach zoznam **ciest** (`zamok1=`, `zamok2=`…),
nie vlastnosť slotu, a platí pre disketu bez ohľadu na to, ktorou cestou
príde: ponuka, slot, `--disk`, obnovená posledná disketa. Cesty sa
porovnávajú cez `Settings::SameDisk` — bez ohľadu na veľkosť písmen, tvar
lomky a koncovú lomku — lebo výber priečinka, ručne napísaný slot a
kanonická cesta z `VirtualDisk` sa líšia presne v týchto veciach a surové
porovnanie by zámok stratilo **potichu**. To isté pravidlo používa aj
dialóg slotov, aby dva sloty s jedným priečinkom nemohli byť zaškrtnuté
rôzne. Disketa v pamäti sa nezapisuje: neexistuje mimo behu, takže jej
zámok trvá presne tak dlho ako ona. Drží to `settings_test`.

Overené zvonka na bežiacom procese: `WM_COMMAND` s `ID_DISK_PROTECT`
prepne titulok na „…, zamknutá" a späť, položka ponuky je po zamknutí
`MF_CHECKED` (stav `0x8`) a po vysunutí diskety `MF_GRAYED` (`0x1`).
Pripomínam, že cez hranicu procesov sa ponuka číta **po pozíciách** —
`MF_BYCOMMAND` vráti −1.

Trvalosť odmeraná na tom istom procese, presne v poradí hromadného
kopírovania: zamknúť `testdisk`, vložiť disketu v pamäti, vrátiť
`testdisk` — titulok povie „zamknutá“ znovu; po reštarte emulátora tiež.
Skúšané v prenosnom režime (`config` vedľa EXE), aby to nesiahlo na
nastavenia majiteľa.

Otvorené zostáva len to, čo bolo dôvodom celej veci: rozdeľovač kolekcie,
ktorý má zamknuté sloty používať.

#### Rozdeľovač kolekcie

Nová vrstva `src/disk_layout.h/.cpp`: vstup je zoznam `{cesta, veľkosť}`,
výstup **plán** — vektor diskiet s položkami a ich 8.3 menami plus zoznam
odmietnutých s dôvodom. Žiadny Win32, žiadny `EurekaMachine`, žiadne
kopírovanie; to sú ďalšie dve vrstvy nad ňou. Testuje sa ako `disk_test`,
lebo algoritmus berie čísla a súborový systém nepotrebuje.

Kapacitné pravidlá **nesmú byť napísané druhýkrát** — preto vrstva v C++
vedľa `virtual_disk.cpp`, nie skript v `tools/`. Dve kópie tých istých
konštánt sú presne ten typ tichej nezhody, na ktorý tento projekt dopláca.

**Obmedzenia sú tri, nie dve** (6.12): 396 blokov po 2 KiB, 256 položiek
adresára — tisíc malých súborov narazí na toto, nie na kapacitu — a
**unikátnosť 8.3 mien v rámci diskety**. Súbor nad 792 KiB sa nezmestí
nikam a skončí v odmietnutých s dôvodom, nikdy sa nestratí ticho.

**Algoritmus nebalí súbory, balí nedeliteľné jednotky.** Majiteľ na to
upozornil menovite: `TP.COM` bez `TURBO.MSG` je nefunkčný program. Je to
prvotný pojem algoritmu, nie záplata — dodatočne sa vlepuje ťažko, lebo
všetky režimy delenia s jednotkami pracujú. Cena jednotky je súčet cien
jej súborov; v CP/M má každý súbor vlastné bloky aj vlastnú položku
adresára, takže tu sa nedá nič ušetriť ani prehliadnuť.

Jednotky vznikajú z troch zdrojov, v poradí spoľahlivosti:

1. **`SPOLU.txt` v zdrojovom priečinku** — jedna skupina na riadok, mená
   oddelené čiarkami. Jediný spoľahlivý zdroj, lebo ostatné dva hádajú,
   a hlavne: prežije opakované delenie. Kto raz zistí, že `TP.COM` chce
   `TURBO.MSG`, zapíše to raz.

   **Doplnené 7. 9. 2026: nepíše ho len človek ručne — mení sa aj
   v sprievodcovi.** Veta „zapíše to raz“ vyššie znela ako ručná úprava
   textového súboru; rozhodnuté je, že keď používateľ jednotku opraví
   v GUI, sprievodca ju zapíše späť do `SPOLU.txt`. Dôvod je ten istý,
   pre ktorý je `SPOLU.txt` prvý v poradí spoľahlivosti: oprava má prežiť
   opakované delenie, a to sa stane len vtedy, keď sa má kam zapísať.
   Z toho plynie požiadavka na vrstvu — jednotky musia byť v pláne
   v tvare, ktorý sa dá upraviť a zapísať späť. Je to zároveň jediná
   výnimka z pravidla, že zdrojový priečinok sa neotvára na zápis, a
   platí len na výslovný pokyn používateľa.
2. **Zhoda mena** (default zapnuté): `TURBO.COM` + `TURBO.MSG` +
   `TURBO.OVR`. Bezpečné a chytí väčšinu skutočných prípadov.
3. **Sprievodné prípony k jedinému `.COM` v priečinku** — `.OVR`, `.OVL`,
   `.MSG`, `.HLP`, `.CFG`, `.INS` (default vypnuté). `WS.COM` +
   `WSMSGS.OVR` + `WSOVLY1.OVR` chytí len toto pravidlo, ale je to
   hádanie.

**Premenovanie je tichšia polovica toho istého problému.** Rozdelenie na
dve diskety je vidieť — program sa nespustí a je jasné prečo. Ale keď
`UniqueCpmName` premenuje `TURBO.MSG` na `TURB~1.MSG` kvôli kolízii,
program svoj súbor nenájde **ani keď sú obidva na jednej diskete**, a
zlyhá to nepochopiteľne. Preto: **vnútri jednotky sa nikdy nepremenováva**
— hroziaca kolízia posiela celú jednotku na inú disketu. Premenovanie je
posledná záchrana pre osamotené súbory a musí byť v pláne vypísané
menovite.

Režimy delenia, vždy deterministické:

- **Sekvenčne, abecedne (default).** Súbory v poradí, plniť do plna,
  nikdy nepreskakovať. Vyzerá to hlúpo a je to najsilnejší režim pre
  nevidiaceho používateľa: „disketa 3 sú súbory od K po P" je vlastnosť,
  ktorú si človek zapamätá, a pridanie súboru neprehádže zvyšok.
- **Podľa priečinkov.** Podpriečinok je skupina, skupiny sa balia cez
  first-fit-decreasing. Skupina väčšia než disketa sa reže pozdĺž
  vlastných podpriečinkov, a keď ani to nestačí, po abecede na
  `HUDBA 1/3`, `2/3`, `3/3`.
- **Natesno.** FFD po jednotlivých súboroch, najmenej diskiet. Pre
  archiváciu, nie pre prácu.

**Optimálne balenie sa zámerne nehľadá.** Nie preto, že je NP-ťažké — to
je ten menší dôvod. Najtesnejšie balenie rozseká priečinky a používateľ
potom nevie, kde čo je; človek hľadá súbor podľa toho, s čím súvisí, nie
podľa toho, kde bola v balení diera. Ušetrená disketa nestojí za stratenú
orientáciu.

Výstup: priečinky `01-HUDBA`, `02-SLOVNIK`, pomenované podľa najväčšej
skupiny na diskete — meno priečinka nesie titulok okna, takže to má
priamu hodnotu. K tomu `OBSAH.txt` v koreni cieľa, mimo diskiet, aby
nežral miesto. Voliteľne aj `OBSAH.TXT` na každej diskete v Eurekinom
kódovaní cez `text_codec.cpp`, aby si zoznam prečítal sám stroj — stojí
to jeden blok a jednu položku a **musí sa to započítať do kapacity pred
delením**, nie po ňom.

**Náhľad plánu pred vykonaním** je druhý krok sprievodcu a musí riziko
pomenovať, nie len spočítať diskety. Tri zoznamy: priečinky rozdelené
medzi viac diskiet (v sekvenčnom režime bežné a presne tam to riziko
žije), zlúčené jednotky aj s pravidlom, ktoré ich zlúčilo, a premenované
či odmietnuté súbory s dôvodom. To isté patrí do `OBSAH.txt`, aby sa to
dalo dohľadať o mesiac. Cieľ musí byť prázdny priečinok; nič sa
neprepisuje a zdroj sa neotvára na zápis vôbec.

#### Výber priečinka: vybrať a pomenovať sú dva úkony

**Doplnené 28. 8. 2026 po pripomienke majiteľa.** Uložiť disketu niekam
nové znamenalo dva kroky — odísť priečinok vytvoriť a vrátiť sa poň.
`IFileOpenDialog` s `FOS_PICKFOLDERS` totiž existujúci priečinok vyžaduje
a pole na meno nemá.

Preto sú vo `win/dialog.h` **dva** výbery a rozdiel medzi nimi je vecný:

- `PickFolder` — vybrať existujúci. Vloženie diskety, priradenie slotu,
  disketa z priečinka.
- `PickFolderToCreate` — pomenovať nový. Ukladací dialóg v režime
  `FOS_PICKFOLDERS`, čo je jediná kombinácia, ktorú shell ponúka s poľom
  na názov. `FOS_PATHMUSTEXIST` zostáva (nadradený priečinok skutočný byť
  musí), `FOS_FILEMUSTEXIST` sa zhasína. Používa sa všade, kde sa píše:
  `Ctrl+U`, ponuka pri ukončení, nový prázdny priečinok.

To pole je aj prístupnejšia polovica veci: napísať meno je jeden editačný
prvok, ktorý čítačka prečíta, kým „Nová zložka“ na paneli nástrojov sa
musí hľadať. Samotné vytvorenie robí `VirtualDisk::ExportTo`, ktoré
`create_directories` volalo celý čas — chýbal len dialóg, čo taký názov
pustí ďalej.

#### Poradie prác

1. ~~Perzistencia, výmena za behu, ponuka `Disketa`, sloty~~ — **hotové
   28. 8. 2026.**
2. ~~Dialóg `Nová disketa` a nenaformátované médium~~ — **hotové
   28. 8. 2026**, aj s režimom `format` v `integration_test`.
3. `disk_layout` a testy, ešte bez GUI.
4. Sprievodca rozdelenia nad hotovou vrstvou.

Zámok proti zápisu je hotový celý, model aj hostiteľská strana.

**Kde rozdeľovač nadviaže na hotový kód.** Keď sa priečinok na disketu
nezmestí, `VirtualDisk::CheckCapacity` už dnes vypíše prebytok v blokoch
aj v KiB, menuje tri najväčšie súbory a končí vetou „Rozdeľte priečinok na
viac priečinkov a striedajte ich ako diskety.“ To je presne tá rada, ktorú
má rozdeľovač nahradiť skutkom — tá hláška je jeho prirodzené miesto
vstupu a je to aj miesto, kde už kapacitná aritmetika stojí hotová.

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
5. **Správa diskiet** (6.22) — perzistencia, výmena za behu, rýchla
   voľba aj dialóg `Nová disketa` s nenaformátovaným médiom **hotové**
   28. 8. 2026; **zámok proti zápisu** a **disketa ako objekt** (zásobník
   slotov, 6.24) **hotové** 29. 8. 2026; **zámok diskety v slote** (6.26)
   a **slovník „neuložená“ s Uložiť ako** (6.28) **hotové** 31. 8. 2026.
   Ostáva **rozdeľovač kolekcie**
   (`disk_layout`), ktorý je na zvyšku nezávislý a dá sa písať aj testovať
   bez GUI a bez ROM. Že sa EurekaDOS po výmene preloguje sám, je odmerané
   (koniec 6.17), zatiaľ ale len ručne — a rovnako chýba test cez ROM na
   hromadné kopírovanie, ktoré je najtvrdšia skúška celej správy diskiet
   (6.24).
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
integration_test ROM DISK_FOLDER format-> PASS (Shift+F8 naformátuje prázdnu)
integration_test ROM DISK_FOLDER wp    -> PASS (zámok číta, zápis odmietne)
integration_test ROM DISK_FOLDER hlaseni -> PASS (tri stavy mechaniky, tri vety)
disk_test                              -> PASS (144 kontrol, bez ROM)
codec_test                             -> PASS (bez ROM)
settings_test                          -> PASS (51 kontrol, bez ROM)
```

Všetkých **trinásť** naraz spustí `run-tests.bat`: paralelne, s jedným
súhrnom na konci a nenulovým návratovým kódom, keď čokoľvek zlyhá. Priečinok
diskety si pripraví sám, takže ručne netreba nič.

Ten počet je jediné miesto, kde sa tento zoznam dá overiť zvonka, a preto tu
stojí číslom: keď režim pribudne do `MODES` v `Makefile` a sem nie, rozdiel
nevidno inak než spočítaním riadkov `PASS`. Presne to sa aj stalo — `wp`
tu chýbal a text hovoril „jedenásť“, kým `run-tests.bat` už dávno púšťal
dvanásť procesov. Trinásty je `hlaseni` (6.30).

Pozor: `com` potrebuje `READ.COM` v priečinku disku a bez neho zlyhá.
Netreba ho hľadať — je v `eurekatech/TECHMAN1/READ.COM`, a `run-tests.bat`
si ho kopíruje sám.

`TP.COM` a `TPS.COM` (Turbo Pascal) sú zatiaľ nevyskúšané a boli by
podstatne tvrdším testom EurekaDOS.
