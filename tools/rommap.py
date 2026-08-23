import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import math, collections
# ROM path: first argument that looks like a file, else $A4ROM, else the
# location the dump was analysed from.
ROM_PATH = _os.environ.get('A4ROM', r'C:\b\a4rom.dmp')

import sys, math, collections
d=open(ROM_PATH, 'rb').read()
print("size",len(d),hex(len(d)))
BS=4096
def ent(b):
    if not b: return 0
    c=collections.Counter(b); n=len(b)
    return -sum((v/n)*math.log2(v/n) for v in c.values())
print(f"{'blk':>4} {'off':>8} {'entropy':>7} {'print%':>6} {'ff%':>5} {'00%':>5}  top-bytes")
for i in range(0,len(d),BS):
    b=d[i:i+BS]
    p=sum(1 for x in b if 32<=x<127 or x in (10,13,9))/len(b)*100
    f=b.count(255)/len(b)*100
    z=b.count(0)/len(b)*100
    c=collections.Counter(b).most_common(3)
    top=" ".join(f"{k:02x}:{v}" for k,v in c)
    print(f"{i//BS:>4} {i:08x} {ent(b):7.3f} {p:6.1f} {f:5.1f} {z:5.1f}  {top}")
