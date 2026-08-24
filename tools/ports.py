import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import collections
# ROM path: first argument that looks like a file, else $A4ROM, else the
# location the dump was analysed from.
ROM_PATH = _os.environ.get('A4ROM', r'C:\b\a4rom.dmp')

from z180len import ilen
d=open(ROM_PATH, 'rb').read()
REG=[(0,0x40e0,'rec'),(0xa000,0xcf90,'basic'),(0xd000,0x15fa0,'aplikacie'),
     (0x18000,0x1e100,'os')]
out=collections.defaultdict(lambda: collections.defaultdict(list))
bc =collections.defaultdict(list)   # IN A,(C) / OUT (C),A
for lo,hi,name in REG:
    p=lo
    while p<hi-3:
        try: n=ilen(d,p)
        except Exception: p+=1; continue
        op=d[p]
        if op==0xd3: out['OUT'][d[p+1]].append((name,p))
        elif op==0xdb: out['IN'][d[p+1]].append((name,p))
        elif op==0xed and d[p+1] in (0x40,0x48,0x50,0x58,0x60,0x68,0x78):
            bc['IN (C)'].append((name,p))
        elif op==0xed and d[p+1] in (0x41,0x49,0x51,0x59,0x61,0x69,0x71,0x79):
            bc['OUT (C)'].append((name,p))
        p+=n
for kind in ('OUT','IN'):
    print(f'=== {kind} (n),A  ===')
    for port in sorted(out[kind]):
        hits=out[kind][port]
        mods=collections.Counter(m for m,_ in hits)
        loc=', '.join(f'{o:05x}' for _,o in hits[:6])
        print(f'  {port:02x}  x{len(hits):<3d} [{" ".join(f"{k}:{v}" for k,v in mods.items())}]  {loc}')
print(f"\nIN A,(C): {len(bc['IN (C)'])}x   OUT (C),A: {len(bc['OUT (C)'])}x")
