# Eureka ako testovací objekt — návrh

Stav: **koncept**, píše sa postupne. Keď sa niektorá časť zrealizuje,
pripíše sa k nej, kde v kóde stojí; keď sa zamietne, pripíše sa prečo.

## Prečo

Headless počítač už máme: `EurekaMachine` nemá okno ani zvukové zariadenie
a testy aj sonda ho používajú priamo. Chýba vrstva **scenára** — „stlač,
počkaj, čo povie“ — a tú máme napísanú dvakrát, pričom ani jedna sa nedá
znovu použiť:

- `tests/diag_probe.cpp` má čitateľný jazyk tokenov (`kXX`, `sXX`, text
  s `~`, `.`, `?text`, `@TEXT`, `+b1`, `vypni`, `slot1`, `cas:`…), ale celý
  sedí vo `wmain` a test ho zavolať nevie.
- `tests/integration_test.cpp` si to isté píše ručne a zakaždým trochu
  inak: `RunAndListen`, `SpeakUntilDone`, `Grind`, `Type`, `TypeScan`,
  `TypeDigit` a opakované slučky typu
  `for (step < 8'000'000) if (!Step() && queued_keys() == 0) break;`
  s ručným `TakeSpeechInput`/`TakeAudio`/`TakeConsoleOutput`.

Čitateľnosť je vedľajší zisk. Hlavný je, že **čakanie bude napísané raz**.
Dnes sa „čakaj na ticho“ v sonde a v testoch líši (viď nižšie) a nič
nestráži, že sa nerozídu. Rozdiel medzi čakaním a klávesom poslaným na
čakanie už raz stál chybné meranie — `k00` uprostred formátovania podsunulo
hlásenie, ktoré tam nepatrí.

## Čo dnes znamená „počkaj“ — tri rôzne veci

Doložené v zdrojákoch 24. 9. 2026:

1. **Sonda, `RunUntilPrompt`** (`diag_probe.cpp`): beží, kým sa pol sekundy
   emulovaného času (`kCpuHz / 2` cyklov) neobjaví nič na **konzole**.
   Prvé tri sekundy po štarte sa za ticho nepočítajú (nábeh). Reč sa do
   toho nepočíta. Pri vypnutom stroji sa raz spýta `WakeOnAlarm()`.
2. **`CheckProtectedDiskStillReads`**: to isté, ale ručne prepísané
   priamo v kontrole.
3. **Väčšina ostatných kontrol**: pevný rozpočet inštrukcií (8, 12, 40
   miliónov) alebo „kým je fronta klávesov prázdna a `Step()` vráti false“.
   Pevný rozpočet je pomalý, keď je veľký, a krehký, keď je malý.

Návrh musí rozhodnúť, ktoré z nich je „ticho“, a ostatné pomenovať inak.

## Ciele

- Klávesy **menami**, nie kódmi: `Shift | F8`, `Alt | F4`, text, akordy
  braillovskej klávesnice. Mená sa berú z `KB.H` a `hardware-map.md`,
  nevymýšľajú sa (rovnaké pravidlo ako pri `src/eureka_io.h`).
- Čakanie ako **slovesá s rozpočtom**: ticho, konkrétna veta, konkrétny
  text na konzole, podmienka nad pamäťou. Vypršaný rozpočet je výslovné
  zlyhanie s tým, čo sa dovtedy počulo — nikdy ticho.
- **Nahrávanie**: začať/ukončiť a dostať prepis reči, výstup konzoly
  a zvukové vzorky za ten úsek.
- **Sledovanie pamäte**: zmena bajtu na adrese vyvolá callback alebo
  ukončí čakanie.
- **Sonda je len parser tokenov nad tým istým objektom.** Bez tohto bodu
  by vznikla tretia kópia namiesto jednej, a vtedy to nerobiť.

## Čo to nie je

- Nie je to nový stroj ani náhrada `EurekaMachine`. Objekt stroj len
  vlastní alebo obaľuje; všetko, čo robí, sa dá urobiť aj priamo.
- Nie je to pre nízkoúrovňové kontroly (holé jadro, DAC, wait states,
  bloková I/O, `CheckZ180IoAddressHighByte`). Tie sa na scenár nehodia
  a zostanú, ako sú.
- Nie je to v `src/` — GUI to nepotrebuje. Žije v `tests/` a linkuje sa
  do sondy a testov.

## Náčrt rozhrania (prvá verzia, na diskusiu)

Pracovné meno `EurekaSession`. Všetko nižšie je návrh, nie rozhodnutie.

