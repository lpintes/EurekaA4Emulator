# Eureka A4 — mapa hardvéru

Zdroj: `a4rom.dmp`, 262144 B, MD5 9aa101ab69fc367e114e1a84b08feea1.

Pôvodne bolo všetko nižšie odvodené výhradne z kódu v tejto ROM. Od
24. 8. 2026 je k dispozícii aj **oficiálny Eureka A4 Technical Manual**
v `eurekatech/` (príloha H `IOPORT.H` a súbor `IOPORT.LIB` sú úplnou
mapou externých portov). Tam, kde sa odvodené a doložené líšilo, je
nižšie uvedené oboje aj s vysvetlením, prečo odvodenie zlyhalo — inak by
sa tá istá chyba dala urobiť znovu.

Oficiálne názvy signálov sú prevzaté z `IOPORT.LIB`.

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
| 80h | W | `modem_latch` — riadenie modemu AM7910, tieň C439h | doložené |
| 88h | W | `dac_port` — 8-bit DAC (reč, zvuk, DTMF, referencia komparátorov) | doložené |
| 89h, 8Ah, 8Ch | R | `bkb_row0`, `bkb_row1`, `bkb_row2` — braillova klávesnica | doložené |
| 90h–97h | R/W | hodiny reálneho času, sedem registrov + deň v týždni | doložené |
| 190h–197h | R/W | `rtc_ram_*` — čas a dátum najbližšieho budíka | doložené |
| 290h | R/W | `rtc_status` (čítanie) / `rtc_mask` (zápis) | doložené |
| 291h | W | `rtc_command` — kryštál, 12/24 h, štart, povolenie prerušenia | doložené |
| 98h | R/W | `fdc_status` / `fdc_command` — WD177x | doložené |
| 99h | R/W | `fdc_track` | doložené |
| 9Ah | R/W | `fdc_sector` | doložené |
| 9Bh | R/W | `fdc_data` | doložené |
| A0h | W | `power_latch` — napájanie podsystémov, tieň C43Ah | doložené |
| A8h | R | `input_buffer` — komparátory a stavové linky | doložené |
| B0h | W | `output_latch` — všeobecný výstupný latch, tieň C438h | doložené |
| B8h | R/W | `pwr_stb` — **akýkoľvek prístup vypne stroj** | doložené |

`B8h` nie je „čítaný po vybití batérie", ako sa pôvodne zdalo. Je to
vypínač: takto sa Eureka vypína štyrmi kurzorovými klávesmi z hlavného
menu aj po nečinnosti.

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

### Port B0h `output_latch` (tieň C438h) — 53 odkazov

| bit | názov | význam |
|---|---|---|
| 7 | `rts1_mask` | RTS na RS-232; nastavuje sa po `BIT 0,C` na 1860D a 1E0B3, nuluje po `IN0 A,(RDR1)` na 183BA |
| 6 | `vmsel_mask` | výber dvojice komparátorov: 0 = vnútorný teplomer + externý voltmeter, 1 = potenciometer rýchlosti reči + **batéria** |
| 5 | `dtmf_mask` | 0 pripojí DAC na telefónnu linku (tónová voľba), 1 pripojí modem |
| 4 | `filtersel_mask` | medzná frekvencia filtra zvukového obvodu; 1 = normálna, 0 = vysoká. Ovláda ho rečový engine |
| 3 | `kb_prime_mask` | aktívne v nule; zaistí, že sa ignoruje prvý hodinový bit bajtu z QWERTY klávesnice — požiadavka 64180 |
| 2 | `kb_rst_mask` | aktívne v nule; reset QWERTY klávesnice, drží sa asi 200 ms |
| 1 | `fdc_drv0` | aktívne v nule; výber disketovej jednotky. Kým nie je vybraná, radič je držaný v resete |
| 0 | `fdc_side` | výber strany diskety, 0 = strana 0, 1 = strana 1 |

