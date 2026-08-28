# Pokyny pre agentov

## Najprv si prečítaj HANDOFF.md

`HANDOFF.md` je nosič kontextu tohto projektu: čo je zistené, čím je to
doložené, čo je otvorené a v akom poradí pokračovať. Bez neho budeš
znovu odvodzovať veci, ktoré sú už overené. Mapa hardvéru je
v `hardware-map.md`.

## Máme oficiálny manuál — pozri doň skôr, než začneš odvodzovať

V `eurekatech/` je **Eureka A4 Technical Manual** od Robotronu aj
s vývojárskou diskétou. Nie je to doplnok, je to **prameň**: príloha H
(`TECHMAN1/IOPORT.H` a `IOPORT.LIB`) je úplná mapa externých I/O portov
s názvami signálov a bitovými maskami, `DEVICES.10` má volania
zariadení, `SYSRAM.A` pomenované premenné, `MEMMAP.E` mapy pamäte
a `FILE-FMT.D` formáty súborov.

Dvakrát sa už stalo, že odvodenie zo samotnej ROM vyzeralo doložene
a bolo nesprávne — vždy tak, že si **susednosť pomýlilo s príčinnosťou**
(prehliadnutý swap registrov, zápisy do DAC vedľa nesúvisiaceho bitu).
Manuál to oba razy rozhodol za pár minút. Keď narazíš na port, bit alebo
volanie, hľadaj najprv tam.

Pozor: obsah `eurekatech/` je materiál tretích strán (Robotron
a Borland) a MIT licencia projektu sa naň nevzťahuje. Viď
`eurekatech/PUVOD.md`.

## O používateľovi

Používateľ je **nevidiaci**, pracuje s čítačom obrazovky (NVDA), hovorí
po slovensky. Z toho plynie:

- Odpovedaj po slovensky.
- Formátuj pre čítač obrazovky: nadpisy a zoznamy áno, ASCII grafika a
  zarovnávanie medzerami nie. Tabuľky používaj striedmo.
- Neodkazuj sa na to, „čo vidno na obrazovke". Výstup nástrojov, ktorý je
  dôležitý, zopakuj v texte.
- Pri tomto projekte je zvuk celé používateľské rozhranie emulovaného
  stroja, nie doplnok. Zmeny, ktoré zhoršia zvuk, sú vážne.

## ROM

Dump je **mimo repozitára** na `C:\b\a4rom.dmp` (262144 B, MD5
`9aa101ab69fc367e114e1a84b08feea1`). Je to firmvér — nikdy ho
necommituj a nikam ho neposielaj. `.gitignore` vylučuje `*.dmp` aj celý
`audio/`, lebo tie WAV sú prevedený obsah ROM.

Nástroje ho hľadajú cez premennú prostredia `A4ROM`.

## Adresy

Adresy sú v celom projekte **fyzické**, teda offsety v dumpe, pokiaľ nie
je výslovne napísané inak. Logická adresa závisí od stavu MMU; pri
common area 1 s CBR=`0Bh` platí fyzická = logická + `B000h`.

## Zostavenie

Prekladá sa **mingw64 z msys2**, ktorý je na stroji v
`C:\msys64\mingw64\bin`. Iný prefix sa dá podstrčiť premennou
`MINGW64`. Visual Studio už netreba.

