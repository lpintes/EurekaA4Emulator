# Eureka A4 Emulator 0.1.0

Natívny 64-bitový emulátor počítača Eureka A4 pre Windows. Používa pôvodný
`A4ROM.DMP`, vykonáva jeho strojový kód a prehráva 8-bitový DAC, takže hlas,
hudba a zvuky vznikajú tým istým algoritmom a z tej istej fonémovej databázy
ako v Eureke.

## Rýchly štart

1. Spustite `EurekaA4Emulator.exe`.
2. Vyberte priečinok, ktorý sa má správať ako 800 KiB disk Eureky.
3. Počkajte na úvodnú vetu „inicializace eureky“.
4. Klávesnica Windows teraz ovláda emulovaný počítač.

Ukončenie a bezpečné uloženie disku: `Ctrl+Shift+Q`. Reset:
`Ctrl+Shift+R`.

Program sa dá spustiť aj z príkazového riadka:

```text
EurekaA4Emulator.exe --rom A4ROM.DMP --disk D:\MOJ_EUREKA_DISK
```

Ak sa `--rom` neuvedie, ROM sa hľadá vedľa EXE. Ak sa neuvedie `--disk`,
program zobrazí systémový výber priečinka.

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
- Bežný Unicode text z Windows sa prevádza do presnej znakovej sady
  Kamenických/KEYBCS2 (často nazývanej CP895), nie do CP852.
- Podporované sú české a slovenské písmená vrátane `č ď ľ ĺ ň ô ŕ ř š ť ž`.
  Moderné úvodzovky, pomlčky a tri bodky sa bezpečne normalizujú. Znak mimo
  8-bitovej sady sa prevedie na základné písmeno alebo `?`.
- Výstup z Eureky sa opačne dekóduje do Unicode, takže konzolu korektne číta
  aj NVDA.

Emulátor nemení obsah programov ani ich textové dáta fonetickými náhradami.
Výslovnostná oprava anglického slova `source` zostáva v samostatnom NVDA
doplnku; tu je prioritou verná činnosť ROM.

## Priečinok ako disk

- Kapacita a geometria zodpovedajú Eureke: 160 logických stôp, 40 záznamov
  na stopu, 128 bajtov na záznam (800 KiB).
- Horná úroveň vybraného priečinka sa pri štarte prevedie na disk CP/M.
- Názvy sa prevedú na veľké 8.3; diakritika v názve sa zloží a kolízie dostanú
  príponu `~1`, `~2` atď.
- Zápisy Eureky sa priebežne ukladajú späť do priečinka.
- Súbor zmazaný v Eureke sa kvôli obnove presunie do `.eureka-trash`, nemaže
  sa nevratne.
- Podpriečinky sa do jednej diskety nezahŕňajú.

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
zariadenia Windows; ich vstupy zostávajú v bezpečnom pokojovom stave. Formátovanie prebehne
a stroj ho ohlási ako dokončené, ale obsah stopy sa zahadzuje: disk je
hostiteľský priečinok a formát v ňom súbory nemaže. Exotické viacsektorové
príkazy WD1772 sú modelované len v rozsahu, ktorý používa dodaná ROM. Táto verzia preto nie je náhradou meracieho
emulátora na overovanie presného časovania externých periférií.

## Diagnostika

Prepínač `--diag` zapne záznam hardvérových prístupov, ktoré model neobsluhuje:

- zápisy do pamäte pod `kRamBase`, zoskupené po 4 KiB stránkach s prvým PC;
- externé porty bez modelu;
- interné registre Z180, ktoré sa len ukladajú a nemajú správanie;
- zmeny troch riadiacich latchov po jednotlivých bitoch;
- kruhový záznam posledných 512 udalostí.

Výpis: `Ctrl+Shift+D` počas behu, a tiež pri ukončení. Bez `--diag` nemá
záznam žiadnu réžiu.

Slúži na dve otvorené otázky, ktoré sa zo samotnej ROM nedajú zodpovedať:
kde naozaj začína zapisovateľná RAM a čo visí na sériovom porte Z180.

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

ROM nie je súčasťou licencie zdrojového kódu; pozrite `ROM-NOTICE.txt`.
Použité jadro Z80 má vlastné MIT oznámenie v `src\LICENSE.superzazu-z80.txt`.

Technické referencie k procesoru: [Hitachi HD64180 User's Manual](https://www.bitsavers.org/components/hitachi/64180/HD64180_Users_Manual_Oct85.pdf),
[Zilog Z180 User Manual](https://www.zilog.com/docs/z180/um0050.pdf) a
[Z180 Product Specification](https://www.zilog.com/docs/z180/ps0140.pdf).
