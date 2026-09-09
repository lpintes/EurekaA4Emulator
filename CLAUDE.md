# Pokyny pre agentov

## Dôležité: NA EDITOVANIE A VYTVáRANIE SÚBOROV NEPOUŽÍVAJ PYTHON ANI INÉ NEŠTANDARDNÉ NÁSTROJE

Robíš neakceptovateľne veľa chýb, heredoc to často kazí. Nevieš to, plytváš tokenmi a zdržuje to.

## Spôsob práce

Kvôli lepšiemu manažovaniu limitu postupuj inkrementálne. Najskôr spravíš časť práce, nejaký zmysluplný krok, zhrnieš čo si urobil a zastavíš sa. Po pokyne pokračuj alebo next urobíš ďalší krok.

## Najprv si prečítaj HANDOFF.md

`HANDOFF.md` je nosič kontextu tohto projektu: čo je zistené, čím je to
doložené, čo je otvorené a v akom poradí pokračovať. Bez neho budeš
znovu odvodzovať veci, ktoré sú už overené. Mapa hardvéru je
v `hardware-map.md`.

Vedľa neho je `HANDOFF-archiv.md` s uzavretými zásahmi aj s meraniami.
**Pri štarte ho nečítaj** — otvor ho, až keď robíš na téme, ktorú menuje
niektorý odsek v `HANDOFF.md`. Zásah sa doň sťahuje, len keď jeho záver
**niekde inde naozaj stojí** (v kóde, tu, v mape alebo v README), text sa
pri sťahovaní neupravuje, čísla `6.x` zostávajú platné — odkazuje sa na ne
zo zdrojákov — a otvorený zvyšok uzavretej témy zostáva v `HANDOFF.md`.
Kým záver inde nestojí, sekcia je otvorená, aj keď je práca hotová.

## Beads evidujú otvorenú prácu, HANDOFF nesie doloženie

Od 7. 9. 2026 je v projekte **beads** (`bd`, prefix `ea4`). Deľba je
jednoslovná: **bead je ukazovateľ na otvorenú prácu, HANDOFF je
doloženie.** Bead nesie názov, prioritu, závislosti a odkaz na sekciu;
prečo je niečo pravda a čím je to zmerané, stojí ďalej v `HANDOFF.md`
a čísla `6.x` sa **nemenia** — odkazuje sa na ne zo zdrojákov.

`bd ready` povie, čo sa dá robiť, `bd list --priority 1`, čo horí. Obsah
je aj v `.beads/issues.jsonl` — commitovaný text, čitateľný bez `bd`.
Samotná databáza je Dolt v `.beads/embeddeddolt` a do gitu nejde.

**Hook `bd prime` vkladá pri štarte relácie pravidlá, ktoré si s týmto
projektom v dvoch bodoch protirečia. Platí toto, nie ony:**

- „Do NOT use markdown files for task tracking" sa na `HANDOFF.md`
  **nevzťahuje**. Nie je to zoznam úloh, je to nosič doloženia a
  udržiava sa ďalej presne podľa sekcie vyššie.
- „Do NOT use MEMORY.md files" **neplatí**. Poznámky používateľa žijú
  v `MEMORY.md` a zostávajú tam. `bd remember` nepoužívaj: automatický
  export ho **nevyváža** (`--include-memories` je vypnuté), takže by
  vznikol obsah, ktorý nie je nikde v gite a stratil by sa ticho.

Sekcia sem nie je vložená šablónou — `bd setup claude` sa zámerne
nespustil, lebo jeho text hovorí to prvé z tých dvoch. Ak ho niekedy
spúšťaš, výsledok najprv prečítaj.

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

### Čísla portov a bitov sú v `src/eureka_io.h`, s menami z manuálu

Namiesto `0xa0` a `0x40` má kód `hw::kPowerLatch` a `hw::kVmselMask`.
Mená sú tie, ktoré používa ich prameň, len prepísané do `kPascalCase`, a pri
každej konštante je v komentári napísané, ktorý súbor v `eurekatech/` ju
dokladá. To je celý zmysel: meno sa dá grepnúť a spor o to, čo bit robí,
rozhodne Robotron.

