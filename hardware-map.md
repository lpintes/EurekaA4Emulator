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

### Rozloženie RAM a buffery aplikácií

RAM je 64 KB na fyzických 70000h–7FFFFh. Logické rozloženie je to, ktoré
manuál (`MEMORY.5`) volá **Standard Eureka A4** — len posunuté o 20000h,
lebo anglické stroje majú RAM na 50000h. Doložené tromi nezávislými
vecami: tiene latchov na C438h–C43Ah (manuál ich má presne tam),
`sysram_addr equ 0c000h` v `SYSEQU.LIB`, a generátor tónov bežiaci v RAM
na logickej B000h = fyzickej 7B000h (HANDOFF 6.10).

Logické adresy pri bežiacej vstavanej aplikácii:

- 0000h–00FFh — page 0 EurekaDOSu
- 0100h–BFFFh — pracovná plocha aplikácie (pri programe z diskety TPA)
- B000h–BFFFh — `application_scratch`, „from here to sysram can be used
  by anything" (`SYSEQU.LIB`)
- C000h–CFFFh — SYSRAM, **vždy viditeľná**; v nej CC00h–CF9Fh RAMCODE
  a CFA0h–CFFFh SYSJUMPS
- D000h–DDFFh telefónny zoznam, DE00h–E5FFh diár, E600h–EFFFh záznamník,
  F000h–F7FFh virtuálna obrazovka, F800h–FDFFh scratch reči,
  FE00h–FFFFh sektorový buffer diskety

Horných 12 KB od D000h je **väčšinu času prekrytých ROM operačného
systému** (reset: CBR=0Bh, teda D000h = 18000h). Buffery aplikácií tam
teda fyzicky sú, ale logicky ich nevidno — `PEEK` na E600h vráti
operačný systém, nie poznámky. To je aj dôvod, prečo `POKE` z BASICu
zväčša nezaberie: pod logickou adresou je ROM a zápis sa **ticho**
stratí. Model to robí rovnako (`EurekaMachine::WritePhysical`).

### Prenosy bufferov cez DMA

Preto sa buffery pred prácou kopírujú dole. Slúži na to `.dma0_move`
zo SYSJUMPS: argumenty sú vložené priamo za `CALL` v poradí `dw` zdroj,
`db` banka, `dw` cieľ, `db` banka, `dw` dĺžka (`SYSJUMPS.11`). V celom
dumpe je tento vzor presne šesťkrát, plus raz pri štarte:

| fyz. adresa | volanie | prenos | dĺžka | čo to je |
|---|---|---|---|---|
| 0EECB | CALL EEEEh | DE00h/07 → B000h/07 | 0800h | diár dole |
| 0EED7 | CALL EEEEh | B000h/07 → DE00h/07 | 0800h | diár späť |
| 139B9 | CALL F9FBh | E600h/07 → B600h/07 | 0A00h | záznamník dole |
| 139C5 | CALL F9FBh | B600h/07 → E600h/07 | 0A00h | záznamník späť |
| 19CF9 | CALL E2F5h | E600h/07 → B600h/07 | 0A00h | záznamník dole (z OS) |
| 19D13 | CALL E2F5h | B600h/07 → E600h/07 | 0A00h | záznamník späť (z OS) |
| 18025 | CALL E2F5h | D000h/01 → CC00h/07 | 0400h | RAMCODE pri štarte |

Banka 07h znamená bity 16–19 fyzickej adresy, teda 7E600h a 7B600h.
Posledný riadok je bootovacia inicializácia: kód obsluhy prerušení sa
z ROM (1D000h) kopíruje do SYSRAM, aby ho nikdy nemohlo vybankovať —
robí sa hneď po nastavení tabuľky vektorov na C180h.

