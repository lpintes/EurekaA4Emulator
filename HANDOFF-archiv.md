# Eureka A4 — archív uzavretých zásahov

Sem sa sťahujú zásahy z `HANDOFF.md`, ktoré sú uzavreté **a ktorých záver
už niekde inde naozaj stojí** — v kóde, v `CLAUDE.md`, v `hardware-map.md`
alebo v README. Nie je to smetisko: väčšinu objemu tu tvoria merania,
adresy v ROM a to, čo sonda naozaj videla, teda jediné, čo tie závery drží
pri zemi. Číta sa **na požiadanie**, nie pri štarte.

Tri pravidlá, aby archív neklamal:

- **Čísla sekcií sa nemenia.** Odkazy `6.x` sú v zdrojákoch aj v ostatných
  dokumentoch; v `HANDOFF.md` po každom presunutom zásahu zostáva odsek
  s tým istým číslom a odkazom sem.
- **Otvorený zvyšok zostáva v `HANDOFF.md`.** Keď je téma uzavretá, ale
  niečo z nej otvorené (6.1), sem ide len tá uzavretá časť.
- **Text sa pri sťahovaní neupravuje.** Čo tu stojí, stálo tak aj
  v `HANDOFF.md`; nové vety pribudli len do odsekov, ktoré tam po zásahu
  zostali.

Presunuté 29. 8. 2026.

---

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

**Hostiteľská strana je hotová 28. 8. 2026** (ponuka `Disketa`, `Ctrl+I`,
vysunutie; návrh je v 6.22). Odmerané na nej je toto:

- Vysunutie za behu prejde celou cestou — príkaz do fronty, `DiskSwappable`,
  `EjectDisk`, `WM_EMU_DISK_CHANGED`, titulok. Overené zvonka:
  `PostMessage(WM_COMMAND, ID_DISK_EJECT)` do bežiaceho procesu a titulok
  sa zmenil z „disketa: testdisk“ na „disketa: žiadna“.
- `Mount` obraz naozaj **prestavia**, nie prekryje: po výmene nezostal ani
  bajt z predošlej diskety v blokoch, ktoré nová nepoužíva
  (`tests/disk_test.cpp`, vtedy 33 kontrol). Overené mutáciou — bez
  `image_.fill(0xe5)` v `BuildImage` padne desať kontrol.
- Vysunutá mechanika odmieta čítanie aj zápis, čo je vstup do firmvérovej
  cesty „vadný disk“ (6.6).

**Odmerané 28. 8. 2026 majiteľom na bežiacom emulátore: výmena dvoch
diskiet za behu prebehla správne.** Tým je doložené aj to hlavné, čo do
tej chvíle stálo len na manuáli — že sa EurekaDOS preloguje sám a že
výmena nepotrebuje reset ani žiadny signál od hardvéru. Sekcia už nie je
odvodenie, je to zistenie.

Ostáva jediná trhlina, a je malá: overené je to **ručne, raz**. Sonda ani
test to nerobia, takže regresiu na tejto ceste by nič nezachytilo. Ak
sa niekedy bude siahať na `DiskSwappable`, `Mount` alebo na front
príkazov, oplatí sa to preskúšať rovnako ručne — alebo to konečne
zapísať ako režim `integration_test`.

### 6.19 Podpriečinky sa ignorujú — rozhodnuté

**Rozhodnuté 28. 8. 2026: podpriečinky prevádzaného priečinka sa
ignorujú a nie je to chyba.** CP/M nemá adresáre (`FILE-FMT.D`: položka
adresára je meno 8.3, číslo používateľa a extenty — nič, kam by sa dal
zavesiť strom), takže disketa nemá kam ich dať. Otázka „ako ich
podporiť“ je tým uzavretá; sploštiť strom do jednej roviny by menilo
mená a strácalo kolízie a to je horšie než ich nechať tak.

Čo z toho platí:

- Prevádza sa **len horná úroveň** vybraného priečinka. Robí to
  `VirtualDisk::ScanFolder`: berie, čo je `is_regular_file`, ostatné
  preskočí. Chyba pri **pohľade** na položku (`status`, `file_size`)
  zostáva pomenovanou chybou — to je iná vec než podpriečinok.
- Podpriečinok zostáva na disku hosťa **nedotknutý**. Zápis späť sa
  týka len súborov, ktoré emulátor sám naimportoval, a mazanie presúva
  do `.eureka-trash` tiež len tie; adresára sa ani jedna cesta nedotkne.
  Drží to `disk_test`, kontrola `podpriecinok_zostane_nedotknuty`:
  po `Flush` musí podpriečinok aj súbor v ňom stále existovať.
