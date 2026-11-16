"""Execute only the original coordinate routine in an isolated x86 CPU emulator.
No DLL initialization or native code execution; vector/Win32 calls are mocked.
Requires tools/oracle-deps unicorn==2.1.4 and spectrum-static-tools/pefile.
"""
from pathlib import Path
import sys,struct,json,hashlib,os
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/oracle-deps'),str(ROOT/'spectrum-static-tools')]
import pefile
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *
DLL=Path(os.environ.get('CSP_ORIGINAL_DLL', str(ROOT/'tools/foo_uie_vis_channel_spectrum.dll')))
def oracle(width=800,height=200,bins=1024,channels=2,pitch=1,spacing=0,linear=0,direction=0,reverse=0,reverse_channels=0,gap=0,fft=0,log=0,scale=100):
    data=DLL.read_bytes()
    assert hashlib.sha256(data).hexdigest()=='e5bc0405fd9da2a59d9889fffc6fc21f8e9da71dc972d68c5e5218794a038c4b'
    pe=pefile.PE(data=data);u=Uc(UC_ARCH_X86,UC_MODE_32)
    u.mem_map(0,0x1000);u.mem_map(0x200000,0x1000000);u.mem_map(0x10000000,0x40000)
    u.mem_write(0x10000000,pe.get_memory_mapped_image())
    def write(a,*values):u.mem_write(a,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
    def read(a):return struct.unpack('<I',u.mem_read(a,4))[0]
    def ret(value=0,extra=0):
        sp=u.reg_read(UC_X86_REG_ESP);u.reg_write(UC_X86_REG_EAX,value&0xffffffff)
        u.reg_write(UC_X86_REG_EIP,read(sp));u.reg_write(UC_X86_REG_ESP,sp+4+extra)
    arrays={};nxt=0x400000
    def arr(a):
        nonlocal nxt
        if a not in arrays:
            arrays[a]=[];write(a+4,nxt);nxt+=0x20000
        return arrays[a]
    # A small vector ABI: get_count, unused, clear, ..., replace_at.
    write(0x300000,*[0x301000+i*16 for i in range(8)])
    for base in (0x310000,0x311000):
        for i in range(19):write(base+i*16,0x300000);arr(base+i*16)
    write(0x312000,0x300000);arr(0x312000)
    write(0x313000,0,0,width,height);write(0x314000,3,7,3,7)
    # CopyRect import -> bounded local stub.
    write(0x1002a180,0x301100)
    sp=0x2ff000
    args=[0x310000,channels,bins,pitch,reverse,reverse_channels,spacing,48000,0x311000,0,linear,log,scale,direction,gap,0x314000,fft]
    write(sp,0x301200,*args)
    u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,0x313000)
    u.reg_write(UC_X86_REG_FPCW,0x27f)
    # CRT dispatch: x87 fallback, avoids CPU-feature OS probes.
    write(0x1003553c,0);write(0x10035540,1)
    def hook(u,a,size,unused):
        sp=u.reg_read(UC_X86_REG_ESP);ecx=u.reg_read(UC_X86_REG_ECX)
        if a==0x1000c3a5:u.emu_stop();return
        if '--trace' in sys.argv and a in (0x1000bca5,0x1000bd52,0x1000becc) and read(sp+0x30) in (130,131,132):
            top=(u.reg_read(UC_X86_REG_FPSW)>>11)&7
            raw=[u.reg_read(UC_X86_REG_FP0+(top+i)%8) for i in range(8)]
            vals=[(-1 if e&32768 else 1)*m*2.**((e&32767)-16383-63) if e&32767 else 0 for m,e in raw]
            print('TRACE',hex(a),read(sp+0x30),u.reg_read(UC_X86_REG_ECX),vals[:6],raw[:3])
        if a==0x10013b20:
            u.mem_write(read(sp+4),bytes([read(sp+8)&255])*read(sp+12));ret(read(sp+4));return
        if a==0x301100:
            u.mem_write(read(sp+4),bytes(u.mem_read(read(sp+8),16)));ret(1,8);return
        if 0x301000<=a<0x301080:
            v=arr(ecx);op=(a-0x301000)//16
            if op==0:ret(len(v))
            elif op==2:v.clear();ret(0,4)
            elif op==7:
                i=read(sp+4);p=read(sp+8);v[i]=list(struct.unpack('<4i',u.mem_read(p,16)))
                u.mem_write(read(ecx+4)+16*i,bytes(u.mem_read(p,16)));ret(0,8)
            else:raise RuntimeError(('vector method',op))
            return
        if a==0x100102b0:
            obj=u.reg_read(UC_X86_REG_EAX);i=ecx;p=read(sp+4);v=arr(obj)
            item=list(struct.unpack('<4i',u.mem_read(p,16)));v.insert(i,item)
            u.mem_write(read(obj+4)+16*i,struct.pack('<4i',*item));ret(0,4);return
        if a==0x100211a0:
            # Frequency threshold helper is separately derived, not under test here.
            obj=read(sp+4);rate=read(sp+8);m=read(sp+12);values=[];n=0
            while n*2000<rate*.5:values.append(max(0,int(n*2000*m/(rate*.5))-1));n+=1
            values.append(m);write(obj+4,0xf00000);write(0xf00000,*values);ret();return
        if not (0x1000b370<=a<0x1000c3a5 or 0x10022880<=a<0x10029000):
            raise RuntimeError(('unexpected code',hex(a)))
    u.hook_add(UC_HOOK_CODE,hook)
    try:u.emu_start(0x1000b370,0x301200,count=5000000)
    except Exception as e:raise RuntimeError(f'{e}; EIP={u.reg_read(UC_X86_REG_EIP):x}') from e
    assert u.reg_read(UC_X86_REG_EIP)==0x1000c3a5,'instruction budget exhausted'
    # amplitude vector is argument 9 + 18*16 (caller passes a 19-element array).
    return {'descriptors':[arrays[0x310000+i*16] for i in range(channels)],'ticks':[arrays.get(0x311000+i*16,[]) for i in range(19)]}