**Telefónny zoznam sa takto neprenáša.** Pristupuje sa k nemu na mieste
cez ukazovateľ (0F2F0: `LD HL,D000h` / `ADD HL,BC` / `LD (C46Eh),HL`)
a horných 12 KB sa preň mapuje prestavením MMU. Vzor toho prestavenia je
v ROM častý — napr. 19332 si `IN0` odloží CBR aj CBAR, prepne ich a skočí
(`.go_overlay`).

### Textový procesor a záznamník sú jeden program

Na 133E6 a 133FB sú **dva vstupy do tej istej rutiny**; obe vetvy
pokračujú na 1340A. Líšia sa len bufferom:

- 133E6 — `IY=0100h`, `DE=B000h`, `A=01h` → **textový procesor**
  (Shift+F1), plocha 0100h–AFFFh
- 133FB — `IY=B600h`, `DE=C000h`, `A=00h` → **záznamník** (F1),
  plocha B600h–BFFFh, teda 0A00h bajtov končiacich tesne pod SYSRAM

`IY` je začiatok bufferu, `DE` jeho koniec. Rozdiel medzi režimami drží
jediný bajt na B02Fh (nastavuje ho 13428). Rutina na 139AF z neho robí
bitovú masku: vráti 01h alebo 20h, čo sú `wp_mask` (bit 0) a `nt_mask`
(bit 5) zo `SYSEQU.LIB`. Zapisujú sa do `activity` (C473h) a `altered`
(C474h) — masiek, ktoré podľa `SYSRAM.A` hovoria, ktoré aplikácie majú
dáta v RAM a ktoré majú neuložené zmeny; číta ich 139A2 a 139A7.

Vnútro bufferu záznamníka doložené nie je. Vieme len, že prvý bajt textu
je na B601h = desiatkovo 46593 (pozorované majiteľom stroja cez `PEEK`
z BASICu, sedí s B600h ako začiatkom bloku); čo je na B600h samotnom,
overené nemáme.

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

V celej ROM je naň **jediný prístup**, `IN A,(B8h)` na 1D144, a hneď za
ním `JR $-2` — firmvér čaká, kým napájanie naozaj zhasne. Cesta k nemu:

- Kód klávesu je **8Fh**, v `KB.LIB` `k_udlr` (alias `k_spare3`), teda
  `k_keypad` plus všetky štyri bity kurzorov. Testuje sa naň jediné
  miesto v ROM, `CP 8Fh` na 18154, a to je slučka **hlavného menu** —
  inde chord nič nerobí.
- Vetva na 18178 povie „konec", nastaví `C66Ch` na 1 a zatočí sa
  navždy. `C66Ch` je odpočítavadlo nečinnosti; dekrementuje ho heartbeat
  na 1D0B0 a pri nule skočí na rutinu vypnutia 1D132. Nekonečná slučka
  nevadí, lebo obsluha prerušenia sa už nevráti.
- Reload je `57E4h` = 22500 tikov, teda pri 75 Hz **presne 5 minút**
  (0AA19, 0D477, 10F0D, 19395, 19C64). Pri `08CAh` (2250 tikov = 30 s
  pred koncom) sa volá `CCFEh` — výstražná znelka.
