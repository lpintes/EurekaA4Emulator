# Pôvod zvukov disketovej mechaniky

Zvuky sú nahrávky **skutočnej disketovej mechaniky Eureky A4**. Nahral ich
**Luboš Krejča**. Všetky súbory sú WAV, 48 kHz, mono, 16 bit.

## Licencia

Nahrávky sa šíria pod rovnakou licenciou ako celý projekt, **MIT**
(`LICENSE.txt` v koreni). Autorom nahrávok je Luboš Krejča.

Autor s tým súhlasil v októbri 2026, ústne, v rozhovore s majiteľom
projektu: súhlasí, aby sa nahrávky stali súčasťou projektu a šírili sa
pod licenciou MIT, a želá si, aby sa pri nich uvádzalo jeho meno.

## Ako vznikli

Z nahrávok autora ich vystrihol skript `tools/sada-mechaniky.sh`
(`ffmpeg`), ktorý z tých istých nahrávok zostaví bajtovo rovnakú sadu.
Celá sada je zosilnená jedným spoločným ziskom 1,4, takže zvuky medzi
sebou zostali v pomere, v akom zneli na stroji:

- `motor-rozbeh.wav` — prvých 3,5 s nahrávky „jednotka jiná verze“
  (25. 10. 2026, Eureka sa v nej snaží čítať disketu 1,44 MB z PC), so
  zapnutím motora a jeho utíšením;
- `motor-slucka.wav` — 3,5 až 12,2 s tej istej nahrávky, ustálený motor;
  emulátor ho hrá po náhodných úsekoch celých otáčok, takže konce na seba
  nadväzovať nemusia;
- `motor-dobeh.wav` — 12,45 s až koniec tej istej nahrávky, s vypnutím
  motora v 12,7 s; nahrávka končí počas dozvuku, preto sa posledných 30 ms
  stlmí;
- `krok-1.wav` — nahrávka „Pohyb7“, jeden krok hlavičky;
- `presun.wav` — nahrávka „pohyb1“, hrabanie hlavičky pri rýchlosti kroku
  6 ms, ktorú používa ROM.

Krok a hrabanie sú z prvej série nahrávok (október 2026). Do 16. 11. 2026
bol aj motor z nej, z nahrávky „Diskove funkce“: 0,75 s slučka, ktorú bolo
počuť opakovať sa, a dobeh dorobený dvojsekundovým stlmením, lebo nahrávka
skončila skôr než motor.

Podrobnosti aj meranie, ktoré k výberu viedlo, sú v HANDOFF 6.54.

## Predchádzajúca sada

Do 24. 10. 2026 tu bola sada z cudzích 3,5" mechaník (motor zo vzoriek
Flopster od autora Shiru pod licenciou CC BY, kroky z Wikimedia Commons
a hrabanie z BigSoundBank pod CC0). Je v histórii gitu, naposledy
v commite `2af55a9`; jej pôvod a licencie sú v tamojšom `PUVOD.md`.