```cpp
EurekaSession eureka(romPath, diskFolder);   // alebo nad existujúcim strojom
eureka.Boot();                                // Reset + čakanie na nábeh

eureka.Press(Shift | F8);                     // "formátovať disk"
eureka.WaitIdle();
eureka.Type("y");

auto heard = eureka.Record([&] {
  eureka.Type("y");
  eureka.WaitSaid("n proti z", 10s);          // ASCII kúsok Kamenických
});
EXPECT(!eureka.machine().disk().has_format());
```

Rozdelenie na skupiny:

- **Vstup:** `Press(kláves)`, `Pc(kláves PC)`, `Type(text)`,
  `Chord(body)`, `Hold`/`Release` pre čiastočné akordy (`HoldMembrane`).
  Typy viď Rozhodnuté.
- **Čakanie:** `WaitIdle()`, `WaitSilent()`, `WaitSaid(text)`,
  `WaitConsole(text)`, `WaitUntil(predikát)`, `Run(trvanie)`. Každé s rozpočtom
  a s návratovou hodnotou, ktorá sa nedá prehliadnuť.
- **Záznam:** `StartRecording()`/`StopRecording()` alebo `Record(lambda)`;
  výsledok nesie prepis reči (surový aj čitateľný), konzolu a vzorky.
- **Médiá a napájanie:** tie isté úkony ako tokeny sondy — vložiť slot,
  vysunúť, nová disketa, ochrana proti zápisu, vypni/zapni/studeno, posun
  hodín.
- **Pamäť:** `Watch(adresa, callback)`, `Peek(adresa)`.

## Rozhodnuté

### Čakanie: dve slovesá pre ticho, jedno pre vetu (24. 9. 2026)

Procesor Eureky nikdy „nič nerobí“ — keď čaká na kláves, točí sa
v slučke nad klávesnicou (meranie 24. 9. 2026: najčastejšie PC
`19B4B`–`19B69`). Nečinnosť sa preto nedá zistiť z procesora priamo;
posudzuje sa podľa toho, čo stroj robí navonok.

- **`WaitIdle(okno)`** — stroj čaká na používateľa: počas celého okna
  (predvolene 500 ms) nepribudlo nič na konzole **ani v reči**. Oproti
  dnešnému `RunUntilPrompt` pribúda reč, ktorá chodí oneskorene za dejom.
- **`WaitSilent(okno)`** — navyše sa počas okna nepohla **hodnota** DAC.
  Sledujú sa zmeny hodnoty, nie zápisy: generátor tónov zapisuje pri
  každom prerušení aj v tichu (viď token `dac:` v sonde).
- **`WaitSaid(text, rozpočet)`** — kým zaznie daný kúsok reči. Kde sa dá,
  má prednosť pred čakaním na ticho, lebo meria to, na čom test stojí.

Token `.` v sonde si v prvom kroku ponechá dnešné správanie (len
konzola) — viď Postup, bod 1. Na `WaitIdle` sa prepne až vedome
a s porovnaním výstupov.

Doklad, že čakanie len na konzolu nestačí (24. 9. 2026):
`diag_probe ROM DISK seq 8000000 kC1 .` — po `kC1` stroj povie
„20 HODIN.“, nasledujúca `.` vráti prázdno. Minúty prídu o vyše milióna
inštrukcií neskôr (viď komentár pri `CheckAnnouncesTime`), keď už konzola
dávno mlčí.

Možné vylepšenie neskôr: zoznam adries čakacích slučiek na kláves
a `WaitIdle` ako „PC je v jednej z nich“. Bolo by to skutočné „čaká na
kláves“ namiesto odhadu, ale každá aplikácia môže mať vlastnú slučku
a jedna zabudnutá by sa neprezradila.

### Čas: emulovaný, v `std::chrono` (24. 9. 2026)

Rozpočty a okná sa zadávajú v **emulovanom čase** ako `std::chrono`
trvania: `WaitSaid("ahoj", 10s)`, `WaitIdle(500ms)`. Vnútri sa prepočítajú
na cykly; milisekunda je presne 6 144 cyklov (`kCpuHz = 6144000`).
Počty inštrukcií sa v scenároch nepoužívajú — závisia od mixu inštrukcií
a nečítajú sa.

Test pritom **nečaká na reálny čas**: bez GUI nie je nič, čo by stroj
brzdilo podľa zvukovej karty, a vzorky sa len hromadia. Zmerané
24. 9. 2026 sondou so zapnutou diagnostikou: asi 22,7 milióna inštrukcií,
zhruba 33 emulovaných sekúnd, za 6,7 s skutočného času, teda rádovo
päťnásobok reálneho času. Každá zbytočne čakaná emulovaná sekunda preto
stojí asi 0,2 s — pevné rozpočty „pre istotu“ sa sčítajú a čakanie na
udalosť je aj rýchlejšie, nielen čitateľnejšie.

