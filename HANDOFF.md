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

### 6.9 Uzavreté otázky

| bývalá otázka | výsledok |
|---|---|
| „slabá baterie" blokuje formátovanie | vyriešené, viď sekcia 5 |
| periféria na CSI/O | je to klávesnica IBM PC (`FFh` = reset, `AAh` = BAT passed) |
| nedotknuté bity latchov | pomenované z `IOPORT.LIB`, tri pôvodné výklady boli chybné |
| zápis `ADh` na port `88h` | prah komparátora pre stráženie batérie počas diskovej operácie |
| RTC — mapovanie registrov | emulátor bol **správne**; navrhovaná oprava by hodiny rozbila |
| adresný priestor | maska zmenená na `0x7ffff`, 19 bitov HD64180 |
| ktorý akord je Insert a ktorý Delete | `KB.LIB` mal pravdu: `8Dh` a `8Eh`. Rozhodla vlastná tabuľka ROM pre klávesnicu PC (`DF05`, scancode `52h` → `8Dh`, `53h` → `8Eh`). Emulátor držal variant z `KB.H` a bol opravený. |

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

1. **Prerenderovať `audio/`** na správnu frekvenciu namiesto štyroch
   hádaných; DAC beží asi 7,5 kHz (`tools/melodies.py` a export dát reči).
2. **Formáty súborov z `FILE-FMT.D`** — telefónny zoznam, diár, melódie,
   databáza a texty sa dajú konvertovať do a z hostiteľských formátov.
   Doteraz to nešlo, lebo formáty neboli známe.
3. **Zahodené zápisy na 1C1FA** (6.2) — krátke, ale treba disassemblovať
   cestu adresára disku.
4. **Sériová relácia** — odblokuje `B0h` bit 7, `A8h` bity 2 a 5 a
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
