# Eureka A4 — mapa hardvéru odvodená z ROM (a4rom.dmp)

Zdroj: `a4rom.dmp`, 262144 B, MD5 9aa101ab69fc367e114e1a84b08feea1.
Všetko nižšie je odvodené výhradne z kódu v tejto ROM.

## Procesor a pamäť

- Hitachi HD64180 / Zilog Z180 (potvrdené inštrukciami IN0/OUT0, MMU, DMA, PRT, CSI/O).
- Reset na 0000h nastaví CBAR=D1h, CBR=0Bh, vypne refresh, ICR=0, skočí na D000h.
- Common area 1 = logické D000h–FFFFh, fyzicky +B000h, teda 18000h–1AFFFh.
- Zvyšok ROM sa mapuje cez bank area (BBR).
- ROM na fyzických 00000h–3FFFFh.
- DMA kanál 1 číta zdroj s bankou 07h, teda RAM siaha aspoň po 70000h+.

## Moduly ROM (kontrolný súčet každého = 0 mod 256)

| rozsah | obsah | build |
|---|---|---|
| 00000–09FFF | program reči | Jan 31 13:42:49 1992 |
| 0A000–0CFFF | BASIC | Dec 31 11:25:53 1990 |
| 0D000–17FFF | užívateľské programy | Jul 22 14:33:16 1992 |
| 18000–1FFFF | operačný systém | Jul 22 14:50:14 1992 |
| 20000–3FFFF | dáta reči (8-bit PCM) | Dec 04 17:39:25 1990 |

Kontrolná rutina: 18000h+1DA90h fyz., `ADD A,(HL)` cez banku, výsledok musí byť 0.

## I/O porty

Dekodér s výberom po 8 portoch: 80, 88, 90, 98, A0, A8, B0, B8.

| port | smer | funkcia | istota |
|---|---|---|---|
| 80h | W | riadiaci latch, tieň C439h | vysoká |
| 88h | W | 8-bit DAC (zvuk aj reč) | vysoká |
| 89h, 8Ah, 8Ch | R | riadky klávesnicovej matice (3 bajty) | vysoká |
| 98h | R/W | WD177x stav / príkaz | istá |
| 99h | R/W | WD177x register stopy | istá |
| 9Ah | R/W | WD177x register sektora | istá |
| 9Bh | R/W | WD177x dátový register | istá |
| A0h | W | riadiaci latch, tieň C43Ah | vysoká |
| A8h | R | stavové vstupy | vysoká |
| B0h | W | riadiaci latch, tieň C438h | vysoká |
| B8h | R | čítaný len v slučke po vybití batérie | nízka |

Disketová radič potvrdený protokolom seek na 19806h:
`IN A,(99h)` (aktuálna stopa) → `OUT (9Bh),A` (cieľová) → príkaz 14h.
Formátovanie: DMA kanál 1, 7000 bajtov (jedna DD stopa) do 9Bh, príkaz F0h/F2h.

## Zvuk

Generátor je čisto softvérový. Obsluha prerušenia PRT0 na fyz. 0FB6D:

```
LD BC,(B0A3h)   ; prírastok fázy = frekvencia
ADD HL,BC       ; fázový akumulátor (B09Bh)
LD L,H / LD H,0 ; horný bajt fázy = index 0..255
ADD HL,DE       ; + základ tabuľky priebehu
ADD A,(HL)      ; sčítanie hlasov
OUT (88h),A     ; vzorka na DAC
```

- RLDR0 = 1Ah (26) je len východisková hodnota, PRT delí 20 → φ / 540.
  Firmvér RLDR0 prepisuje **za behu** (13041-krát počas jedného prechodu
  aplikáciami, hodnoty 15h aj 28h), čím mení rýchlosť a výšku reči.
  To je tá softvérová páka vedľa potenciometra; vzorkovacia frekvencia
  teda nie je konštantná.
- Priebehy: 10 tabuliek po 64 bajtoch od 00677h, hodnota = bajt − 20h, rozsah −32..+31.
  0 = píla, 1 = sínus, 8 = obdĺžnik, 9 = trojuholník, 2/4/5/6 = farby nástrojov,
  3/7 = úzke impulzy.
- Klávesnica sa skenuje vnútri tejto istej obsluhy (čítanie 89h/8Ah/8Ch hneď po OUT 88h).

### Príkazový jazyk zvuku

`<frekv>[:<frekv>…]/…[*<priebeh>]`, parser na fyz. 01636–016B7.

- `:` oddeľuje súčasné hlasy, maximum 4 (`LD B,04h`)
- prázdne pole = hlas mlčí
- `/` trvanie, `*` číslo priebehu 0–9 (`LD HL,0677h`, posun n×64)
- frekvencia aj trvanie sa násobia ôsmimi
- `/` a `*` sa dedia medzi príkazmi, hlasy sa pred každým príkazom nulujú
- CR (0Dh) oddeľuje príkazy vnútri reťazca, prázdny reťazec ukončuje melódiu
- `&` sa rozvinie na `!%T`, `$` na `!%T77:78` (rutiny 0D59F a 0D58F)
- `!%P` = softvérové nastavenie výšky reči, `!%E` = ďalší parameter

## Bity riadiacich latchov

### Port B0h (tieň C438h) — 53 odkazov