- Rutina 1D132 pred strobom uloží do `C45Ah` hodnotu `FFh`. To je značka
  „som vypnutý" a číta ju boot na 180CB — ale **len keď ho zobudil
  budík**, a vtedy `JP NZ,CFD9h` vedie cez SYSJUMPS (`1D3D9`) na `CD32h`
  = fyz. 1D132, teda **na tú istú rutinu vypnutia**. Budík zobudí vypnutý
  stroj, firmvér ho obslúži a stroj zase zaspí.
  **Len ak nikto neodpovie** (zmerané 11. 9. 2026, HANDOFF 6.32): značka
  sa číta až po `CALL CFEBh` na 180C8, a kláves, ktorý budík odklikne, ju
  vynuluje — boot potom ide na 180D2 a stroj zostane v hlavnom menu.
  Rozhoduje o tom bit 0 bajtu, ktorý si boot odloží na 0040h už na 18011
  (`IN A,(C)` s `BC=0290h`, teda `rtc_status`); bit 0 je `kRtcEventAlarm`.
  Pri zapnutí rukou je nula, ide sa na 180D2 (`schedule_alarm`) a značka
  sa na 180D6 zmaže.
  **Neplatí, čo tu stálo do 6. 9. 2026:** že boot podľa značky „pokračuje
  namiesto plnej inicializácie" a že preto sa stroj vracal tam, kde ho
  používateľ nechal. `CFD9h` nie je pokračovanie, je to vypnutie, a boot
  beží v oboch prípadoch celý. Stroj sa vracal preto, že RAM sa nemazala
  (`GLOSSARY.TXT`: napájanie RAM ani hodín sa nikdy neodpájalo) a `magic`
  na `C45Bh` sedel — nezhoda s `55AAh` je to, čo na 18132 povie
  „inicializace eureky" a na 1805E vymaže `C43Ch`–`C508h`. Viď HANDOFF
  6.15, kde je celá cesta rozobratá aj s dôkazom mapovania
  `CC00h` ↔ `1D000h`.

**Z externej klávesnice sa vypnúť nedalo.** Bajt `8Fh` nie je ani
v jednej zo štyroch prekladových tabuliek ROM (1DF05 základná, 1DF5E
shift, 1DF98 pravý Alt, 1DFD6 rozšírená pre `E0h`) a scancody sa
prekladajú po jednom, nikdy sa nezlučujú. Chordovať sa dá len na
membráne, kde ROM číta celý riadok `8Ch` naraz. Jediný výskyt bajtu
`8Fh` v pásme obsluhy klávesnice (1DE9A) patrí tabuľke skladania
diakritiky: `A` + čiarka = `Á`, lebo `8Fh` je v Kamenických práve `Á`.
To je aj dôvod pre `BIT 0,H` pred `CP 8Fh` — odlišuje kód klávesu od
znaku.

Disketová radič potvrdený protokolom seek na 19806h:
`IN A,(99h)` (aktuálna stopa) → `OUT (9Bh),A` (cieľová) → príkaz 14h.
Formátovanie: DMA kanál 1, 7000 bajtov (jedna DD stopa) do 9Bh, príkaz F0h/F2h.

Ten príkaz `14h` je Seek s **verify** (`fdc_verify` = `100b`, SYSEQU.LIB)
a je to **jediný verify v celom firmvéri** — je to `fdc_ctl_disk_test`
(197FB) a stojí na ňom celý rozdiel medzi „disk není naformátován“,
„disk není založen“ a „vadný disk“ (HANDOFF 6.30). Číta sa troma
spôsobmi: bity 3 a 4 (`AND 18h` na 19821) → nečitateľná stopa, bit 7 →
radič príkaz nedokončil, teda prázdna mechanika, bit 6 → ochrana proti
zápisu. Bity 1 a 7 **nie sú z radiča** — `EA00` (19A00) stav maskuje
`5Dh` a tie dva si vyhradzuje pre slabú batériu (port `A8h` bit 1)
a pre svoj vlastný časový limit 2000 pokusov (vráti `80h` na 19A52).

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
- `!%P` = softvérové nastavenie výšky reči
- `!%E<n>` = jeden z očíslovaných hotových zvukov rečového obvodu, nie tón

### Kliky (`!%E` a klikové kódy `SPCHAR`)

Kapitola `SPEECH.14` manuálu to pomenúva zo strany volania: `SPCHAR`
berie okrem kódu znaku aj **klikové kódy 10–19**, desať zvukov, ktoré
stroj používa na ozvučenie klávesnice. Sedem z nich sú obyčajné tóny
v jazyku vyššie, tri sú `!%E`:

