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

Prvé zásahy sem prišli 29. 8. 2026, ďalšie pribúdajú priebežne. Kedy
ktorý, hovorí pahýľ, ktorý po ňom zostal v `HANDOFF.md` — nie tento
odsek.

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

### 6.9 Hudba sa nedá prerušiť — medzerník vyriešený

*Otvorený zvyšok zostal v `HANDOFF.md` 6.9: umŕtvená klávesnica po čase,
na ktorú stále nikto nemá postup na reprodukciu.*

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
| sériová klávesnica (scan `39h`) | medzerník | 5000 | nie |

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
- **Externá klávesnica znelku tiež nezastaví**, a to je verné.
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

### 6.11 Funkčné klávesy hladujú: emulátor odreže ROM od jej vlastnej fronty

*Otvorený zvyšok zostal v `HANDOFF.md` 6.11: meno súboru pri `SAVE`, ktoré
sa sonde do jednoriadkového editora nedostane. Úvodná veta „oprava nie"
platila v deň, keď sekcia vznikla — od 26. 8. 2026 je oprava v strome
a končí ňou aj tento text.*

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
| `Esc` | riadky `89h` a `8Ch` naraz — shift plus holý medzerník | áno — takto Escape vyrába aj stroj (1D52F) |
| samotný `Shift` | riadok `8Ch` bit 6, držaný cez celú frontu rámcov | áno — je to dvadsiaty kláves a sám zastaví reč |
| písmená, číslice, interpunkcia | vlastná fronta ROM (`C67B`) | **nie** |

Prvých šesť riadkov nie je obchádzka: každý z nich sa premietne na kláves
alebo akord, ktorý stroj naozaj má, a `Home` až `Delete` sú len pohodlný
názov pre kurzorový akord. Tie zostávajú.

Riadok s `Esc` pribudol 28. 8. 2026 a je to práve to pohodlné meno, nie
nový kód: `Shift+Medzerník` v tomto režime fungoval vždy, len naň nikto
nesiahne, keď má ruka pod prstom `Esc`. Posiela sa na **stlačenie**, nie
na pustenie — nie je to vzorec bodov, ktorý sa zbiera, ale jeden úmyselný
akt — a opakovanie od Windows sa zahadzuje, lebo `PressBraille` nič
nedrží dole a tridsať akordov za sekundu by stroj dobiehal dlho po tom,
čo prst odišiel.

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
píše ich na emulovanej externej klávesnici, teda tou istou cestou, ktorou od
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

### 6.12 Kapacita diskety je z manuálu, nie z odhadu

*Otvorený zvyšok zostal v `HANDOFF.md` 6.12: číslovanie logických sektorov
1 až 40 v `BIOS.9` proti 0 až 39 v `VirtualDisk::ReadRecord`.*

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
4. **Podpriečinky mizli bez slova.** CP/M ich nepozná; vtedy sme to
   považovali za tichú stratu a začali ich hlásiť. **Neplatí od
   28. 8. 2026** — je to zámerné ignorovanie a hlási sa v dokumentácii,
   nie dialógom, viď 6.19.

Opravené: prehľadávanie je `ScanFolder` s `increment(ec)` a každé
zlyhanie je pomenovaná chyba. Hlásenie
o kapacite hovorí prebytok v blokoch aj v KiB a menuje tri najväčšie
súbory; číslovky sa ohýbajú (1 blok, 2 bloky, 5 blokov), lebo to znie
čítač obrazovky.

Regresiu drží `tests/disk_test.cpp` (vtedy 18 kontrol; dnes ich je viac). Testovacie priečinky
si generuje sám v `%TEMP%` — skutočný diskový priečinok je pohyblivý
cieľ a test, ktorý ho číta, meria to, čo tam práve niekto nechal.
Overené mutáciami: kontrola o dva bloky štedrejšia aj návrat tichého
`continue` test zhodí.

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

### 6.18 GUI — okno, klávesnica a NVDA

*Otvorený zvyšok tejto témy zostal v `HANDOFF.md` 6.18: tretia vec
z kontroly dokumentácie (`PressMembraneKey` zahadzuje bit Altu),
medzerníkové akordy na ovládanie emulátora, sonda na to, čo Eureka
z klávesnice zje, a kusy neodskúšané v ostrej relácii s NVDA. Preto tu
odsek „Čo z toho vyliezlo pri kontrole dokumentácie" sľubuje tri veci
a menuje dve.*

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

**Hotové 27. 8. 2026: okno, vlákno a dialógy.** Konzolovka je preložená
na Win32 aplikáciu. Čo pribudlo a prečo tak:

- **`src/win/`** — tenká vlastná obálka nad Win32, o emulátore nevie nič.
  wxWidgets sa neťahalo: rozsah je jedno okno, ponuka, akcelerátory a dva
  dialógy, prístupnosť by nepriniesla nič navyše (obe kreslia natívne
  kontroly) a celý zisk z GUI podľa tejto sekcie sú **surové správy** —
  `lParam` bit 30, scan kód, `WM_KILLFOCUS`, `WM_SYSKEYDOWN`. Framework je
  od toho, aby ich schoval.
- **`src/emulator_thread.*`** — stroj aj zvuk bežia na vlastnom vlákne.
  Nie kvôli poriadku: rozbalená ponuka, modálny dialóg aj ťahanie okna si
  spustia vlastnú správovú slučku a jednovláknový emulátor by v nich stál.
  Pri 22 ms latencie to znamená reč preseknutú uprostred slova.
  **Odmerané:** s otvoreným dialógom `Nastavenia` spotreboval proces 0,47 s
  CPU za 3 s reálneho času, bez dialógu 0,34 s. Vlákno nestálo.
- Okno sa stroja **nedotýka**. Klávesy aj príkazy idú jednou frontou, takže
  poradie drží a nad strojom nie je ani jeden zámok.
- **Dialógy sú skutočné dialógy** zo šablón v `.rc`, nie okná, ktoré tak
  vyzerajú (to boli formuláre Delphi). Overené na bežiacom procese: trieda
  okna je `#32770`, teda rola „dialóg“ pre MSAA/UIA. Odtiaľ je zadarmo
  poradie Tab, Esc, Enter, mnemoniky a to, že NVDA dialóg ohlási a prečíta.
- **Ponuku otvára F12.** Alt je modifikátor braillovskej klávesnice a F10
  je Eurekino „kde som“, takže obe predvolené cesty do ponuky sú obsadené.
  (Pôvodné odôvodnenie pokračovalo tým, že F11 a F12 stroj nepozná. To je
  **nesprávne** a je to opravené nižšie, 27. 8. 2026.)
- **Skratky vlastní akcelerátorová tabuľka**, jedno miesto. Preklad
  klávesov na vlákne o nich už nevie; `Ctrl+Q/R/D` a `Ctrl+K` z neho
  vypadli. Rezervované sú navyše `Ctrl+U/V/N/H`. (Vtedy to bolo
  `Ctrl+Shift+…`; Shift padol 28. 8. 2026, viď koniec 6.18.)
- Z toho, čo táto sekcia sľubovala, je hotové aj **`WM_KILLFOCUS`**
  (pustí všetko deterministicky; `ForgetStaleArrows` zostal ako poistka na
  release stratený inak) a **explicitný autorepeat z `lParam` bitu 30**
  v braillovskom akorde.
- Hostiteľská konzola **zostala**, ale už nie je povrchom — je to
  diagnostická plocha. Editačný prvok v okne sa nerobil, text sa zatiaľ
  nevypisuje.

**Doplnené 27. 8. 2026: konzola sa už neotvára sama.** Emulátor je program
GUI subsystému (`-mwindows`, vstupný bod `wWinMain`), takže pri bežnom
spustení žiadne okno konzoly nevyskočí — dovtedy vyskočilo, vzalo si fokus
a používateľ musel okno emulátora hľadať Alt+Tabom.

- `AttachToParentConsole()` na štarte je zadarmo a bez okna: `--help`
  a `--diag` zo shellu píšu tam, kam majú.
