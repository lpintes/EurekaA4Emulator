R=['B','C','D','E','H','L','(HL)','A']
RP=['BC','DE','HL','SP']; RP2=['BC','DE','HL','AF']
CC=['NZ','Z','NC','C','PO','PE','P','M']
ALU=['ADD A,','ADC A,','SUB ','SBC A,','AND ','XOR ','OR ','CP ']
ROT=['RLC','RRC','RL','RR','SLA','SRA','SLL','SRL']
Z180R={0x00:'CNTLA0',0x01:'CNTLA1',0x02:'CNTLB0',0x03:'CNTLB1',0x04:'STAT0',0x05:'STAT1',
 0x06:'TDR0',0x07:'TDR1',0x08:'RDR0',0x09:'RDR1',0x0a:'CNTR',0x0b:'TRDR',0x0c:'TMDR0L',
 0x0d:'TMDR0H',0x0e:'RLDR0L',0x0f:'RLDR0H',0x10:'TCR',0x14:'TMDR1L',0x15:'TMDR1H',
 0x16:'RLDR1L',0x17:'RLDR1H',0x18:'FRC',0x20:'SAR0L',0x21:'SAR0H',0x22:'SAR0B',0x23:'DAR0L',
 0x24:'DAR0H',0x25:'DAR0B',0x26:'BCR0L',0x27:'BCR0H',0x28:'SAR1L',0x29:'SAR1H',0x2a:'SAR1B',
 0x2b:'DAR1L',0x2c:'DAR1H',0x2d:'DAR1B',0x2e:'BCR1L',0x2f:'BCR1H',0x30:'DSTAT',0x31:'DMODE',
 0x32:'DCNTL',0x33:'IL',0x34:'ITC',0x36:'RCR',0x38:'CBR',0x39:'BBR',0x3a:'CBAR',0x3f:'ICR'}
def _p(d,p): return d[p]|d[p+1]<<8
def _z(n): return Z180R.get(n, f'{n:02X}h')

