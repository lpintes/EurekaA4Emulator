# Ako urobiť vlastné zvuky disketovej mechaniky

Tento priečinok je sada zvukov, ktorú emulátor Eureky A4 hrá, keď je
v Nastaveniach zapnutá **Verná disketová mechanika (zvuk aj rýchlosť)**.
Sú to nahrávky skutočnej mechaniky Eureky. Rovnaká sada je zabudovaná priamo
v emulátore. Táto kópia slúži ako vzor: vypočujte si ju, nahraďte súbory
vlastnými a emulátor bude hrať vaše.

Pôvod a licencia súborov sú v `PUVOD.md`. Ak sadu ďalej šírite, posielajte
ho s ňou.

## Kam sadu dať

Priečinok sa musí volať **`zvuky-mechaniky`** a ležať vedľa súboru
s nastaveniami emulátora `nastavenia.txt`:

- v priečinku `config` vedľa `EurekaA4Emulator.exe`, ak taký priečinok
  existuje (prenosná inštalácia),
- inak v `%APPDATA%\EurekaA4`.

Emulátor ho načíta **pri štarte**, takže po výmene súborov ho treba zavrieť
a znovu spustiť. Keď sa vlastná sada nedá načítať a verná mechanika je
zapnutá, emulátor povie, ktorý súbor a prečo, a hrá zabudovanú sadu.

## Formát

Každý súbor je **WAV, PCM, 48 000 Hz, mono, 16 bitov**. Iný formát emulátor
odmietne a neprevzorkuje ho — zvuk s inou frekvenciou by hral v nesprávnej
výške. Sada musí byť **celá**: chýbajúci súbor znamená zabudovanú sadu.

## Súbory a čo robia

Emulátor zvuky nestrihá ani nenaťahuje, len ich spúšťa vo chvíľach, keď
mechanika naozaj niečo robí. Časy nižšie sú časy skutočnej mechaniky Eureky
(radič WD1772, 300 otáčok za minútu, krok 6 ms).

- **`motor-rozbeh.wav`** — motor sa zapína. Hrá sa raz, keď sa stojaci
  motor zapne, a hneď za ním pokračuje ustálený motor, takže koniec rozbehu
  má nadväzovať na začiatok `motor-slucka.wav` — najľahšie tak, že oba
  vystrihnete z jednej nahrávky v tom istom bode. Mechanika čaká na
  roztočenie **1,2 s**, až potom pohne hlavičkou. Motor Eureky je naplno
  takmer hneď, ale prvé asi tri sekundy hrá o 7 dB hlasnejšie a potom sa
  utíši; vzor má preto 3,5 s a to utíšenie je v ňom.
- **`motor-slucka.wav`** — ustálený motor, bez kroku. Čo sa s ním deje,
  závisí od dĺžky:
  - **aspoň 4,5 s, teda vyše 20 otáčok** — emulátor ho **nehrá dookola**, ale
    po náhodných úsekoch 5 až 15 celých otáčok, ktoré spája vždy na hranici
    otáčky. Žiadne poradie sa neopakuje, takže sa nedá rozpoznať ani pri
    minútovom formátovaní. Hranice otáčok si emulátor nájde v nahrávke sám;
    predpokladá pritom **300 otáčok za minútu**, ako má Eureka a každá
    3,5" mechanika. Konce nahrávky na seba nadväzovať nemusia. Čím je
    dlhšia, tým viac úsekov má z čoho vyberať; vzor má 8,7 s.
  - **kratšia než asi 4 s** — hrá sa **dookola** celá, takže koniec musí plynulo
    nadväzovať na začiatok, bez cvaknutia a bez zmeny hlasitosti. Krátku
    slučku počuť opakovať sa: 0,75 s aj 8,7 s dokola rozpoznal hudobník
    bez zaváhania.

  Motor beží ešte **1,8 s** po poslednej práci.
- **`motor-dobeh.wav`** — motor sa vypína. Hrá sa, keď motor zastane, a má
  znieť ako to, čo motor naozaj urobí: motor Eureky **nedobieha pomaly**,
  zvuk za 50 ms spadne asi na tretinu, slabo cvakne a nasleduje tichý
  dozvuk. Vzor má 0,6 s, z toho 0,3 s ustáleného motora pred vypnutím.
  Pomalé stlmenie namiesto skutočného vypnutia znie umelo. Keď mechanika
  počas dobehu znovu začne pracovať, emulátor skočí rovno do ustáleného
  motora (s 10 ms prelínaním), lebo disketa sa ešte točí.
