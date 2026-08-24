import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import re, collections
# ROM path: first argument that looks like a file, else $A4ROM, else the
# location the dump was analysed from.
ROM_PATH = _os.environ.get('A4ROM', r'C:\b\a4rom.dmp')

from z180dis import dis
d=open(ROM_PATH, 'rb').read()
REG=[(0,0x40e0),(0xa000,0xcf90),(0xd000,0x15fa0),(0x18000,0x1e100)]
LAT={0xC438:'B0',0xC439:'80',0xC43A:'A0'}

stream=[]           # (addr, text, nbytes)
for lo,hi in REG:
    p=lo
    while p<hi-4:
        try: s,n=dis(d,p)
        except Exception: p+=1; continue
        stream.append((p,s,n)); p+=n
idx={a:i for i,(a,_,_) in enumerate(stream)}

hits=collections.defaultdict(list)
for i,(a,s,n) in enumerate(stream):
    m=re.search(r'\((C43[89A])h\)',s)
    if m: hits[int(m.group(1),16)].append(i)

masks=collections.defaultdict(lambda: collections.defaultdict(set))
print('POCET ODKAZOV')
for addr,name in LAT.items():
    print(f'  {addr:04X}h (port {name}): {len(hits[addr])}')
print()

for addr,name in LAT.items():
    print(f'================ {addr:04X}h -> OUT ({name}h),A ================')
    for i in hits[addr]:
        a,s,_=stream[i]
        if 'LD A,' not in s: continue          # zaujimaju nas citania pred modifikaciou
        win=stream[i:i+6]
        ops=[]
        for wa,ws,_ in win:
            ops.append(ws)
            if ws.startswith('OUT (') or ws.startswith('CALL') or ws.startswith('RET'): break
        line=' ; '.join(ops)
        print(f'  {a:05X}  {line}')
        for ws in ops:
            mm=re.match(r'(AND|OR|XOR) ([0-9A-F]{2})h$',ws)
            if mm:
                v=int(mm.group(2),16)
                if mm.group(1)=='AND':
                    for b in range(8):
                        if not v>>b&1: masks[addr][b].add('clr')
                elif mm.group(1)=='OR':
                    for b in range(8):
                        if v>>b&1: masks[addr][b].add('set')
                else:
                    for b in range(8):
                        if v>>b&1: masks[addr][b].add('tgl')
            mm=re.match(r'(RES|SET) ([0-7]),A$',ws)
            if mm: masks[addr][int(mm.group(2))].add('clr' if mm.group(1)=='RES' else 'set')
    print()

print('================ SUHRN BITOV ================')
for addr,name in LAT.items():
    print(f'{addr:04X}h / port {name}h:')
    for b in range(7,-1,-1):
        k=masks[addr].get(b)
        print(f'   bit {b} ({1<<b:02X}h): {"riadeny: "+",".join(sorted(k)) if k else "-"}')
    print()