Pôvodné odvodenie z ROM sa tu mýlilo v troch bitoch. Bit 0 nie je
hustota, ale strana. Bit 6 nie je „disk"; `OR 42h` pri roztočení
jednotky nastavuje bity 1 a 6 naraz preto, že s vybraným diskom sa
zároveň prepnú komparátory na **stráženie batérie** počas diskovej
operácie. A bity 2 a 3 nepatria neznámej periférii, ale klávesnici
IBM PC.

### Port 80h `modem_latch` (tieň C439h) — 14 odkazov

Celý tento latch ovláda modemový čip AM7910. So zvukom **nemá nič
spoločné**.

| bit | názov | význam |
|---|---|---|
| 7 | `loop_mask` | aktívne vo vysokej; obsadenie telefónnej linky. Pulzná voľba sa robí kývaním tohto bitu |
| 6 | `mantx_mask` | ručné vysielanie; linka sa logicky násobí s vysielacím výstupom UART (ten je vo vysokej, keď sa UART nepoužíva). Takto Eureka vysiela 75 baudov, čo UART nevie nastaviť. Skladá sa na 18F1F a 1D081 |
| 5–1 | `mdm_mode_mask` | riadiace bity režimu AM7910; tabuľka na 18F5C, 4 B na položku |
| 0 | `dtr_mask` | DTR, aktívne vo vysokej. Rutina 18FDE zapíše hodnotu s ním, počká, potom bez neho |

### Prečo sa bit 7 javil ako „povolenie zvuku"

Na 190D9 je vytáčacia slučka:

```
190CD  CP 3Ah / SUB 30h       ; ASCII číslica na počet impulzov
190D9  LD A,(C439h) / AND 7Fh / OUT (80h),A    ; rozpoj slučku
190E3  LD A,40h / OUT (88h),A
190E7  LD B,3Ch / CALL E2ECh                    ; pauza 60 (break)
190EC  LD A,(C439h) / OR 80h  / OUT (80h),A    ; spoj slučku
190F6  LD A,80h / OUT (88h),A
190FA  LD B,28h / CALL E2ECh                    ; pauza 40 (make)
```

Je to **pulzná voľba** v pomere 60/40 ms. Zápisy `40h` a `80h` do DAC
sú vedľajšie — parkujú prevodník, aby impulzy neklikali do reproduktora.
Kto si ich spojí s tým bitom, dostane „povolenie zvuku". Nie je.

Pokojový stav C0h (bity 7 a 6) je stav **počas hovoru**: slučka spojená,
ručné vysielanie vo vysokej. Nie je to stav zavesenej linky.

**Zvuk sa nepovoľuje nikde na porte 80h.** Napájanie zvukového obvodu je
`pol_voice`, teda `A0h` bit 3, a manuál k nemu píše, že býva trvalo
zapnutý, lebo po zapnutí treba čakať asi pol sekundy, než je reč možná.

### Port A0h `power_latch` (tieň C43Ah) — 28 odkazov

| bit | názov | význam |
|---|---|---|
| 7–4 | — | nepoužité |
| 3 | `pol_voice` | napájanie zvukového obvodu. Po zapnutí treba čakať asi pol sekundy, než je reč možná — preto býva trvalo zapnutý. Na 004D9 `TST 08h`, potom `OR 08h`, a ak bol 0, inicializácia |
| 2 | `pol_relay` | zopne relé spájajúce oddeľovací transformátor modemu s telefónnou linkou. Smie byť zopnuté len pri detekcii oznamovacieho tónu, tónovej voľbe a komunikácii — počas pulznej voľby musí byť rozopnuté |
| 1 | `pol_modem` | napájanie obvodov modemu |
| 0 | `pol_disk` | napájanie radiča a disketovej jednotky; 19A84 roztočí, 1D0D4 po timeoute zhasne |

Bit 2 teda nie je „výber výstupného zariadenia". Podmienka na 19047,
ktorá k tomu odvodeniu viedla, rozlišuje výstup na modem od ostatných
ciest — čo relé skutočne ovláda, ale význam je telefónny, nie
konzolový.