| kód | rozvinie sa na | čo to je |
|---|---|---|
| 10 | `!%T750/10*1` | malé písmeno |
| 11 | `!%T1000/10*1` | veľké písmeno |
| 12 | `!%T600/10*1` | číslica |
| 13 | `!%T600/10*2` | interpunkcia |
| 14 | `!%T750/20*0` | skratka |
| 15 | `!%E0` | prefix |
| 16 | `!%T300/40*5` | zlá skratka |
| 17 | `!%E2` | funkčné klávesy |
| 18 | `!%E2` | kurzorové klávesy |
| 19 | `!%T150/120*5` | chyba |

`!%E` je teda **o vrstvu nižšie než klik**: klik je kód pre `SPCHAR`,
`!%E<n>` je hotový zvuk, z ktorého sú tri z tých desiatich poskladané.
Vlastný číselník `!%E` je širší než tie dve čísla — zmerané v textovom
procesore vydá `E1` šípka hore aj dole na okraji textu a `E7` sa pridá
vždy, keď sa kurzor nemá kam pohnúť (`!%e2 !%e7 !%e2` pri šípke vpravo na
konci riadka). `E2` sedí na manuál presne: je to klik kurzorových
klávesov. Čo presne je `E1` a `E7`, rozhodne až obsluha návestia v ROM.

**V ROM sú tie kódy o desať nižšie a je ich deväť** (odčítané 10. 9. 2026,
bead ea4-l16). `.spchar` (`0106h` → `00304`) použije každý bajt pod `09h`
(`CP 09h` na `00307`) a aj medzeru ako index do tabuľky deviatich
ukazovateľov na `00369`; reťazce sú hneď za ňou a posledný znak každého má
bit 7. Na `C7E2h` z neho zloží `!%` a príkaz a pošle ho rečovému stroju.
Bajt `00h` je teda kód 10 z tabuľky vyššie, `08h` kód 18, medzera kód 13
a **kód 19 (chyba) v tabuľke nie je**. S meraním to sedí: pri každom otvorení
aplikácie príde `07h`, teda kód 17 pre funkčné klávesy. Manuál ešte píše, že
`0`–`3` nastavujú režim interpunkcie; v tejto ROM idú do tej istej vetvy ako
kliky, takže ako sa tu interpunkcia pre `SPCHAR` nastavuje, overené nie je.

Do prepisu reči sa `.spchar` nedáva; prečo, je v HANDOFF v sekcii 3.

BASIC má aj kľúčové slovo `CLICK` (tabuľka kľúčových slov od `0C134`,
`CLICK` na `0C1EF` v tvare `CLIC`+`CBh`, token `ADh`; vedľa neho `SOUND`
`ABh`, `OFF` `ACh`, `PITCH` `AEh`). Či berie tie isté kódy 10–19, nie je
overené — obsluha príkazu zatiaľ nie je nájdená.

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

### Budík a prerušovacia linka

`rtc_mask` (zápis na 290h) vyberá, ktoré udalosti sa hlásia: bit 0 je
zhoda času s registrami `rtc_ram_*`, bity 1 až 6 periodicky stotina,
desatina, sekunda, minúta, hodina, deň. `rtc_status` (čítanie z 290h)
hlási, ktorá nastala, bit 7 je „nastala aspoň jedna", a **čítanie
register nuluje**.

Do alarmových registrov (190h–197h) sa do polí, ktoré sa porovnávať
nemajú, zapisuje **80h**. Robí to `0DA5B` a je to jednoznačné: registre
sú binárne a nikdy nepresiahnu 99, takže bit 7 nemôže byť platná
hodnota. Stotiny a deň v týždni sú ľubovoľné vždy, sekundy podľa
`C43Fh`.