- **Nič sa nehlási.** Pôvodne `main.cpp` vypisoval mená preskočených
  položiek v `MessageBox` a `VirtualDisk` ich preto zbieral do
  `skipped_entries_`. Používateľ to 28. 8. 2026 zamietol: dialóg pri
  každom štarte otravuje a nehovorí nič, čo by sa dalo urobiť. Zbieranie
  aj hlásenie sú **zmazané** — `skipped_entries_`, `skipped_entries()`
  aj pomocná `CountItems` v `main.cpp`. Tichým zlyhaním to nie je, lebo
  to nie je zlyhanie; kde súbor je, hovorí README.
- Naše vlastné veci (`.eureka-trash` a čokoľvek s prefixom `.eureka-`)
  sa preskakujú z tej istej vetvy a tiež bez hlásenia — patria hosťovi,
  nie diskete.

Jediná známa zrážka: keby Eureka vytvorila súbor s menom, ktoré sa po
prevode na 8.3 zhoduje s menom podpriečinka, zápis späť doň nemôže
zapísať. Nie je to tiché — `std::ofstream` nad adresárom sa neotvorí
(odskúšané zvlášť malým programom, `mingw64`, návrat `false`), `Flush`
vráti pomenovanú chybu „Nemožno zapísať súbor …“ a vlákno ju pošle oknu
cez `WM_EMU_DISK_ERROR`. Prepísať cudzí adresár by bolo horšie než
zastaviť sa.

### 6.20 Ctrl+D vyzeral pokazene, keď bol len vypnutý — opravené

**Nahlásené a opravené 28. 8. 2026.** `Ctrl+D` (a tá istá položka
Nástroje → Výpis diagnostiky) navonok nerobil nič. Nebola to chyba
skratky — brány boli tri a všetky tri boli ticho:

1. `Ctrl+D` je v `IDR_ACCELERATORS_HOST`, takže bez `F11` alebo
   `Shift+F11` ide Eureke a okno ho nikdy neuvidí. To je zámer a ponuka
   to aj hovorí („F11, Ctrl+D“).
2. Diagnostika je predvolene vypnutá.
3. `host::Print` sa pri chýbajúcom `STD_OUTPUT_HANDLE` mlčky vráti
   (`host_console.cpp:74`), a emulátor spustený z ikony konzolu nemá —
   vyrába ju len `--diag`, `--help` a zapnutie diagnostiky v
   Nastaveniach.

Zlá časť bola tretia v kombinácii s druhou: vetva `kDumpDiagnostics`
posielala pri vypnutej diagnostike vetu „Diagnostika je vypnutá; zapnite
ju v Nastaveniach alebo cez --diag“ cez `host::Print`. Text pre
používateľa do neexistujúcej konzoly, teda presne to, čo `CLAUDE.md`
zakazuje. Používateľ nemal ako zistiť, že systém urobil všetko správne.

Ako je to teraz: rozhoduje `MainWindow` v obsluhe `ID_TOOLS_DIAGDUMP`.
Pri vypnutej diagnostike ukáže `MessageBox` s návodom a príkaz vôbec
nepošle; pri zapnutej pre istotu doplní konzolu (`HasConsole` →
`OpenConsole`) a pošle. Vetva vo vlákne volá `Report()` bezpodmienečne.

Prečo na vlákne okna a nie tam, kde to bolo:

- Vlákno stroja nesmie otvárať okná — ten istý dôvod, pre ktorý konzolu
  otvára dialóg Nastavení, nie `kSetDiagnostics`.
- `MessageBox` si spustí vlastnú správovú slučku. Na vlákne stroja by
  zastavil emuláciu **aj zvuk** na celý čas, čo je dialóg otvorený. Je to
  ten istý dôvod, pre ktorý je synchrónny `Beep()` únosný len vďaka tomu,
  že stroj má vlastné vlákno.

**Zošedenie položky sa zvažovalo a zamietlo.** Zošedená položka čítačke
povie „nedostupné“ a nepovie prečo — používateľ zostane presne tam, kde
bol. Navyše by na nej prestal fungovať akcelerátor, takže `Ctrl+D` by
neurobil nič a bez zvuku: tichá porucha vymenená za tichú poruchu.

Cesta pri ukončení (`main.cpp:302`) podmienku `enabled()` mala už
predtým, takže obe cesty sú teraz rovnaké.

### 6.21 clangd hlásil chyby v kóde, ktorý sa prekladá čisto

