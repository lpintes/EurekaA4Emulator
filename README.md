# Eureka A4 Emulator 0.1.0

Natívny 64-bitový emulátor počítača Eureka A4 pre Windows. Používa pôvodný
`A4ROM.DMP`, vykonáva jeho strojový kód a prehráva 8-bitový DAC, takže hlas,
hudba a zvuky vznikajú tým istým algoritmom a z tej istej fonémovej databázy
ako v Eureke.

## Rýchly štart

1. Spustite `EurekaA4Emulator.exe`.
2. Vyberte priečinok, ktorý sa má správať ako 800 KiB disk Eureky. Výber
   sa dá zrušiť — Eureka potom beží bez diskety, tak ako skutočný stroj
   s prázdnou mechanikou. **Pýta sa len pri prvom spustení:** ďalšie razy
   sa vloží disketa, ktorú ste mali naposledy.
3. Počkajte na úvodnú vetu „inicializace eureky“.
4. Klávesnica Windows teraz ovláda emulovaný počítač.

Emulátor má vlastné okno s ponukou. **Ponuku otvára `F12`** — nie `Alt`
ani `F10`, tie patria Eureke: `Alt` je modifikátor braillovskej klávesnice
a `F10` je Eurekino „kde som“.

Keď potrebujete kláves pre Windows a nie pre Eureku:

- **`F11`** pustí do Windows **nasledujúci jeden kláves**. Hodí sa
  napríklad na `Alt+medzerník`, teda systémovú ponuku okna. Vysoký tón
  znamená, že je nachystaný, nižší, že sa minul.
- **`Shift+F11`** uvoľní klávesnicu **úplne**: do Eureky nejde nič a okno
  sa správa ako hociktoré iné okno Windows. Klesajúca dvojica tónov
  znamená, že klávesy Eureku opúšťajú, stúpajúca že sa vracajú. Kým to
  platí, je to napísané aj v titulku okna, takže sa na to dá kedykoľvek
  spýtať `NVDA+T`.