Emulátor: `build.bat` z ľubovoľného príkazového riadka — cestu k mingw
si predradí sám a na globálny PATH sa nespolieha. Hotové EXE ide do
`bin\`, medzivýstupy do `build\`; oba sú v `.gitignore`.

Tri veci, bez ktorých sa emulátor nezlinkuje alebo nepobeží správne:
`-municode` (vstupný bod je širokoznakový), `-mwindows` (GUI subsystém,
vstupný bod je preto `wWinMain`, nie `wmain`) a
`-static -static-libgcc -static-libstdc++`, inak EXE pýta mingw DLL
a mimo msys2 shellu sa nespustí.

Testy a sonda `-mwindows` **nemajú** — sú to konzolové programy s `wmain`.

Zdroje okna (`src/res/eureka.rc`) prekladá `windres` — ponuka,
akcelerátory, šablóny dialógov a manifest. Dve veci na ňom:
`--codepage=65001`, lebo `.rc` je v UTF-8 a sú v ňom slovenské reťazce
(bez toho by windres čítal bajty ako ANSI a diakritika by sa do zdrojov
dostala rozsypaná, a **potichu**), a `--include-dir src/res`, aby vedľa
`.rc` našiel `resource.h` aj `eureka.manifest`. Manifest si pýta Common
Controls 6, preto pribudlo `-lcomctl32` a `InitCommonControlsEx`.

Keď na `.rc` siahneš, over výsledok na hotovom EXE, nie na zdrojáku —
reťazce sú v ňom v UTF-16, takže `s.encode('utf-16-le') in data`
povie, či prežili. Sám preklad nezlyhá ani keď ich rozsype.

`z80.c` sa prekladá ako C, nie C++; `Makefile` to už rieši.

Doplnok pre NVDA: `build-addon.bat`. Nie je v `Makefile` a nemá to byť —
je to zip a jeden preklad `.po` → `.mo` cez `msgfmt` z msys2, teda nič, čo
by malo závislosti alebo sa dalo zbytočne prekladať znovu. `build-addon.bat
scratchpad` nakopíruje modul do vývojového priečinka NVDA; to je vývojová
slučka, `NVDA+Ctrl+F3` ho načíta znovu. Pozor, v scratchpade nefunguje
`addonHandler.initTranslation()` (nie je to doplnok) — modul to ošetruje.

Sonda a testy: `build-tests.bat`. Zostaví `bin\diag_probe.exe`,
`bin\integration_test.exe`, `bin\codec_test.exe` a `bin\disk_test.exe`,
a linkuje ich proti objektom z `build\`.

Všetky tri dávky sú len obálky nad `Makefile` — nastavia PATH, dopočítajú
`-j` z počtu jadier a zavolajú `mingw32-make`. Meniť pravidlá prekladu má
zmysel v `Makefile`, nie v nich.

Objekty nikdy nie sú staršie než zdrojáky, ale drží to niečo iné než
kedysi. Kým tam bola podmienka na existenciu `build\machine.o`, testy sa
dali zlinkovať proti kódu, ktorý sa nepreložil, a meranie ukazovalo
správanie, ktoré už v zdrojáku nebolo. Riešilo sa to prekladom **všetkého**
pri každom spustení, čo bolo správne, ale hrubé: dávka nevedela, na ktorej
hlavičke ktorý objekt visí. Teraz to vie `make` z `.d` súborov, ktoré
vypisuje `gcc -MMD -MP`. Zastaraný objekt tak nevznikne a pritom sa
neprekladá viac, než treba — po dotyku na `machine.h` sa preložia presne
`machine.cpp` a `main.cpp`.

**Nepridávaj podmienky typu „ak už existuje, preskoč".** Sú zbytočné (od
toho je make) a presne takto vznikla tá pôvodná pasca.

Pozor na jednu vec v `Makefile`: `make` si shell vyberá podľa PATH — z
`cmd.exe` použije cmd, z bashu `sh.exe` z Gitu. Recepty preto nesmú
používať `copy`, `if not exist` ani `mkdir -p`. `mkdir build` funguje v
oboch, kopírovanie `READ.COM` nie, a preto ho robí `run-tests.bat`.

Pozor: `codec_test` a `disk_test` majú obyčajný `main`, takže sa prekladajú
**bez** `-municode`; s ním linker spadne na chýbajúcom `wWinMain`.

`disk_test` beží bez ROM aj bez diskového priečinka — testovacie priečinky si
generuje v `%TEMP%` a po sebe ich maže. Drží pravidlá kapacity diskety
(396 blokov po 2 KiB, 256 položiek adresára) a to, že sa žiadny súbor
nestratí potichu. Skutočný diskový priečinok na to nepoužívaj, mení sa pod
rukami.

## Spustenie testov

`run-tests.bat` zostaví testy a pustí všetkých deväť naraz — dva
samostatné testy a sedem režimov `integration_test`. Sú to nezávislé
procesy, nič nezdieľajú. Priečinok diskety si vyrobí čerstvý v
`build\testdisk` a skopíruje doň `eurekatech\TECHMAN1\READ.COM`, bez
ktorého režim `com` zlyhá. ROM berie z argumentu, inak z `%A4ROM%`, inak
`C:\b\a4rom.dmp`.

Výstup drží pohromade `--output-sync=target`; bez neho sa riadky deviatich
procesov premiešajú. `-k` nechá dobehnúť aj zvyšok po prvom zlyhaní.

**Pasca, do ktorej som už spadol:** režimy sa v `Makefile` generujú ako
výslovné pravidlá cez `foreach`/`eval`. Vzorové pravidlo `check-%` tam
najprv bolo a bolo tiché — `make` implicitné ani vzorové pravidlá na
`.PHONY` cieľoch nehľadá, takže sedem režimov zostalo bez receptu, make ich
vyhlásil za splnené a `run-tests.bat` ohlásil úspech bez toho, aby čokoľvek
z nich bežalo. Keď na tú časť siahneš, over počet riadkov `PASS` — musí ich
byť deväť — a raz to skús s nezmyselnou ROM, či poistka naozaj zvoní.

## Diagnostická sonda

`tests/diag_probe.cpp` je hlavný vyšetrovací nástroj. Nabootuje ROM
a vypíše, čo model hardvéru neobsluhuje.

```
diag_probe ROM DISK_FOLDER boot
diag_probe ROM DISK_FOLDER sweep 4000000
diag_probe ROM DISK_FOLDER seq 15000000 kD7 Y Y
diag_probe ROM DISK_FOLDER trace 20000000 formatovaci
```

Pred hádaním, čo firmvér robí, ho radšej spusti a pozri sa. Takto sa
našla chyba `BIT b,(HL)` v jadre aj to, kde presne viazlo formátovanie.

Sondu si pokojne dopĺňaj o dočasnú inštrumentáciu (počítadlá, záznam
udalostí, histogram PC) v kópii zdrojáka v scratchpade — repozitár tým
nešpiň. Takto sa odhalilo, že firmvér povolí DMA skôr, než vydá príkaz
radiču.

## Štýl kódu

Emulátor je C++20, dvojmedzerové odsadenie, `PascalCase` metódy, členy
s podtržníkom na konci. **Komentáre v zdrojákoch po anglicky**,
dokumentácia a používateľské reťazce po slovensky alebo česky.

Komentuj *prečo*, nie *čo*. Pri hardvérových predpokladoch pripíš
adresu v ROM, ktorá ich dokladá — inak ich nikto neskôr neoverí.

Python v `nvda-addon/` sa riadi **štýlom NVDA**, nie štýlom `tools/`:
tabulátory (nie medzery), `camelCase`, riadok do 110 znakov, LF, UTF-8 bez
BOM a každý reťazec pre používateľa cez `_()` s komentárom `# Translators:`
tesne pred ním. Je to vynucované u nich, nie u nás, ale doplnok, ktorý ich
štýl nedodrží, sa nedá poslať ďalej. A licencia je tam iná: **GPL v2+**,
lebo NVDA považuje doplnky za odvodené dielo (`nvda-addon/COPYING.txt`).