Keď pridávaš konštantu, **nevymýšľaj meno, kým si nepozrel `IOPORT.LIB`,
`IOREG.LIB`, `SYSEQU.LIB` a `KB.H`**. Kde prameň nesiaha (príkazy WD177x,
adresy v ROM), patrí k menu odkaz na miesto, ktoré ho potvrdzuje.
`SYSEQU.LIB` pribudol 1. 9. 2026 s `fdc_verify`; nemá s ostatnými dvoma ani
jednu kolíziu, takže kontrolu iba rozširuje.

Hlavička na dvoch miestach zámerne drží **dve mená pre tú istú hodnotu** —
stavové bity WD177x znamenajú po príkaze typu I niečo iné než po prenose dát.
Nezlučuj ich. A `IOPORT.LIB` si s textom prílohy H protirečí v číslovaní
`bkb_row0`/`bkb_row2`; hlavička hovorí, ako je to rozhodnuté a prečo
(HANDOFF 6.29).

**Po zásahu do hlavičky spusti `python tools/check_io_names.py`.** Prehodená
maska prejde prekladom aj všetkými trinástimi testami — sú to nezávislé veci.
Tento skript porovná každú konštantu s tým, čo o nej hovorí manuál, a je
jediné, čo taký preklep chytí.

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

Preklad je jediná pravda o chybách. Ak ti editor alebo LSP hlási chyby
v kóde, ktorý `build.bat` preloží bez jediného varovania, pozri sa
najprv na `.clangd` — clangd do `Makefile` nevidí a bez neho prekladá
pod C++17, s MSVC hlavičkami z Visual Studia a s `-I`, ktoré si
vyhodnocuje voči priečinku súboru, nie voči koreňu. Sú to tri nezávislé
zdroje falošných chýb a `.clangd` rieši všetky tri. Overené na všetkých
vtedajších sedemnástich zdrojákoch cez `clangd --check` — nula chýb;
odvtedy pribudli `settings.cpp` a `settings_test.cpp`. Nemaž ho a
keď siahneš na prepínače v `Makefile`, zosúlaď ho; je to jediné miesto,
kde sú zdvojené.

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

Od 9. 9. 2026 má každé prekladové pravidlo aj **`Makefile` ako
prerekvizitu**. `.d` súbory sledujú hlavičky, nie prepínače, takže zmena
`CXXFLAGS` objekty nepreložila a `make` vyhlásil za hotové niečo, čo je
preložené inak, než hovoria pravidlá. Chytilo sa to hneď pri pridaní
`-ffunction-sections`: EXE nezmenilo veľkosť ani o bajt, lebo sa nič
neprekladalo. Je to tá istá pasca ako to „ak už existuje, preskoč“, len
tichšia — a udrie práve vtedy, keď sa meria dopad prepínača.

Emulátor sa linkuje s `-s` a `-Wl,--gc-sections`: prvé zahodí symboly,
druhé kód, na ktorý sa nikto neodkazuje (2 032 128 → 1 937 408 bajtov).
To druhé funguje len vďaka `-ffunction-sections -fdata-sections` pri
preklade — sú v premennej `SECTIONS`. Testy a sonda si symboly nechávajú
zámerne: keď spadnú, chce sa vedieť kde. A **`-Os` neskúšaj** — pri ňom
prestane byť presúvací konštruktor `std::string` inlinovaný a v libstdc++
ako samostatný symbol neexistuje, takže sa štyri jednotky vôbec
nezlinkujú.

Pozor na jednu vec v `Makefile`: `make` si shell vyberá podľa PATH — z
`cmd.exe` použije cmd, z bashu `sh.exe` z Gitu. Recepty preto nesmú
používať `copy`, `if not exist` ani `mkdir -p`. `mkdir build` funguje v
oboch, kopírovanie `READ.COM` nie, a preto ho robí `run-tests.bat`.

Pozor: `codec_test`, `disk_test` a `settings_test` majú obyčajný `main`, takže
sa prekladajú **bez** `-municode`; s ním linker spadne na chýbajúcom `wWinMain`.

`disk_test` beží bez ROM aj bez diskového priečinka — testovacie priečinky si
generuje v `%TEMP%` a po sebe ich maže. Drží pravidlá kapacity diskety
(396 blokov po 2 KiB, 256 položiek adresára) a to, že sa žiadny súbor
nestratí potichu. Skutočný diskový priečinok na to nepoužívaj, mení sa pod
rukami.

