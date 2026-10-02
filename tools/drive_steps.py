"""Cuts head steps out of a drive recording, with the drive's own hum taken out.

    drive_steps.py IN.wav OUT_PREFIX NOISE_T0 NOISE_T1 STEP_T0 [STEP_T0 ...]

Each step is cut 150 ms wide from 20 ms before STEP_T0 and written as
OUT_PREFIX-1.wav, OUT_PREFIX-2.wav and on, with a 30 ms fade at the end.
NOISE_T0..NOISE_T1 (seconds) is a stretch between steps where only the
spindle runs; its average spectrum is subtracted from every step.

Why (HANDOFF 6.54): in the Wikimedia Commons recording the steps stand only
about 12 dB above the noise of the turning diskette.  In context the ear finds
them by their rhythm; cut out on their own they were heard as nothing but more
of that noise.  ffmpeg's afftdn could not take the noise out (it is not white,
the motor has tones in it), so this subtracts it per frequency: about 20 dB
less noise after the step, the step itself unchanged.

The steps in the drive sound set were made with
    drive_steps.py commons.wav krok 7.0 7.6 6.730 9.125 12.722
from that recording converted to 48 kHz mono.  Pure Python, no numpy.
"""
import array
import cmath
import math
import sys
import wave

N = 1024
HOP = 256
OVER = 2.0  # oversubtraction
FLOOR = 0.03


def fft(x):
    n = len(x)
    j = 0
    x = list(x)
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    size = 2
    while size <= n:
        w = cmath.exp(-2j * math.pi / size)
        for start in range(0, n, size):
            wk = 1
            for k in range(size // 2):
                a = x[start + k]
                b = x[start + k + size // 2] * wk
                x[start + k] = a + b
                x[start + k + size // 2] = a - b
                wk *= w
        size *= 2
    return x


def ifft(x):
    y = fft([v.conjugate() for v in x])
    return [v.conjugate() / len(x) for v in y]


win = [0.5 - 0.5 * math.cos(2 * math.pi * i / N) for i in range(N)]


def frames(sig):
    out = []
    for s in range(0, len(sig) - N, HOP):
        out.append(fft([sig[s + i] * win[i] for i in range(N)]))
    return out


def main():
    src, prefix = sys.argv[1], sys.argv[2]
    n0, n1 = float(sys.argv[3]), float(sys.argv[4])
    events = [float(v) for v in sys.argv[5:]]
    w = wave.open(src)
    rate = w.getframerate()
    a = array.array('h')
    a.frombytes(w.readframes(w.getnframes()))
    noise = a[int(n0 * rate):int(n1 * rate)]
    nf = frames(noise)
    profile = [sum(abs(f[k]) for f in nf) / len(nf) for k in range(N)]
    for idx, t in enumerate(events, 1):
        start = int((t - 0.02) * rate)
        seg = list(a[start:start + int(0.15 * rate) + N])
        out = [0.0] * len(seg)
        norm = [0.0] * len(seg)
        for s in range(0, len(seg) - N, HOP):
            spec = fft([seg[s + i] * win[i] for i in range(N)])
            clean = []
            for k in range(N):
                mag = abs(spec[k])
                keep = max(mag - OVER * profile[k], FLOOR * mag)
                clean.append(spec[k] * (keep / mag) if mag > 0 else 0)
            back = ifft(clean)
            for i in range(N):
                out[s + i] += back[i].real * win[i]
                norm[s + i] += win[i] * win[i]
        res = [out[i] / norm[i] if norm[i] > 1e-3 else 0.0 for i in range(len(out))]
        res = res[:int(0.15 * rate)]
        fade = int(0.03 * rate)
        for i in range(fade):
            res[-1 - i] *= i / fade
        o = wave.open(f"{prefix}-{idx}.wav", 'wb')
        o.setnchannels(1)
        o.setsampwidth(2)
        o.setframerate(rate)
        o.writeframes(array.array('h', [max(-32768, min(32767, int(v))) for v in res]).tobytes())
        o.close()
        before = (sum(v * v for v in seg[:int(0.015 * rate)]) / int(0.015 * rate)) ** .5
        peak = max(res[int(0.015 * rate):int(0.07 * rate)], key=abs)
        tail = (sum(v * v for v in res[int(0.09 * rate):int(0.12 * rate)]) / int(0.03 * rate)) ** .5
        print(f"krok {idx}: spicka {abs(peak):.0f}, sum za krokom {tail:.0f} (predtym {before:.0f})")


main()