### Port A8h `input_buffer` (čítanie)

| bit | názov | význam |
|---|---|---|
| 7, 6 | — | nepoužité |
| 5 | `dcd0_mask` | aktívne v nule; detekcia nosnej z modemu AM7910. Pollované s počítadlom pokusov na 18F99, 18FB1, 19097 |
| 3 | `ring_mask` | aktívne v nule; prítomnosť vyzváňacieho napätia (automatické zdvihnutie) |
| 2 | `cts1_mask` | aktívne v nule; CTS na RS-232 |
| 1 | `vm2_mask` | druhý komparátor: podľa `vmsel` externý voltmeter alebo **napätie batérie** |
| 0 | `vm1_mask` | prvý komparátor: podľa `vmsel` vnútorný teplomer alebo potenciometer rýchlosti reči |

Bity 0 a 1 sú výsledky porovnania meraného napätia s napätím z DAC
(0 až 1,31 V). Firmvér hodnotu získava binárnym vyhľadávaním — príloha
kapitoly 10 pri `dev_battery` píše doslova „Input: DAC value in A after
binary search".

**Na bite 1 nie je INTRQ.** To odvodenie bolo chybné a v emulátore
spôsobilo, že každý diskový príkaz skončil hláškou „slabá baterie":
nastavený bit 1 znamená *vybitú batériu*, nie pripravenosť radiča. Na
19837 sa `IN A,(A8h) / AND 02h / RET NZ` vracia s chybovým bajtom `02h`,
čo je práve bit „slabá baterie". Slučka, ktorá naozaj čaká na disk,
číta hneď za tým stav radiča na porte 98h a testuje jeho bit 1, teda
**INDEX**.

Poznámka: bit 5 je podľa manuálu DCD z modemu, ale táto ROM ho pollne
aj v ceste tlačového výstupu. Buď je to v českej verzii inak zapojené,
alebo je odvodenie „pripravenosť tlačiarne" nepresné. Otvorené.

## Hodiny reálneho času

Devätnásť registrov: sedem pre čas a dátum, sedem pre budík, tri riadiace.
Horný bajt adresy sa dekóduje, takže 0290h/0291h sú iné porty než 90h/91h
(riadiace hodnoty 04h pred zápisom, 0Ch po ňom — `rtc_default` je 0Ch:
normálny režim, prerušenie zakázané, beh, 24-hodinový formát, 32 kHz).

Poradie registrov na porte:

| port | význam |
|---|---|
| 90h | stotiny sekundy, 0–99 |
| 91h | hodiny, 0–23 |
| 92h | minúty, 0–59 |
| 93h | sekundy, 0–59 |
| 94h | mesiac, 1–12 |
| 95h | deň v mesiaci, 1–31 |
| 96h | rok, 0–99 (1985 až 2084) |
| 97h | deň v týždni, 0–6 — táto ROM ho nikdy nečíta |

**Čítanie musí začať registrom 90h.** Ten okamih zapíše (zapuzdrí)
všetky ostatné registre, takže dávkové čítanie nemôže preskočiť tik.
Emulátor to modeluje v `SampleRtc()`.

Hodnoty sú **binárne, nie BCD** — dôkaz je redukcia roku modulo sto
binárnym `CP 64h / SUB 64h` na 0DF8A a rovnaký vzor na 0DA5B, plus
`CP 55h` (85) na 0DFD6, ktoré rozlišuje roky 1985–1999 od 2000+.

### Prečo pôvodné odvodenie vyšlo prehodené

Čítacia slučka na 0DFA4 uloží porty 90h..96h **zostupne** do CB8B..CB85
a hneď za ňou, na 0DFBB, firmvér **sám prehodí dve dvojice**:
`CB86` s `CB87` (porty 95h a 94h) a `CB8A` s `CB88` (porty 91h a 93h).