- `OpenConsole()` konzolu vyrobí, a volá sa len pre `--diag`, `--help`
  a zapnutie diagnostiky v Nastaveniach.
- Štandardné handly sa preberajú len keď tam nič použiteľné nie je, takže
  `--help > subor.txt` ďalej funguje. Odskúšané.
- Konzolové zariadenie hosťa sa vypisuje **len so zapnutou diagnostikou**.
  Berie sa vždy, aby buffer nerástol.

**Dôsledok, ktorý sa dá ľahko prehliadnuť:** čokoľvek pre používateľa
poslané cez `host::Print` odteraz padne do konzoly, ktorú nikto neotvoril,
a **zmizne potichu**. Preto sú v `MessageBox` chyby pri štarte,
zlyhanie zvuku (to hlási vlákno oknu cez `WM_EMU_NO_AUDIO`,
lebo modálne okno z pracovného vlákna by ho zablokovalo) a hlásenia pri
ukladaní diskety. `HoldConsole` prestalo byť potrebné na chyby — dialóg sa
nezavrie sám.

**Dve pasce, obe odchytené až meraním:**

1. *Pripojenie k rodičovskej konzole pri štarte vyzerá zadarmo a nie je.*
   Konzola žije, kým je na ňu niekto pripojený, takže spustené z dávky,
   ktorá potom skončí, by okno toho shellu zostalo visieť prázdne celú
   reláciu. Pripája sa preto až tam, kde sa píše — `OpenConsole()`
   a `Fail()`, a v `Fail()` len preto, že proces hneď skončí.
2. *`cmd` na program GUI subsystému čaká.* `Spustit-Eureku.bat` teda
   držala okno dávky na obrazovke celý beh — konzolu sme z emulátora
   odstránili a vrátila sa zadnými dverami. Dávka preto volá `start`.

Odskúšané: priamo EXE bez rodičovskej konzoly (žiadna konzola), cez dávku
(žiadna, a `cmd` skončí hneď), cez dávku s `--diag` (vznikne
`ConsoleWindowClass` „Eureka A4 — diagnostika"), zo shellu (pripojí sa,
nové okno nevzniká), presmerovanie `--help > subor.txt` (zachované),
a zapnutie diagnostiky v Nastaveniach za behu.

**Pasca, do ktorej som pri tom spadol.** Príkaz „Vypnúť Eureku" najprv
posielal `QueueKey(0x8f)` a hneď za tým `ReleaseKey(0x8f)`. Vyzeralo to
symetricky a bolo to nefunkčné: pustenie, ktoré príde skôr, než sa
stlačenie dostane na porty, spadne v `ReleaseKey` do vetvy „pustené pred
prvým skenom" a **skráti stlačenie zo 120 ms na 40 ms**
(`machine.cpp:1182`). ROM ten akord nestihne zosnímať — odmerané: stroj sa
nevypne vôbec a `C45Ah` zostane `00` namiesto `FF`.

Prečo to test `power` nechytil: skladal si akord sám, takže testoval
stroj, ale **cestu hostiteľa netestoval vôbec**. Preto je akord teraz
v `EurekaMachine::PressPowerOffChord()` a test aj ponuka volajú to isté;
hostiteľ si ho už neskladá. Poučenie je všeobecnejšie než tento akord: keď
test aj aplikácia robia „to isté" dvoma zápismi, test nehovorí o aplikácii
nič.

**Hotové 27. 8. 2026: F11 a Shift+F11.** Majiteľ nápad s akordmi (nižšie)
odložil v prospech niečoho jednoduchšieho, a je to lepšie riešenie:

- **F11** pustí do Windows nasledujúci jeden kláves; okno ho nechá
  `DefWindowProc`, takže `Alt`, `F10` aj `Alt+medzerník` robia to, čo robia
  všade inde. `Alt+medzerník` je ten okamžitý zisk — systémovú ponuku okna
  sme dovtedy zožierali.
- **Shift+F11** uvoľní klávesnicu úplne, ako „host key“ vo virtuálnom
  stroji. Toto je z tých dvoch to užitočnejšie a jeho hodnota porastie
  s každou kontrolou, ktorá do okna pribudne.
- Oboje je v akcelerátorovej tabuľke a v ponuke Klávesnica, stav je v
  titulku a **každá zmena znie** (viď `CLAUDE.md`, „Režim, ktorý nepočuť“).
- Pri zmene sa posiela `PostFocusLost()`, aby hosťovi nezostal visieť
  modifikátor.
- Položky režimu sa počas uvoľnenia **nezošedievajú**: vybrať si, ako sa
  klávesnica vráti, je zmysluplné, a `F11`, `Ctrl+K` ide ďalej —
  zošedená položka vedľa fungujúcej skratky hovorí dve rôzne veci.

**Dve pasce z toho, obe odmerané:**

1. *Akcelerátor zje stlačenie, nie pustenie.* F11 nachystal jednorazovku a
   pustenie toho istého F11 ju o zlomok sekundy neskôr minulo — F11 potom
   Alt neurobilo nič. Minie sa preto len na klávese, ktorého **stlačenie**
   okno videlo.
2. *Modifikátor musí jednorazovku míňať.* Prvé pravidlo modifikátory
   z míňania vynímalo, lenže `Alt` sám je úplný úkon (ponuku otvára na
   svojom pustení). Jedno F11 tak zaplo prepúšťanie **natrvalo** — sticky
   režim, ktorý nikto nechcel, a k tomu tichý.

**Opravené 27. 8. 2026: Alt+F4 už emulátor nezabíja.** Hlásené z používania:
majiteľ si prechádzal nápovedu funkcií — `Alt+F1` záznamník, `Alt+F2`, …
— a na `Alt+F4` emulátor skončil. Bola to výnimka v `WM_SYSKEYDOWN`
s odôvodnením, že „F4 sa dá stlačiť aj bez Altu". To odôvodnenie je
nesprávne rovnakým spôsobom ako tie dva prípady, keď sa susednosť pomýlila
s príčinnosťou: **Alt tu nie je modifikátor hostiteľa, je to kláves stroja**
(`SpecialKey` mu dáva bit `20h`), takže `Alt+F4` je `E3h`, komunikácia,
a `F4` samotné je `C3h` — iná funkcia. Rada `Alt+F1`–`Alt+F10` je jedna
súvislá ponuka, po ktorej sa chodí, a diera v jej strede, ktorá zabije
proces, stojí viac než štandardné zatváranie okna. Výnimka je preč; okno
zatvára `F12` → Súbor → Skončiť (cesta bez podmienky, disketu uloží)
a po `F11` či `Shift+F11` sa `Alt+F4` chová ako všade inde, lebo
`HostKeepsKey` ho vtedy pustí do `DefWindowProc`.

**Opravené 27. 8. 2026: F11 a F12 stroj pozná.** Majiteľ si prechádzal
nápovedu funkcií a našiel, že `Alt+F11` povie **dáta ROM** (to isté, čo
D-akord) a `Alt+F12` že je nepoužité. Predpoklad, na ktorom stálo celé
rozdelenie klávesnice — že F11 a F12 sú jediné klávesy, ktoré stroj
nepozná — bol teda nesprávny. Vzišiel z toho, že `KB.H` končí na
`K_F10`; lenže hlavička nie je stroj.

Doložené priamo z dumpu: tabuľka klávesnice PC je fyzicky na `1DF05`
a indexuje sa scancodom (kontrolný bod: `[38h]` = `F8h`, ľavý Alt,
`[3Bh..44h]` = `C0h..C9h`). Ďalej v nej je `[57h]` = **`CAh`** a `[58h]`
= **`CBh`**, teda `10 | K_FUNCTION` a `11 | K_FUNCTION`. S bitom Altu
`20h` je z toho `EAh` a `EBh`, čo presne sedí na to, čo hovorí nápoveda.

**V externom režime by F11 a F12 chodili už dnes.** Emulátor tam neprekladá nič
— posiela surový scancode a prekladá ROM — takže ich zožiera len
akcelerátorová tabuľka okna.