**28. 8. 2026.** LSP hlásil vo `main_window.cpp` a `emulator_thread.cpp`
neexistujúci `std::filesystem`, `std::clamp`, `wstring_view` a
`starts_with` — v súboroch, ktoré `build.bat` preloží bez jediného
varovania. Pri odvodzovaní zo zdrojáka je to nebezpečný šum: chyby, na
ktoré si zvykneš ignorovať, sú chyby, ktoré prehliadneš.

clangd do `Makefile` nevidí a bez kompilačnej databázy si prepínače
domyslí. Boli tri nezávislé príčiny, nie jedna, a druhé dve sa ukázali
až pri overovaní prvej:

1. **Štandard.** Bez `-std=c++20` prekladá pod C++17 — odtiaľ všetky
   štyri hlásenia.
2. **Cieľ.** Predvolený cieľ clangd je `x86_64-pc-windows-msvc`, takže
   bral STL z Visual Studia 2022 a hlavičky z Windows SDK, teda inú
   knižnicu, než ktorou sa projekt prekladá. Hlavičky pritom nachádzal,
   len cudzie — preto to vyzeralo, že cieľ netreba riešiť.
   S `--target=x86_64-w64-windows-gnu` siahne do
   `C:\msys64\mingw64\include\c++\16.2.0`, presne tam, kam `g++`;
   sysroot si nájde sám, v konfigurácii nie je žiadna absolútna cesta.
3. **Cesty k hlavičkám.** clangd relatívne `-I` vyhodnocuje voči
   priečinku súboru, nie voči koreňu projektu. `-Isrc` teda fungovalo
   len na `src/*.cpp`, a to náhodou — hlavičky ležia vedľa nich.
   `src/win/window.cpp` mal 55 chýb a `tests/diag_probe.cpp` 501.
   Rieši to `-Isrc, -I../src, -I../../src`: pre každý z tých priečinkov
   platí jedna, neexistujúce clang mlčky preskočí.

Je to `.clangd` v koreni. `compile_commands.json` by bol vernejší, ale
musel by ho niečo generovať a znovu generovať po každej zmene pravidiel;
pri rovnakých prepínačoch pre všetky súbory je to réžia bez úžitku.
`z80.c` má výnimku na `-std=c11`, inak by naň clangd pustil C++20.

Overené `clangd --check` na všetkých sedemnástich zdrojákoch v `src/`,
`src/win/` a `tests/`: nula chýb. Pozor pri opakovaní merania —
`--check` počíta do „N errors“ aj neúspešné sondy refaktoringov
(`tweak: ExtractFunction ==> FAIL`), ktorých bývajú stovky a s kódom
nesúvisia. Skutočné diagnostiky sú riadky `E[...]` **bez** `tweak:`.

### 6.23 Export orezával programy BASICu na prvom 1Ah — opravené

Nahlásené majiteľom 29. 8. 2026 a je to najhoršia trieda chyby, akú tento
projekt má: **tichá strata dát**. Postup, ktorý ju vyrobí, je úplne bežný —
disketa `C:\b\e_games`, v BASICu `LOAD "LET.BAS"`, potom prázdna disketa
v pamäti, `SAVE "HRALET.BAS"`, načítať späť, program beží. Po skončení
emulátora sa disketa uloží do priečinka a v ňom je `HRALET.BAS`
o veľkosti **6399** bajtov namiesto 9856.

Príčina bola v `VirtualDisk::ExportImage`. CP/M nepozná presnú dĺžku
súboru, len počet 128-bajtových záznamov, takže export mal dve cesty:
pre súbor, ktorý na disketu prišiel z hostiteľského priečinka, sa dĺžka
vzala z `imported_`, a pre ostatné sa u „textových“ typov orezalo na
prvom `1Ah`. Disketa v pamäti nemá `imported_` **žiadne** — nebolo odkiaľ
importovať — takže na súbor napísaný hosťom vždy padla tá druhá cesta.
A `IsTextType` mala v zozname aj `BAS`.

`.BAS` textový nie je. Manuál to hovorí priamo (`FILE-FMT.D`, oddiel
*BASIC (.BAS)*): trojbajtová hlavička `0C2h` (Standard) alebo `0E2h`
(Advanced) a 16-bitová dĺžka programu, za ňou tokenizované riadky, kde
tokeny sú bajty `80h`–`0FFh` a `1Ah` je obyčajný dátový bajt. Nie je to
ani teoretická možnosť: `LET.BAS` má prvý `1Ah` na offsete **6399** z 9856
a ďalšie na 6400, 6406, 6472 a 6500. Sedí to na bajt — 6399 je presne to,
čo majiteľ nameral.