```
0DFBB  LD B,(IX+1) / LD A,(IX+2) / LD (IX+1),A / LD (IX+2),B
0DFC7  LD B,(IX+5) / LD A,(IX+3) / LD (IX+5),A / LD (IX+3),B
```

Vnútorný buffer CB85..CB8B teda ide vzostupne rok, mesiac, deň, hodina,
minúta, sekunda, stotiny — v tomto poradí funguje viacbajtové porovnanie
dátumov (0DA00) priamo. Rutina rozdielu časov na 0DC92 chodí po bufferi
od CB8B nadol, takže `ADD A,64h`, `3Ch`, `3Ch`, `18h` sedí na stotiny,
sekundy, minúty, hodiny — ale **v bufferi, nie na portoch**. Kto ten
swap prehliadne, dostane hodiny prehodené so sekundami a mesiac s dňom.

Zapisovacia rutina (0DF5A, 0DF7C) používa oproti čítacej zrkadlené
poradie bufferu, čo tú istú pascu kladie druhýkrát.

## Braillova klávesnica

Dvadsať klávesov v troch riadkoch, každý riadok jeden port len na
čítanie, pravdivá logika (nastavený bit = stlačený kláves).

| port | riadok | obsah |
|---|---|---|
| 89h | `bkb_row0` | bity 0–5 šesť bodových klávesov, bit 7 **medzerník** |
| 8Ah | `bkb_row1` | bity 0–7 funkčné klávesy F1–F8 |
| 8Ch | `bkb_row2` | bity 0–3 kurzorové klávesy hore/dolu/vľavo/vpravo, bit 6 **shift** |

**Pozor: text manuálu si riadky 0 a 2 prehodil.** Príloha H tvrdí, že
braillove body sú na `8Ch` a kurzory na `89h`; ekvivalenty v
`IOPORT.LIB` (`bkb_row0 equ keyboard or 001b`) hovoria opak a rozhoduje
ROM: na 1D41F maskuje `89h` hodnotou `3Fh`, aby dostala šesť bodov, a
na 1D206 maskuje `8Ch` hodnotou `0Fh`, aby *ignorovala shift* — presne
to, čo o riadku 2 píše `KEYSCAN.MAC`.

Kód klávesu skladá dekodér na 1D4F3 a ukladá ho na C638h:

- bity 7–6: `10` kurzorová plocha, `11` funkčný kláves
- bit 5 (`k_alt`): medzerník, teda `89h` bit 7
- bit 4 (`k_shift`): `8Ch` bit 6
- dolné štyri bity: číslo funkčného klávesu, alebo **maska naraz
  stlačených kurzorových klávesov** — preto je Home hore+vľavo (`85h`)
  a Delete vľavo+vpravo (`8Ch`)

Funkčné klávesy sú len štyri páry, teda osem. **F9 a F10 vlastný kláves
nemajú**: sú to akordy medzerníka a braillových bodov, ktoré rozhoduje
tabuľka na 1D541.

| akord | `89h` | kód | význam |
|---|---|---|---|
| medzerník + bod 1 | `84h` | `C8h` | F9 = MODE |
| medzerník + bod 4 | `88h` | `C9h` | F10 = WHERE |
| medzerník + body 1,2 | `86h` | `D8h` | Shift+F9 = stav batérie |
| medzerník + body 4,5 | `98h` | `D9h` | Shift+F10 = sebekontrola |
| medzerník + body 1,4,5 | `9Ch` | `CAh` | (nepoužité) |

### Poradie bitov v braillovom riadku

Bity idú v poradí klávesov zľava doprava, nie podľa čísel bodov:
**bit 0 = bod 3, bit 1 = bod 2, bit 2 = bod 1, bit 3 = bod 4,
bit 4 = bod 5, bit 5 = bod 6.** Preto je `04h` bod 1, nie bod 3.

Doložené tým, že ROM tým bajtom **priamo indexuje** prekladovú tabuľku
(`1D79C`: `ADD HL,BC` s `B=0`). Na indexe `04h` je `a`, na `06h` `b`,
na `0Ch` `c`, na `1Ch` `d` — učebnicový braill.