Riešenie (voľba majiteľa): skratky sa **nepresúvajú**, F11 a F12 zostávajú
oknu. Pribudla podponuka `Klávesnica → Poslať Eureke kláves` s položkami
F11, Alt+F11, F12 a Alt+F12. Nestojí to žiadny kláves, čítačka ponuku
prečíta a `SyntheticKey` postaví udalosť tak, ako by prišla z Windows —
scancode si vypýta od `MapVirtualKeyW`, nie z konštanty, lebo v externom režime
je scancode celá správa. Alt drží pri stlačení a púšťa pri pustení, takže
`SyncModifiers` po sebe nenechá visieť `38h`.

#### Čo z toho vyliezlo pri kontrole dokumentácie

Kontrola, či je všetko poznačené, našla tri veci a všetky sú **odmerané**
dočasnou sondou v scratchpade (kópia `diag_probe` s dvoma tokenmi navyše:
`sXX…` pošle surové scancody na sériovú klávesnicu, `bXX` jeden braillovský
akord priamo cez `PressBraille`).

**1. `Alt+Fn` funkciu iba pomenuje, nespustí ju.** To je tá rada, po ktorej
majiteľ chodil. Zo samotnej reči sa to nepozná — `F4` aj `Alt+F4` povedia
„komunikace“ — a rozhodne to až Escape poslaný hneď za klávesom: po `F4`
sa Eureka spýta „ukoncit?, ano nebo ne?“, teda je vnútri komunikácie, po
`Alt+F4` mlčí, lebo zostala v hlavnom menu. Predtým tu stálo, že „Alt+F4
je komunikácia“; presnejšie je, že ju **pomenuje**.

**2. F11 je skutočná funkcia a má ju aj braillovská klávesnica.** Odmerané:
scancode `57h` → „ROM operacniho systemu“, `Alt+F11` → „data ROMu“,
`58h` → ticho, `Alt+F12` → „nepouzito“. A `d-akord` (`9Ch`, medzerník +
body 1,4,5) povie to isté čo `57h`. Ten akord stál v tabuľke
v `hardware-map.md` roky — ako `CAh` — a `PressMembraneKey` ho napriek tomu
**nemal**: `kCA` cez kód klávesu nerobil nič, kým tie isté body napísané
priamo fungovali. Doplnené (`case 0xca: frame.row0 = 0x9c`), odmerané
znovu, a `SpecialKey` preto končí na `VK_F11`, nie na `VK_F10`. F12 akord
nemá, takže v braillovskom režime je z podponuky aktívne len F11.

**Hotové 27. 8. 2026: NVDA spí nad oknom stroja, nie nad aplikáciou.**
Majiteľ hlásil, že emulátor sa s NVDA tlčie a jediná obrana — uspať čítačku
cez `NVDA+Shift+S` — umlčí aj Nastavenia a ponuku, teda presne to, kde je
čítačka potrebná. Riešené doplnkom v `nvda-addon/`.

Prečo nie hook v okne. Cesta „ako virtuálny stroj", teda `WH_KEYBOARD_LL`
v emulátore, technicky funguje — hooky sa volajú od naposledy
zaregistrovaného a emulátor štartuje po NVDA — ale **rozpadne sa potichu**:
stačí reštart NVDA alebo `NVDA+Ctrl+F3` a čítačka je v reťazci pred nami.
K tomu zaseknuté okno znamená mŕtvu klávesnicu pre celý systém. Pre tento
projekt je to zlá výmena; klávesnica, ktorá nikam nechodí, je jeho stará
pasca.

Čo to teda robí a čím je to doložené (čítané zo zdrojákov NVDA, tag
`release-2026.1.1`, a preverené aj proti `release-2024.1`):

- `NVDAObject` má `_cache_sleepMode = False` a `eventHandler.executeEvent`
  aj `inputCore.executeGesture` sa pýtajú **objektu**, ktorého sa vec týka.
  Spánok sa preto dá viazať na jedno okno: doplnok cez
  `chooseNVDAObjectOverlayClasses` podstrčí vlastnú triedu oknu
  `EurekaA4EmulatorWindow`. Dialógy (`#32770`) a ponuka (`#32768`) sú iné
  objekty a zostávajú bdelé.
- V spánku sa gesto zahodí (`NoInputGestureAction`) a kláves ide do
  aplikácie, udalosti sa nespracujú. Teda: žiadne echo, žiadne dvojité
  rozprávanie a Eureka dostane aj numerickú klávesnicu a `NVDA+šípky`.
