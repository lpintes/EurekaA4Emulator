import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
# ROM path: first argument that looks like a file, else $A4ROM, else the
# location the dump was analysed from.
ROM_PATH = _os.environ.get('A4ROM', r'C:\b\a4rom.dmp')

import wave, os

ROM = open(ROM_PATH, 'rb').read()

# Modul dat reci je poslednych 128 KiB dumpu (fyz. 20000h-40000h), 8-bit
# unsigned PCM. Timer 0 ho vysype na DAC (port 88h) rychlostou
#   phi / (20 * (RLDR0 + 1))
# phi je 6,144 MHz (dolozene: melodia bezi pri RLDR0=26 = 11 377,8 Hz,
# HANDOFF 6.10). Firmver RLDR0 pri reci nastavuje na 28h (40), takze
#   6 144 000 / (20 * 41) = 7492,68 Hz
# co je tych "asi 7,5 kHz", ktore manual uvadza pre obsluhu reci cez
# Timer 0. RLDR0=15h (21) je dvojnasobok pre zvukove efekty. Do 23. 8. 2026
# boli tieto WAV-y v styroch hadanych frekvenciach; toto je ta zmerana
# (bead ea4-upc, HANDOFF 6.1 "Co zostava").
PHI      = 6_144_000
RLDR0    = 0x28
RATE     = round(PHI / (20 * (RLDR0 + 1)))   # 7493

SPEECH_LO = 0x20000
SPEECH_HI = 0x40000

OUT = _os.path.join(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))), "audio")
os.makedirs(OUT, exist_ok=True)


def save(name, data, rate):
    p = os.path.join(OUT, name)
    w = wave.open(p, 'wb')
    w.setnchannels(1)
    w.setsampwidth(1)            # WAV 8-bit == unsigned, bajt na bajt z ROM
    w.setframerate(rate)
    w.writeframes(data)
    w.close()
    return p, len(data) / rate


# Volitelne argumenty: [ROM] [LO] [HI] [RATE]  (LO/HI/RATE su cisla, aj 0x...)
args = [a for a in _sys.argv[1:] if not _os.path.isfile(a)]
lo   = int(args[0], 0) if len(args) > 0 else SPEECH_LO
hi   = int(args[1], 0) if len(args) > 1 else SPEECH_HI
rate = int(args[2], 0) if len(args) > 2 else RATE

data = ROM[lo:hi]
p, dur = save(f'rec_{rate}Hz.wav', data, rate)
print(f'{lo:05x}-{hi:05x}  {len(data)} B  @ {rate} Hz  ->  {os.path.basename(p)}  {dur:.2f} s')