## Rozvrstvenie GUI

- `src/win/` je tenká obálka nad Win32 a **o emulátore nevie nič** —
  dá sa vziať do iného projektu tak, ako je. Nech to tak zostane.
- `src/emulator_thread.*` vlastní `EurekaMachine` aj `AudioPlayer` a beží
  na vlastnom vlákne. Okno sa stroja **nedotýka**, všetko mu posiela cez
  jednu frontu príkazov — preto tam nie je ani jeden zámok nad strojom.
  Klávesy idú tou istou frontou ako príkazy, aby si prepnutie režimu
  nepredbehlo kláves napísaný po ňom.
- Vlákno tam nie je kvôli poriadku. Rozbalená ponuka, modálny dialóg aj
  ťahanie okna si spustia **vlastnú správovú slučku**; jednovláknový
  emulátor by v nich stál a pri 22 ms latencie by sa reč zasekla uprostred
  slova. **Nevracaj emulátor do správovej slučky okna.**
- Dialógy sú **skutočné dialógy** zo šablón (`DialogBoxParamW`), nie okná,
  ktoré tak vyzerajú. Rolu „dialóg“ pre NVDA, poradie Tab, Esc, Enter
  a mnemoniky dáva správca dialógov; vlastné `WS_POPUP` okno nedá nič
  z toho. Overiť sa to dá triedou okna — musí byť `#32770`.