### Zlyhanie kroku je výnimka (24. 9. 2026)

Krok scenára, ktorý nevyjde (vypršaný rozpočet, nenapísateľný znak,
neznáme meno klávesu), vyhodí výnimku. Scenár je potom zoznam krokov bez
podmienok a kontrolu obalí jeden `try` tam, kde sa spúšťa.

Dôvod nie je stručnosť, ale to, že **zlyhanie sa nedá prehliadnuť**.
Pri návratovej hodnote zabudnutý `if` znamená, že test ticho pokračuje
ďalej — presne tá tichá chyba, proti ktorej projekt stojí.

Výnimka nesie všetko, čo treba na diagnózu bez opätovného spustenia:

- ktorý krok a čo čakal (napr. `WaitSaid("n proti z", 10s)`),
- čo sa od začiatku kroku naozaj počulo, v čitateľnom tvare (Kamenických
  prevedený ako dnes `Readable` v sonde), a čo pribudlo na konzole,
- koľko emulovaného času krok bežal a fyzické PC v okamihu zlyhania,
- či je stroj vypnutý.

Platí **len v testovacej vrstve**. Emulátor ani GUI výnimky nepoužívajú
a nezačnú.

### Reč, konzola a zvuk sa nahrávajú; pamäť má callback (24. 9. 2026)

**Priebežný prepis.** Session si celú reč a konzolu vedie vždy sama, od
`Boot()` po koniec, bez ohľadu na to, či niekto nahráva. Z neho
`WaitSaid` pozná, že veta zaznela, a výnimka z neho povie, čo sa počulo.
Každé `Step()` stroja ide cez session, takže nič nemôže zjesť
`TakeSpeechInput()` mimo nej.

**Záznam** je výrez z priebežného prepisu:

```cpp
auto heard = eureka.Record([&] {
  eureka.Press("F10");
  eureka.WaitIdle();
});
// heard.speech, heard.console, heard.audio
```

Callback na reč sa nerobí: testy sa takmer vždy pýtajú „čo zaznelo
medzi A a B“ a callback by ten istý záznam skladal ručne do premennej
mimo seba.

**Zvuk** sa na rozdiel od reči priebežne neuchováva celý (48 000 vzoriek
na emulovanú sekundu). Session drží len to, čo treba na `WaitSilent`,
a vzorky ukladá len počas `Record`.

**Pamäť** má dve slovesá:

- `WaitUntil(predikát, rozpočet)` — čakanie na podmienku, napr.
  `eureka.WaitUntil([&] { return eureka.Peek(0xC621) != 0xFF; }, 5s)`.
  Presne toto dnes ručne robí `SpeakUntilDone`.
- `Watch(adresa, callback)` — zavolá sa pri **každej** zmene hodnoty
  počas behu; na počítanie prepnutí a podobne.

Obe sa vyhodnocujú **po každej inštrukcii, len v testovacej vrstve**.
`WriteMemory` v emulátore sa nemení — beží aj v GUI, na každej inštrukcii.
Vedomé obmedzenie: hodnota, ktorú jedna inštrukcia zapíše a ďalšia
prepíše späť, sa neprezradí. Premenné firmvéru to nerieši; keby bolo
treba chytiť každý zápis, rieši sa to až vtedy.

### Klávesy sú enumy, typ určuje cestu (24. 9. 2026)

Stroj má tri vstupné cesty a nie sú zameniteľné: hotový kód do fronty
firmvéru (`QueueKey`), klávesnica PC cez prekladovú rutinu ROM
(`QueueScanCode`, tabuľka `1DF05`) a membrána (`PressBraille`,
`HoldMembrane`). Escape poslaný ako hotový kód v aplikácii hodín neurobí
nič, ten istý ako scancode povie „ahoj“ a vráti do menu (CLAUDE.md,
token `sXX`).

Preto má každá cesta **vlastný typ** a vlastnú metódu:

```cpp
using namespace eureka::keys;

eureka.Press(Shift | F8);          // K_SF8
eureka.Press(Alt | F4);            // E3h, "komunikace"
eureka.Pc(Esc);                    // scancode cez ROM
eureka.Chord(Dot1 | Dot4 | Dot5);  // membrána
eureka.Type("READ~");              // text cez klávesnicu PC, ~ je Enter
```

