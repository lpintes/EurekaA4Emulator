# Ako urobiť vlastné zvuky disketovej mechaniky

Tento priečinok je sada zvukov, ktorú emulátor Eureky A4 hrá, keď je
v Nastaveniach zapnutá **Verná disketová mechanika (zvuk aj rýchlosť)**.
Rovnaká sada je zabudovaná priamo v emulátore. Táto kópia slúži ako vzor:
vypočujte si ju, nahraďte súbory vlastnými a emulátor bude hrať vaše.

Pôvod a licencie súborov sú v `PUVOD.md`. Ak sadu ďalej šírite, posielajte
ho s ňou — motor je pod licenciou CC BY a tá vyžaduje uviesť autora.

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

- **`motor-rozbeh.wav`** — motor sa roztáča z pokoja. Hrá sa raz, keď sa
  stojaci motor zapne, a hneď za ním pokračuje slučka. Mechanika čaká na
  roztočenie **1,2 s**, až potom pohne hlavičkou; vzor má 1,1 s.
- **`motor-slucka.wav`** — motor beží naplno. Hrá sa **dookola**, takže
  koniec musí plynulo nadväzovať na začiatok, bez cvaknutia a bez zmeny
  hlasitosti. Dĺžka nie je predpísaná; vzor má 3 s. Motor beží ešte
  **1,8 s** po poslednej práci.
- **`motor-dobeh.wav`** — motor dobieha do ticha. Hrá sa, keď motor
  zastane. Keď mechanika počas dobehu znovu začne pracovať, emulátor skočí
  rovno do slučky (s 10 ms prelínaním), lebo disketa sa ešte točí; vzor má
  0,5 s.
- **`krok-1.wav`, `krok-2.wav`, `krok-3.wav` …** — **jeden krok hlavičky**
  o jednu stopu. Krokov môže byť ľubovoľne veľa, najmenej `krok-1.wav`;
  emulátor ich berie po poradí, kým nejaké číslo nechýba, a pri hraní ich
  strieda, aby rad krokov neznel ako jedna pečiatka. Krok má byť **bez zvuku
  motora** — motor hrá pod ním zo slučky. Vzory majú 150 ms, z toho počuť
  asi 40 ms.
- **`presun.wav`** — **hrabanie** hlavičky pri presune o dve a viac stôp
  naraz. Hrá sa toľko, koľko presun trvá (6 ms na stopu, cez celú disketu
  asi pol sekundy), potom 15 ms doznieva. Dlhší presun než nahrávka ju
  opakuje dookola, takže aj tu pomôže plynulý koniec; vzor má 0,53 s.

## Hlasitosť

Emulátor stlmí **celú sadu na štvrtinu** a jeden **krok ešte na 0,3** toho.
Nahrávajte preto v pomere, v akom to znie na skutočnom stroji, a dosť
nahlas — vo vzore má slučka motora špičku okolo 1 800 (zo 32 767), krok
okolo 10 000 a hrabanie okolo 9 700. Krok tichší než vo vzore v emulátore
zanikne pod motorom.

Mechanika **mlčí, kým Eureka hovorí**, tak ako na skutočnom stroji: firmvér
počas reči na disketu nesiaha, motor len dobieha.