**Prerušovacia linka RTC nejde na procesor.** ROM nikdy nenastaví
`ITE1` ani `ITE2` (jediný zápis do `ITC` na `196BE` zhadzuje len TRAP),
neobsahuje ani jednu inštrukciu `IM`, a vektory `INT1` aj `INT2`
ukazujú na pahýľ `EI; RET` na `CC37`. Linka je zapojená na spínač
napájania: studený štart číta `rtc_status` na `18000`, odloží ho do
`0040h` a na `180C2` z neho vyvolá `.service_alarm1` — budík zapne
vypnutý stroj. Kým stroj beží, budík sa hľadá **pollovaním** v
heartbeate na `PRT1` (`1D0FB` → `.service_alarm`, čítanie na `CF61`).
Emulátor to modeluje v `UpdateRtcEvents()`; podrobnosti v HANDOFF 6.13.

Vypnutý stroj modeluje `WakeOnAlarm()` (HANDOFF 6.32). Z toho plynie aj to,
že `rtc_mask` a `rtc_command` prežijú vypnutie aj zapnutie rovnako ako
alarmové registre — sú v tom istom obvode na tom istom napájaní.

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
| medzerník + bod 1 (`a`) | `84h` | `C8h` | F9 = režim |
| medzerník + bod 4 | `88h` | `C9h` | F10 = kde som |
| medzerník + body 1,2 (`b`) | `86h` | `D8h` | Shift+F9 = stav batérie |
| medzerník + body 4,5 | `98h` | `D9h` | Shift+F10 = sebekontrola |
| medzerník + body 1,4,5 (`d`) | `9Ch` | `CAh` | **F11** = ROM operačného systému, teda dátumy všetkých modulov |

Pod prstami sú to dva zrkadlové páry, nie štyri vzory: ukazovák ľavej
ruky, ukazovák pravej, a to isté s prostredníkom navyše. Že „b-akord"
je zároveň *baterie*, je zhoda; symetria je to, čo si ruka pamätá.

### Akordy s medzerníkom, ktoré v manuáli nie sú

Zmerané stlačením všetkých 64 akordov po studenom štarte. Okrem
piatich vyššie robia niečo ešte tieto štyri — mlčia, lebo neprepínajú
aplikáciu, ale spôsob písania. V `KB.LIB` ani `KB.H` nie sú.

| akord | `89h` | čo robí | kde |
|---|---|---|---|
| medzerník + `v` (body 1,2,3,6) | `A7h` | **veľké písmená zapnúť** | 1D56B |
| medzerník + `m` (body 1,3,4) | `8Dh` | **veľké písmená vypnúť** | 1D563 |
| medzerník + body 4,5,6 | `B8h` | predpona **riadiaceho znaku** | 1D577 |
| medzerník + `p` (body 1,2,3,4) | `8Fh` | predpona, čaká na `f` | 1D57F |

**Veľké písmená** sú bit 4 na `C61Fh`. Keď je nastavený, preklad ide
vždy cez `D734` (malé na veľké, vrátane diakritiky podľa tabuľky na
`D754`); keď nie je, len pri držanom shifte. České mnemotechniky:
`v` ako *veľké*, `m` ako *malé*. Zmerané: po `A7h` napíšu akordy
`04 16 15 1A` reťazec „AHOJ", s `8Dh` vloženým po prvom písmene
„Ahoj".

**Riadiaci znak**: nasledujúca bunka sa preloží počítačovou tabuľkou
a odpočíta sa `60h` (1D5C8), takže z `a` vznikne `01h`, teda Ctrl+A.
Mimo rozsahu `60h`–`7Fh` sa ozve chybový tón. Stroj to vie aj ohlásiť —
na `D678` je reťazec `KONTROL `.

**A jedna veľkonočná kraslica.** Po `medzerník + p` prijme ROM jedine
`f` (`0Eh`); čokoľvek iné potichu zahodí oboje. Pri `f` skopíruje
dvanásť bajtov z `D6FD` do rečového bufferu (1D5E6) a stroj povie
**„Hello world"** — anglicky, v českej ROM, bez akéhokoľvek ohlásenia.
Overené na skutočnom behu emulátora.