Drží aj **zásobník diskiet** (`DiskStash`): že slot vráti tú istú disketu aj
s jej obsahom, že sa sloty navzájom nemiešajú, že disketa s priečinkom sa
neodkladá a že slot 0 neexistuje. A od 6.26 aj **zámok**: že cestuje na
poličku a späť, že sa dá hýbať aj kým disketa leží v slote, a že prázdny slot
sa zamknúť nedá — to je jediný dôvod zošediť to políčko.

Drží aj to, že **slot dostane disketu, keď sa zakladá, nie až pri vložení**
(`CreateEmptyIfMissing`), a hlavne že **druhé volanie ju nevyrobí znovu** —
inak by ďalšie OK v dialógu vrátilo prázdnu disketu namiesto popísanej, teda
tichá strata z 6.24 dosiahnutá z dialógu.

Drží aj **to, že uloženie je „uložiť ako“** (6.28): `VirtualDisk::SaveAs`
priečinok prevezme, a hlavne prestavia `imported_`. Bez toho by disketa
v deň uloženia vyzerala v poriadku a odvtedy by ticho prestala rešpektovať
mazanie súborov — write-back presúva do `.eureka-trash` len súbory, o ktorých
vie. Drží to `SavingAdoptsTheFolder`.

Drží aj **klasifikáciu typov súborov pri exporte**, a to je jediné, čo ju
drží. `VirtualDisk::IsTextType` je allowlist a jeho dve chyby stoja rôzne:
typ zle označený za textový sa oreže na prvom `1Ah` a **stratí dáta
natrvalo**, typ zle označený za binárny nechá pár bajtov výplne. `BAS` v tom
zozname stál dva roky a prezradil sa až zničenou zálohou (HANDOFF 6.23).
Keď na `IsTextType` siahneš, zmenu musí prijať `klasifikacia_typov_je_pribita`
— a nový typ patrí najprv overiť v `FILE-FMT.D`, nie odhadnúť podľa toho, ako
koncovka vyzerá.

Drží aj **rozdeľovač kolekcie** (`src/disk_layout.*`), teda vrstvu, ktorá
z veľkého priečinka poskladá plán diskiet. Beží bez ROM aj bez jediného súboru
na disku, lebo berie len mená a veľkosti a vracia plán — nekopíruje nič.
Kapacitu ani mená si nepočíta sama: pýta sa `src/cpm_disk.h`, kde sú od
9. 9. 2026 v jednej kópii aj pre `virtual_disk.cpp`. **Keď siahneš na kapacitu
diskety alebo na skladanie 8.3 mena, patrí to tam.** Druhá kópia by dala plán,
ktorý sľúbi disketu, akú obraz nepostaví, a obe polovice by sa ďalej prekladali
aj testovali nazeleno.

Drží aj **vykonanie plánu** (`src/disk_split.*`), teda vrstvu nad ním, ktorá
sa disku už dotýka: prečíta kolekciu, plán opíše a vykoná ho. Testuje sa
v priečinkoch v `%TEMP%` a drží to, čo je o dátach, nie o algoritme — že sa
každý súbor skopíruje **práve raz a bajt na bajt** pod menom, ktoré sľúbil
plán, že zdrojový priečinok má po rozdelení presne toľko súborov ako pred ním,
a že **neprázdny cieľ aj cieľ vnútri zdroja sú odmietnuté a nič v nich
nevznikne**. To pravidlo o cieli je `TargetIsUsable` a je **jedno** — pýta sa
naň aj prvá stránka sprievodcu, aby ho používateľ počul pred plánom, nie po
ňom. Do zdroja zapisuje jediná funkcia, `WriteSpolu`, a len na výslovný pokyn.
Overené mutáciou: vypnutá kontrola prázdneho cieľa zhodí dve kontroly.

