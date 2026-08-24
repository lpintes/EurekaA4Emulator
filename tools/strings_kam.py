import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
# ROM path: first argument that looks like a file, else $A4ROM, else the
# location the dump was analysed from.
ROM_PATH = _os.environ.get('A4ROM', r'C:\b\a4rom.dmp')

import sys
HI = ("Čüéďä" "ĎŤčěĚĹÍľĺÄÁÉ" "žŽôöÓůÚýÖÜŠĽÝŘť" "áíóúňŇŮÔšřŕŔ¼§«»")
tbl=[]
for i in range(128):
    tbl.append(chr(i))
for i,ch in enumerate(HI):
    tbl.append(ch)
while len(tbl)<256: tbl.append("·")
def dec(b): return "".join(tbl[x] for x in b)
if __name__=="__main__":
    d=open(ROM_PATH, 'rb').read()
    minlen=int(sys.argv[1]); lo=int(sys.argv[2],0); hi=int(sys.argv[3],0)
    def ok(c): return 32<=c<127 or 128<=c<176
    cur=b"";start=0;out=[]
    for i in range(lo,hi):
        c=d[i]
        if ok(c):
            if not cur: start=i
            cur+=bytes([c])
        else:
            if len(cur)>=minlen: out.append((start,cur))
            cur=b""
    if len(cur)>=minlen: out.append((start,cur))
    for s,b in out: print(f"{s:06x} {dec(b)}")
