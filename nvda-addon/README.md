# Doplnok pre NVDA: klávesnica patrí Eureke

Emulátor a čítačka obrazovky sa bijú o tú istú klávesnicu. Eureka je
sebeozvučujúci stroj — hovorí vlastným hlasom z vlastného DAC — a chce
všetky klávesy vrátane tých, ktoré si NVDA berie pre seba. NVDA na to má
odpoveď, spánkový režim (`NVDA+Shift+S`), lenže ten sa viaže na **celú
aplikáciu**: umlčí aj ponuku emulátora, aj jeho dialógy, aj hlásenia. Práve
tam je čítačka potrebná.

Tento doplnok uspí NVDA **len nad hlavným oknom** emulátora. Nikde inde.

## Čo sa zmení

- Nad oknom stroja NVDA mlčí a **všetky klávesy idú do Eureky** —
  numerická klávesnica, `NVDA+šípky`, `Caps Lock` aj gestá, ktoré by si
  inak zobrala pre seba.
- **Ponuka, dialógy a hlásenia** emulátora sa čítajú úplne normálne. Sú to
  iné okná, a rozhoduje sa pre každé zvlášť.
- **`Shift+F11`** v emulátore (uvoľnenie klávesnice) prebudí NVDA aj nad
  oknom stroja. Kým klávesnica patrí Windows, správa sa okno ako každé iné.
- **`NVDA+T`** funguje aj počas spánku. Vnútri emulátora ho nahrádza vlastný
  príkaz, lebo titulok okna nesie stav klávesnice a bola by škoda, aby sa
  naň nedalo spýtať. Dvakrát ho vyhláskuje, trikrát skopíruje do schránky —
  ako pôvodný príkaz NVDA.
- **Posuvníky emulátora sa ohlásia.** Keď po `F11` (alebo po `Shift+F11`)
  pohnete posuvníkom — `Ctrl+šípka` rýchlosť reči, `Alt+šípka` hlasitosť —
  NVDA povie novú polohu, napríklad „hlasitosť 12 z 20“ alebo „rýchlosť
  17 z 32“. Hlasitosť sa počíta od nuly, lebo nula je ticho; rýchlosť od
  jednotky. Na konci posuvníka NVDA mlčí a pípne emulátor.
- **`NVDA+Shift+S`** zostáva núdzová brzda: uspí alebo prebudí celú
  aplikáciu naraz a prebije rozhodovanie tohto doplnku.

  Vyzerá to ako druhý spánok popri tomto, ale nie je: `appModule.sleepMode`
  je celý čas **vypnutý**, aplikácia v spánku nie je. Spí jediný objekt —
  okno stroja — a keďže na ňom nie je čo ohlasovať, jediné, čo z toho vidno,
  je že nereaguje na klávesy. Keď teda `NVDA+Shift+S` nad ním povie „režim
  spánku zapnutý", hovorí pravdu o aplikácii, nie o okne.

## Inštalácia

Buď hotový balík:

```text
build-addon.bat
```

vyrobí `bin\eurekaA4Emulator.nvda-addon`; ten sa otvorí a NVDA sa spýta na
inštaláciu.

Alebo vývojová slučka bez inštalácie:

```text
build-addon.bat scratchpad
```

skopíruje modul do vývojového priečinka NVDA (`scratchpad`). Treba k tomu
zapnúť *Nastavenia → Rozšírené → Načítať vlastný kód z vývojového
priečinka* a NVDA reštartovať; ďalšie zmeny stačí načítať cez
`NVDA+Ctrl+F3`.

## Ako to funguje

NVDA sa na `sleepMode` pýta **objektu**, ktorého sa udalosť alebo gesto
týka, a `NVDAObject` má `_cache_sleepMode = False`, takže odpoveď sa
necachuje. Doplnok preto cez `chooseNVDAObjectOverlayClasses` podstrčí
vlastnú triedu jedinému oknu triedy `EurekaA4EmulatorWindow` a spánok
rozhoduje ona. Zvyšok procesu zostáva nedotknutý.

V spánku sa gesto zahodí ako `NoInputGestureAction` a kláves ide do
aplikácie, a udalosti sa nespracujú — teda žiadne echo, žiadne dvojité
rozprávanie a nič, čo by Eureke kláves zjedlo.