- Súradnice sa nepočítajú nikde. Rozloženie dialógov je v dialógových
  jednotkách v `.rc` a škáluje sa s fontom. Ak by niektorý dialóg pýtal
  layout engine, je príliš zložitý na dialóg.

## Klávesnica a kto ju vlastní

Okno posiela do Eureky **všetko**, takže bežné konvencie Windows sú
v konflikte s hosťom. Platí:

- **Alt neotvára ponuku** — je to modifikátor braillovskej klávesnice
  (`SpecialKey` mu nastavuje bit `0x20`). `WM_SYSKEYDOWN` sa preto
  spracuje a vráti 0. **Ani Alt+F4 nie je výnimka** — Alt+F1 až Alt+F10 je
  rada, ktorá funkciu **pomenuje a nespustí** (Alt+F4 je `E3h` a povie
  „komunikace“; samotné F4 do nej vojde — odmerané, viď nižšie). Je to
  nápoveda, po ktorej sa chodí, a diera v jej strede, ktorá zabije
  emulátor, stojí viac než štandardné zatváranie okna. Okno zatvára
  `F12` → ponuka Súbor → Skončiť — tá cesta je bezpodmienečná — alebo
  `F11`, `Ctrl+Q`; a po `F11` či `Shift+F11` funguje aj Alt+F4.
- **F10 ani Shift+F10 nie sú voľné** — Eureka nimi hovorí, kde ste, a robí
  sebakontrolu. Ponuku preto otvára **F12**, uvoľnenie klávesnice **F11**.
- **Voľný kláves neexistuje, ani F11 a F12.** Roky tu stálo, že sú to
  jediné klávesy, ktoré stroj nepozná — z toho, že `KB.H` končí na
  `K_F10`. Hlavička nie je stroj: tabuľka klávesnice PC v ROM (`1DF05`
  indexované scancodom) mapuje `57h` na `CAh` a `58h` na `CBh`. Odmerané
  na bežiacej ROM: **F11 je ROM operačného systému** (`Alt+F11` povie
  „data ROMu“), F12 je nepoužité (samotné mlčí, `Alt+F12` povie
  „nepouzito“). Každá hostiteľská skratka teda niečo hosťovi berie;
  tieto dve berú najmenej z toho, čo bolo v ponuke.
  Späť ich dáva podponuka **Klávesnica → Poslať Eureke kláves**, ktorá
  nestojí žiadny kláves a čítačka ju prečíta.