`settings_test` beží tiež bez ROM a v `%TEMP%`. Drží formát súboru
s nastaveniami a hlavne to, že cesta s diakritikou prežije zápis aj čítanie.
Drží aj **zámok diskety proti zápisu** (`zamok1=`, `zamok2=`… so zoznamom
ciest) — že prežije uloženie, že zrušený zámok zo súboru zmizne a že tá istá
cesta napísaná inak je tá istá disketa. To posledné nie je kozmetika: výber
priečinka, ručne napísaný slot a kanonická cesta z `VirtualDisk` sa líšia
veľkosťou písmen, tvarom lomky a koncovou lomkou, a surové porovnanie by zámok
stratilo potichu. Pravidlo je `Settings::SameDisk` a je len jedno — pýta sa naň
aj dialóg slotov.
Overené mutáciou: `CP_UTF8` → `CP_ACP` v `settings.cpp` zhodí tri kontroly.
Skutočný súbor nastavení na to nepoužívaj — patrí tomu, kto testy spúšťa.

## Spustenie testov

`run-tests.bat` zostaví testy a pustí všetkých trinásť naraz — tri
samostatné testy a desať režimov `integration_test`. Sú to nezávislé
procesy, nič nezdieľajú. Priečinok diskety si vyrobí čerstvý v
`build\testdisk` a skopíruje doň `eurekatech\TECHMAN1\READ.COM`, bez
ktorého režim `com` zlyhá. ROM berie z argumentu, inak z `%A4ROM%`, inak
`C:\b\a4rom.dmp`.

Výstup drží pohromade `--output-sync=target`; bez neho sa riadky trinástich
procesov premiešajú. `-k` nechá dobehnúť aj zvyšok po prvom zlyhaní.

**Pasca, do ktorej som už spadol:** režimy sa v `Makefile` generujú ako
výslovné pravidlá cez `foreach`/`eval`. Vzorové pravidlo `check-%` tam
najprv bolo a bolo tiché — `make` implicitné ani vzorové pravidlá na
`.PHONY` cieľoch nehľadá, takže všetky režimy zostali bez receptu, make ich
vyhlásil za splnené a `run-tests.bat` ohlásil úspech bez toho, aby čokoľvek
z nich bežalo. Keď na tú časť siahneš, over počet riadkov `PASS` — musí ich
byť trinásť — a raz to skús s nezmyselnou ROM, či poistka naozaj zvoní.

## Diagnostická sonda

`tests/diag_probe.cpp` je hlavný vyšetrovací nástroj. Nabootuje ROM
a vypíše, čo model hardvéru neobsluhuje.

```
diag_probe ROM DISK_FOLDER boot
diag_probe ROM DISK_FOLDER sweep 4000000
diag_probe ROM DISK_FOLDER seq 15000000 kD7 Y Y
diag_probe ROM DISK_FOLDER trace 20000000 formatovaci
```

Tokeny sekvencie:

- `kXX` — kód klávesu v šestnástkovej sústave.
- text — napíše sa na klávesnici; `~` je Enter.
- `.` — **čakanie bez klávesu**. Nie je to pohodlie: kláves poslaný len
  preto, aby sa čakalo, je odpoveď na otázku, ktorú stroj ešte nepoložil,
  a mne raz takto `k00` uprostred formátovania podsunulo hlásenie, ktoré
  tam inak nepatrí. Keď meriaš dialóg, čakaj `.`, nie klávesom.
- `?text` — čaká, kým stroj **nepovie** daný text. Reč chodí oneskorene za
  dejom, takže pevné čakanie vymení disketu uprostred kroku namiesto pri
  výzve, ktorá si o ňu povedala. Porovnávaj len ASCII kúsky: reč je
  v Kamenických, takže „vlož cílový disk“ príde ako `vlo. c.lov. disk`.
- `+wp`, `-wp` — zapne a vypne ochranu diskety proti zápisu.
- `ram`, `folder`, `mount:CESTA`, `slot1`, `slot2` — výmena diskety tak,
  ako ju robí okno: počká na `DiskSwappable`, flushne a až potom vymení.
  `slot1`/`slot2` idú cez `DiskStash`, teda vrátia **tú istú** disketu.
- `nova`, `vysun` — nenaformátovaná disketa a prázdna mechanika. Sú to dve
  médiá, ktoré rýchla voľba vyrobiť nevie a firmvér o nich hovorí inak než
  o pokazenej diskete; bez nich sa ten rozdiel nedá zmerať (HANDOFF 6.30).