- Preklep ani zlá cesta sa nepreložia: `Press(Esc)` je chyba prekladu,
  nie tichý kláves za behu.
- `|` je definované len tam, kde má zmysel: modifikátor s klávesom áno,
  `F8 | F9` nie. Skladá sa tak, ako skladá `KB.H` — `K_SF8` je
  `K_F8 | K_SHIFT`, `K_ALEFT` je `K_LEFT | K_ALT`.
- Hodnoty sú **konštanty zo `src/eureka_io.h`** (`hw::kKeyF1`,
  `hw::kKeyUp`…), mená tie z `KB.H` bez `K_`. Druhá tabuľka nevzniká.
- Klávesy PC `KB.H` nemenuje; ich mená sú mená klávesov PC a pri hodnotách
  stojí odkaz na tabuľku v ROM `1DF05`.

**Parser mien** je len v sonde, lebo tokeny prichádzajú z príkazového
riadka. Mená hľadá v tej istej tabuľke, z ktorej sú enumy, aby sa sonda
a testy nerozišli v tom, ako sa kláves volá.

Obmedzenie: text zostáva reťazec. Znak, ktorý sa na klávesnici Eureky
napísať nedá, sa prezradí až za behu — výnimkou, nie ticho.

## Otvorené otázky

1. ~~Čo je ticho.~~ Rozhodnuté vyššie.
2. ~~Chyba ako návratová hodnota, alebo výnimka?~~ Rozhodnuté vyššie:
   výnimka.
3. ~~Callback, alebo záznam?~~ Rozhodnuté vyššie: záznam, callback len
   pre pamäť.
4. ~~Kde sa sleduje pamäť.~~ Rozhodnuté vyššie: po každej inštrukcii
   v testovacej vrstve. Čo to stojí, sa zmeria pri realizácii.
5. ~~Mená klávesov.~~ Rozhodnuté vyššie: enumy, typ podľa cesty.
6. ~~Rozpočty.~~ Rozhodnuté vyššie: emulovaný čas v `std::chrono`.

## Krok 1 — sonda nad `EurekaSession` (implementačné detaily, návrh)

Cieľ kroku: session existuje, sonda ju používa a **výstup sondy sa
nezmenil ani o bajt**. Nič nové sa v tomto kroku správať inak nesmie;
nové čakania (`WaitIdle` so sledovaním reči, `WaitSilent`) síce vzniknú,
ale sonda ich ešte nevolá.

### Súbory

- `tests/eureka_keys.h` — enumy klávesov (typy `Key`, `PcKey`, `Dots`)
  a tabuľka mien pre parser sondy. Hodnoty zo `src/eureka_io.h`.
- `tests/eureka_session.h`, `tests/eureka_session.cpp` — session.
- `Makefile`: vzorové pravidlo `$(BUILD)/test_%.o: tests/%.cpp` už
  existuje, takže pribudne len premenná `SESSION_OBJS :=
  $(BUILD)/test_eureka_session.o` a tá sa pridá k prerekvizitám
  `diag_probe.exe` (v kroku 3 aj `integration_test.exe`). Nič nové
  v `run-tests.bat` ani v ostatných dávkach.

### Čo sa zo sondy presúva do session

Všetko, čo **hýbe strojom**. Každé `Step()` ide cez session, aby jej
priebežný prepis nemohol nič obísť.

- `Readable` (Kamenických → ASCII) — potrebuje ho výnimka aj záznam.
- `RunUntilPrompt` → `WaitIdle`. Aby sa výstup nezmenil, dostane
  parameter, čo sleduje: len konzolu (dnešné správanie, volá ho sonda)
  alebo konzolu aj reč (predvolené pre testy). Nábeh — prvé tri sekundy
  sa za ticho nerátajú — a `WakeOnAlarm()` pri vypnutom stroji idú s ním.
- `?text` → `WaitSaid`; `@TEXT` → `WaitConsole`, ktorý navyše vráti
  cykly od odoslania riadku a z toho cykly v programe (RAM pod `C000h`),
  lebo na tom stojí HANDOFF 6.46.
- `kXX` → `Press`, `sXX` → `Pc`, text → `Type`. Sonda berie ľubovoľný
  hex, preto enumy majú aj výslovnú únikovú cestu `Key::Raw(0xD7)`
  a `PcKey::Raw(0x01)`; v testoch sa nemá používať.
- `+b1`…`-b` → `Hold`/`Release`; držané riadky membrány sú stav session,
  nie sondy.
