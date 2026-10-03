# Pôvod zvukov disketovej mechaniky

Zvuky sú nahrávky skutočných 3,5" mechaník, nie mechaniky Eureky A4. Tú
zatiaľ nikto nenahral; keď sa to stane, nahradí táto sada (HANDOFF 6.54).
Všetky súbory sú WAV, 48 kHz, mono, 16 bit. Ako vznikli zo zdrojov, je
zapísané do posledného čísla v HANDOFF 6.54 („Zvolená sada, verzia 8“).

Licencia projektu (MIT) sa na tieto súbory nevzťahuje. Platí licencia ich
autorov:

| súbory | zdroj | autor | licencia |
|---|---|---|---|
| `motor-rozbeh.wav`, `motor-slucka.wav`, `motor-dobeh.wav` | `spindle.wav` zo vzoriek Flopster, mechanika NEC FD1231H; utlmené výšky a klapanie | Shiru (2017) | [CC BY](https://creativecommons.org/licenses/by/4.0/) |
| `krok-1.wav`, `krok-2.wav`, `krok-3.wav` | „Floppy drive sounds.ogg“ z Wikimedia Commons; vystrihnuté kroky s odčítaným šumom točenia | AlepouTheFox (2013) | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `presun.wav` | „Floppy disk (3.5"), reading“ z BigSoundBank (zvuk č. 1396); dva úseky hrabania hlavičky | Joseph Sardin | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |

Odkazy na pôvodné nahrávky:

- Flopster: <https://github.com/linuxmao-org/shiru-plugins> (`reference/flopster-1.01/samples`), autor <http://shiru.untergrund.net/>
- Wikimedia Commons: <https://commons.wikimedia.org/wiki/File:Floppy_drive_sounds.ogg>
- BigSoundBank: <https://bigsoundbank.com/detail-1396-floppy-disk-3-5-reading.html>

Zmeny oproti originálom (CC BY ich vyžaduje uviesť): zo `spindle.wav` je
prevzorkovaný a pásmovou priepusťou nad 1500 Hz a kompresorom stlmený motor,
rozdelený podľa značiek slučky v pôvodnom súbore.