- `stav` — vypíše, čo je naozaj na diskete v mechanike (médium, počet
  súborov, zámok). Reč hovorí, čo si stroj myslí; toto hovorí, čo je na
  médiu, a práve ten rozdiel odhalil 6.24.
- `spin:N` — prebehne N inštrukcií a vypíše histogram fyzického PC. Takto
  dostane zaseknutie adresu namiesto dohadu.
- `trace` — od tejto chvíle sleduje porty radiča (`98h`–`9Bh`). Zámerne
  nie `A8h`: ten sa číta v nečinnej slučke a zaplavil by ring buffer.

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
- `src/settings.*` je to, čo emulátor vie medzi behmi: súbor
  `nastavenia.txt` v priečinku `config` vedľa EXE, ak taký existuje, inak
  v `%APPDATA%\EurekaA4`. O emulátore nevie nič a nič nehlási — chýbajúci
  súbor znamená defaulty, zlyhaný zápis vráti dôvod volajúcemu, ktorý ho
  ohlási v okamihu úkonu. Viď HANDOFF 6.22.
- **Slovník diskety je zmluva a je jednoslovný.** Disketa buď má **domov**
  (`VirtualDisk::has_home()`, priečinok, z ktorého sa načíta a do ktorého sa
  sama zapisuje), alebo nemá; pre používateľa je tá druhá **„neuložená“**,
  nie „v pamäti“ — v pamäti sú obe, `Mount` priečinok len raz prečíta do
  obrazu. Slovo „RAM“ nepomenúvalo ani jednu z dvoch vecí, na ktorých
  záleží, a dalo sa preto prečítať ako „nedá sa zamknúť“ (6.26, 6.28).
  Jediná výnimka je `FormatTrack`: má rovnakú odpoveď, ale iný dôvod, takže
  má vlastný predikát `formatting_erases()`. **Nezlučuj ich.**
  Značka v `nastavenia.txt` zostáva `*pamat` — je v existujúcich súboroch
  a jej zmena by ticho vyprázdnila každý neuložený slot.
- `src/disk_stash.*` je **zásobník diskiet**: sloty 1 až 9, v ktorých
  diskety **sú**, kým nie sú v mechanike. Disketa je objekt, nie recept na
  jej výrobu — slot, ktorý pri každom vložení vyrobil novú prázdnu, zabil
  Eurekine hromadné kopírovanie a tichou stratou dát (HANDOFF 6.24).
  Odkladajú sa len neuložené diskety; tú s priečinkom drží priečinok. Slot
  označený `*pamat` disketu **má** — vyrobí sa pri jeho založení a znovu pri
  každom štarte (`main.cpp` po `Start()`), nie až pri prvom vložení.
  A `kInsertSlot` sa pýta najprv na to, **čo slot menuje**, a až potom siaha
  na poličku; opačné poradie podávalo disketu z poličky pri slote, ktorý
  medzitým menoval priečinok.
  **Slot 0 zámerne neexistuje** a nie je tu ani polička na disketu bez
  slotu: polička drží jednu, takže druhá odložená by prvú ticho prepísala.
  Neuložená disketa bez slotu je rozhodnutie používateľa, a pýta sa naň
  `MainWindow::ConfirmLosingDiskette` pred **každou** cestou, ktorá by ju
  z mechaniky vytlačila.
- `src/emulator_thread.*` vlastní `EurekaMachine` aj `AudioPlayer` a beží
  na vlastnom vlákne. Vlastní aj zásobník; okno mu posiela číslo slotu
  a späť dostáva v `DiskState`, ktorému slotu disketa v mechanike patrí,
  plus dve atomické masky `stash_holds()` a `stash_locked()` — bez nich sa
  dialóg slotov nemá ako spýtať, či v slote vôbec nejaká disketa je.
  Okno sa stroja **nedotýka**, všetko mu posiela cez
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
- **`WS_GROUP` je to, po čom krúžia šípky, a mýli sa ľahko.** Skupina
  začína prvkom, ktorý ho má, a končí až **ďalším** takým prvkom; čo je
  medzi, patrí do nej, aj keď to fokus prijať nevie. Prepínače
  v `IDD_SETTINGS` preto nekrúžili — skupina pokračovala do `GROUPBOX`
  pod nimi (HANDOFF 6.27). Rovnaká štruktúra v `IDD_NEWDISK` fungovala
  len preto, že **windres dáva `LTEXT` `WS_GROUP` implicitne**, takže
  rozdiel nebol v `.rc` vidieť. Skupinu ukončuj výslovne a overuj
  meraním, nie čítaním: šablónu z hotového EXE vyrobí
  `CreateDialogIndirectParamW` skrytú a `IsDialogMessageW` prijme ručne
  poskladaný `WM_KEYDOWN`, takže na to netreba obrazovku.
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
  `IDR_ACCELERATORS_HOST` (`Ctrl+U I M Q R V K N D H Z` a `Ctrl+0` až `Ctrl+9`
  pre sloty s disketami) platí **len keď je
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
ohlási ako dialóg a prečíta. Takto sú riešené chyby pri štarte, zlyhanie
zvuku a hlásenia pri ukladaní diskety.