def dis(d,p):
    """vrati (text, dlzka)"""
    o=d[p]
    if o==0xcb:
        b=d[p+1]; r=R[b&7]
        if b<0x40: return f'{ROT[b>>3]} {r}',2
        return f'{["BIT","RES","SET"][(b>>6)-1]} {(b>>3)&7},{r}',2
    if o==0xed:
        b=d[p+1]
        if b<0x40:
            lo=b&7; g=R[b>>3]
            if lo==0: return f'IN0 {g},({_z(d[p+2])})',3
            if lo==1: return f'OUT0 ({_z(d[p+2])}),{g}',3
            if b==0x04: return f'TST {g}',2
            if b==0x64: return f'TST {d[p+2]:02X}h',3
            if b==0x74: return f'TSTIO {d[p+2]:02X}h',3
            if b in (0x4c,0x5c,0x6c,0x7c): return f'MLT {RP[(b>>4)&3]}',2
            if b==0x76: return 'SLP',2
            return f'db ED,{b:02X}',2
        if b==0x64: return f'TST {d[p+2]:02X}h',3
        if b==0x74: return f'TSTIO {d[p+2]:02X}h',3
        if b&0xc7==0x40: return f'IN {R[(b>>3)&7]},(C)',2
        if b&0xc7==0x41: return f'OUT (C),{R[(b>>3)&7]}',2
        if b&0xcf==0x42: return f'SBC HL,{RP[(b>>4)&3]}',2
        if b&0xcf==0x4a: return f'ADC HL,{RP[(b>>4)&3]}',2
        if b&0xcf==0x43: return f'LD ({_p(d,p+2):04X}h),{RP[(b>>4)&3]}',4
        if b&0xcf==0x4b: return f'LD {RP[(b>>4)&3]},({_p(d,p+2):04X}h)',4
        return {0x44:'NEG',0x45:'RETN',0x4d:'RETI',0x46:'IM 0',0x56:'IM 1',0x5e:'IM 2',
                0x47:'LD I,A',0x4f:'LD R,A',0x57:'LD A,I',0x5f:'LD A,R',0x67:'RRD',0x6f:'RLD',
                0xa0:'LDI',0xa1:'CPI',0xa2:'INI',0xa3:'OUTI',0xa8:'LDD',0xa9:'CPD',
                0xb0:'LDIR',0xb1:'CPIR',0xb2:'INIR',0xb3:'OTIR',0xb8:'LDDR',0xb9:'CPDR'
               }.get(b,f'db ED,{b:02X}'),2
    if o in (0xdd,0xfd):
        ix='IX' if o==0xdd else 'IY'
        b=d[p+1]
        if b==0xcb:
            dd=d[p+2]; op=d[p+3]; t=f'({ix}{dd-256 if dd>127 else dd:+d})'
            if op<0x40: return f'{ROT[op>>3]} {t}',4
            return f'{["BIT","RES","SET"][(op>>6)-1]} {(op>>3)&7},{t}',4
        s,n=dis(d,p+1)
        if b==0x36:
            dd=d[p+2]; return f'LD ({ix}{dd-256 if dd>127 else dd:+d}),{d[p+3]:02X}h',4
        if '(HL)' in s:
            dd=d[p+2]; s=s.replace('(HL)',f'({ix}{dd-256 if dd>127 else dd:+d})'); return s,n+2
        return s.replace('HL',ix),n+1
    if o==0x00: return 'NOP',1
    if o==0x76: return 'HALT',1
    if 0x40<=o<0x80: return f'LD {R[(o>>3)&7]},{R[o&7]}',1
    if 0x80<=o<0xc0: return f'{ALU[(o>>3)&7]}{R[o&7]}',1
    if o&0xcf==0x01: return f'LD {RP[(o>>4)&3]},{_p(d,p+1):04X}h',3
    if o&0xcf==0x09: return f'ADD HL,{RP[(o>>4)&3]}',1
    if o&0xcf==0x03: return f'INC {RP[(o>>4)&3]}',1
    if o&0xcf==0x0b: return f'DEC {RP[(o>>4)&3]}',1
    if o&0xc7==0x04: return f'INC {R[(o>>3)&7]}',1
    if o&0xc7==0x05: return f'DEC {R[(o>>3)&7]}',1
    if o&0xc7==0x06: return f'LD {R[(o>>3)&7]},{d[p+1]:02X}h',2
    if o&0xc7==0xc0: return f'RET {CC[(o>>3)&7]}',1
    if o&0xcf==0xc1: return f'POP {RP2[(o>>4)&3]}',1
    if o&0xcf==0xc5: return f'PUSH {RP2[(o>>4)&3]}',1
    if o&0xc7==0xc2: return f'JP {CC[(o>>3)&7]},{_p(d,p+1):04X}h',3
    if o&0xc7==0xc4: return f'CALL {CC[(o>>3)&7]},{_p(d,p+1):04X}h',3
    if o&0xc7==0xc6: return f'{ALU[(o>>3)&7]}{d[p+1]:02X}h',2
    if o&0xc7==0xc7: return f'RST {o&0x38:02X}h',1
    e=lambda: p+2+(d[p+1]-256 if d[p+1]>127 else d[p+1])
    return {0x02:('LD (BC),A',1),0x0a:('LD A,(BC)',1),0x12:('LD (DE),A',1),0x1a:('LD A,(DE)',1),
      0x07:('RLCA',1),0x0f:('RRCA',1),0x17:('RLA',1),0x1f:('RRA',1),0x27:('DAA',1),
      0x2f:('CPL',1),0x37:('SCF',1),0x3f:('CCF',1),0x08:("EX AF,AF'",1),
      0x22:(f'LD ({_p(d,p+1):04X}h),HL',3),0x2a:(f'LD HL,({_p(d,p+1):04X}h)',3),
      0x32:(f'LD ({_p(d,p+1):04X}h),A',3),0x3a:(f'LD A,({_p(d,p+1):04X}h)',3),
      0x10:(f'DJNZ {e():04X}h',2),0x18:(f'JR {e():04X}h',2),
      0x20:(f'JR NZ,{e():04X}h',2),0x28:(f'JR Z,{e():04X}h',2),
      0x30:(f'JR NC,{e():04X}h',2),0x38:(f'JR C,{e():04X}h',2),
      0xc3:(f'JP {_p(d,p+1):04X}h',3),0xc9:('RET',1),0xcd:(f'CALL {_p(d,p+1):04X}h',3),
      0xd3:(f'OUT ({d[p+1]:02X}h),A',2),0xdb:(f'IN A,({d[p+1]:02X}h)',2),
      0xd9:('EXX',1),0xe3:('EX (SP),HL',1),0xe9:('JP (HL)',1),0xeb:('EX DE,HL',1),
      0xf3:('DI',1),0xfb:('EI',1),0xf9:('LD SP,HL',1),
     }.get(o,(f'db {o:02X}h',1))