- **Výnimka je kláves NVDA:** `keyboardHandler.internal_keyDownEvent` ho
  nepustí ďalej nikdy („Never pass the NVDA modifier key to the OS"). Eureka
  `Insert` pozná (`8Dh`), takže sa doň dostane len dvojitým stlačením
  (bypass cez `multiPressTimeout`, tiež v tom hooku) alebo cez `Shift+F11`.
- `NVDA+Shift+S` má `allowInSleepMode=True`, takže zostáva núdzová brzda aj
  v spánku. Preto doplnok vracia `True`, nie `SLEEP_FULL`.
- `NVDA+T` by v spánku nefungovalo — a titulok je pritom jediný nosič stavu,
  na ktorý sa dá spýtať kedykoľvek. Doplnok ho preto **vnútri emulátora
  nahrádza** vlastným príkazom s `allowInSleepMode=True`; skripty
  aplikačného modulu sa hľadajú pred `globalCommands`
  (`scriptHandler._yieldObjectsForFindScript`), takže mimo emulátora sa
  nemení nič.

Zo strany emulátora pribudlo jedno: `PublishKeyboardState()` vystaví
`released_` ako vlastnosť okna `EurekaA4.KeyboardReleased`. Vlastnosť okna,
nie pomenovaná udalosť — viaže sa na konkrétne okno, takže dva bežiace
emulátory si neprekážajú, a jej neprítomnosť znamená „Eureka vlastní
klávesnicu", čo je aj to, čo odpovie staršie EXE. **Odmerané na bežiacom
procese** (`GetPropW` z iného procesu cez `ctypes`): pri štarte `0`, po
`Shift+F11` `1`, po vrátení `0`, a titulok sa mení s tým.

Jednorazovka (F11) sa zámerne nepublikuje — prebudená čítačka by ten jeden
kláves zjedla ako svoj príkaz, okno by ho nikdy nevidelo a jednorazovka by
zostala nachystaná navždy. To je presne tá tichá sticky pasca, na ktorej sa
`HostKeepsKey` už raz popálilo.

**Opravené hneď pri prvom skúšaní so zapnutým NVDA: ponuka bola ticho.**
Majiteľ hlásil, že po `F12` sa ponuka otvorí, ale nepočuť ju — ozve sa až
prvá šípka dole. Príčina je štrukturálna a stojí za zapamätanie:
**ponuková lišta nie je okno.** Patrí HWND, ktorý ju vlastní, takže objekty,
ktoré NVDA po otvorení ponuky postaví, nesú triedu `EurekaA4EmulatorWindow`
a padnú na ten istý overlay ako okno stroja — a ten ich uspal. Rozbaľovacie
menu naopak **vlastné okno je** (`#32768`), overlay naň nesadá, a preto sa
prvá šípka ozvala.

Odmerané na bežiacom emulátore (`GetGUIThreadInfo` na vlákne okna): pred
ponukou `flags=0` a `hwndMenuOwner=0`, po `SC_KEYMENU` (to, čo robí `F12`)
`GUI_INMENUMODE` a `hwndMenuOwner` = naše okno, po rozbalení bez zmeny, po
`Esc` zase `0`. **`hwndFocus` je celý čas hlavné okno** — to je to
doloženie, že ponuka vlastný HWND nemá.

Doplnok sa preto pýta aj na režim ponuky a kým je ponuka hore, nespí.
Vecne to sedí: v režime ponuky berie klávesy Windows a Eureka z nich
nedostane nič, takže nie je pre koho mlčať. Overené proti bežiacemu
emulátoru cez rozhodovaciu funkciu samotného doplnku — okno stroja `True`,
otvorená ponuka `False`, rozbalená `False`, po zavretí `True`, po
`Shift+F11` `False`, po vrátení `True`.

Poučenie na ďalšie kolo: **„trieda okna" a „okno" nie sú to isté.** Čokoľvek
ďalšie, čo bude žiť na HWND hlavného okna, spadne na ten istý overlay.

**Potvrdené majiteľom 27. 8. 2026 v ostrej relácii s NVDA:** nad oknom
stroja čítačka mlčí a klávesy patria Eureke, a po oprave sa ponuka ohlási
hneď pri otvorení. To je jediný druh dôkazu, ktorý tu platí — všetko
ostatné okolo doplnku je meranie zvonka a čítanie zdrojákov NVDA, lebo
doplnok beží vnútri cudzieho procesu, do ktorého sa odtiaľto nedá vidieť.

**Skratku okna môže zobrať iný program, a je to ticho.** Pri skúšaní sa
ukázalo, že `Ctrl+Shift+H` (Klávesové skratky) nerobí nič. Príkaz aj ponuka
sú v poriadku — overené poslaním `WM_COMMAND` s `ID_HELP_KEYS`, dialóg
vznikne. Kláves sa do okna nedostane: niekto iný v relácii ho drží ako
**globálnu skratku** cez `RegisterHotKey`, a tá vyhráva nad akcelerátorovou
tabuľkou aplikácie. Na stroji majiteľa je to organizér a berie aj
`Ctrl+Shift+R`, teda Reset.

Diagnostika je jednoduchá a stojí za zapamätanie: skúsiť si tú kombináciu
zaregistrovať sám. `RegisterHotKey` vráti chybu **1409**
(`ERROR_HOTKEY_ALREADY_REGISTERED`), keď ju už niekto drží; keď je voľná,
prejde a hneď sa dá odregistrovať. Takto sa z ôsmich skratiek emulátora
našli presne tie dve zabraté.

Z toho plynie, že **ponuka musí zostať plnohodnotnou cestou ku všetkému** —
je to jediná cesta, ktorú cudzí program nezoberie. Dnes to tak je.

~~**Rozhodnuté: skratky sa kvôli tomu presúvať nebudú.**~~ Argument znel, že
ktorúkoľvek náhradu môže mať obsadenú zase niekto iný, takže je to iná
lotéria a nie riešenie. **Prehodnotené 28. 8. 2026 (viď nižšie): nie je to
tá istá lotéria.**

#### Hotové 28. 8. 2026: skratky sú `Ctrl+písmeno`, nie `Ctrl+Shift+písmeno`

`Ctrl+Shift` nie je náhodná polovica priestoru — je to práve tá, kam si
programy vešajú **globálne** skratky, lebo aplikácie ju samy používajú
zriedka. Presun na holé `Ctrl` teda nie je výmena jedného lósu za druhý,
ale odchod z inkasa, kde sa losuje. Rozhodol majiteľ.

Sedem skratiek stratilo Shift, `Ctrl+K` bolo bez neho už predtým:

| bolo | je | čo robí |
| --- | --- | --- |
| `Ctrl+Shift+U` | `Ctrl+U` | uloží disketu do priečinka |
| `Ctrl+Shift+Q` | `Ctrl+Q` | uloží disketu a skončí |
| `Ctrl+Shift+R` | `Ctrl+R` | reset |
| `Ctrl+Shift+V` | `Ctrl+V` | vypne Eureku, ako to robí ona sama |
| `Ctrl+K` | `Ctrl+K` | prepne klávesnicu (bez zmeny) |
| `Ctrl+Shift+N` | `Ctrl+N` | nastavenia |
| `Ctrl+Shift+D` | `Ctrl+D` | výpis diagnostiky |
| `Ctrl+Shift+H` | `Ctrl+H` | klávesové skratky |

**Odmerané tou istou sondou, ktorá kolíziu našla** (`RegisterHotKey`,
chyba 1409 = zabraté). Všetkých osem nových kombinácií je na stroji
majiteľa voľných. Kontrola, že sonda naozaj zvoní: na starej sade
ohlásila presne `Ctrl+Shift+R` a `Ctrl+Shift+H`, teda tie dve, o ktorých
sa už vie — ostatných päť voľných.

Zmenené na piatich miestach a všetky sú len text alebo tabuľka:
`src/res/eureka.rc` (akcelerátory + ponuka + reťazec v Nastaveniach),
`src/main_window.cpp` (`kShortcutHelp`), `src/main.cpp` (`--help`),
`README.md`, `CLAUDE.md`.

Čo tým nezaniklo: **ponuka zostáva plnohodnotnou cestou ku všetkému.**
Skratku môže zobrať cudzí program kedykoľvek a znovu to bude ticho —
`Ctrl` je menej obľúbené miesto, nie chránené.

Poznámka k cene, ktorá tu najprv stála zle: cena za `Ctrl+písmeno` **nebola
nová**. Na exterke dekodér ROM na `1DE0E` skladá Ctrl+písmeno cez `AND 1Fh`
na riadiaci znak, ale tabuľku podľa Shiftu vyberá skôr (`1DDD0`: `DF05h`
bez, `DF5Eh` s ním) — a `55h & 1Fh` je to isté ako `75h & 1Fh`. Odmerané
na všetkých ôsmich písmenách: `Ctrl+Shift+X` aj `Ctrl+X` dajú hosťovi
rovnaký kód. Tých osem kódov mu brala už stará sada. Zanikli až
o deň neskôr, viď ďalej.

#### Hotové 28. 8. 2026: skratky sú dvojhmatové, `F11` a potom `Ctrl+písmeno`

Predošlá zmena riešila globálne hooky a hosťa nechala tak, ako bol. Majiteľ
chcel niečo iné a lepšie: **nekradnúť Eureke nič.** Skratky sú preto
dvojhmatové — `F11`, `Ctrl+R` je reset, `F11`, `Ctrl+K` prepne klávesnicu.
Bez `F11` idú tie klávesy Eureke, takže `Ctrl+H` na exterke robí `08h`, ako
robí na skutočnom stroji.

**Okno teraz berie Eureke tri klávesy a nič viac:** `F12`, `F11`,
`Shift+F11`. Predtým ich bolo jedenásť.

Ako to je spravené:

- **Dve akcelerátorové tabuľky.** `IDR_ACCELERATORS` (`F11`, `Shift+F11`,
  `F12`) platí vždy; `IDR_ACCELERATORS_HOST` (`Ctrl+U Q R V K N D H`) len
  keď je klávesnica hosťova. Overené na hotovom EXE cez `FindResourceW`:
  typ `RT_ACCELERATOR`, id 101 má 3 položky, id 102 osem.
- **Bránu vyhodnocuje `MainWindow::HostShortcutsActive()`**
  (`released_ || passOnce_`) a `win::RunMessageLoop` sa jej pýta pri každej
  správe, nie raz na štarte. Slučka dostala druhú tabuľku a predikát;
  `src/win/` o emulátore naďalej nevie nič.
- **Jednorazovku míňa `WM_COMMAND`,** nie `HostKeepsKey`. Toto je to
  miesto, kde sa to dá pokaziť: okno stlačenie skratky nevidí, lebo ho zje
  `TranslateAccelerator`, takže by `F11` zostalo nachystané navždy — ten
  istý tichý sticky režim, na ktorom sa `HostKeepsKey` už raz popálilo.
  Rozlišuje sa podľa `HIWORD(wParam) == 1` (akcelerátor, nie ponuka),
  s výnimkou `ID_KEYBOARD_PASSONCE` a `ID_KEYBOARD_RELEASE` — tie ten stav
  vlastnia samy a inak by si `F11` odzvonilo dva tóny a odzbrojilo sa.

Dôsledky, ktoré treba mať povedané:

- **`Ctrl+Q` už nezatvára okno na jeden hmat.** Bezpodmienečná cesta von je
  `F12` → Súbor → Skončiť. Preto `F12` prefix **nedostalo**: ponuka musí
  byť dosiahnuteľná vždy a Eureku nestojí nič (samotné mlčí, `Alt+F12`
  povie „nepouzito“).
- **Položka ponuky sa volá „Jeden kláves do Windows alebo skratka“**, aby
  bolo z čoho zistiť, že `F11` je prefix. Ponuka ukazuje skratky ako
  `F11, Ctrl+R`, čítačka to prečíta.
- **Po `Shift+F11` ponuka prefix zahodí** a píše len `Ctrl+R`. Nie je to
  kozmetika: po uvoľnení klávesnice `F11` nerobí **nič** — `SetPassOnce`
  ho v tom stave odmieta a položka preň je zošedená — takže kto by ho podľa
  ponuky stlačil, nedočkal by sa ani pípnutia. Robí to
  `MainWindow::RefreshShortcutText` z `RefreshMenu`, teda pri
  `WM_INITMENUPOPUP`. Písmená si číta späť z ponuky a len nasadzuje
  a sníma prefix, aby ich `.rc` menoval ako jediný.
  **Odmerané na bežiacom procese** (`GetSubMenu` + `GetMenuStringW` po
  pozíciách, `WM_COMMAND` s `ID_KEYBOARD_RELEASE` z druhého procesu):
  na začiatku `F11, Ctrl+U/Q/R/K/H`, po uvoľnení `Ctrl+U/Q/R/K/H`, po
  vrátení zase s prefixom a bez zdvojenia. Pozor, cez hranicu procesov
  sa ponuka **nedá** čítať `MF_BYCOMMAND` (vráti −1), lebo podponuky
  patria cudziemu procesu; po pozíciách áno.
- Text v Nastaveniach mal ten istý problém a je preto bez skratky:
  „výpis v ponuke Nástroje“.
- **V braillovskom režime sa nemení nič**: Ctrl na tej klávesnici nie je
  kláves a `emulator_thread` ho tam zahadzuje na stlačení aj na pustení.
  Komentár pri tom guarde bol po tejto zmene nepresný (odvolával sa na to,
  že stlačenie zjedla tabuľka) a je prepísaný.

Deväť testov prechádza. Samotné správanie okna testy nepokrývajú —
`main.cpp` ani `main_window.cpp` — takže na ňom záležalo najviac:
**majiteľ to 28. 8. 2026 odskúšal ručne a funguje.** Že `Ctrl+K` bez
`F11` klávesnicu neprepne, že `F11`, `Ctrl+K` ju prepne, a že po skratke
zaznie nižší tón, teda že sa jednorazovka minula a nezostala visieť —
to posledné bolo jediné miesto, kde by chyba bola tichá.

**Rozhodnuté: doplnok v repozitári zostáva.** Je pod GPL v2+, kým zvyšok je
MIT, a to je v poriadku — nie je to zmiešanie licencií v jednom diele, ale
dva vedľa seba: `nvda-addon/` je samostatný plugin do cudzieho programu,
ktorý si GPL vynucuje na svojich doplnkoch, a nič z emulátora ho nepoužíva
ani ho nepotrebuje. Hranica je priečinok a je napísaná na troch miestach:
`LICENSE.txt` (rozsah MIT), `nvda-addon/COPYING.txt` a README oboch strán.

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

~~Otvorené zostáva len to, čo bolo dôvodom celej veci: rozdeľovač kolekcie,
ktorý má zamknuté sloty používať.~~ **Hotový 9. 9. 2026**; README hotové
diskety rovno odporúča zamknúť, presne kvôli hromadnému kopírovaniu.

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

**Vrstva `disk_layout` je hotová 9. 9. 2026, sprievodca nie.** Návrh vyššie
platí celý; toto je to, čo sa rozhodlo až pri písaní a v ňom nestálo.
(Sprievodca je hotový v ten istý deň — o kúsok nižšie. Táto veta zostáva
v pôvodnom znení, lebo to, čo je pod ňou, sa rozhodlo pri vrstve, nie pri ňom.)

- **Kapacita aj mená sú v `src/cpm_disk.h`, v jednej kópii.** Konštanty z DPB
  stáli v anonymnom priestore mien vo `virtual_disk.cpp` a rozdeľovač ich
  potrebuje tiež; s nimi sa presunulo aj skladanie 8.3 mena (`MakeName`,
  `UniqueName`). Nie je to upratovanie: plán, ktorý sľúbi meno alebo kapacitu,
  akú obraz nakoniec nedá, klame — a nezachytí to nič, lebo obe polovice by sa
  ďalej prekladali aj testovali nazeleno.
- **Jednotka nikdy nepresiahne priečinok.** `TURBO.COM` v jednom priečinku
  a `TURBO.MSG` v druhom sú dve kópie od dvoch ľudí, nie jeden program. Všetky
  tri pravidlá preto bežia vnútri každého priečinka zvlášť.
- **Riadok v `SPOLU.txt` je pravidlo, nie zoznam konkrétnych súborov.** Uplatní
  sa v každom priečinku samostatne, takže jeden riadok pokryje program, ktorý
  leží v piatich. Práve preto spätný zápis (`UnitsAsGroups` → `FormatSpolu`)
  prežije opakované delenie a `ParseSpolu` z neho vyrobí tie isté jednotky.
  Meno v tom súbore sa navyše musí porovnávať po častiach, nie celé: skladanie
  mena mení bodku na podtržník, takže `WS.COM` by ako jeden reťazec nesedelo
  ani na vlastný súbor.
- **Osamotený súbor sa premenuje na tej istej diskete, novú nezaloží.**
  „Premenovanie je posledná záchrana“ znamená, že sa najprv skúsi disketa, kde
  je meno voľné — nie že sa kvôli menu otvorí ďalšia disketa. Skrátenie na osem
  znakov robí kolízie bežnými (`PRIBEHY-JAR.TXT` aj `PRIBEHY-LETO.TXT` sú
  `PRIBEHY-.TXT`) a z diskety na súbor by bola nepoužiteľná hromada. Jednotka
  o viac súboroch sa naopak posunie celá, a keď si dva jej súbory pýtajú jedno
  meno, odmietne sa celá s dôvodom: premenovať sa v nej nesmie a rozdeliť ju
  tiež nie.
- **`OBSAH.TXT` na diskete sa započítava pri každom pridaní**, nie raz na konci
  — rezervácia rastie s počtom položiek, takže sa nemôže stať, že posledný
  súbor sadne presne na miesto katalógu.
- **Meno diskety je poradové číslo a najväčšia skupina na nej** (`01-HUDBA`),
  prehnaná tou istou funkciou, ktorá skladá 8.3 mená.

Testy sú v `disk_test` (deväť skupín, spolu 185 kontrol v tom teste) a bežia
bez ROM aj bez jediného súboru na disku. `PlanProblem` v nich prepočíta každú
disketu zo vstupu a plán odmietne, keď je v ňom súbor dvakrát, ani raz, alebo
na diskete, kde je jeho meno už obsadené — súčtom, nie vzorkou.

~~Otvorené zostáva to, čo sa dotýka používateľa: sprievodca, ktorý plán ukáže,
dá `SPOLU.txt` opraviť a plán vykoná.~~ **Hotové 9. 9. 2026, viď nižšie.**
Vrstva sama ďalej nekopíruje nič a do zdrojového priečinka nesiaha vôbec —
to sa nezmenilo, len nad ňou odvtedy stojí `src/disk_split.*`.

#### Sprievodca a vrstva, ktorá plán vykoná — hotové 9. 9. 2026

Medzi `disk_layout` a dialóg pribudla tretia vrstva, `src/disk_split.*`, a je
to presne to, čo si prvá veta návrhu vyžiadala tým, že si čítanie kolekcie
a kopírovanie výslovne odopiera. Rozdelenie je jednoslovné: **`disk_layout`
sa nedotkne súboru, `disk_split` sa nedotkne rozhodnutia.**

- `Scan` prečíta kolekciu **rekurzívne** — na rozdiel od `VirtualDisk::Mount`,
  ktorý podpriečinky ignoruje (6.19). Nie je to nedôslednosť: disketa je jeden
  plochý adresár CP/M, ale kolekcia je práve tá vec, ktorá sa doň nezmestí,
  a jej podpriečinky sú to, čím režim „podľa priečinkov“ balí. `SPOLU.txt`
  v koreni je návod, nie súbor kolekcie, takže sa medzi ne nepočíta; ten
  hlbšie v strome tam patrí normálne.
- `TargetIsUsable` je pravidlo o cieli v **jednej kópii**: prázdny priečinok
  alebo taký, ktorý ešte neexistuje, a nikdy vnútri zdroja. Pýta sa naň
  `Execute` tesne pred tým, než čokoľvek vyrobí, **aj prvá stránka dialógu** —
  inak by sa používateľ dozvedel o nepoužiteľnom cieli až po tom, ako prečítal
  plán na štyridsať diskiet. Dve kópie toho pravidla by boli dve odpovede a tá
  počutá by nebola tá, ktorá rozhoduje.
- `Describe` vyrába **jeden text pre náhľad aj pre `OBSAH.txt`**. Náhľad je to,
  podľa čoho sa používateľ rozhoduje, `OBSAH.txt` to, čo si prečíta o mesiac;
  dva texty by boli dve príležitosti rozísť sa v tom, čo sa vlastne stalo.
- **`OBSAH.TXT` na diskete ustúpi menom používateľovmu súboru**, nie naopak.
  Plán rezervuje blok a položku adresára, **nie meno** — kolekcia, ktorá si
  vlastný `OBSAH.TXT` nesie, je nezvyklá, ale prepísať ho by bola presne tá
  tichá strata, kvôli ktorej táto vrstva existuje. Náš zoznam sa uhne na
  `OBSAH~1.TXT`. Končí znakom `1Ah`: `TXT` je v `IsTextType` textový typ,
  takže export ho na ňom oreže, a bez neho by výplň rástla pri každej ceste
  von a späť.

**Sprievodca sú dva dialógy za sebou, nie `PropertySheet`.** Tu už naozaj ide
o dva kroky — druhá stránka ukazuje plán postavený z odpovedí prvej —, ale
každý z nich zostáva **jeden dialóg zo šablóny**: jedna rola, ktorú čítačka
ohlási, jedno poradie Tab a jedno Enter. Odmerané na hotovom EXE, tak ako
6.27: trieda oboch je `#32770`, prepínače v `IDD_SPLIT` krúžia 1044 → 1045 →
1046 → 1044 a hore naopak, teda `WS_GROUP` na rámčeku `Čo patrí k sebe`
skupinu naozaj ukončuje, a Tab v `IDD_SPLITPLAN` ide plán → jednotky →
prepočítať → SPOLU.txt → OK → Zrušiť.

**Plán, ktorý sa vykoná, je vždy plán, ktorý používateľ videl.** Pole jednotiek
sa dá upraviť bez prepočítania a Rozdeliť by vtedy delilo podľa niečoho, čo na
obrazovke nebolo. Dialóg sa preto pri zmenených jednotkách **nezavrie**:
prepočíta plán, dá naň fokus a čaká na druhé stlačenie. Bez hlásenia zámerne —
odpoveďou na „prepočítať“ je ten plán, a čítačka ho v tej chvíli prečíta.

Či sú jednotky zmenené, sa **porovnáva textom, nesleduje sa `EN_CHANGE`**.
Nie je to vec vkusu: `WM_SETTEXT` posiela `EN_CHANGE` tiež, takže naplnenie
poľa v `OnInit` by prišlo ako „používateľ to upravil“ a prvé Rozdeliť by
namiesto delenia prepočítavalo. Porovnanie sa v tom pomýliť nemá ako.

**Spätný zápis do `SPOLU.txt` ide pred kopírovaním**, nie po ňom: kopírovanie
trvá a zlyhanie zápisu treba počuť, kým ešte znamená to, na čo sa používateľ
práve pozeral.

**Vstupný bod je hlásenie o kapacite, a nie len slovami.** `CheckCapacity`
končila radou „Rozdeľte priečinok na viac priečinkov a striedajte ich ako
diskety“; tá veta **už neplatí** a nahradilo ju pomenovanie položky v ponuke.
Okno navyše rozdelenie ponúkne rovno v tom hlásení, s priečinkom už vyplneným.
Aby to vedelo, pribudol `bool* tooBig` cez `VirtualDisk::Mount` →
`EurekaMachine::MountDisk` → `DiskChange`. Je to **hodnota, nie hľadanie
podreťazca v hláške**: hláška je text pre používateľa a rozhodovať sa podľa
nej by znamenalo, že jej preformulovanie ticho vypne ponuku.

Skratku sprievodca **nedostal**, tak ako `Spravovať sloty…`. Každá hostiteľská
skratka je kláves, ktorý Eureka už nikdy nedostane, a toto je úkon raz za čas.

Testy: `disk_test` má o päť skupín viac (spolu 214 kontrol) a bežia ďalej bez
ROM. Držia to, čo je o dátach: že sa každý súbor skopíruje **práve raz a bajt
na bajt** pod menom, ktoré sľúbil plán, že zdrojový priečinok má po rozdelení
presne toľko súborov ako pred ním, že neprázdny cieľ aj cieľ vnútri zdroja sú
odmietnuté a **nič v nich nevznikne**, a že `SPOLU.txt` prežije cestu von
a späť ako tie isté jednotky. Poistka overená mutáciou: keď sa vypne kontrola
prázdneho cieľa, padnú dve kontroly.

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
3. ~~`disk_layout` a testy, ešte bez GUI~~ — **hotové 9. 9. 2026**, aj
   s `src/cpm_disk.h`, kam sa presťahovala kapacita a skladanie mien.
4. ~~Sprievodca rozdelenia nad hotovou vrstvou.~~ — **hotové 9. 9. 2026**, aj
   s `src/disk_split.*`, teda vrstvou, ktorá kolekciu prečíta a plán vykoná.

Tým je 6.22 celá spravená a nič z nej nezostáva otvorené.

Zámok proti zápisu je hotový celý, model aj hostiteľská strana.

**Kde rozdeľovač nadviaže na hotový kód.** Keď sa priečinok na disketu
nezmestí, `VirtualDisk::CheckCapacity` už dnes vypíše prebytok v blokoch
aj v KiB, menuje tri najväčšie súbory a končí vetou „Rozdeľte priečinok na
viac priečinkov a striedajte ich ako diskety.“ To je presne tá rada, ktorú
má rozdeľovač nahradiť skutkom — tá hláška je jeho prirodzené miesto
vstupu a je to aj miesto, kde už kapacitná aritmetika stojí hotová.

**Tak sa to aj stalo, 9. 9. 2026.** Citovaná veta v hláške **už nestojí** —
namiesto rady menuje položku ponuky — a okno rozdelenie ponúkne rovno v tom
hlásení, s priečinkom vyplneným. Rozlíšiť tú jednu odmietnutú disketu od
ostatných vie cez `bool* tooBig`, nie podľa textu hlášky.

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

### 6.24 Disketa je objekt, nie recept — hromadné kopírovanie to odhalilo

*Otvorený zvyšok zostal v `HANDOFF.md` 6.24: režim `kopia` v `integration_test`,
teda celá cesta cez ROM, ktorá testom krytá nie je.*

Nahlásené majiteľom 29. 8. 2026 hneď po zámku proti zápisu, a je to
druhýkrát v ten deň **tichá strata dát**.

**Scenár.** Diskové funkcie (`Shift+F6`), označiť dva súbory (`F3`),
hromadné kopírovanie (`Shift+F4`). ROM žiada, aby bola **zdrojová disketa
chránená proti zápisu** — inak povie „zdrojový disk není chráněn proti
zápisu“ (`14269`) — potom nakopíruje do pamäte, koľko sa zmestí, a vypýta
si cieľovú disketu. Ďalej strieda: cieľ, zdroj, cieľ. Majiteľ dostal
„soubor nelze najít“ a „hromadné kopírování zrušeno“ a cieľová disketa
zostala **prázdna**, hoci prvý súbor sa zmestil celý.

**Príčina.** Slot rýchlej voľby s `*pamat` vyrobil pri **každom** vložení
novú prázdnu disketu. Prvá výmena na cieľ teda zapísala, druhá dostala
čistú disketu, ROM na nej nenašla súbor, ktorý sama založila, a prácu
zrušila. Reprodukované sondou krok za krokom, so stavom média čítaným
priamo z modelu: po prvej výmene `suborov=2`, po druhej `suborov=0`.

**Model, ktorý to rieši** (majiteľov, a je správny): disketa je objekt.
Sloty 1 až 9 sú miesta, kde diskety sú, kým nie sú v mechanike; mechanika
je desiate miesto. Vloženie je presun, pri ktorom sa nič nestráca. Z toho
vypadli tri veci:

- **Priečinková disketa sa neodkladá.** Jej obsah žije v priečinku: pri
  vytiahnutí sa doň zapíše, pri vrátení sa z neho načíta. Držať jej obraz
  by znamenalo, že zmena zvonka sa neprejaví — a to by tá istá disketa
  neurobila.
- **Polička na disketu bez slotu je pasca.** Skúsil som ju a je to tá istá
  chyba v menšom: drží jednu disketu, takže druhá odložená prvú ticho
  prepíše. Zrušená.
- **Disketa v pamäti bez slotu je rozhodnutie používateľa.**
  `MainWindow::ConfirmLosingDiskette` sa pýta pred **každou** cestou, ktorá
  by ju z mechaniky vytlačila — vloženie z priečinka, nová disketa, slot aj
  vysunutie. Voľby: uložiť do priečinka, zahodiť, nechať v mechanike.
  Slot jej dá dialóg rýchlej voľby tlačidlom „Sem vloženú disketu“, ktoré
  odteraz naozaj pridá druhú referenciu (`PostAssignSlot`), nie iba značku
  do nastavení. Pri ukončení sa emulátor pýta aj na diskety čakajúce
  v slotoch.

`DiskStash` drží diskety **na halde** a `Take` vracia ukazovateľ. Deväť
`VirtualDisk`ov po 800 KiB v poli je sedem megabajtov a tento objekt žije
na zásobníku vlákna: prvá verzia spadla na pretečení zásobníka skôr, než
stihla vypísať riadok (`0xC00000FD`).

**Odmerané sondou** na majiteľovom postupe: striedanie zdroj → cieľ →
zdroj → cieľ prejde, „soubor nelze najít“ nepríde a stroj povie „počet
okopírovaných souborů 2“.

### 6.31 Vypnutá Eureka zostáva v okne — dialóg preč, studený štart otvorený

**Rozhodnuté s majiteľom 9. 9. 2026.** Dovtedy vypnutie stroja zavrelo
celý emulátor: `WM_EMU_POWERED_OFF` ukázal `MessageBox` a hneď za ním
poslal `WM_CLOSE`.

Boli to dve chyby v jednom mieste. Text dialógu znel „na skutočnom
stroji by RAM aj hodiny zostali pod napätím a ďalšie zapnutie by
pokračovalo tam, kde ste skončili“ — to je opis toho, čo **nemáme
naprogramované**, teda implementačný detail v texte pre používateľa,
a nedá sa naň nijako odpovedať. A `WM_CLOSE` za ním bral stavu možnosť
byť stavom: vypnutie na tomto stroji **nie je koniec**, je to
pohotovostný stav, v ktorom RAM aj hodiny žijú ďalej.

Majiteľ k tomu pridal dôvod, ktorý siaha ďalej než pohodlie: keby sa
okno dalo minimalizovať, vypnutá Eureka by mohla **budiť, reagovať na
diár a odbíjať hodiny**. To sú veci, ktoré na skutočnom stroji vypnutie
prežívajú a firmvér ich obsluhuje — alarm zobudí vypnutý stroj, firmvér
ho obslúži a uloží ho späť (viď `power_down_marker` v `machine.h`).
Zavretý proces ich obslúžiť nemá ako.

Čo je spravené:

- **Vlákno stroja slučku neopúšťa.** `EmulatorThread::Run` sleduje
  prechod `machine.powered_off()` a hlási len zmenu; slučka beží ďalej
  a obsluhuje príkazy. Odtok zvuku pred „konec“ zostal, len sa už
  nerobí cestou von. Nič nemusí CPU zastavovať: `Step()` vráti `false`
  sám a strop dlhu nad ním pripne `guestClock` štvrť sekundy pred
  hodiny, ktoré sa nehýbu, takže zapnutie **nepreletí** čas strávený
  vypnutím.
- **Stav je počuť a stojí v titulku.** Tri klesajúce tóny na vypnutie,
  tri stúpajúce na zapnutie — štvrtý tvar vedľa klávesnice, diskety
  a zámku. Stroj síce povie „konec“ vlastným hlasom, ale to, čo za tým
  nasleduje, je ticho, a ticho znie rovnako ako spadnutý emulátor.
  Titulok odpovedá kedykoľvek potom a vypnutie v ňom **predbieha
  všetko ostatné**, lebo režim klávesnice prestal byť otázkou.
- **`Vypnúť Eureku` je vo vypnutom stave zošedené**, `Reset` vedľa neho
  nie — inak by jediný stav v tomto okne nemal nikde pomenovanú cestu
  von.
- **Vypnuté a uvoľnená klávesnica je jeden stav, nie dva** (doplnené po
  pripomienke majiteľa v ten istý deň). Vypnutý stroj nemá kam prijať
  kláves, takže držať mu klávesnicu by znamenalo, že každý kláves na nej
  je ticho — presne ten režim, ktorý nepočuť, okolo ktorého je celý
  `main_window.cpp` postavený — a NVDA by navyše spalo nad oknom, v
  ktorom nie je čo čítať. `SetReleased(true)` teda ide s vypnutím
  a `SetReleased(false)` so zapnutím, oboje **bez vlastného tónu**:
  hlási to tón napájania, lebo je to jedna udalosť, a dva klesajúce
  tvary za sebou sa rozoznávajú horšie než ktorýkoľvek z nich sám.
  `Shift+F11` v tom stave nerobí nič — `SetReleased` odmieta jediný
  smer, teda vzatie klávesnice späť, a jeho položka je zošedená, aby to
  čítačka povedala. Tiché odmietnutie je tu bezpečné len preto, že
  klávesnica **už je tam, kam by ju ten kláves dal**; pasca, do ktorej
  tento súbor padá, je kláves, ktorý zmení smer klávesov potichu, nie
  kláves, ktorý potichu odmietne nezmeniť nič.
  Vedľajší dôsledok, ktorý stojí za zapamätanie: `HostShortcutsActive()`
  je vo vypnutom stave pravdivé, takže `Reset` je **samotné `Ctrl+R`**
  bez `F11` a `RefreshShortcutText` prefix sníme sám. README aj Pomocník
  to tak hovoria.

Čo zostáva otvorené: **zapnutie je dnes studený štart.** `Reset()` maže
RAM, takže sa stroj rozbehne odznova a nenadviaže tam, kde používateľ
skončil. Na hardvéri je to naopak — a keďže objekt `EurekaMachine`
v procese celý čas žije aj s RAM, teplé zapnutie je odtiaľto na dosah:
nový vstupný bod, ktorý urobí to, čo `Reset()`, ale **nesiahne** na
`memory_` nad ROM, na `rtcRam_` ani na hodiny. Čo firmvér potom naozaj
urobí, sa **musí odmerať, nie odhadnúť** — sonda dnes nevie stroj
vypnúť a znovu zapnúť, takže k tomu patrí aj token do nej. Patrí to
k epicu `ea4-aip` (zachovanie RAM medzi behmi): tam je to o prežití
procesu, tu o prežití vypnutia, ale je to tá istá RAM a ten istý
magický `55AAh` na `C45Bh`.

Do tej chvíle platí: `Reset` je jediná cesta späť a je **studená**.
README aj Pomocník to hovoria slovami o svete („začne odznova“), nie
o kóde.

#### Doplnené 9. 9. 2026: teplé zapnutie je v modeli a je odmerané

Dva odseky vyššie prestali platiť: **sonda už stroj vypnúť a zapnúť vie**
a teplé zapnutie nie je „na dosah“, je urobené. Neplatí ani veta, že
`Reset` je jediná cesta späť — v modeli nie je; **v okne zatiaľ áno**, tá
časť 6.31 zostáva otvorená a je to zvyšok `ea4-aip.1`.

`EurekaMachine::Reset()` sa rozdelil na dve: `Reset()` zmaže RAM nad ROM,
`rtcRam_` a počítadlo cyklov a potom zavolá `PowerOn()`; `PowerOn()` vráti
do stavu po zapnutí všetko ostatné — MMU, časovače, periférie, CPU. Delí
ich to, čo si **nechávajú**, nie to, čo robia, a preto je to druhý vstupný
bod a nie parameter. Na skutočnom stroji je to hranica napájania: RAM aj
hodiny majú vlastný zdroj, ktorý vypínač nepretína (`GLOSSARY.TXT`).

**Odmerané sondou** (`diag_probe ROM DISK seq 60000000 . vypni zapni . .`),
a je to presne to, čo 6.15 predpovedal:

- `vypni` → `[vypnute, C45Ah=FF] konec`.
- `zapni` → `[bezi, C45Ah=00]` a **ani slovo**. Studený štart na tom istom
  mieste (`studeno`) povie `inicializace eureky`. Firmvér teda našiel
  `magic` `55AAh` na `C45Bh`, nevymazal `C43Ch`–`C508h` (1805E) a hlásenie
  z 18132 nespustil.
- `C45Ah` je po teplom zapnutí `00`: boot ho zmazal na 180D6, teda šiel
  vetvou „zapnuté rukou“. To je tá vetva, ktorou ísť má — pri bite 0
  v `rtc_status` by šiel cez obsluhu budíka a hneď späť do vypnutia
  (1D132). Preto `PowerOn()` `rtcStatus_` **nuluje**, aj keď hodiny inak
  nechá na pokoji; čo firmvér urobí s budíkom, ktorý dopadol počas
  vypnutia, je `ea4-oti` a musí sa to odmerať, nie dopísať sem.
- Po teplom zapnutí stroj normálne reaguje: `kC3` povie `komunikace`.

Drží to `integration_test … power`, ktorý po vypnutí zapne teplo a overí,
že hlásenie **nezaznelo**, a hneď za tým studeno, že zaznelo — bez toho
druhého by kontrola prešla aj strojom, ktorý sa vôbec nerozbehol. Overené
mutáciou: `PowerOn()` doplnený o mazanie RAM zhodí režim `power`.

Jedna vec, ktorá vyzerá ako drobnosť: `vypni` z **aplikácie** stroj
nevypne, akord je vec hlavného menu. Sonda to nezakrýva — vypíše `[bezi]`
a beží ďalej.

#### Ešte v ten deň: okno to už vie tiež, 6.31 je celá zavretá

Odsek vyššie napísal, že v okne je `Reset` stále jediná cesta späť. **Už
nie je** a celá 6.31 je tým uzavretá; otvorené na epicu `ea4-aip` zostáva
len prežitie **procesu**, teda snímka do súboru.

Ponuka `Stroj` má tri položky: `Reset` (`Ctrl+R`), `Zapnúť Eureku`
(`Ctrl+P`) a `Vypnúť Eureku` (`Ctrl+V`). Zapnutie a vypnutie sú **dva
príkazy, nie jedna prepínacia položka** — rozhodol majiteľ 9. 9. 2026.
Vždy je dostupný práve jeden z nich a druhý je zošedený, čo je ten istý
vzor ako `Vysunúť disketu` alebo `Zamknúť`: zošedený prvok stojí sám
a čítačka ho prečíta ako nedostupný. Položka, ktorá by menila názov pod
rukami, by za tú istú skratku raz vypínala a raz zapínala. `Reset` je
zapnutý v oboch stavoch, lebo je to studený štart, teda voľba v oboch,
nie cesta von z jedného.

`Ctrl+P` je v hostiteľskej tabuľke, takže Eureke neberie nič a vo
vypnutom stave platí aj bez `F11` — `HostShortcutsActive()` je vtedy
pravdivé. Jednorazovku míňa všeobecná vetva `WM_COMMAND`, nič zvláštne
sa preň nepridávalo.

Vlákno má `PostPowerOn` a príkaz `kPowerOn`. Na rozdiel od `kReset`
**nesiaha na zvukové zariadenie**: vypnutie ho odtieklo a počkalo naň,
a znovuotvorenie by len zahodilo, čo sa riadenie latencie naučilo.
`guestClock` sa tiež neopravuje — počítadlo cyklov teplé zapnutie
prežije a strop dlhu ho počas vypnutia držal štvrť sekundy pred hodinami,
ktoré sa nehýbali. Tón, titulok aj návrat klávesnice Eureke robí ten istý
`WM_EMU_POWERED_OFF`, ktorý 6.31 spravila obojsmerným; nepribudlo k tomu
nič.

README aj Pomocník hovoria oboje slovami o svete: zapnutie **nadviaže tam,
kde ste skončili**, `Reset` **začne odznova, ako po prepnutí vypínača
batérie**. To prirovnanie nie je ozdoba a nie je ani vymyslené: `INSTALL.2`
opisuje „Power Cut-off Switch“, drobný prepínač v dierke na ľavej hrane,
ktorý sa prepínal perom alebo skrutkovačom a odpájal batériu, a hovorí,
že po ňom je pri ďalšom zapnutí **prázdna pamäť aj hodiny**. To je presne
to, čo `Reset()` robí, a je to zároveň doloženie, prečo maže `rtcRam_`.
Prvá verzia tohto textu hovorila o „vybratí batérií“ — Eureka má jednu
batériu a tá sa nevyberá, opravil to majiteľ 9. 9. 2026 odkazom na
`HARDWARE.1` a `INSTALL.2`. Obe miesta výslovne dodávajú, že zavretie
emulátora je iná vec — pamäť sa zatiaľ nikam neukladá — aby sa „nadviaže“
nečítalo ako sľub, ktorý platí aj cez reštart procesu.

#### Titulok okna nemenuje klávesy — ani v jednej vetve

Zamietnuté majiteľom 9. 9. 2026. Titulok mal do vtedy v dvoch stavoch
z troch radu, ktorý kláves stav vráti: „vypnutá, **zapne ju Reset**“
a „klávesnica uvoľnená, **vráti ju Shift+F11**“. Teraz znie
`Eureka A4 — vypnutá — disketa: …`, resp.
`Eureka A4 — klávesnica uvoľnená — režim: … — disketa: …`.

Dôvod je ten istý ako pri zamietnutých popiskách z 31. 8. 2026, len
o riadok vyššie: titulok sa číta **nahlas pri každom `Alt+Tab`
a `NVDA+T`**, takže odpovedá na „čo to teraz je“ a každé slovo navyše
platí tú cenu znovu a znovu. Kláves patrí do ponuky a do Pomocníka, kde
sa prečíta raz a zámerne.

Druhá polovica dôvodu je, že **rada v stavovom riadku ticho hnije**: to
„zapne ju Reset“ prestalo byť pravdivé v tej istej hodine, keď pribudlo
`Ctrl+P`, a nič na to neupozornilo. Vetva o klávesnici bola najprv
opravená len tá prvá a druhá nechaná ako „mimo zadania“; majiteľ to
zamietol tiež — dva rovnaké riadky, jeden opravený a druhý nie, sú
horšie než tá pôvodná rada. Opravené sú obe.
