# Nástroje na analýzu ROM

Čistý Python 3, bez závislostí. ROM sa hľadá cez premennú `A4ROM`,
inak na `C:\b\a4rom.dmp`.

| nástroj | čo robí |
|---|---|
| `z180dis.py` | disassembler Z80/Z180 (knižnica, importuje sa) |
| `z180len.py` | dĺžky inštrukcií (knižnica) |
| `dis.py LO:HI [LO:HI …]` | disassembluje rozsahy fyzických adries |
| `strings_kam.py MIN LO HI` | reťazce s dekódovaním Kamenických/KEYBCS2 |
| `rommap.py` | entropia a zloženie ROM po 4 KB blokoch |
| `ports.py` | mapa I/O portov lineárnym rozmetaním inštrukcií |
| `latches.py` | odkazy na tiene riadiacich latchov + súhrn po bitoch |
| `melodies.py` | vyrenderuje melódie z ROM do `../audio/melodie/` |
| `speech_wav.py [ROM] [LO] [HI] [RATE]` | dáta reči (`20000h–40000h`) do `../audio/` ako 8-bit WAV pri φ/(20·41) ≈ 7493 Hz |

Adresy sú vždy **fyzické** (offset v dumpe), nie logické. Prepočet na
logické závisí od stavu MMU: pri common area 1 s CBR=0Bh platí
fyzická = logická + B000h.

```
set A4ROM=C:\b\a4rom.dmp
python dis.py 19837:19880
python strings_kam.py 8 0xd000 0xe000
python latches.py
```