| bit | význam | istota |
|---|---|---|
| 7 | RTS / riadenie toku pre ASCI1; nastavuje sa po `BIT 0,C` (len kanál 1) na 1860D a 1E0B3, nuluje sa po `IN0 A,(RDR1)` na 183BA | vysoká |
| 6 | disk; nastavuje sa spolu s bitom 1 pri roztočení (`OR 42h`) | stredná |
| 5 | povolenie sériového/tlačového bloku (`OR 20h` pri otvorení výstupu) | stredná |
| 4 | prepínač zvukovej cesty; mení sa až po vyprázdnení výstupného buffera | stredná |
| 3 | riadiaca linka periférie na CSI/O; pulzuje sa pred `OUT0 (CNTR)` | vysoká |
| 2 | druhá riadiaca linka tej istej periférie; na 1889F sa preklápa 18-krát | vysoká |
| 1 | výber jednotky/strany diskety; nuluje sa pred štartom motora, nastavuje po ňom | vysoká |
| 0 | hustota / dátová rýchlosť diskety; berie sa z (IY+2Fh) štruktúry jednotky | vysoká |

### Port 80h (tieň C439h) — 14 odkazov

| bit | význam | istota |
|---|---|---|
| 7 | povolenie zvuku; nulované pri výkyve DAC na 40h, obnovené pri návrate na 80h | vysoká |
| 6 | nastavovaný pri každom zápise dát; späť sa doň skladá stav z A8h bit 5 | stredná |
| 5–1 | dátové bity pre latch sériových parametrov (tabuľka na 18F5C, 4 B na položku) | stredná |
| 0 | strobe; rutina 18FDE zapíše hodnotu s bitom 0 nastaveným, počká, potom bez neho | vysoká |

Pokojový stav je C0h.

### Port A0h (tieň C43Ah) — 28 odkazov

| bit | význam | istota |
|---|---|---|
| 7–4 | nikde sa nenastavujú, nepoužité | vysoká |
| 3 | napájanie rečového obvodu; na 004D9 `TST 08h`, potom `OR 08h`, a ak bol 0, inicializácia | stredná |
| 2 | výber výstupného zariadenia; na 19047 podľa toho, či (C43Dh)==50h | stredná |
| 1 | povolenie výstupu; nuluje sa spolu s bitom 2 (`AND F9h`) pri zatvorení | stredná |
| 0 | motor diskety; 19A84 roztočí a nastaví, 1D0D4 po timeoute skontroluje stav radiča a zhasne | istá |

### Port A8h (čítanie)

| bit | význam | istota |
|---|---|---|
| 5 | pripravenosť vysielača/tlačiarne; pollované s počítadlom pokusov na 18F99, 18FB1, 19097 | vysoká |
| 1 | INTRQ disketovej radiča; na 19833 sa pollne a potom sa číta stav 98h | vysoká |
| ostatné | nikde sa netestujú | — |

## Hodiny reálneho času

Sedem registrov na portoch 90h–96h, riadiaci register na porte 0290h/0291h
(hodnoty 04h pred zápisom, 0Ch po ňom). Horný bajt adresy sa dekóduje.

Čítanie: slučka na 0DFA4 načíta 90h..96h zostupne do CB8B..CB85.
Význam potvrdený konštantami pri prenose v rutine rozdielu časov na 0DC92:

| port | význam | dôkaz |
|---|---|---|
| 90h | stotiny sekundy | `ADD A,64h` (100) na 0DC9C |
| 91h | sekundy | `ADD A,3Ch` (60) na 0DCB1 |
| 92h | minúty | `ADD A,3Ch` (60) na 0DCBD |
| 93h | hodiny | `ADD A,18h` (24) na 0DCC9 |
| 94h | deň | poradie trojice CB85..CB87 pri viacbajtovom porovnaní (0DA00) |
| 95h | mesiac | to isté |
| 96h | rok mod 100 | `CP 64h / SUB 64h` na 0DF8A |

Hodnoty sú **binárne, nie BCD** — dôkaz je redukcia roku modulo sto
binárnym `CP 64h / SUB 64h` na 0DF8A a rovnaký vzor na 0DA5B.

Pozor: zapisovacia rutina (0DF5A, 0DF7C) používa oproti čítacej
zrkadlené poradie bufferu. Mapovanie portov treba brať z čítacej cesty.

Port 97h táto ROM nikdy nečíta.

## Neidentifikovaná periféria na CSI/O

Kód na 18801–188B0 a 1E001 používa Z180 CSI/O (CNTR 0Ah, TRDR 0Bh)
spolu s bitmi 2 a 3 portu B0h ako ručne kývanými riadiacimi linkami.
Postupnosť: vyšli FFh, prepni linky, prijmi, hľadaj odpoveď **AAh**
s timeoutom 6000h. Zariadenie sa nepodarilo pomenovať.
Kandidáti: voltmeter/teplomer (BASIC má `DVM`, `TIC`, `TIF`, `TEC`, `TEF`).

## Poznámky pre emulátor

- Všetky tri latche sú v hardvéri len na zápis, ale firmvér ich zrkadlí
  v RAM na C438h/C439h/C43Ah. Emulátor sa proti tomu zrkadlu dá overiť.
- Port 88h treba brať ako DAC pri každom zápise; jediný nevysvetlený
  zápis je hodnota ADh na 19A01 v diskovej ceste.
- Prvý míľnik pri rozbiehaní: ROM má zabudovaný autotest, ktorý zráta
  všetkých päť modulov a povie „všechny ROMy oukej".
