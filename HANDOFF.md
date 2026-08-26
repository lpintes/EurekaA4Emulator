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

### Tri režimy písania

Emulátor sa prepína medzi troma a nazývajú sa takto — pozor na to, že
„klávesnica Eureky" nie je názov žiadneho z nich, lebo tou je práve tá
braillovská:

| režim | prepína | čo je pod rukami |
|---|---|---|
| **default** | (štartový) | dvadsať klávesov verne, text skratkou do fronty ROM |
| **braillovská klávesnica** | `Ctrl+Shift+B` | default plus body na `F D S J K L` a medzerníku |
| **externá klávesnica PC** | `Ctrl+Shift+E` | úplne všetko ide scancodmi po sériovom porte |

Tou istou skratkou sa ide späť na default.

**Rozhodnuté 25. 8. 2026: default sa ruší.** Nie je to režim stroja, je to
obchádzka — Eureka mala dve klávesnice, vlastnú braillovskú a voliteľnú PC
na sériovom porte, tretia neexistovala. Zostanú dva režimy a štartovým bude
**exterka**, teda stav „Eureka s pripojenou klávesnicou". Dôvod aj postup
sú v 6.11.

### Braillovská klávesnica

`Ctrl+Shift+B` prepne písanie na šesť bodových klávesov: `F D S` sú
body 1, 2, 3 a `J K L` body 4, 5, 6, medzerník zostáva medzerníkom.
Akord sa zbiera, kým sú prsty dole, a vydá sa naraz pri pustení
posledného — to je perkinsovské správanie a `ReleaseKey` ho umožňuje.

Emulátor **neprekladá nič**. Pošle šesť bitov na riadok `89h`
a znak si nájde ROM sama (`1D79C`), takže fungujú všetky tri tabuľky
vrátane číselného režimu aj akordy s medzerníkom, o ktorých nevieme.
Doložené: štyri akordy napíšu v textovom procesore „ahoj" a `Home` to
prečíta späť — presne to robí `integration_test … kbd`.