**Ponuková lišta nie je okno.** Patrí HWND, ktorý ju vlastní, takže objekty,
ktoré NVDA po `F12` postaví, nesú triedu hlavného okna a spadnú na ten istý
overlay. Prvá verzia ich preto uspala a ponuka bola ticho; ozvala sa až prvá
šípka dole, lebo rozbaľovacie menu **je** vlastné okno (`#32768`) a overlay
naň nesadá. Doplnok sa preto pýta aj na režim ponuky
(`GetGUIThreadInfo`) a kým je ponuka hore, nespí. Odmerané na bežiacom
emulátore: pred ponukou `flags=0`, po otvorení `GUI_INMENUMODE`, po zavretí
zase `0`, a `hwndFocus` je celý čas hlavné okno. Vecne to sedí aj s tým, čo
sa deje s klávesmi — v režime ponuky ich Eureka aj tak nedostáva.

Kto vlastní klávesnicu, je vec emulátora, nie doplnku. Emulátor to
vystavuje ako vlastnosť okna `EurekaA4.KeyboardReleased`
(`MainWindow::PublishKeyboardState` v `src/main_window.cpp`), doplnok si ju
prečíta cez `GetPropW` z druhého procesu. Vlastnosť okna, nie pomenovaná
udalosť: viaže sa na konkrétne okno, takže dva bežiace emulátory si
neprekážajú.

**Názov triedy okna a názov vlastnosti sú zmluva medzi dvoma
projektmi.** Premenovanie ktoréhokoľvek z nich doplnok vypne, a vypne ho
potichu — nič nezlyhá, NVDA len prestane spať.

### Posuvníky

Polohu oboch posuvníkov emulátor vystavuje tým istým spôsobom, vo
vlastnostiach okna `EurekaA4.SpeechRate` a `EurekaA4.Volume`
(`MainWindow::PublishSliders`), ako čísla priamo zo `src/sliders.h`. Aj tie
dva názvy patria do zmluvy a počty polôh (32 a 21) sú v doplnku napevno.

Kedy sa pozrieť, rozhoduje kláves. Skript naviazaný na `Ctrl+šípku` by to
nebol: skripty aplikačného modulu majú prednosť pred skriptmi zaostreného
prvku, takže by v dialógoch emulátora vzal editačným poliam skok po slovách.
Doplnok sa preto zaregistruje do `inputCore.decide_executeGesture`. NVDA sa
na ten bod pýta **pred** kontrolou spánku, takže kláves vidno aj v spánku,
a keďže odpoveď je vždy „pokračuj“, kláves ide ďalej nedotknutý.

Po klávese sa doplnok pozrie na vlastnosť o 30, 100 a 250 ms. Ak sa poloha
zmenila, zruší rozprávanie a ohlási ju. Ak nie, kláves šiel Eureke (bez
`F11`) alebo bol posuvník na konci, a doplnok nepovie nič. O `F11` teda
vedieť nemusí — a to je dobre, lebo jednorazovka sa zámerne nezverejňuje.

**V scratchpade sú hlásenia anglicky** („volume 12 of 20“):
`addonHandler.initTranslation()` tam nefunguje, lebo to nie je doplnok.
Po slovensky hovorí nainštalovaný balík z `build-addon.bat`.

## Čo doplnok nedokáže

- **Kláves NVDA (`Insert`) sa do Eureky nedostane** ani v spánku: čítačka
  si svoj modifikátor necháva vždy. Eureka ho pozná (kód `8Dh`), takže keď
  ho naozaj treba, pomôže vlastná finta NVDA — **dve rýchle stlačenia za
  sebou** pošlú kláves aplikácii. Druhá možnosť je nechať v NVDA ako
  modifikátor len `Caps Lock`.
- **Príkazy NVDA v spánku nebežia** — okrem `NVDA+T` a `NVDA+Shift+S`. Je
  to zámer: presne o to ide pri privlastnení klávesnice. Keď treba čítačku
  naplno, `Shift+F11`.
- **Zmena stavu sa neohlási sama.** NVDA sa nový stav dozvie až pri
  najbližšom klávese alebo udalosti. Nie je to strata: emulátor každú zmenu
  sám ozve dvojicou tónov a napíše do titulku.

## Licencia

NVDA považuje doplnky za odvodené dielo, takže obsah tohto priečinka je pod
**GNU GPL verzia 2 alebo novšia** — na rozdiel od zvyšku repozitára, ktorý
je pod MIT. Viď `COPYING.txt`.