- `vypni`, `zapni`, `studeno` → `PowerOff`, `PowerOn`, `ColdStart`.
- `+wp`/`-wp`, `nova`, `vysun`, `ram`, `folder`, `mount:`, `slot1`,
  `slot2` → metódy diskety. `settleForSwap` a `DiskStash` sa presúvajú
  so session, lebo výmena bez čakania na `DiskSwappable` je stav, aký
  používateľ nevyrobí.
- `cas:` → `ShiftClock(trvanie)`; súčet posunov (`rtcShift`) drží
  session a vráti, či posun stroj zobudil budíkom.
- `rychlost:`, `hlasitost:` → polohy posuvníkov zo `src/sliders.h`.

### Čo zostáva v sonde

Všetko, čo stroj len **pozoruje a vypisuje**: `zvuk`, `wav:`, `dac:`,
`spin:`, `trace`, `budik`, `stav`, záverečná správa diagnostiky
a formát riadkov `token -> odpoveď`. `spin:` a `dac:` bežia po
jednotlivých inštrukciách — pôjdu cez `session.Step()`, nie priamo cez
stroj. Režimy `sweep`, `trace` a `boot` zostávajú v sonde, len nabootujú
a čakajú cez session.

### Rozpočty v sonde zostávajú v inštrukciách

Rozpočet sondy (`argv[4]`) je počet inštrukcií a tak je zapísaný
v príkazoch v `HANDOFF.md` a `tests/README.md`. Je to rozhranie sondy,
nemení sa. Session preto vnútri pozná rozpočet v emulovanom čase aj
v inštrukciách; verejné rozhranie pre testy berie len `std::chrono`,
inštrukcie sú len pre sondu.

### Merítko: výstup pred a po

Zmerané 24. 9. 2026: dva behy tej istej sekvencie bez hodín dajú
**bajtovo zhodný** výstup vrátane počtu inštrukcií a správy diagnostiky.
Hodiny idú z hostiteľa, takže sekvencia, v ktorej stroj hovorí čas
(`kC1`, `sweep` — ten prechádza aj `C1`, `budik`, `cas:`), sa medzi
minútami líši aj bez zmeny kódu. Tie sa porovnávajú len vtedy, keď oba
behy padnú do tej istej minúty, alebo sa z porovnania vynechajú.

Postup: **pred** prvou zmenou kódu sa súčasnou sondou vyrobia
referenčné výstupy do `build\golden\` (nie do repozitára — sú odvodené
z ROM), po zmene sa pustia znova a porovnajú `cmp`. Robí to
`tests/probe_golden.sh OUT` z bashu; každá sekvencia dostane čerstvú kópiu
diskového priečinka. Sada prejde každou rodinou tokenov aspoň raz:

1. `boot`
2. menu, `?` a Escape: `seq 8000000 kC9 kD7 Y ?disk k1B`
3. scancode: `seq 8000000 s3B . s01` — „zaznamnik“, „ahoj“
4. membrána: `seq 8000000 +bs +b1 +b4 +b5 -b` — F11 ako d-akord,
   ozýva sa počas skladania („rezim“, „ROM“, „operacniho systemu“)
5. disketa: `seq 8000000 +wp kD7 Y Y stav -wp nova kD7 Y . stav vysun kC8 .`
6. sloty: `seq 8000000 slot2 stav slot1 stav slot2 stav`
7. napájanie: `seq 8000000 vypni zapni studeno`
8. posuvníky a zvuk: `seq 8000000 rychlost:0 hlasitost:20 kC9 zvuk dac:200000`
9. text a `@`: `seq 8000000 kD6 . READ~ @Read_which`
10. `spin:` a `trace`: `seq 8000000 trace kD7 spin:200000`

Tri pôvodne navrhnuté sekvencie neobstáli pri prvom behu (24. 9. 2026):
`s3C` je F2, teda hodiny, a výstup závisel od minúty; akord `+b1 +b4 +b5`
v hlavnom menu nepovedal nič; `?READ` sa nedočkal, lebo `Read which
file?` ide na konzolu, nie do reči. Hodiny (`budik`, `cas:`) sa overia
ručne v jednej minúte.

## Postup

1. Vytiahnuť zo sondy vstup a čakanie do `EurekaSession`, sondu na ňu
   prepojiť. Merítko: výstup sondy na niekoľkých známych sekvenciách je
   bajt na bajt rovnaký ako predtým.
2. Pridať záznam a sledovanie pamäte.
3. Preniesť dve alebo tri scenárové kontroly z `integration_test` ako
   ukážku; aspoň pri jednej mutáciou overiť, že stále chytá, čo chytala.
4. Zvyšok prenášať, až keď sa na danú kontrolu siahne.