Orezaný program sa **nenačíta**. Hlavička nesie dĺžku programu (9798),
takže zavádzač si vypýta 9798 bajtov, v súbore ich zvyšuje 6396, narazí
na koniec a BASIC ohlási chybu. Tretina programu jednoducho chýba.

Ticho teda nie je pri načítaní, ale **pri ukladaní**: export prebehne bez
jediného hlásenia a súbor vyzerá ako hotová záloha. Že je zničený, sa
zistí až pri pokuse použiť ju — možno o mesiace, možno keď už originál
nie je. To je horšie než hlučná chyba v okamihu zápisu.

Rovnaký zdroj mal aj druhý dopad, len menej viditeľný: `.BAS`, ktorý hosť
**vytvorí alebo skráti** na priečinkovej diskete, ide tou istou cestou
(v `imported_` buď nie je, alebo je s väčšou `exact_size`), takže sa
orezal tiež.

Oprava je jeden riadok — `BAS` je zo zoznamu preč. Ostatné typy tam
zostávajú právom: `TXT`, `DOC` a zdrojáky vývojárskej diskety sú textové
súbory CP/M a manuál pre ne to pravidlo výslovne stanovuje („the last
character emitted is always an End of File character (ASCII code $1A) …
you should ignore all characters after the End of File character“).
Binárne formáty Eureky (`.TEL`, `.DIA`, `.MEL`, `.DAT`, `.ARK`) v zozname
nikdy neboli.

**Meranie, ktoré ten zoznam odteraz drží.** Zbierka v `C:\b\eureka`, 723
súborov. Kritérium: nájdi prvý `1Ah` a pozri sa, či je za ním ešte niečo
iné než výplň (`1Ah`, `00`, `E5`). Ak áno, orezanie na tom mieste zahodí
skutočné dáta.

| typ | súborov | rozsypalo by sa | najhorší prípad |
|---|---|---|---|
| `.MEL` | 240 | 95 | `koledy1.mel` → 565 z 29056 |
| `.BAS` | 108 | 61 | `chram3.bas` → 1288 zo 44416 |
| `.COM` | 81 | 78 | `database.com` → 131 z 36736 |
| `.ARC` | 49 | 49 | `ac_slv_2.arc` → **0** zo 470123 |
| `.ARK` | 16 | 16 | `basic.ark` → **0** z 35840 |
| `.EXE`, `.GRF` | 6 | 6 | `EUREKA.GRF` → 2706 z 2816 |
| `.MBS`, `.EUR`, `.DAT` | 12 | 5 | `BACKSPK.MBS` → 187 z 512 |
| `.SYS`, `.OVR`, `.SNG`, `.TAB` | 4 | 4 | `ccp.sys` → 565 z 2048 |
| `.PAS` | 52 | 5 | všetky zvyšok po dlhšej verzii |
| `.TXT`, `.DOC`, `.C` | 62 | 4 | všetky zvyšok po dlhšej verzii |
| `.ASM`, `.H`, `.INC`, `.LIB`, `.MAC`, `.BAT`, `.LST` | 75 | 0 | — |

Prvých osem riadkov je dôvod, prečo je zoznam allowlist: `1Ah` je
v binárke bežný bajt, nie zvláštnosť. U `.COM` je to priam zákonité,
lebo `1Ah` je inštrukcia `LD A,(DE)`; u `.MEL` je to výška tónu 26 zo 47.

Posledné tri riadky sú dôvod, prečo textové typy orezávať ďalej.
Deväť súborov, kde za `1Ah` niečo je, som prešiel jeden po druhom a vo
všetkých deviatich je to **zvyšok po dlhšej predošlej verzii** — CP/M
nevie súbor skrátiť, takže kratší zápis nechá starý chvost ležať.
`EUR.PAS` má za značkou 1228 bajtov starého pascalu, `database.txt` 119
bajtov starého textu, `joy.doc` zvyšok bloku po melódii. Neorezať by
znamenalo vrátiť do exportu zdvojený zdroják, a to je horšie než výplň.
Ani jeden protipríklad.

Klasifikáciu odteraz pribíja `disk_test`, kontrola
`klasifikacia_typov_je_pribita`: položí na disketu po jednom súbore od
každého typu, exportuje a porovná, ktoré sa orezali. Presun typu cez tú
hranicu tak zhodí test a povie meno — overené mutáciou, `MEL` do zoznamu
dá `T13.MEL 3 (cakane 128)`.