- **F11 má aj braillovská klávesnica, F12 nie.** Membrána má osem
  funkčných klávesov na riadku 1 a F9, F10 aj **F11** skladá z akordov
  s medzerníkom (`1D541`); F11 je „d-akord“, medzerník + body 1,4,5
  (`9Ch`). Odmerané: povie to isté „ROM operacniho systemu“ ako scancode
  `57h`. Preto `SpecialKey` končí na `VK_F11` a v podponuke je F11
  aktívne v oboch režimoch. Zošedené sú len F12 (akord nemá) a Alt+F11
  s Alt+F12 (Alt je medzerník a ten už akord používa).
- **Tabuľka akordov v `hardware-map.md` je zdroj pravdy, nie zoznam
  nápadov.** `d-akord` = `CAh` v nej stál roky a `PressMembraneKey` ho
  napriek tomu nemal — kód bol dosiahnuteľný bodmi a nedosiahnuteľný
  svojím kódom klávesu, a ten rozdiel bol **tichý**. Keď do tej funkcie
  siahneš, porovnaj ju s tou tabuľkou.
- **Akcelerátorové tabuľky sú dve a to je celý trik.** `IDR_ACCELERATORS`
  platí vždy a je to celá trvalá cena: `F11`, `Shift+F11`, `F12`.
  `IDR_ACCELERATORS_HOST` (`Ctrl+U Q R V K N D H`) platí **len keď je
  klávesnica hosťova** — po `F11` alebo `Shift+F11`. Bez toho prefixu idú
  tie klávesy Eureke, takže `Ctrl+H` na exterke naozaj urobí to, čo robí
  na stroji. Bránu vyhodnocuje `MainWindow::HostShortcutsActive()` a pýta
  sa na ňu `win::RunMessageLoop` pri **každej správe**, nie raz na štarte.
- **Text skratky v ponuke sa mení so stavom.** Po `Shift+F11` `F11` nerobí
  nič — `SetPassOnce` ho odmieta a jeho položka je zošedená — takže ponuka,
  ktorá by v tom stave stále písala `F11, Ctrl+R`, radí kláves, po ktorom
  ani nepípne. Prefix nasadzuje a sníma `RefreshShortcutText`
  z `RefreshMenu`. Písmená si číta späť z ponuky, takže ich `.rc` menuje ako
  jediný; keď na to siahneš, zachovaj to. Overiť sa to dá zvonka, ale len
  **po pozíciách** — `MF_BYCOMMAND` cez hranicu procesov vráti −1.
- **Kto pridá položku do tej druhej tabuľky, musí vedieť o jednorazovke.**
  Okno stlačenie nevidí (zje ho `TranslateAccelerator`), takže ho
  `HostKeepsKey` nemá na čom minúť a `F11` by zostalo nachystané navždy —
  presne ten tichý sticky režim. Míňa to `WM_COMMAND` v `main_window.cpp`
  podľa `HIWORD(wParam) == 1`, s výnimkou `ID_KEYBOARD_PASSONCE`
  a `ID_KEYBOARD_RELEASE`, ktoré ten stav vlastnia samy.
- **Hostiteľské skratky sú `Ctrl` s písmenom, nie `Ctrl+Shift`.** Nie je to
  vec vkusu: do `Ctrl+Shift` vešajú iné programy svoje **globálne** skratky
  (`RegisterHotKey`) a tie vyhrávajú nad akcelerátorovou tabuľkou okna, nech
  ju bráni čokoľvek — kláves do aplikácie nedôjde vôbec a je to **ticho**.
  Na stroji majiteľa takto roky nefungovali `Ctrl+Shift+H` a `Ctrl+Shift+R`.
  Diagnostika: skúsiť si kombináciu zaregistrovať sám; `RegisterHotKey`
  vráti chybu **1409** (`ERROR_HOTKEY_ALREADY_REGISTERED`), keď ju už
  niekto drží.
- **Akcelerátorové tabuľky sú jediný vlastník hostiteľských skratiek.**
  `TranslateAccelerator` ich zje skôr, než ich okno uvidí, takže preklad
  klávesov na vlákne o nich nevie a vedieť nemá. Nepridávaj druhú
  kontrolu skratky do prekladu.