Toto pravidlo si už raz vybralo daň: `Ctrl+D` pri vypnutej diagnostike
posielal vetu „zapnite ju v Nastaveniach“ cez `host::Print`, teda do
konzoly, ktorá v tej chvíli ešte neexistuje — skratka vyzerala pokazene
namiesto vypnutej (HANDOFF 6.20). Rozhoduje o tom teraz okno, nie vlákno
stroja, a to z dvoch dôvodov: vlákno stroja nesmie otvárať okná a
`MessageBox` si spustí vlastnú správovú slučku, takže by na ten čas
zastavil emuláciu aj zvuk.

**Do textov pre používateľa nepatria implementačné detaily.** Ani návody na
klávesy v popiskách, ani vysvetlenie, prečo je niečo zošedené, ani kedy sa čo
vnútri vyrobí. Každé slovo v popiske, v riadku zoznamu a na tlačidle sa číta
**nahlas pri každom prechode**, takže text navyše nie je pomoc, je to šum — a
keď opisuje náš mechanizmus, býva navyše nepravdivý z pohľadu používateľa.
Zamietnuté 31. 8. 2026: `Neuložená disketa (vznikne pri vložení)` (pravda
o kóde, lož o svete — disketa vzniká pri vytvorení) a popiska
`Sloty — vkladá ich F11 a číslo s Ctrl:` (skratky sú v ponuke, v Pomocníkovi aj
v README). Popiska hovorí, **čo tá vec je**; dôvod patrí do README alebo do
komentára v zdrojáku. Zošedený prvok stojí sám. Je to tá istá chyba ako 6.25,
len z druhej strany.

Druhá polovica toho pravidla: **dialóg je na to, čo sa pokazilo alebo si
žiada rozhodnutie, nie na pravidlá, ktorými disk beží.** Podpriečinky sa
ignorujú a nehlási sa to nijako — hlásili sa a používateľ to zamietol ako
otravovanie pri každom štarte (HANDOFF 6.19). Miesto pre takú vec je
README, nie `MessageBox`.

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

## Čo znamená hotovo

Kým toto neplatí, nehlás hotovo — a nehlás ani „malo by to fungovať“:

1. `build.bat` prejde bez jediného varovania.
2. `run-tests.bat` dá **trinásť** riadkov `PASS`. Že sa to preložilo, nie je
   výsledok merania.
3. Dokumentácia dobehla **v tom istom kroku**, nie „potom“. README, keď sa
   zmenilo správanie; HANDOFF, keď v ňom niečo prestalo platiť — ten odsek sa
   nemaže, pripíše sa pod neho, čo už neplatí a prečo; a komentár v zdrojáku,
   ktorý zmenu zdôvodňoval, lebo komentár s neplatným dôvodom je horší než
   žiadny. HANDOFF je nosič kontextu: zastaraná veta v ňom sa číta ako pravda.
4. Reťazce pre používateľa sa overujú na hotovom EXE v UTF-16, nie na zdrojáku.

Až potom sa pýtaj na commit. Keď niečo z toho nevyšlo, povedz to rovno aj
s výstupom — nedokončená vec ohlásená ako hotová stojí viac než nedokončená
vec.

Rozsah drží zadanie: keď zmena spraví neplatnou vec, ktorá je mimo neho,
vypíš ju, nespravuj ju bez slova. Bod 3 je o tom, čo tou istou zmenou
prestalo platiť, nie povolenie prepisovať okolie.

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