Sonda to nezachytí, hoci reťazec v bufferi `C7E2h` vidno: zastavuje sa
v okamihu, keď firmvér čaká na kláves, takže reč nestihne odznieť. Nie
je to chyba emulátora, je to chyba merania — dobré vedieť skôr, než sa
niekto pustí opravovať niečo, čo je v poriadku.

Ostatných 55 akordov s medzerníkom padne na spoločnú vetvu (1D591)
a nerobí nič. Samotný medzerník píše medzeru; **medzerník so shiftom
je Escape** (1D52F) — zmerané 26. 8. 2026, `C638h` = `1Bh`.

### Shift je dvadsiaty kláves, nie príznak

Shift leží na riadku `8Ch`, bit 6, a dekodér ho číta na dvoch miestach:

- `1D4F9` skladá kód klávesu: `C62E AND 80h` (medzerník) a `C630 AND 40h`
  (shift), potom `RRCA` dvakrát, takže z medzerníka je bit 5 (`k_alt`)
  a zo shiftu bit 4 (`k_shift`).
- `1D60C` rozhoduje o veľkosti písmena: pri bodovom akorde a stlačenom
  shifte ide písmeno cez `D72D`, inak sa nechá tak. Ak je zapnutý zámok
  veľkých písmen (bit 4 na `(IY+0)`, testovaný na `1D605`), ide vždy cez
  `D734` a shift sa už nepýta.

**Medzerník so shiftom je jediný kód, ktorý potrebuje dva riadky naraz.**
Hostiteľ, ktorý shift drží len ako príznak a stláča jediný riadok, ho
nevyrobí — a nevyrobí ani veľké písmeno.

**Shift stlačený sám nedá žiadny kód a napriek tomu niečo robí:
zastaví reč.** Nie je to výnimka pre shift, platí to pre každý novo
stlačený kláves (`spabrt`, viď nižšie), ale shift je jediný, ktorý pri
tom nič nenapíše a nikam nevojde — preto sa ním plynulé čítanie
pozastavuje. Ide to tadiaľto: záchyt na `1D22A` ukladá do tieňa
`C62E`–`C630` **surový** bajt portu (`LD (HL),B`), kým maska `0Fh`
z `1D206` sa týka len kópie pre dekodér na `C631`–`C633`. Shift je teda
v tieni, hoci v kóde klávesu nie je, a každý, kto porovnáva port
s tieňom, ho vidí.

### Kto číta riadky priamo, mimo dekodéra

Dekodér na 1D4F3 nie je jediný odberateľ. Dve slučky, ktoré nesmú
čakať na heartbeat, si riadky čítajú samy — a preto ich **kláves
podstrčený do fronty ROM na `C67B` neprebudí**.

- **Rečový syntetizátor**, vzorková slučka 005DD–00676. Po `OUT (88h),A`
  na 0063D prečíta všetky tri riadky (`89h`, `8Ah`, `8Ch`) a porovná ich
  s tieňom naposledy nasnímaného stavu na `C62E`–`C630`. Čokoľvek navyše
  ukončí dávku a nastaví `C620h` = `FFh`. `SYSRAM.A` ten bajt volá
  **`spabrt`**, teda *space abort*. Nie je to *space* ako medzerník:
  `SYSJUMPS.11` ho rozpisuje na *SPeech ABoRT* a hovorí, že z neho
  textový procesor zisťuje, na ktorom slove skončilo **plynulé čítanie**.
  Ozbrojený je len počas reči — `C621h` je `FFh` od spustenia dávky
  (`002D5`) po jej koniec (`0055E`) a nový kláves ho iba skopíruje do
  `C620h`. Rovnako to robí aj zachytávacia rutina heartbeatu na `1D216`
  (`CPL` / `AND B`), takže reč zastaví kláves aj mimo vzorkovej slučky.