**Kurzory sa v tomto režime chordujú tiež** a je to tak zámerne: kto píše
na braillovej klávesnici, čaká, že sa aj kurzory budú správať ako na
Eureke. Nie je to zvláštnosť tohto režimu — chordovanie je zapnuté všade
okrem exterky, kde sa scancody prekladajú po jednom a chordovať sa
nedajú (viď „Vypínanie stroja").

### Klávesnica IBM PC na sériovom porte

`Ctrl+Shift+E` prepne hostiteľskú klávesnicu na tú, ktorá sa k Eureke
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
- **Stroj nesmie byť zaparkovaný.** `InterceptBios` zastavuje CPU, keď
  aplikácia čaká na kláves. Scancode ale potrebuje, aby procesor bežal
  — inak sa prerušenie nemá kedy vyvolať. Preto `HardwareInputBusy()`:
  kým je vstup na ceste cez skutočný hardvér, parkovanie sa vypína.

Zostáva jediná skratka: text v režime **default** ide priamo do fronty
ROM na `C67B`. Je to zámerné a má to jeden konkrétny dôvod — je to
**jediný spôsob, ako napísať `ľ ĺ ŕ ô ä Ľ`**. Overené: v tabuľkách
klávesnice PC (`DF05`, `DF5E`, `DF98`) ani v braillových (`D7A0`,
`D7E0`, `D820`) tie znaky nie sú, sú tam len české `é č ě ž ů ý á í ú
ň š ř`. Je to česká ROM, takže skutočná Eureka slovensky písať
nevedela; default je teda jediné miesto, kde emulátor stroj zámerne
prevyšuje.

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

#### Otvorené vedľa toho: znaky nad 7Fh sa stláčajú ako akordy

`QueueKey` posiela každý bajt s bitom 7 do `PressMembraneKey`. Znak `Á`
je v Kamenických **8Fh**, takže napísať ho v režime **default** v hlavnom
menu znamená stlačiť štyri kurzory a stroj sa vypne; ostatné akcentované
znaky sa podobne stlačia ako nejaký iný akord namiesto toho, aby sa
napísali. Je to ďalší dôvod pre už rozhodnuté zrušenie defaultu (6.11)
a zároveň to spochybňuje tvrdenie, že default je jediná cesta, ako
napísať `ľ ĺ ŕ ô ä Ľ` — treba to preveriť, kým sa ten režim ruší.

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
- `EurekaMachine::RenderIdle()` dorenderuje cykly, ktoré zaparkovaný
  procesor neodbehol. Nie je to trik: skutočná Eureka tam len točí
  čakaciu slučku, DAC drží poslednú hodnotu a filter za ním beží ďalej.
  Overené, že tá držaná hodnota je stred stupnice — jedna sekunda
  nečinnosti má priemer 0,0 a rozsah 0 až 29, takže sa do výstupu
  netlačí jednosmerná zložka a nástup reči neluplne.

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

### 6.9 Hudba v hudobnom editore sa nedá prerušiť

Hlásené **majiteľom skutočného stroja z ostrého používania**, nie
z merania. Upresnené 25. 8. 2026 — pôvodné znenie („hranie sa nedá
zastaviť") je pravdivé len sčasti:

- **Medzerníkom sa hranie zastaviť nedá.** Toto je isté a platí vždy.
  Manuál pritom naznačuje, že by skladbu mal ukončiť **ľubovoľný**
  kláves. Je to najtvrdší bod, od ktorého sa dá začať.
- **Úplná nemožnosť prerušiť skladbu čímkoľvek je pravda, ale nie za
  každých okolností.** Stroj sa do toho stavu dá dostať, presné kroky
  na reprodukciu zatiaľ nikto nemá. Je to teda skôr stav, do ktorého
  emulátor občas spadne, než trvalá vlastnosť prehrávania.
- **Po čase občas prestanú fungovať aj šípky mimo hudby** — napríklad
  do adresára disku sa dá vojsť, ale nedá sa v ňom listovať. Opäť bez
  postupu na vyvolanie. Ak sú tie dva javy jeden, hľadá sa niečo, čo
  umŕtvi klávesnicu **naprieč aplikáciami** (zaseknutý stav dekodéra
  ROM alebo fronty emulátora), nie chyba prehrávacej slučky editora.

Čo je k tomu už známe, aby sa to znovu neodvodzovalo:

- **Nesúvisí to s dĺžkou stlačenia.** Tá je v cykloch hosťa
  (`kPressMs` = 120 ms) a rámce klávesnice sa posúvajú až pri čítaní
  portu `89h`, takže je nezávislá od toho, ako často beží hostiteľská
  slučka. Oprava slučky zo 63 na 352 Hz (sekcia 6.1) teda toto
  pravdepodobne **nerieši** — zrýchlila len príchod klávesu do
  emulátora zo 16 ms na 2,8 ms.
- **Klávesnica je počas hudby snímaná často, nie zriedka.** Obsluha
  generátora tónov na `00642` skenuje klávesnicu rýchlosťou DAC, teda
  tisíckrát za sekundu. Práve preto sa kedysi počítanie skenov namiesto
  času ukázalo ako chyba.
- Predchádzajúca oprava hudobného editora (sekcia 5, bod 2) riešila
  iný jav — že sa kláves *stratil*. Tento je, že sa neprejaví.

Kadiaľ ísť — v tomto poradí, lebo prvý krok je jediný spoľahlivo
reprodukovateľný:

1. **Medzerník počas hrania.** Zistiť, **čo presne prehrávacia slučka
   editora pýta** — či stav konzoly cez BIOS, alebo priamo porty
   membrány, a ktorý kláves vôbec hranie ukončuje. Sonda to vie ukázať
   bez hádania: `diag_probe ROM DISK seq … kC6` otvorí editor, `trace`
   zaznamená porty. Kým to nie je zmerané, nedá sa rozlíšiť, či
   emulátor kláves nedoručí, alebo či ho ROM v tej slučke nečíta.
   Pozor na to, že medzerník je zároveň ALT (sekcia 5) — je teda
   pravdepodobnejším kandidátom na zvláštne zaobchádzanie než ostatné
   klávesy.
2. **Umŕtvená klávesnica po čase.** Prejav mimo hudby (šípky v adresári)
   dáva jednoduchší terén: dosť dlhý `seq` s opakovanými šípkami
   a porovnanie, či ROM prestane čítať porty, alebo emulátor prestane
   vydávať. Bez postupu na vyvolanie je to však beh naslepo, takže sa
   oplatí až po bode 1.

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
namiesto stredných 128. `RenderIdle` ju drží ďalej, takže výstup má
konštantnú jednosmernú zložku −49 % rozsahu — overené, od 10. do 25.
sekundy je každá vzorka presne −16128. Skutočný stroj to nemá kam pustiť,
v emulátore to lupne pri nábehu a zoberie polovicu odstupu od orezania
všetkému, čo príde potom. Overenie zo sekcie 6.1, že držaná hodnota je
stred stupnice, platí **pre parkovanie po reči, nie po hudbe**.

Doplnené 26. 8. 2026: nie je to zvláštnosť editora. Rovnaké to je po
každej aplikácii, len s inou hodnotou, a príčina je jedna — chýbajúca
hornopriepusť. Viď **6.16**; tam sa to aj opraví, tento odsek zanikne.

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
treba emulátor spúšťať s **`--pc`** (alebo hneď dať `Ctrl+Shift+E`).

#### Prečo to samo o sebe nestačí: default režim

Zrušenie parkovania **rozbije režim default**. Text v ňom išiel do
hostiteľskej fronty `keys_` a vyzdvihovalo ho práve zachytené čítanie
konzoly; keď sa ROM prestane vracať ku dverám BIOS-u, je z tej fronty slepá
schránka a v defaulte prestane fungovať všetko. Braille a exterka idú cez
hardvér, tých sa to netýka.

Pokus posielať aj default do fronty ROM (`C67B`) neuspel: vstup sa dostal
dnu, ale hromadný text sa rozsypal a BASIC hlásil `chyba 26`. Nepomohlo ani
spomalenie vkladania pod tempo heartbeatu.

#### Rozhodnutý koniec: default sa ruší

Nie opraviť skratku, ale odstrániť ju. Rozsah:

- `main.cpp` — zrušiť default, štartovať v exterke, `Ctrl+Shift+B` bude
  prepínať medzi dvoma režimami namiesto troch
- `machine.cpp` — preč so zachytávaním konzolového vstupu (funkcie 2 a 3),
  s `keys_`, `keyboardInitialized_`, `biosWaiting_` aj parkovaním
- testy — `QueueText` musí písať scancodmi. Tabuľku **neodhadovať**:
  obrátiť ROM-ové `DF05`, `DF5E` a `DF98`. Znak, ktorý sa v nich nenájde,
  nech test ohlási nahlas, nie potichu napíše nezmysel. Pacing podľa ticha.
- `HANDOFF.md` — sekcia 5 sa stane dvoma režimami, 6.1 stratí dôvod pre
  `RenderIdle`

Cenou je, že `ľ ĺ ŕ ô ä Ľ` sa nebude dať napísať. To je správne: česká ROM
ich nemá v žiadnej tabuľke, takže ich nevedela napísať ani skutočná Eureka,
a default bol jediné miesto, kde emulátor stroj zámerne prevyšoval.

#### A čím sa pri tom stane braillovský režim

Vyvstalo to 26. 8. 2026 pri chordovaní kurzorov a patrí to do tej istej
práce, lebo dnes je braillovský režim doslova „default plus body". Keď
default zanikne, treba rozhodnúť, čo z toho ostatného v ňom zostane.

Braillova klávesnica Eureky má **presne dvadsať klávesov** (`IOPORT.H`):
šesť bodov a medzerník na riadku `89h`, osem funkčných na `8Ah`, štyri
kurzorové a shift na `8Ch`. Nič iné na nej nie je.

Čo tým režimom dnes prejde a ako to obstojí:

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

Posledný riadok obchádzka **je** — a je to presne tá skratka, ktorú táto
sekcia ruší. Takže **áno, majú sa ignorovať**: po zrušení defaultu nemá
braillovský režim prepúšťať text. Kto chce v ňom písať, píše body, tak
ako na stroji.

Dve veci, ktoré pri tom nezabudnúť:

- Ignorovanie musí byť **ignorovanie, nie tichý nezmysel**. Stlačené `a`
  nemá urobiť nič — a keďže na tomto stroji je „nič" na nerozoznanie od
  „kláves neprišiel", nech to aspoň pri `--diag` povie trasovanie
  klávesov, ktoré tam už je.
- S textom padne aj `Ctrl`+písmeno, lebo `BrailleBit` sa dnes preskakuje
  práve pri stlačenom `Ctrl`. Nie je to strata: `Ctrl+C`, ktorý hlavné
  menu testuje na 18149, sa na skutočnej klávesnici vyrába akordom, nie
  ovládacím klávesom.

#### Nedoriešené vedľa toho

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

### 6.13 Časové funkcie nefungujú

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

#### Zvyšné tri stoja na RTC prerušení, ktoré sa nemodeluje

**Diagnóza z 26. 8. 2026, oprava nespravená.** Je to jeden spoločný
mechanizmus, nie tri chyby.

Manuál (`IOPORT.H`, `rtc_mask`, port `290h` na zápis):

| bit masky | udalosť, ktorá vyvolá prerušenie |
|---|---|
| 0 | čas a dátum sa zhodujú s registrami `rtc_ram_*` (`190h`–`197h`) |
| 1–6 | periodicky každú stotinu, desatinu, sekundu, minútu, **hodinu**, deň |

Bit 0 **je** budík. Hodinový bit je odbíjanie. `rtc_status` (čítanie
z `290h`) hlási, ktorá udalosť nastala, bit 7 hlási prerušenie bez ohľadu
na povolenie v `rtc_command`, a **register sa čítaním nuluje**.
`rtc_command` (`291h`) má v bite 4 povolenie, aby RTC pri budíku zdvihol
svoju prerušovaciu linku; `rtc_default` je `00001100b`.

Emulátor má na to `case 0x0290: return 0;` (`machine.cpp`). Stav sa nikdy
nenastaví a RTC prerušenie sa negeneruje vôbec — modelované sú len tri
vnútorné zdroje Z180 (časovač 0, časovač 1, CSI/O). Že to nie je mŕtvy
kód, je doložené trasovaním: ROM ten register naozaj pollne, na `PC CF63h`,
a vždy dostane nulu.

Pri oprave pozor na aliasing, ktorý tam dnes je a zatiaľ nevadí: zápisy na
`290h`/`291h` idú do `io_[0x90]` a `io_[0x91]`, teda **do tých istých
buniek ako hodinové registre `90h` a `91h`**. Kým sa maska nečíta, je to
neškodné; potom by sa nastavenie hodín a maska prerušenia navzájom
prepisovali.

Čo bude treba: maska, stavový register s nulovaním pri čítaní, periodické
udalosti odvodené od toho istého hostiteľského času ako `ReadRtc`, zhoda
s `rtc_ram_*` a externé prerušenie do jadra. A až potom sa dá povedať, či
tie tri funkcie ožijú — je to najpravdepodobnejšia príčina, nie dokázaná.

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

### 6.16 Lupanie: chýba väzobný kondenzátor

**Zmerané 26. 8. 2026, oprava nespravená.** Hlásené z ostrého používania:
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

Oprava je jednopólová hornopriepusť rádovo 20–30 Hz, teda **modelovanie
hardvéru, nie kozmetika** — ten istý argument, akým sa do modelu dostala
dolnopriepusť. Na rozdiel od jej medznej frekvencie tu nie je čo
kalibrovať počúvaním; hodnota musí byť len dosť nízko, aby nezobrala
basy reči.

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

1. **Medzerník počas hrania nezastaví skladbu** (6.9). Z chýb hlásených
   z ostrého používania je to jediná, ktorá sa dá spoľahlivo vyvolať,
   takže sa dá aj zmerať. Ostatné body ďalej dole sú vylepšenia.
2. **Hudba hrá o 14,5 % pomalšie** (6.10). Zmerané; hľadá sa okolo dvoch
   percent zle započítaných cyklov v obsluhe generátora tónov. Pozor na
   páku 8 : 1 — tempo reaguje osemkrát citlivejšie než cena obsluhy.
3. **Zrušiť default režim a s ním zachytávanie vstupu** (6.11). Rozhodnuté
   a overené, že to rieši funkčné klávesy aj ukladanie. Zásah do `main.cpp`,
   `machine.cpp` aj testov naraz, s ručným odskúšaním. Parkovanie
   a `RenderIdle` tým zaniknú.
4. **Umŕtvená klávesnica po čase** (6.9) — čaká na postup na
   reprodukciu; bez neho je to beh naslepo. Pozor: kým nebol modelovaný
   vypínací strob, päť minút nečinnosti stroj potichu zabilo, takže časť
   starších pozorovaní „po čase prestane reagovať" môže byť práve toto.
5. **Lupanie a jednosmerná zložka** (6.16). Zmerané, príčina istá,
   oprava je malá: hornopriepusť do `RenderAudio`. Odstráni aj vedľajší
   nález zo 6.10. Najlepší pomer prínos/námaha z celého zoznamu.
6. **RTC prerušenie** (6.13) — odblokuje naraz budík, odbíjanie aj diár.
   Diagnóza je hotová a doložená manuálom; je to najväčší kus práce
   z tejto trojice, ale aj najviac funkcií naraz.
7. **Zachovanie RAM medzi behmi** (6.15) — rozhodnuté, nespravené.
   Vypnutie je hotové a `C45Ah` už nesie značku, ktorú na to ROM sama
   používa.
8. **Prerenderovať `audio/`** na správnu frekvenciu namiesto štyroch
   hádaných; DAC beží asi 7,5 kHz (`tools/melodies.py` a export dát reči).
9. **Formáty súborov z `FILE-FMT.D`** — telefónny zoznam, diár, melódie,
   databáza a texty sa dajú konvertovať do a z hostiteľských formátov.
   Doteraz to nešlo, lebo formáty neboli známe.
10. **Zahodené zápisy na 1C1FA** (6.2) — krátke, ale treba disassemblovať
   cestu adresára disku.
11. **Sériová relácia** — odblokuje `B0h` bit 7, `A8h` bity 2 a 5 a
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
disk_test                              -> PASS (18 kontrol, bez ROM)
codec_test                             -> PASS (bez ROM)
```

Pozor: `com` potrebuje `READ.COM` v priečinku disku a bez neho zlyhá.
Netreba ho hľadať — je v `eurekatech/TECHMAN1/READ.COM`.

`TP.COM` a `TPS.COM` (Turbo Pascal) sú zatiaľ nevyskúšané a boli by
podstatne tvrdším testom EurekaDOS.
