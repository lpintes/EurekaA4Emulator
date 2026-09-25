# Eureka ako testovací objekt — návrh

Stav: **koncept**, píše sa postupne. Keď sa niektorá časť zrealizuje,
pripíše sa k nej, kde v kóde stojí; keď sa zamietne, pripíše sa prečo.

## Kde pokračovať (stav 25. 9. 2026, bead `ea4-7t4`)

Hotové: kroky 1, 1b, 2 a 3 — režim `session`, `WaitIdle` podľa kontroly
klávesu vo firmvéri a dve prenesené kontroly (sekcie nižšie, každá so
svojím overením). Bead je zavretý. Kód: `tests/eureka_keys.h`, `tests/eureka_session.*`,
sonda a v `tests/integration_test.cpp` `CheckSession`,
`CheckProtectedDiskRefusesFormat` a `CheckAnnouncesTime`.

Ďalej podľa Postupu už len bod 4: ostatné kontroly prenášať, až keď sa na
ne siahne. `WaitSaid` od 25. 9. 2026 počúva len odpoveď na posledný
kláves (krok 3).

Nástroje po ruke:
- `sh tests/probe_golden.sh build/after` a porovnanie s `build\golden\`
  (mimo gitu, znovu vyrobiť, ak chýba) — sonda sa nesmie zmeniť.
- `integration_test ROM DISK session` — test samotnej session.
- Pracovné poznámky subagenta k čakaniu na kláves:
  `build\keywait\progress.md` (mimo gitu, závery sú nižšie).

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

**Doplnené pri realizácii kroku 2 (24. 9. 2026):** sledovať nové bajty
reči nestačí. Firmvér podá syntezátoru celú vetu naraz a potom ju sekundu
hovorí, takže pol sekundy bez nových bajtov nie je ticho. `WaitIdle`
(s rečou) a `WaitSilent` preto rátajú ako činnosť aj **čas, kým
syntezátor hovorí** — `C621h` je `FFh` len počas reči (HANDOFF, `spabrt`;
nastaví `002D5`, zmaže `0055E`), v session `Speaking()`. Bez toho záznam
po F2 obsahoval len „21 HODINA.“; s tým „21 HODINA. 8 MINUT.“.

**Hranica odhadu ticha (24. 9. 2026):** Escape z hodín povie „ahoj“,
potom je **viac než pol sekundy** ticho (ani konzola, ani reč, syntezátor
nehovorí) a až potom zaznie znelka hlavného menu `!%t154/50*5!%t230`.
Predvolený `WaitIdle` skončí v tej medzere a ďalší kláves sa stratí pri
prechode do menu — v režime `session` sa takto stratilo F10. Sonda to
ukáže tiež: po `s01` vráti prvá `.` prázdno, až druhá znelku. Scenár si
tu musí pomôcť sám (`WaitSaid("ahoj")` a potom `WaitIdle` s oknom 2 s);
natrvalo by to vyriešilo „čaká na kláves“ podľa PC, viď nižšie.
*Vyriešené v ten istý deň, obchádzka z režimu `session` zmizla — viď
„Čakanie na kláves“.*

Niektoré odpovede sú dlhé: F11 prečíta všetkých päť ROM s dátumami
a trvá dlhšie než predvolených 10 s `WaitIdle`. To nie je chyba čakania —
taký krok potrebuje väčší rozpočet alebo `WaitSaid` na vetu, o ktorú ide.

Možné vylepšenie neskôr: zoznam adries čakacích slučiek na kláves
a `WaitIdle` ako „PC je v jednej z nich“. Bolo by to skutočné „čaká na
kláves“ namiesto odhadu, ale každá aplikácia môže mať vlastnú slučku
a jedna zabudnutá by sa neprezradila.

**Doplnené 24. 9. 2026 — urobené, a `WaitIdle` už nestojí na tichu.**
Odseky vyššie o tom, že `WaitIdle` počúva konzolu a reč a ráta čas, kým
syntezátor hovorí (`Speaking()`), **pre testy už neplatia**: medzeru po
„ahoj“ ticho rozlíšiť nevedelo. Režim s rečou (`kConsoleAndSpeech`) je
preč; `WaitIdle` sa predvolene pýta firmvéru (`Quiet::kKeyPrompt`), sonda
ostáva na tichu konzoly (`Quiet::kConsole`). `Speaking()` zostáva pre
`WaitSilent` a na priamu otázku. Viac nižšie v „Čakanie na kláves“.

### Čakanie na kláves (24. 9. 2026)

Zmerané subagentom na bežiacej ROM (pracovné poznámky
v `build\keywait\progress.md`, mimo gitu). Obava zo zoznamu slučiek sa
potvrdila — hodiny rozdeľovač udalostí vôbec nepoužívajú — ale riešenie
je jedna adresa, nie zoznam:

- Blokujúci rozdeľovač udalostí je `19B41`–`19B6C`, neblokujúci dopyt
  `19B6D`–`19B94`; hodiny volajú ten druhý zo svojej slučky (`0DFB3`).
- Oba volajú **kontrolu klávesu na fyzickej `18675h`** (logicky `D675h`,
  slot 2 tabuľky kontrol na `EBDDh`), ktorá číta udalosť klávesu
  v `C677h`. Kým stroj čaká, beží každých 0,3–0,5 ms (najdlhšia medzera
  0,54 ms v hodinách).
- Kým hovorí (`00213`–`00236`), formátuje, bootuje alebo sedí
  v oneskorovacej slučke `19CD9`–`19CEC` po „ahoj“, nebeží vôbec.
  Najdlhší falošný beh mimo čakania bol 4,6 ms (prechod rozdeľovačom hneď
  po Escape).

Predikát `WaitingForKey()`: kontrola bežala bez medzery dlhšej než 2 ms
aspoň 20 ms, `C677h` je 0 a v strojových frontách nie je kláves.
Pravda v hlavnom menu, v hodinách po dohovorení, na otázke formátovania,
v BASICu, v READ.COM a v záznamníku; nepravda počas reči, formátovania,
štartu, v medzere po „ahoj“ a počas tónov menu. Overené posielaním
klávesov: F10 v okamihu, keď predikát začne platiť po Escape, povie
„hlavní menu“; F10 300 ms po „ahoj“ sa stratí.

V režime `session` overené mutáciou: návrat k tichu konzoly zhodí minúty
po F2 aj F10 po Escape; zrušenie 20 ms behu zhodí F10 po Escape.
Podmienka na strojové fronty je len poistka — bez nej režim prejde aj
s písaním riadku, ROM má kláves v `C677h` do 20 ms.

Výhrady: nezmerané pri súvislom čítaní a pri programe v BASICu, ktorý sa
pýta na kláves (`INKEY`) — tam môže ROM kontrolovať klávesy uprostred
práce a predikát by platil, hoci stroj niečo robí (kláves by však prijal).
Vstup zo sériového portu a fronty `C510`/`C59A` predikát nesleduje.

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

### Reč sa vypisuje s diakritikou, porovnáva bez nej (24. 9. 2026)

Dnešné `Readable` v sonde reč len zbavuje diakritiky, a to **chybne**:
jeho tabuľka pre `80h`–`ABh` má o znak viac, než má, a od istého miesta
je posunutá — `ř` (`A9h`) vychádza ako `s`, „Přeformátovat“ ako
„Pseformatovat“. Správny dekodér projekt má (`DecodeKamenicky`
v `src/text_codec`) a sonda ho má zlinkovaný.

- **Výpis** (sonda, výnimka, záznam): `DecodeKamenicky` → UTF-8, text
  s diakritikou. Konzola to znesie, správa diagnostiky sa tak vypisuje
  už dnes.
- **Porovnávanie** (`?text`, `WaitSaid`): diakritiky sa zbaví **oboje**,
  počuté aj hľadané. `?vlož` aj `?vloz` nájdu „vlož“, staré príkazy
  v HANDOFF platia ďalej a pravidlo „porovnávaj len ASCII kúsky“
  z CLAUDE.md prestane byť potrebné.
- **Riadiace sekvencie syntezátora** (`!%t154/50*5!%t230` — dva tóny po
  resete) sa vypisujú ďalej. Sú to tiež reč: syntezátor ich premení na
  zvuk ako všetko ostatné.
- **Poradie:** samostatný krok **po** refaktore sondy (krok 1b).
  Refaktor musí najprv dať bajtovo zhodný výstup s referenciou; potom sa
  referencia vyrobí znova a v rozdiele smú byť len riadky s rečou.
  Obe zmeny naraz by porovnanie znehodnotili.

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

## Krok 3 — kontroly na session

**Prvá časť hotová 24. 9. 2026: režim `session` v `integration_test`.**
Test samotnej session, hlavne čakania — keby sa pokazilo, padali by
scenáre nad ním a chyba by sa hľadala v hodinách alebo disketách.
Kontroly: prepis, záznam F10 (reč s diakritikou aj bez, zvuk), `Watch`
na `C621h`, `WaitUntil` na začiatok a koniec reči, `WaitSilent`, záznam
po F2 s hodinou aj minútami, záznam cez reset, výnimka z `WaitSaid`
s tým, čo sa počulo. Overené mutáciou: bez `Speaking()` vo `WaitIdle`
režim padne s „WaitIdle nepockal na minuty po F2: "21 HODINA."“.
`run-tests.bat` má odteraz **19** riadkov `PASS` (17 bez manuálu);
CLAUDE.md a `tests/README.md` opravené.

*Doplnené v ten istý deň:* mutácia so `Speaking()` sa už nedá zopakovať —
`WaitIdle` sa pýta firmvéru (viď „Čakanie na kláves“). Pribudla kontrola,
že F10 hneď po Escape z hodín povie „hlavní menu“, a obchádzka s 2 s
oknom zmizla. Platné mutácie sú dnes dve: ticho konzoly namiesto kontroly
klávesu (padnú minúty po F2 aj F10 po Escape) a zrušený 20 ms beh (padne
F10 po Escape).

Zostáva: preniesť dve alebo tri existujúce scenárové kontroly
z `integration_test` na session a pri jednej overiť mutáciou, že chytá,
čo chytala.

**Druhá časť hotová 25. 9. 2026:** na session sú prenesené
`CheckProtectedDiskRefusesFormat` (režim `wp`) a `CheckAnnouncesTime`
(režim `rtc`). Pevné rozpočty (8, 12 a 40 miliónov inštrukcií) nahradili
`WaitIdle`, `WaitSaid` na otázku a na odmietnutie a `WaitUntil` na prvú
naformátovanú stopu; posun hodín ide cez `ShiftClock` a na konci sa vráti.

Pri prenose sa ukázalo, že pôvodná kontrola posielala **druhé `y`
zbytočne**. Sonda (`nova +wp studeno . kD7 ?ne y ?proti`) ukázala, že
nenaformátovaná disketa dostane jedinú otázku „mám formátovat disk, ano
nebo ne?“ a hneď po „ano“ zaznie „disk je chráněn proti zápisu“; druhá
otázka „preformátovať“ prichádza len pri diskete s formátom. Prenesená
kontrola posiela jedno `y`.

Overené mutáciou, oboma verziami kontroly: keď stavový bit radiča
(`machine.cpp`, `kFdcStatusWriteProtect` v stave po príkaze typu I)
ochranu nehlási, pôvodná aj prenesená padnú na tom istom — stroj povie
„formátovací chyba“ namiesto „chráněn proti zápisu“. Prenesená navyše
povie čas a PC (`WaitSaid(„chráněn proti zápisu“) sa nedočkal po 20 s,
PC 18685`) a reč s diakritikou.

Druhá mutácia **neprešla ani jednou verziou**: vetva Write Track, ktorá
na chránenej diskete vráti `kFdcStatusWriteProtect`, sa dá vypnúť a režim
`wp` prejde. ROM odmietne skôr, podľa stavového bitu, takže na Write Track
nedôjde. Tú vetvu dnes nedrží nič v tomto režime; či ju drží iný, sa
nezisťovalo.

Pasca v rozhraní, na ktorú sa pri prenose narazilo: `WaitSaid` hľadá
v reči **od posledného `TakeSpeech()`**, nie od začiatku kroku. Sonda
berie reč po každom tokene, takže to nevidí; test, ktorý ju neberie, by
v druhej polovici našiel otázku z prvej hneď. Kontrola preto volá
`TakeSpeech()` pred každou otázkou. Či to má riešiť session (napríklad
`WaitSaid` od značky), zostáva otvorené.

*Vyriešené v ten istý deň:* `WaitSaid` počúva **odpoveď na posledný
vstup** — reč od posledného klávesu (`Press`, `Pc`, `Type`, `Hold`,
`Release`, `ReleaseAll`), resetu alebo `TakeSpeech()`, podľa toho, čo
bolo neskôr. Sonda berie reč po každom tokene, takže sa pre ňu nič
nezmenilo: všetkých desať referenčných výstupov bajtovo zhodných.
`TakeSpeech()` z kontroly `wp` zmizlo. Drží to nová kontrola v režime
`session`: po dvoch „hlavní menu“, ktoré nikto nevzal, F2 nesmie
„hlavní menu“ nájsť. Overené mutáciou: bez značky v `Press` režim padne
s „WaitSaid nasiel odpoved spred klavesu“.

## Krok 2 — záznam a pamäť

**Stav 24. 9. 2026: hotový.** V `EurekaSession` pribudli:

- priebežný prepis reči a konzoly od vzniku session, ktorý nemaže ani
  reset (`Transcript()`);
- záznam: `StartRecording()`/`StopRecording(značka)` a `Record(lambda)`,
  výsledok `Recording` s rečou, konzolou a zvukom a s `Said()`, `Heard()`,
  `Shown()`. Zvuk sa zo stroja preleje len na hraniciach záznamu
  a pri `TakeAudio`; odkladá sa, len kým beží aspoň jeden záznam, a pred
  resetom sa vyzbiera, takže záznam cez reset drží obe strany. Výnimka
  z tela `Record` záznam uzavrie a letí ďalej;
- pamäť: `Peek`, `Speaking()`, `TryWaitUntil`/`WaitUntil`, `Watch`
  a `Unwatch` (kontrola po inštrukcii, len keď niečo sledujeme);
- `Run(trvanie)` — len bež.

Overené skúšobným programom mimo repozitára (`build\scratch_session.cpp`):
záznam F10 obsahuje „hlavní menu“ aj zvuk, `Watch` na `C621h` videl dve
zmeny, `WaitUntil` na začiatok a koniec reči, `WaitSilent`, záznam po F2
s hodinou aj minútami, akord F11, záznam cez reset, výnimky z `WaitSaid`
a `Type`. Sonda bajtovo zhodná s referenciou, 18× PASS.

## Krok 1 — sonda nad `EurekaSession`

**Stav 24. 9. 2026: časť A hotová.** V kóde: `tests/eureka_keys.h`,
`tests/eureka_session.h` a `.cpp`, sonda prepojená, `SESSION_OBJS`
v `Makefile`. Cez session idú všetky kroky stroja, reset, zapnutie,
zobudenie budíkom, čakania (`.`, `?`, `@`), klávesy (`kXX` a `sXX` cez
`Raw`), text a celá rodina `+b…`/`-b`. Overenie: všetkých desať
sekvencií `tests/probe_golden.sh` je bajtovo zhodných s referenciou;
`sweep` a `trace` aj tokeny `cas:`/`budik` sú zhodné so starou sondou
zostavenou z gitu (pri hodinách sa líšili len sekundy hostiteľa medzi
dvoma behmi); `run-tests.bat` 18× PASS.

**Časť B hotová v ten istý deň:** diskety a zásobník
(`TrySettleForSwap`, `InsertFromSlot`, `InsertBlank`, `Eject`, `Mount`,
`Protect`), vypínač (`TryPowerOff`), posun hodín (`ShiftClock`, súčet
posunov drží session) a posuvníky (`SetRate`, `SetVolume`). Sonda už
na stroji len číta stav a zapína diagnostiku. Overenie rovnaké ako pri
časti A, navyše sekvencia `slot2 +wp stav slot1 stav slot2 stav
mount:… stav ram stav folder stav` zhodná so starou sondou; 18× PASS.
**Krok 1b hotový v ten istý deň:** `eureka::Readable` dekóduje cez
`DecodeKamenicky` do UTF-8, riadiace znaky ostávajú ako `.`;
`eureka::Contains` porovnáva bez diakritiky na oboch stranách (rozklad
znaku robí Windows, `NormalizeString`, rovnako ako `EncodeKamenicky` —
žiadna vlastná tabuľka). Sonda odovzdáva hľadaný text ako UTF-8 (predtým
orezávala znaky na bajt, `?vlož` by nenašlo nič) a stĺpec tokenu
zarovnáva podľa znakov, nie bajtov. Nová referencia sa od starej líšila
**len v riadkoch s rečou** („hlavní menu“, „záznamník“, „Přeformátovat“),
počty inštrukcií a správa diagnostiky zhodné. Overené: `?uz` aj `?už`
nájdu „už“, `?Preformatovat` aj `?Přeformátovat` nájdu „Přeformátovat“.
CLAUDE.md (token `?text`) opravený. 18× PASS.

Dve odchýlky od pôvodného kódu, ktoré referencia nezachytí, lebo ležia
na hrane rozpočtu: `?text`, ktorý text dostane práve poslednou
inštrukciou rozpočtu, sa teraz ohlási ako nájdený (predtým `NEDOCKAL
SA`); a `@TEXT`, ktorý text nájde ešte pred prvou inštrukciou, zahodí
nahromadený zvuk (predtým nie).

Pôvodný návrh nasleduje.

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
   Potom krok 1b: reč s diakritikou (viď Rozhodnuté).
2. Pridať záznam a sledovanie pamäte.
3. Preniesť dve alebo tri scenárové kontroly z `integration_test` ako
   ukážku; aspoň pri jednej mutáciou overiť, že stále chytá, čo chytala.
4. Zvyšok prenášať, až keď sa na danú kontrolu siahne.