Po oprave sa súbor z diskety v pamäti exportuje zarovnaný na záznamy,
teda 9856 bajtov — presne to, čo na diskete naozaj je, a presne to, ako
vyzerajú súbory v pôvodnej zbierke (všetky sú násobkom 128). Presnejšiu
dĺžku by dala hlavička `.BAS` (`3 + dĺžka`, teda 9801), ale vymýšľať
dĺžku, ktorú médium nenesie, nie je čo zlepšovať.

Drží to `disk_test`, kontroly `bas_z_ram_diskety_sa_neskrati_na_1ah`,
`bas_z_ram_diskety_je_bajt_na_bajt` a `txt_z_ram_diskety_konci_na_1ah`.
Test si disketu v pamäti popíše tak, ako to robí hosť — cez
`WritePhysicalSector` položí adresárovú položku aj bloky — lebo cez
`Mount` sa na túto cestu vôbec nedá dostať: priečinková disketa má
`imported_` plné a chyba sa v nej neprejaví. Overené mutáciou: `BAS`
späť do zoznamu zhodí obe kontroly nad `.BAS` a tú nad `.TXT` nechá
prejsť.

**Poznatok na inokedy:** dva roky tu stálo, že koncovka rozhoduje o tom,
či je súbor text. Rozhoduje o tom formát, a ten je v manuáli. Keď bude
treba pridať ďalší typ, patrí to overiť v `FILE-FMT.D`, nie odhadnúť
podľa toho, ako koncovka vyzerá.

**Neznámy typ znamená výplň, a to je zámer.** Zoznam je allowlist:
súbor bez bodky v mene (`text1`, `poznamky`) aj s neznámou koncovkou
(`citaj.ma`) sa neoreže nikdy, exportuje sa zarovnaný na 128 bajtov aj so
značkou a výplňou. Drží to `disk_test`, kontroly
`subor_bez_pripony_sa_exportuje` a `subor_bez_pripony_sa_neoreze`.
Asymetria je tým vedomá: `poznamky.txt` sa oreže, `poznamky` nie.
Zlý odhad „text“ maže dáta, zlý odhad „binárka“ nechá pár bajtov výplne
navyše — a za takú cenu sa neháda.

**Otvorené, s nízkou prioritou: zoznam ako nastavenie.** Ponúka sa nechať
používateľa dopísať vlastné textové koncovky do `nastavenia.txt`. Je to
bezpečné, lebo rozhoduje človek, ktorý tie súbory píše, a nie odhad
z bajtov. Či to za tú prácu stojí, závisí na jedinom: **ako veľmi `1Ah`
na konci naozaj prekáža.** Odmerané na Windows 11 na súbore s 50 bajtmi
`1Ah` na konci:

- Nechajú ho tak a nekrátia: `type`, `more`, `copy` nad jedným súborom,
  `copy /b`, `findstr`, PowerShell aj `[IO.File]::ReadAllText`, Python
  `open('r')`.
- Orežú na ňom: `copy /a` (a dopíše si ďalší `1Ah`), spájanie
  `copy a.txt+b.txt` — to je režim ASCII **implicitne**, stačí `+`
  a v C `fopen(..., "r")`, teda textový režim CRT, ktorý zo 62 bajtov
  prečíta 10. Overené s mingw gcc; `"rb"` prečíta všetkých 62.

Takže prekáža málo a tam, kde prekáža, je to stará cesta. K tomu
prichádza, že písať na emulovanej Eureke texty nikto nečaká — je to
stroj na hranie starých programov. Preto nízka priorita.

**Rozhodovanie z obsahu bolo zmerané a zamietnuté.** Pravidlo „orež
koncový beh `1Ah` kratší než jeden záznam“ siahne na zbierke v
`C:\b\eureka` (723 súborov) na **164** z nich, vrátane 95 `.BAS` a
všetkých 33 `.ICO`. Pridanie podmienky „obsah pred chvostom je čistý
text“ to zúži na **11** a zastaví všetky `.BAS`, `.ICO`, `.ARC`, `.ARK`
aj `.COM` — trafí presne nápovedy a hlásenia s divnou koncovkou
(`EUR.HLP`, `EUREKA.MSG`, `DE.TUT`, `INTRO.SPK`). Napriek tomu nie:
v tých jedenástich sú tri tokenizované `.MBS`, ktoré prešli preto, že
tokeny sú bajty nad `80h` a tá oblasť sa musí povoliť kvôli diakritike.
Diera je viditeľná a neškodná; problém je, že heuristika s takou dierou
zlyháva rovnako ticho ako chyba vyššie, a zisk je najviac 127 bajtov.