- **Akcelerátor zje stlačenie, ale nie pustenie.** Pustenie príde do
  `WndProc` normálne. Pre stroj je to neškodné (break bez make), ale pre
  čokoľvek so stavom je to pasca: jednorazovka na F11 sa takto míňala sama
  hneď po nachystaní. Preto sa míňa len na klávese, ktorého **stlačenie**
  okno naozaj videlo. Rovnaká pasca zožrala akord v braillovskom režime.
- **Uvoľnenie klávesnice (`released_` v `MainWindow`) je stav okna, nie
  stroja.** Okno kláves jednoducho nepošle a nechá ho `DefWindowProc`.
  Pri každej zmene sa posiela `PostFocusLost()`, inak by hosťovi zostal
  visieť modifikátor, na ktorý už pustenie nikdy nepríde.

## Čítačka obrazovky je tretí hráč o klávesnicu

NVDA sedí nad aplikáciou v nízkoúrovňovom hooku, takže rozhoduje **skôr než
okno**. Nič v `main_window.cpp` preto nevie klávesu vziať čítačke — to sa dá
len dohodou, a tá je v `nvda-addon/`: doplnok uspí NVDA nad oknom triedy
`EurekaA4EmulatorWindow` a nikde inde, takže ponuka a dialógy sa čítajú ďalej.

Emulátor do toho hovorí jedinou vecou: vlastnosťou okna
`EurekaA4.KeyboardReleased` (`PublishKeyboardState`), ktorú si doplnok číta
cez `GetPropW` z druhého procesu. Overené na bežiacom procese — pri štarte
je `0`, po `Shift+F11` `1`, po vrátení zase `0`.

Tri veci, na ktoré si dať pozor:

- **Názov triedy okna a názov vlastnosti sú zmluva medzi dvoma projektmi.**
  Premenovanie ktoréhokoľvek doplnok vypne, a **potichu** — nič nezlyhá,
  NVDA len prestane spať. Oba názvy sú v `main_window.cpp` aj
  v `eurekaa4emulator.py` a musia si zodpovedať.
- **Jednorazovka (F11) sa zámerne nepublikuje.** Vyzerá to logicky — na
  jeden kláves patrí klávesnica hosťovi — ale prebudená čítačka by práve
  ten kláves zjedla ako svoj príkaz, okno by ho nikdy nevidelo a
  jednorazovka by zostala nachystaná navždy. To je ten istý tichý sticky
  režim, na ktorom sa už raz `HostKeepsKey` popálilo.
- **Kláves NVDA si čítačka necháva aj v spánku.** `Insert` je pre Eureku
  platný kláves (`8Dh`), takže sa doň dostane len dvoma rýchlymi
  stlačeniami za sebou (vlastná finta NVDA) alebo cez `Shift+F11`.
- **Ponuková lišta nie je okno**, patrí HWND hlavného okna. Objekty ponuky
  teda nesú triedu `EurekaA4EmulatorWindow` a spánok viazaný na triedu okna
  ich uspí tiež — ponuka je po `F12` ticho a ozve sa až prvá šípka, lebo
  rozbaľovacie menu už vlastné okno (`#32768`) je. Doplnok sa preto pýta aj
  na `GetGUIThreadInfo` a v režime ponuky nespí. Platí to pre čokoľvek
  ďalšie, čo bude žiť na tom istom HWND.

## Konzola je diagnostika, nie výstup

Emulátor je program GUI subsystému, takže **konzolu nemá**, pokiaľ si ju
nevypýta. Nevyžiadané okno konzoly pri štarte zoberie fokus a potom sa
musí hľadať Alt+Tabom — to je šum, nie informácia. `host_console.cpp`:

- `AttachToParentConsole()` na začiatku. Je zadarmo a neotvára okno; robí
  len to, že `--help` a `--diag` spustené zo shellu píšu tam, kam majú.
- `OpenConsole()` konzolu naozaj vyrobí, a preto je len pre výstup, ktorý
  si používateľ vyžiadal menom: `--diag`, `--help`, zapnutie diagnostiky
  v Nastaveniach.
- Štandardné handly sa preberajú **len keď tam nič použiteľné nie je**.
  Inak by `EurekaA4Emulator.exe --help > subor.txt` poslalo nápovedu na
  obrazovku namiesto do súboru.

**Z toho plynie pravidlo: čo je pre používateľa, nesmie ísť cez
`host::Print`.** Padlo by to do konzoly, ktorú nikto neotvoril, a zmizlo
by potichu. Patrí to do `MessageBox` — ten existuje vždy a čítačka ho
ohlási ako dialóg a prečíta. Takto sú riešené chyby pri štarte, preskočené
podpriečinky, zlyhanie zvuku a hlásenia pri ukladaní diskety.

Na konzolu patrí len: `--diag`, výpis diagnostiky, záznam klávesov,
konzolové zariadenie hosťa (a to sa vypisuje **iba** so zapnutou
diagnostikou) a `--help`.

Dve veci okolo toho, ktoré vyzerajú ako drobnosť a nie sú:

- **Nepripájaj sa k rodičovskej konzole pri štarte.** Vyzerá to zadarmo —
  žiadne okno nevznikne — ale konzola žije, kým je na ňu niekto pripojený.
  Spustené z dávky, ktorá potom skončí, by okno toho shellu zostalo visieť
  prázdne celú reláciu. Pripája sa až tam, kde sa naozaj píše: v
  `OpenConsole()` a v `Fail()` (a tam len preto, že proces hneď skončí).
- **`Spustit-Eureku.bat` musí volať `start`.** `cmd` na program GUI
  subsystému **čaká**, takže bez `start` zostane okno dávky na obrazovke
  celý čas behu — konzola je preč z emulátora a vráti sa zadnými dverami.
  Odskúšané oboje.

## Režim, ktorý nepočuť, je chyba

Klávesnica, ktorá nikam nechodí, a pokazená klávesnica sú **obe ticho** —
to je pasca, do ktorej tento projekt padá dokola. Preto každá zmena toho,
kam idú klávesy, znie: `Beep()` hostiteľa, ktorý sa nedá pomýliť
s Eurekiným hlasom z DAC. Klesajúca dvojica = klávesy Eureku opúšťajú,
stúpajúca = vracajú sa, krátky vysoký = jednorazovka nachystaná, nižší =
minutá. Stav navyše stojí v titulku okna, takže NVDA+T na neho odpovie
kedykoľvek, nie len v okamihu zmeny.

`Beep()` je synchrónny a to je v poriadku **len preto, že stroj beží na
vlastnom vlákne**. Keby sa emulátor niekedy vrátil do správovej slučky,
toto je jedno z miest, ktoré by ho zaseklo.

## Git

Commituj len keď o to používateľ požiada. Nepushuj bez vyzvania.

Konce riadkov drží `.gitattributes`, nie ty: `* text=auto` ukladá do
repozitára LF, `*.bat` zostáva CRLF (`cmd.exe` na LF-only dávkach vie
zlyhať na `if/else` a `goto`, a až za behu) a `eurekatech/**` je z
konverzie vyňatý úplne, lebo je v ňom osemnásť binárok z roku 1992 a
test `com` závisí na tom, že `READ.COM` je bajt na bajt z diskety.

Ak na ten súbor siahneš, over, že pravidlo pre `eurekatech/` zostalo
**posledné** — pri zhode viacerých vzorov vyhráva to nižšie. A keď
uvidíš diff, v ktorom je prepísaný celý súbor namiesto zmenených
riadkov, nie je to tvoja zmena, sú to konce riadkov.