Tabuľky sú tri, nie dve, a vyberá medzi nimi rutina na 1D784 podľa
`C835h` a `C668h`:

| adresa | obsah |
|---|---|
| `1D7A0` | počítačový braill, plná ASCII sada |
| `1D7E0` | literárna sada s diakritikou |
| `1D820` | číselný režim (`a`–`j` sú `1`–`0`) |

Cesta od stlačenia ku kódu má tri kroky a každý má vlastné časovanie:

1. **Sken.** Obsluha heartbeatu na 1D1AE porovná tri riadky s tieňmi
   C62E–C630. Klávesnicu skenuje aj generátor tónov (00642) — ten beží
   rýchlosťou DAC, teda tisíckrát za sekundu.
2. **Záchyt.** Pri zmene sa na 1D1FA uloží nový stav do C631–C633
   a `C61E` sa nastaví na 3.
3. **Dekódovanie.** `C61E` sa každý tik znižuje a pri nule sa naplánuje
   dekodér (1D1D5 → D41C → D4B0). Každá ďalšia zmena riadka počítadlo
   znovu naplní, takže **všetko, čo sa stlačí do troch tikov (asi
   40 ms), je jeden akord**.

Z bodu 3 plynie nepríjemnosť pre modifikátor ALT: medzerník stlačený
súčasne s kurzorom sa dekóduje ako braillov znak a kurzor sa zahodí
(D4B0 sa pozerá najprv na braillov riadok). Aby vznikol `A1h` a spol.,
musí medzerník ísť dole **skôr než o tri tiky**. Shift takú podmienku
nemá — zmeny `8Ch` nad bitom 3 sa do porovnania vôbec nepočítajú.

## Klávesnica IBM PC na CSI/O

Kód na 18801–188B0 a 1E001 používa Z180 CSI/O (CNTR 0Ah, TRDR 0Bh)
spolu s bitmi 2 a 3 portu B0h. Tie bity sú `kb_rst_mask`
(reset klávesnice, aktívny v nule, drží sa asi 200 ms) a
`kb_prime_mask`, ktorý nastaví latch tak, aby sa **ignoroval prvý
hodinový bit** prichádzajúceho bajtu — manuál to označuje priamo za
požiadavku 64180.

Postupnosť „vyšli FFh, prepni linky, prijmi, čakaj na **AAh**
s timeoutom 6000h" je teda štandardné privítanie klávesnice: `FFh` je
príkaz Reset a `AAh` je odpoveď *Basic Assurance Test passed*.

Meracia periféria to nie je. Voltmeter a teplomer (`DVM`, `TIC`, `TIF`)
sa čítajú komparátormi na porte A8h proti DAC, nie cez CSI/O.

## Poznámky pre emulátor

- Všetky tri latche sú v hardvéri len na zápis, ale firmvér ich zrkadlí
  v RAM na C438h/C439h/C43Ah. Emulátor sa proti tomu zrkadlu dá overiť.
- Port 88h treba brať ako DAC pri každom zápise. Zápis `ADh` na 19A01
  pred každým príkazom radiča **nie je nevysvetlený**: je to nastavenie
  prahu komparátora pre stráženie batérie počas diskovej operácie
  (`ADh` z rozsahu 1,31 V je asi 0,89 V). Servisná kapitola manuálu to
  potvrdzuje aj z druhej strany — poškodená batéria „sa javí ako funkčná,
  kým sa nesiahne na disk, a vtedy sa ohlási Low battery".
- DAC je zdieľaný: reč, hudba, tónová voľba aj referencia komparátorov.
  Meranie má zmysel len vtedy, keď cezeň práve nehrá reč.
- Prvý míľnik pri rozbiehaní: ROM má zabudovaný autotest, ktorý zráta
  všetkých päť modulov a povie „všechny ROMy oukej".