- **Prehrávač melódií**, slučka 10EFF–10F6B. Raz za takt skladby prečíta
  `8Ch` (10F13, proti vlastnému tieňu na `B0F5h`) a `89h` (10F1C, kde
  stačí čokoľvek nenulové). **Riadok `8Ah` nečíta**, takže funkčné
  klávesy skladbu neukončia. Medzerník má vlastnú vetvu: `CP 80h` na
  10F94 rozozná medzerník bez bodov a podľa toho sa 10F6E rozhodne, či
  uloží pozíciu v skladbe.

Prehrávač si pri každom takte zároveň prepíše `countdown` (`C66Ch`,
autovypnutie) na `57E4h` — 22 500 sedemdesiatpätín, teda päť minút —
takže stroj sa počas hrania nevypne.

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

### Príjem scancodov

Po úspešnom privítaní nastaví ROM `CNTR` na `6Fh`, teda RE aj EIE.
Bajt potom príde prerušením: vektor `C18C` (tabuľka na `C180`, `I`=`C1`,
`IL`=`80`) → `CD62`, ktorý naplánuje úlohu `D409` → `DD2E`.

Bity `CNTR`: bit 7 EF (bajt čaká v `TRDR`), bit 6 EIE, bit 5 RE,
bit 4 TE. Čítanie `TRDR` EF zhodí.

**Prijatie bajtu zhodí `RE`.** Je to vlastnosť 64180, nie firmvéru,
a v ROM ju vidno: obsluha prerušenia na `1E012` prijímač po každom
jednom bajte znovu zapína (`CNTR = 6Fh`). Kto to v modeli vynechá,
dostane emulátor, na ktorom chodia písmená a nechodia šípky — obsluha
totiž na `1DFFE` urobí ešte zahadzovacie čítanie `TRDR`, a to pri
trvalo zapnutom `RE` zje **druhý bajt dvojbajtovej sekvencie** `E0h`.
Jednobajtové klávesy si to nikdy nevšimnú.

Druhá pasca je fronta: príkaz `FFh` (Reset) musí vyprázdniť aj to, čo
klávesnica ešte drží na výstupe. Skutočná to robí. Bez toho stačí, aby
používateľ pustil kláves skôr, než stroj vykoná prvú inštrukciu —
napríklad Enter, ktorým emulátor spustil — a ROM si pri privítaní
vytiahne ten break kód namiesto `AAh`, usúdi, že klávesnica nie je
pripojená, a prijímač vypne natrvalo.

Je to **XT sada 1**: make pod `80h`, break s bitom 7, `E0h` pred
rozšírenými. Preklad robia štyri tabuľky:

| tabuľka | kedy |
|---|---|
| `DF05` | základná; od `3Bh` aj funkčné klávesy a kurzory → kódy Eureky |
| `DF5E` | so shiftom (`C670h` bity 0–1) |
| `DF98` | s pravým Altom (`C670h` bit 4) |
| `DFD6` | dvojice pre kódy s prefixom `E0h` |

Rozloženie je **česká QWERTZ**: `15h` je `z`, `2Ch` je `y`,
nezhiftovaná číselná rada dáva `ěščřžýáíé` a číslice sú až so shiftom.
Pravý Alt dáva `@ # $ ~ ^ & * { } [ ] ' \``.

Tabuľka `DF05` od `3Bh` je zároveň jediné doložené slovo o tom, ktoré
kurzorové akordy sú Insert a Delete: scancode `52h` → `8Dh`,
`53h` → `8Eh`. `KB.H` tvrdí `8Bh`/`8Ch` a mýli sa.

Fyzicky je základná tabuľka na **`1DF05`** a indexuje sa scancodom
priamo; `0DF05` je kód, nie tabuľka, a kto sa tam pozrie, nájde nezmysly.
Kontrolné body: `[38h]` = `F8h` (ľavý Alt), `[3Bh..44h]` = `C0h..C9h`.