- **`krok-1.wav`, `krok-2.wav` …** — **jeden krok hlavičky** o jednu stopu.
  Krokov môže byť ľubovoľne veľa, najmenej `krok-1.wav`; emulátor ich berie
  po poradí, kým nejaké číslo nechýba, a pri hraní ich strieda. Viac
  záberov toho istého kroku zabráni tomu, aby rad krokov znel ako jedna
  pečiatka — ale **len rovnaké kroky**: iný zvuk medzi nimi, napríklad prvý
  pohyb po roztočení, ktorý na Eureke znie ako dvojitý úder, by sa ozýval pri
  každom druhom kroku. Krok má byť **bez zvuku motora** — motor hrá pod ním
  zo slučky. Vzor má 0,1 s, z toho počuť asi 50 ms.
- **`presun.wav`** — **hrabanie** hlavičky pri presune o dve a viac stôp
  naraz. Hrá sa toľko, koľko presun trvá (6 ms na stopu, cez celú disketu
  asi pol sekundy), potom 15 ms doznieva. Dlhší presun než nahrávka ju
  opakuje dookola, takže aj tu pomôže plynulý koniec. Nahrajte ho pri
  rýchlosti kroku 6 ms — klepanie v ňom má prísť každých 6 ms; vzor má 0,56 s.

## Hlasitosť

Emulátor stlmí **celú sadu na štvrtinu** a medzi zvukmi nemení nič.
Nahrávajte preto všetko s rovnakým nastavením nahrávania, v pomere, v akom
to znie na skutočnom stroji, a dosť nahlas — vo vzore má ustálený motor
špičku okolo 1 200 (zo 32 767), rozbeh okolo 5 000, krok okolo 13 200
a hrabanie okolo 7 100.

## Ako nahrávať

Takto vznikla sada vo vzore a takto sa dá urobiť nová, napríklad z inej
Eureky:

- **Všetko jedným nastavením.** Ten istý mikrofón, tá istá vzdialenosť od
  mechaniky a tá istá úroveň nahrávania pre všetky nahrávky. Emulátor
  ich pomer nemení, takže motor nahraný bližšie by bol hlasnejší aj
  v emulátore.
- **Motor na dlho, bez krokov.** Vložte do Eureky **disketu 1,44 MB
  z PC**: Eureka sa ju snaží čítať a motor pritom točí asi 12 s bez
  jediného kroku. Nahrávajte od zapnutia motora až do chvíle, keď je po
  jeho vypnutí niekoľko sekúnd ticho. Z tej jednej nahrávky sa vystrihnú
  všetky tri zvuky motora: rozbeh od zapnutia, kým sa motor utíši (na
  Eureke asi 3,5 s), ustálený motor odtiaľ až kúsok pred vypnutie
  a vypnutie s dozvukom do ticha.
- **Krok.** Jeden krok hlavičky o jednu stopu. Nahrajte ich viac a
  vyberte rovnaké — prvý pohyb po roztočení znie na Eureke inak (dvojitý
  úder) a do sady nepatrí.
- **Hrabanie.** Presun hlavičky cez celú disketu pri rýchlosti kroku
  6 ms, ktorú používa ROM.
- **Strih.** Skript `tools/sada-mechaniky.sh` v zdrojákoch emulátora
  ukazuje presne, kde a ako bola strihaná sada vo vzore (`ffmpeg`); pre
  novú nahrávku sa v ňom menia len časy.

**Staršie sady fungujú ďalej.** Sada s krátkou slučkou, napríklad
predchádzajúca sada z cudzích mechaník s 3 s slučkou, sa hrá ako predtým,
dookola. Náhodné úseky sa zapnú samy, len keď je ustálený motor dlhý
aspoň asi 4 s (s istotou od 4,5 s); jeho súbor stačí nahradiť dlhším
a nič iné sa meniť nemusí.

Mechanika **mlčí, kým Eureka hovorí**, tak ako na skutočnom stroji: firmvér
počas reči na disketu nesiaha, motor len dobieha.