if __name__=='__main__':
    if '--trace' in sys.argv:oracle();sys.exit()
    cases=[dict(width=800),dict(width=800,pitch=2),dict(width=1780),dict(width=37,pitch=3,bins=256),
           dict(width=3000,pitch=2),dict(width=161,pitch=4,spacing=2,linear=1,bins=256),
           dict(width=161,pitch=4,spacing=2,linear=0,bins=256),dict(width=803,pitch=3,channels=3,gap=4,reverse_channels=1,bins=256),
           dict(width=801,direction=1,reverse=1,bins=256),dict(width=801,direction=2,bins=256),
           dict(width=800,fft=1),dict(width=800,fft=1,log=1),dict(width=800,log=1,scale=150)]
    records=[];lines=[]
    for case in cases:
        r=oracle(**case);records.append(dict(parameters=case,**r))
        params=[case.get(k,d) for k,d in [('width',800),('height',200),('bins',1024),('channels',2),('pitch',1),('spacing',0),('linear',0),('direction',0),('reverse',0),('reverse_channels',0),('gap',0),('fft',0),('log',0),('scale',100)]]
        lines.append(' '.join(map(str,params)))
        for channel in r['descriptors']:
            for row in channel:lines.append(' '.join(map(str,row)))
        rows=r['ticks'][18];lines.append(str(len(rows)))
        for row in rows:lines.append(' '.join(map(str,row)))
        print('Emulated:',case,flush=True)
    dest=ROOT/'tests/fixtures';dest.mkdir(exist_ok=True)
    (dest/'original-coordinates.txt').write_bytes(('\n'.join(lines)+'\n').replace('\n','\r\n').encode())
    (ROOT/'analysis/evidence/original-coordinate-oracle.json').write_bytes((json.dumps(records,indent=2)+'\n').replace('\n','\r\n').encode())