**Mýli sa aj `KB.H` v tom, kde funkčné klávesy končia.** Hlavička má
`K_F10` ako posledný, ale tabuľka pokračuje: scancode `57h` → **`CAh`**,
`58h` → **`CBh`**, teda F11 a F12. F11 nie je výsada externej klávesnice —
membrána ho má ako `d-akord` (`CAh` v tabuľke akordov vyššie), takže je to
ten istý kláves z dvoch strán. F12 akord nemá. Odmerané na bežiacej ROM:

| kláves | scancode | povie |
|---|---|---|
| F11 | `57h` | „ROM operacniho systemu“ — vojde do funkcie |
| Alt+F11 | `38h 57h` | „data ROMu“ — len pomenuje |
| F12 | `58h` | nič |
| Alt+F12 | `38h 58h` | „nepouzito“ |

Z toho plynie vec, ktorá platí pre **celú** radu funkčných klávesov a nie
je nikde v manuáli: **Alt+Fn funkciu iba pomenuje, nespustí ju.** Rozhodol
to Escape poslaný hneď za klávesom — po samotnom `F4` sa Eureka spýta
„ukoncit?, ano nebo ne?“, teda je vnútri komunikácie; po `Alt+F4` mlčí,
lebo zostala v hlavnom menu. Obe pritom povedia to isté slovo
„komunikace“, takže zo samotnej reči sa to rozlíšiť nedá.

Tabuľka `DFD6` (fyzicky `1DFD6`) je zoznam dvojíc ukončený `00 00`:
`E0 1Ch` Enter z numerickej časti, `1Dh`/`9Dh` pravý Ctrl, `35h` lomka,
`37h` PrtSc, `38h`/`B8h` pravý Alt, `47h`–`53h` kurzorová plocha. **Nie
je v nej `5Bh`, `5Ch` ani `5Dh`** — klávesy Windows a kláves Aplikácie
v roku 1992 neexistovali. Kláves Aplikácie je tak jediný kláves
klávesnice PC, ktorý stroj nepozná vôbec.

### Aj externá klávesnica zastaví reč — ale nemá na to nevinný kláves

Vzorková slučka syntetizátora číta len tri membránové riadky, takže cez
ňu sa externá klávesnica k `spabrt` nedostane. **Má vlastné miesto**: doručovacia
rutina klávesu na `1DE47` končí na `1DDC4`–`1DDCA`, kde skopíruje `C621h`
do `C620h`. Je to tretí, na predošlých dvoch nezávislý zdroj `spabrt`
(vedľa `0066B` vo vzorkovej slučke a `1D21F` v záchyte membrány).

Odmerané na bežiacej ROM — dĺžka vety „hlavní menu" v krokoch, kláves
poslaný v jej štvrtine (celá veta je 783 493):

| kláves | trvanie | |
|---|---|---|
| šípky vľavo, vpravo, hore | ~196 400 | zastaví |
| písmeno `a`, medzerník, Escape | ~196 300 | zastaví |
| shift (`2Ah`) | 783 488 | **nezastaví** |
| Ctrl (`1Dh`) | 783 488 | **nezastaví** |

Odmeraná je aj cesta: šípka ide `1DDB0` → `1DE47` → `1DDC4` → `1DDC7`,
písmeno tou istou koncovkou. Shift a Ctrl sa k nej nedostanú vôbec —
rozcestník na `1DD74`/`1DD79` ich odbočí k obsluhe modifikátorov, tie
nedoručia žiadny kód, a bez doručeného kódu niet čo prerušiť.

**Z toho plynie rozdiel oproti membráne, ktorý je praktický, nie
teoretický.** Na braillovskej klávesnici je shift kláves, ktorý reč
zastaví a nič iné neurobí. Na externej klávesnici taký kláves nie je: zastaví
každý, ktorý niečo doručí, a práve tie dva, ktoré by boli neškodné, sú
jediné dva, ktoré nefungujú. Preto sa tam plynulé čítanie prerušuje
šípkou — je to najmenej rušivý kláves z tých, ktoré to vedia, a
v textovom procesore navyše nechá kurzor tam, kde sa prestalo čítať.

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
