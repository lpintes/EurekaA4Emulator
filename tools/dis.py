import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
# ROM path: first argument that looks like a file, else $A4ROM, else the
# location the dump was analysed from.
ROM_PATH = _os.environ.get('A4ROM', r'C:\b\a4rom.dmp')
import sys

from z180dis import dis
d=open(ROM_PATH, 'rb').read()
for arg in sys.argv[1:]:
    lo,hi=[int(x,16) for x in arg.split(':')]
    print(f'---------- {lo:05X}..{hi:05X}')
    p=lo
    while p<hi:
        s,n=dis(d,p)
        print(f'{p:05X}  {d[p:p+n].hex():10s} {s}')
        p+=n