V priečinku `nvda-addon\` je **doplnok pre NVDA**, ktorý čítačku nad oknom
stroja uspí, kým klávesnicu vlastní Eureka — a len nad ním, takže ponuka
a dialógy sa čítajú ďalej. Bez neho si NVDA berie časť klávesov pre seba
a hovorí cez Eureku. Podrobnosti a inštalácia sú v `nvda-addon\README.md`.

`Alt+F4` okno **nezatvára** — patrí Eureke. `Alt+F1` až `Alt+F10` je rada,
ktorá funkciu len **pomenuje** a nespustí ju (`Alt+F4` povie „komunikace",
samotné `F4` do komunikácie vojde), takže sa ňou dá prejsť, čo kde je, a
diera v jej strede by bola na obtiaž. Keď `Alt+F4` chcete pre Windows,
stlačte najprv `F11`.

Naopak `F11` a `F12` si berie okno, hoci na externej klávesnici sú to platné
klávesy Eureky: `F11` je ROM operačného systému (`Alt+F11` povie „data
ROMu"), `F12` je nepoužité. Pošle ich ponuka **Klávesnica → Poslať Eureke
kláves**.

V braillovskom režime je z tých štyroch položiek aktívne len `F11`, lebo
ten kláves membrána má — je to „d-akord", medzerník + body 1, 4, 5.
Rovnako sa dá stlačiť aj priamo bodmi. `F12` akord nemá a `Alt` je na tej
klávesnici medzerník, ktorý akord už používa, takže zvyšné tri sú
zošedené.

Ukončenie a bezpečné uloženie disku: `F11`, `Ctrl+Q`. Reset: `F11`,
`Ctrl+R`. Úplný zoznam skratiek je v ponuke Pomocník alebo pod `F11`,
`Ctrl+H`. Nápoveda k prepínačom príkazového riadka: `--help`.

**Okno berie Eureke tri klávesy a nič viac:** `F12` (ponuka), `F11`
a `Shift+F11`. Ostatné skratky sú dvojhmatové — najprv `F11`, potom
`Ctrl` s písmenom. Bez `F11` idú tie klávesy Eureke, takže `Ctrl+H`
naozaj urobí to, čo `Ctrl+H` na skutočnom stroji. Po `Shift+F11` platia
rovno, lebo vtedy je klávesnica hosťova aj tak — a ponuka vtedy prefix
`F11` prestane ukazovať, aby neradila kláves, ktorý v tom stave nerobí nič.

Písmená sú `Ctrl+U I M Q R V K N D H`, k tomu `Ctrl+0` až `Ctrl+9` pre
rýchlu voľbu diskiet. Že je to `Ctrl` a nie `Ctrl+Shift`,
má dôvod: do `Ctrl+Shift` si svoje **globálne** skratky vešajú iné
programy a globálna skratka vyhrá nad oknom bez ohľadu na `F11`, takže
by kláves do emulátora vôbec nedošiel a bolo by to ticho.

Do ponuky sa dostanete vždy a bez prefixu, takže cesta ku všetkému je
otvorená aj vtedy, keď niektorú skratku zoberie cudzí program.

## Dve klávesnice

Eureka mala dve klávesnice a emulátor môže byť ktoroukoľvek z nich.
Prepína sa `F11`, `Ctrl+K` alebo v ponuke Klávesnica:

- **externá klávesnica** — takto sa štartuje. Píše sa normálne;
  emulátor posiela scancody po sériovom porte a ROM si ich prekladá sama.
  Pozor, ROM čaká **českú** klávesnicu: `z` a `y` sú prehodené,
  nezhiftovaná číselná rada dáva `ěščřžýáíé` a číslice sú až so shiftom.
- **braillovská klávesnica** — dvadsať klávesov, ktoré stroj naozaj mal.
  `F D S` sú body 1, 2, 3 a `J K L` body 4, 5, 6, medzerník je medzerník;
  akord sa vydá pri pustení posledného prsta. Shift patrí do akordu: robí
  veľké písmeno a so samotným medzerníkom je Escape — kláves `Esc` stlačí
  presne ten akord, takže funguje aj on. **Písmená sa v tomto režime
  nepíšu** — píše sa bodmi, tak ako na stroji.

  Shift je tu aj **kláves sám osebe: stlačený samotný zastaví reč.** Takto
  sa pozastavuje plynulé čítanie v textovom procesore. Na externej
  klávesnici taký kláves nie je — tam reč zastaví každý kláves, ktorý niečo doručí,
  a shift ani Ctrl medzi ne nepatria; preto sa tam čítanie prerušuje
  šípkou, ktorá navyše nechá kurzor tam, kde sa prestalo čítať.

V oboch režimoch fungujú `F1`–`F10` so shiftom, kurzory aj ich akordy
(`Home`, `End`, `PgUp`, `PgDn`, `Insert`, `Delete`).

Dá sa štartovať rovno v braillovskom režime prepínačom `--braille`.

Eureku možno vypnúť aj tak, ako sa vypínala naozaj: v hlavnom menu
podržte **všetky štyri kurzorové klávesy naraz**. Stroj zahlási „konec“
a zhasne, disk sa pritom uloží. Vypne sa aj sám po piatich minútach
nečinnosti a tridsať sekúnd vopred to ohlási tónmi.

Emulátor má **len svoje okno**. Žiadna konzola pri spustení nevyskočí a
nič vám nezoberie zameranie.

Konzola sa otvorí jedine vtedy, keď o ňu požiadate: pri `--diag`, pri
`--help` a keď diagnostiku zapnete v Nastaveniach. Je to diagnostická
plocha, nie používateľské rozhranie — tým je zvuk. Keď emulátor spustíte
z už otvoreného príkazového riadka, píše sa rovno doň a nové okno
nevzniká.

Všetko, čo sa týka vás a nie ladenia — chyba pri štarte, súbory, ktoré sa
na disketu nezmestili, zlyhanie zvukového zariadenia, ukladanie diskety —
sa ohlási dialógom, ktorý čítačka prečíta.

Program sa dá spustiť aj z príkazového riadka:

```text
EurekaA4Emulator.exe --rom A4ROM.DMP --disk D:\MOJ_EUREKA_DISK
```

Ak sa `--rom` neuvedie, ROM sa hľadá postupne v premennej prostredia
`A4ROM`, vedľa EXE, o úroveň vyššie a v aktuálnom priečinku. Keď ju
nenájde, vypíše, kde všade hľadal. Ak sa neuvedie `--disk`, vloží sa
disketa z minulého spustenia; systémový výber priečinka sa zobrazí len
vtedy, keď si emulátor nemá čo pamätať.

Disketa je nepovinná:

- `--no-disk` spustí Eureku bez diskety a bez pýtania. Diskové funkcie
  ohlásia chybu disku, všetko ostatné funguje.
- `--ram-disk` vloží prázdnu naformátovanú disketu, ktorá existuje len
  v pamäti. Ak na nej pri ukončení nejaké súbory sú, emulátor sa spýta,
  či ich uložiť do priečinka; ak odmietnete alebo výber zrušíte, obsah
  zanikne.

## Výmena diskety za behu

Disketa sa dá vymeniť bez toho, aby ste Eureku ukončili: ponuka
**Disketa → Vložiť z priečinka…**, alebo `F11`, `Ctrl+I`. Vysunúť sa dá
tou istou ponukou.

Na skutočnom stroji bola výmena bezstarostná vec a tu je to rovnako:
EurekaDOS si nový disk prihlási sám, lebo si ku každému záznamu adresára
drží kontrolný súčet a podľa neho výmenu zbadá. Nemusíte teda nič
resetovať.

Emulátor pritom stráži dve veci, ktoré vy strážiť nemusíte:

- disketu vymení až medzi dvoma sektormi, nikdy uprostred zápisu,
- a všetko, čo Eureka na starú disketu zapísala, najprv uloží do jej
  priečinka.

Vloženie a vysunutie **počuť** — vloženie je dvojica tónov nahor,
vysunutie jeden nízky. Sú zámerne iné než tie, ktoré ohlasujú prepnutie
klávesnice. Čo je práve v mechanike, hovorí aj titulok okna, takže sa na
to dá kedykoľvek spýtať cez `NVDA+T`.

**Zmena diskety sa na nič nepýta**, ani keď je v mechanike disketa
v pamäti. Vymieňať je bežná práca — napríklad práve preto, aby ste mali
odkiaľ kopírovať — a otázka pred každou výmenou by zavadzala. Disketa
v pamäti sa výmenou stratí, ale to, že v mechanike je, hovorí titulok
okna celý čas.

Na uloženie sa emulátor spýta **pri ukončení**, čo je posledná chvíľa,
keď to má zmysel. Uložiť ju môžete aj kedykoľvek predtým cez `F11`,
`Ctrl+U`.

Pri ukladaní **priečinok nemusí existovať**. Dialóg má pole na názov,
tak ako pri ukladaní súboru: napíšete meno a priečinok sa vytvorí. Nie
je teda nutné ho najprv niekde založiť a potom sa poň vracať.

## Nová disketa

Ponuka **Disketa → Nová disketa…** (`F11`, `Ctrl+M`) vyrobí disketu
štyroch druhov:

- **z existujúceho priečinka** — to isté ako Vložiť z priečinka,
- **nový prázdny priečinok** — vytvorí sa a hneď vloží; taká disketa
  prežije aj ukončenie emulátora,
- **prázdna v pamäti, naformátovaná** — pripravená na písanie, ale
  zanikne s emulátorom (pri ukončení sa emulátor spýta, či ju uložiť),
- **prázdna v pamäti, nenaformátovaná** — Eureka ju ohlási ako **vadný
  disk**, presne ako skutočná nová disketa z krabice.

Tá posledná nie je len pre zábavu: je to jediné médium, na ktorom
formátovanie **naozaj niečo robí**. Disketa z priečinka je hostiteľský
priečinok a formátovanie na nej zámerne nemaže vaše súbory, takže
`Shift+F8` na nej prejde a nezanechá stopu. Na nenaformátovanej diskete
v pamäti prebehne celé a disketa začne fungovať.

Formátovanie sa pýta **dvakrát**: „mám formátovat disk, ano nebo ne?"
a potom „disk je už naformátován, přeformátovat, ano nebo ne?" — tú
druhú otázku dostanete vždy, aj na úplne prázdnej diskete. Odpovedá sa
klávesom **`y`**, nie `a`: otázka je česká, ale kláves anglický.

**Pozor na to, ktorý kláves `y` naozaj je.** Eureka má vlastnú tabuľku
klávesnice a tá je **česká QWERTZ**, takže `y` leží na klávese, ktorý má
americká klávesnica ako `Z` (scancode `2C`). S klávesnicou QWERTZ teda
stlačíte jednoducho `Y`; s **QWERTY** musíte stlačiť kláves označený
`Z`. Rozhoduje fyzická klávesnica, nie rozloženie nastavené vo Windows —
emulátor posiela stroju polohu klávesu a preklad si robí ROM sama.
Platí to pre celé písanie, nielen pre túto otázku.

Titulok okna hovorí, čo je v mechanike, takže `NVDA+T` odpovie
kedykoľvek. Disketa v pamäti je v ňom vždy „v pamäti" — či je
naformátovaná, alebo nie, je vec, ktorú práve meníte, nie to, čo tá
disketa je.

V dialógu sa dá novej diskete rovno priradiť **slot rýchlej voľby**.
Zoznam pri každom slote ukáže, čo v ňom práve je, a keď doň priraďujete
cez niečo, čo tam už bolo, emulátor sa ešte spýta. Pri nenaformátovanej
diskete je výber slotu neprístupný — nenaformátovaná je stav, ktorý trvá
po prvé `Shift+F8`, takže pod skratkou by nedával zmysel.

## Rýchla voľba diskiet

Deväť diskiet sa dá mať poruke pod `F11` a číslom s `Ctrl`:

- `F11`, `Ctrl+1` až `Ctrl+9` vloží disketu z príslušného slotu,
- `F11`, `Ctrl+0` disketu vysunie.

Sloty sa priraďujú v ponuke **Disketa → Spravovať rýchlu voľbu…**.
V dialógu si vyberiete slot a buď mu priradíte priečinok, alebo doň
uložíte disketu, ktorá je práve vložená. Zmeny sa zapíšu až tlačidlom OK.
Slot sa dá priradiť aj rovno pri vytváraní novej diskety.

V slote môže byť aj **disketa v pamäti**. Pozor na to, čo to znamená:
taká disketa nikde neexistuje po ukončení emulátora, takže slot ju
nevracia — **vyrobí novú prázdnu**. Preto sa v ponuke aj v dialógu volá
„nová prázdna v pamäti". Je to rýchly spôsob, ako mať čistú disketu na
jeden hmat.

Čo je v ktorom slote, **je napísané priamo v ponuke Disketa**, takže sa to
dá prečítať aj bez toho, aby ste si to pamätali: položka `3 Slovník`
znamená, že `F11`, `Ctrl+3` vloží Slovník. Prázdny slot je označený ako
`(prázdny)` a keď ho stlačíte, emulátor povie, kde sa napĺňa.

Sloty sú **stabilné**: menia sa len vtedy, keď ich zmeníte vy. Zámerne
sa nenapĺňajú automaticky z histórie — slot, ktorý sa mení pod prstom, by
bol horší než žiadny.

Skratky platia až po `F11`, tak ako všetky ostatné hostiteľské skratky.
Vďaka tomu deväť diskiet poruke nestojí Eureku ani jeden kláves.

## Čo si emulátor pamätá

Dve veci: **disketu, ktorú ste mali naposledy**, a **deväť slotov rýchlej
voľby**. Posledná disketa sa vloží pri ďalšom štarte, takže sa program
nepýta na priečinok pri každom spustení; zapíše sa vždy, keď disketu
vložíte — pri štarte, pri výmene za behu aj z rýchlej voľby. Pamätá sa
len disketa z priečinka — disketa v pamäti nemá čo obnovovať.

Keď priečinok medzitým zmizne (typicky odpojený USB disk), Eureka
naštartuje s prázdnou mechanikou a spýta sa, či si ho má pamätať aj
naďalej. Odpoveď **Áno** má zmysel práve pri odpojenom disku; **Nie**
záznam zabudne.

Nastavenia sú v obyčajnom textovom súbore `nastavenia.txt`, ktorý sa dá
otvoriť v poznámkovom bloku. Emulátor si ho hľadá na dvoch miestach:

- v priečinku **`config`** vedľa `EurekaA4Emulator.exe`, ak taký priečinok
  vytvoríte — to je prenosný režim, vhodný na USB kľúč,
- inak v `%APPDATA%\EurekaA4`.

Priečinok `config` si emulátor nikdy nevytvorí sám; prenosný režim je tak
vždy vaše rozhodnutie a nezapne sa omylom. Súbor emulátor prepisuje celý,
takže vlastné poznámky v ňom neprežijú.

## Spúšťanie súborov

### COM

Stlačte `Shift+F7` (pôvodná funkcia Run Disk Program), napíšte názov bez
prípony a potvrďte Enterom. Napríklad súbor `READ.COM` sa spustí zadaním
`READ`.

### BAS

Stlačte `F6`, čím sa otvorí pôvodný Eureka BASIC. Potom zadajte:

```text
LOAD "BEEP"
RUN
```

Súbor musí byť v natívnom tokenizovanom formáte Eureka BASIC. Štandardná
Eureka používa hlavičku `C2`, Advanced Eureka hlavičku `E2`; obyčajný textový
Microsoft/QBASIC súbor s rovnakou príponou nie je ten istý formát.

Ostatné pôvodné typy súborov sa používajú cez ich aplikácie v ROM presne ako
na Eureke. Prípona sama osebe nerobí z DOS/Windows programu program pre
HD64180; spustiteľný `.COM` musí byť určený pre EurekaDOS/CP/M 2.2.

## Klávesnica a kódovanie

- `F1` až `F10`, kurzory, Home, End, Page Up/Down, Insert a Delete sa mapujú
  na pôvodné kódy Eureky; fungujú aj kombinácie Shift a Alt.
- Kurzorové klávesy sú na Eureke **jedna klávesnica, nie štyri klávesy**:
  ROM ich číta ako množinu naraz stlačených. Preto sa dajú aj chordovať —
  podržte dva a viac naraz a Eureka dostane ich kombináciu, keď posledný
  pustíte. Všetky štyri sú príkaz „vypni sa“.
- Bežný Unicode text z Windows sa prevádza do presnej znakovej sady
  Kamenických/KEYBCS2 (často nazývanej CP895), nie do CP852.
- Podporované sú české a slovenské písmená vrátane `č ď ľ ĺ ň ô ŕ ř š ť ž`.
  Moderné úvodzovky, pomlčky a tri bodky sa bezpečne normalizujú. Znak mimo
  8-bitovej sady sa prevedie na základné písmeno alebo `?`.
- Výstup z Eureky sa opačne dekóduje do Unicode, takže diagnostický výpis
  na konzole korektne číta aj NVDA.

Emulátor nemení obsah programov ani ich textové dáta fonetickými náhradami.
Výslovnostná oprava anglického slova `source` zostáva v samostatnom NVDA
doplnku; tu je prioritou verná činnosť ROM.

## Priečinok ako disk

- Kapacita a geometria zodpovedajú Eureke: 160 logických stôp, 40 záznamov
  na stopu, 128 bajtov na záznam (800 KiB).
- Horná úroveň vybraného priečinka sa pri štarte prevedie na disk CP/M.
- Názvy sa prevedú na veľké 8.3; diakritika v názve sa zloží a kolízie dostanú
  príponu `~1`, `~2` atď.
- Zápisy Eureky sa ukladajú späť do priečinka vždy, keď sa disk na sekundu
  utíši. Nie na pevný takt — obraz zapísaný uprostred úpravy adresára by
  bol roztrhnutý a súbor práve prepisovaný by vyzeral ako zmazaný.
- Súbor zmazaný v Eureke sa kvôli obnove presunie do `.eureka-trash`, nemaže
  sa nevratne.
- **Podpriečinky sa ignorujú, a to bez hlásenia.** Je to zámer, nie chýbajúca
  funkcia: CP/M žiadne adresáre nepozná, takže do diskety niet kam ich dať.
  Prevedie sa len horná úroveň; podpriečinok aj s obsahom zostáva
  v hostiteľskom priečinku nedotknutý a Eureka o ňom nevie. Ak teda niektorý
  súbor na diskete chýba, pozrite sa, či nie je v podpriečinku.
- Nezmestí sa toľko, koľko by veľkosť priečinka naznačovala: CP/M prideľuje
  miesto po blokoch 2 KiB, takže aj 300-bajtový súbor zaberie celý blok.
  Na disk sa vojde 396 blokov a 256 položiek adresára. Keď sa priečinok
  nezmestí, emulátor povie koľko blokov a položiek by bolo treba.

Pred priamou úpravou súborov vo vybranom priečinku emulátor ukončite, aby sa
neprepísal obsah pripojeného obrazu.

## Čo sa emuluje

- inštrukčné jadro Z80/HD64180 a rozšírené opkódy Z180;
- 19-bitový adresný priestor, ROM/RAM a MMU registre CBR/BBR/CBAR;
- PRT0/PRT1, interné vektorované prerušenia a oba kanály DMA;
- braillová maticová klávesnica a rozhranie klávesnice PC/XT;
- WD1772 na úrovni sektorov a CP/M BIOS na úrovni 128-bajtových záznamov;
- RTC, napäťové komparátory, napájacie a výstupné registre;
- 8-bitový DAC s rekonštrukčným filtrom, pôvodný syntetizátor, hudba
  a zvuky cez Windows `waveOut`;
- základné stavové registre ASCI a CSI/O potrebné na štart ROM.

Externý modem, telefónna linka a fyzický sériový kábel zatiaľ nemajú most na
zariadenia Windows; ich vstupy zostávajú v bezpečnom pokojovom stave. Formátovanie
hostiteľského priečinka prebehne a stroj ho ohlási ako dokončené, ale obsah
stopy sa zahadzuje: disk je váš priečinok a formát v ňom súbory nemaže. Na
diskete v pamäti sa formátovanie naopak prejaví celé — viď Nová disketa. Exotické viacsektorové
príkazy WD1772 sú modelované len v rozsahu, ktorý používa dodaná ROM. Táto verzia preto nie je náhradou meracieho
emulátora na overovanie presného časovania externých periférií.

## Diagnostika

Prepínač `--diag` zapne záznam hardvérových prístupov, ktoré model neobsluhuje:

- zápisy do pamäte pod `kRamBase`, zoskupené po 4 KiB stránkach s prvým PC;
- externé porty bez modelu;
- interné registre Z180, ktoré sa len ukladajú a nemajú správanie;
- zmeny troch riadiacich latchov po jednotlivých bitoch;
- kruhový záznam posledných 512 udalostí;
- záznam každej klávesovej udalosti aj s tým, čo z nej emulátor urobil.
  To je jediný spôsob, ako odlíšiť kláves, ktorý sa zámerne ignoruje —
  napríklad písmeno v braillovskom režime — od klávesu, ktorý vôbec
  neprišiel. Oboje je inak ticho.

Výpis: `F11`, `Ctrl+D` počas behu, a tiež pri ukončení. Zapnúť sa dá aj
za behu v Nastaveniach (`F11`, `Ctrl+N`); vtedy sa otvorí okno konzoly, do
ktorého sa dá čítať. Bez `--diag` nemá záznam žiadnu réžiu. Keď je
diagnostika vypnutá, `Ctrl+D` to povie dialógom — konzola vtedy ešte
neexistuje, takže hlásenie do nej by zmizlo a skratka by vyzerala pokazene.

Obe otázky, kvôli ktorým vznikol, sú medzitým zodpovedané: zapisovateľná
RAM začína na `0x70000` a na sériovom porte Z180 visí klávesnica IBM PC.
Zostáva ako nástroj na hľadanie portov a zápisov, ktoré model neobsluhuje —
naprieč všetkými aplikáciami v ROM dnes nehlási žiadny port bez modelu.

## Zostavenie

Vyžaduje **mingw64 z msys2** (balík `mingw-w64-x86_64-gcc`). Predvolene sa
hľadá v `C:\msys64\mingw64`; iné umiestnenie sa podstrčí premennou
prostredia `MINGW64`. Z ľubovoľného príkazového riadka spustite:

```text
build.bat
```

Výsledok bude v `bin\EurekaA4Emulator.exe`; `build\` obsahuje len
medzivýstupy prekladu. Oba priečinky sú mimo verziovania. Spustiť sa dá
aj cez `Spustit-Eureku.bat`, ktorý si EXE nájde v `bin\`.
EXE je linkované staticky, takže beží aj mimo msys2 a nepotrebuje
žiadne mingw DLL.

Súbor `.clangd` v koreni je pre editory a jazykové servery, na preklad
nemá vplyv. Bez neho hlási clangd chyby v kóde, ktorý sa prekladá čisto,
lebo do `Makefile` nevidí a domyslí si iný štandard aj iné hlavičky.

Doplnok pre NVDA sa prekladá zvlášť:

```text
build-addon.bat
```

Výsledok je `bin\eurekaA4Emulator.nvda-addon`. Preklad hlások potrebuje
`msgfmt` z msys2 (`C:\msys64\usr\bin`); bez neho sa doplnok zostaví, len
bez prekladu. `build-addon.bat scratchpad` modul namiesto toho nakopíruje
do vývojového priečinka NVDA.

ROM nie je súčasťou licencie zdrojového kódu; pozrite `ROM-NOTICE.txt`.
Použité jadro Z80 má vlastné MIT oznámenie v `src\LICENSE.superzazu-z80.txt`.
Doplnok v `nvda-addon\` je pod GPL v2+, lebo NVDA považuje doplnky za
odvodené dielo; pozrite `nvda-addon\COPYING.txt`.

Technické referencie k procesoru: [Hitachi HD64180 User's Manual](https://www.bitsavers.org/components/hitachi/64180/HD64180_Users_Manual_Oct85.pdf),
[Zilog Z180 User Manual](https://www.zilog.com/docs/z180/um0050.pdf) a
[Z180 Product Specification](https://www.zilog.com/docs/z180/ps0140.pdf).
