import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
# ROM path: first argument that looks like a file, else $A4ROM, else the
# location the dump was analysed from.
ROM_PATH = _os.environ.get('A4ROM', r'C:\b\a4rom.dmp')

import re, wave, struct, os
ROM = open(ROM_PATH, 'rb').read()
# Skutocny takt DAC pri hrani melodie: obsluha generatora tonov v RAM bezi
# phi / (20 * (RLDR0 + 1)) = 6 144 000 / 540 = 11 377,8 Hz (RLDR0 = 26,
# zmerane histogramom PC, HANDOFF 6.10). Do 23. 8. 2026 tu stalo 22050 ako
# standardny nadvzorkovany takt; bead ea4-upc chce zmeranu frekvenciu.
# Faza sa skaluje s SR, takze vyska tonu ostava, meni sa len vystupny takt.
SR  = 11378
OUT = _os.path.join(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))), "audio", "melodie")
os.makedirs(OUT, exist_ok=True)
WAVES = [[ROM[0x677+n*64+i]-0x20 for i in range(64)] for n in range(10)]

SOUND = re.compile(rb'^(?:!%[Tt])?([0-9]*(?::[0-9]*)*)(?:/([0-9]+))?(?:\*([0-9]+))?$')

def expand(s):
    if s.startswith(b'&'): return b'!%T' + s[1:]
    if s.startswith(b'$'): return b'!%T77:78' + s[1:]
    return s

def parse(s):
    m = SOUND.match(expand(s))
    if not m: return None
    fields = m.group(1).split(b':') if m.group(1) else []
    if not fields and not m.group(2) and not m.group(3): return None
    v = [0,0,0,0]
    for k,f in enumerate(fields[:4]): v[k] = int(f) if f else 0
    return v, (int(m.group(2)) if m.group(2) else None), (int(m.group(3)) if m.group(3) else None)

def cmds_of(s):
    """retazec -> zoznam prikazov (CR je oddelovac); None ak to nie je zvuk"""
    parts = [p for p in s.split(b'\r') if p]
    if not parts: return None
    out=[]
    for p in parts:
        if not (p.startswith(b'&') or p.startswith(b'$') or p.startswith(b'!%T') or p.startswith(b'!%t')):
            return None
        q = parse(p)
        if q is None: return None
        out.append((p,q))
    return out

# --- najdi vsetky skupiny susediacich zvukovych retazcov ---
groups=[]; i=0
while i < len(ROM)-1:
    if ROM[i] in (0x26,0x24) or ROM[i:i+3] in (b'!%T',b'!%t'):
        start=i; grp=[]; j=i
        while True:
            k = ROM.find(b'\x00', j)
            if k < 0 or k-j > 48 or k==j: break
            s = ROM[j:k]
            if not all(32 <= c < 127 or c==13 for c in s): break
            c = cmds_of(s)
            if c is None: break
            grp.append((j,s,c)); j = k+1
        if grp:
            groups.append((start,grp)); i = j; continue
    i += 1

def render(grp, dur=200, wv=8):
    ph=[0.0]*4; out=[]
    for _,_,cl in grp:
        for _,(voices,d,w) in cl:
            if d is not None: dur = d
            if w is not None and w < 10: wv = w
            n = max(1,int(SR*dur/1000)); tbl=WAVES[wv]; fade=max(1,int(SR*0.001))
            for k in range(n):
                acc=0.0; live=0
                for v in range(4):
                    f=voices[v]
                    if f<=0: continue
                    live+=1; acc += tbl[int(ph[v])&63]/32.0; ph[v]+=64.0*f/SR
                if live: acc/=live
                if k<fade: acc*=k/fade
                elif k>n-fade: acc*=max(0,n-k)/fade
                out.append(acc)
    return out

def save(name, s):
    p=os.path.join(OUT,name)
    w=wave.open(p,'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(b''.join(struct.pack('<h',max(-32767,min(32767,int(x*26000)))) for x in s)); w.close()
    return p, len(s)/SR

n=0
for start,grp in groups:
    ncmd = sum(len(c) for _,_,c in grp)
    if ncmd < 2: continue
    n+=1
    print(f'--- {start:05x}  ({ncmd} prikazov)')
    for off,s,_ in grp: print(f'      {off:05x}  {s.decode().replace(chr(13)," | ")}')
    p,d = save(f'{start:05x}.wav', render(grp))
    print(f'      -> {os.path.basename(p)}  {d:.2f} s')
print(f'\nspolu {n} melodii')

# --- jednopríkazové UI zvuky ---
extra=[('start_eureky',[b'!%t154/50*5',b'!%t230']),
       ('menu_pip',[b'!%T300:440/100*5']),
       ('pip_kratky',[b'!%T300/100*5']),
       ('modem_pip',[b'!%T1000/10',b'!%T400/100'])]
for name,cl in extra:
    grp=[(0,b'',[(c,parse(c)) for c in cl])]
    p,d=save(name+'.wav',render(grp)); print(f'{name}: {d:.2f} s')

import math
NAMES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','H']
def note(f):
    if f<=0: return 'ticho'
    n=round(12*math.log2(f/440.0))+57
    return f'{NAMES[n%12]}{n//12}'
print('\nprepis frekvencii na noty:')
seen=set()
for start,grp in groups:
    for _,s,cl in grp:
        for raw,(v,d,w) in cl:
            fs=[x for x in v if x>0]
            if not fs: continue
            k=tuple(fs)
            if k in seen: continue
            seen.add(k)
            print(f'   {" ".join(str(x) for x in fs):24s} = {" ".join(note(x) for x in fs)}')
