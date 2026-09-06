"""Read ABI 2 metadata without executing a payload. Firmware validates independently."""
from pathlib import Path
import re
import struct

ALLOWED = set(re.findall(r'APP_IMPORT\("([^"\n]+)"\)',
    (Path(__file__).resolve().parents[1]/"sdk/abi2_imports.inc").read_text()))

def manifest(data):
    def fail():
        raise ValueError("invalid ELF section or ABI 2 manifest/import")
    def span(start, size):
        if start < 0 or size < 0 or start > len(data) or size > len(data)-start: fail()
        return data[start:start+size]
    def string(blob, start):
        if start >= len(blob): fail()
        end=blob.find(b"\0",start)
        if end<0: fail()
        try: return blob[start:end].decode("ascii")
        except UnicodeDecodeError: fail()
    if len(data)<52: fail()
    off=struct.unpack_from("<I",data,32)[0]
    entry,count,names=struct.unpack_from("<HHH",data,46)
    if entry!=40 or not 0<count<=256 or names>=count: fail()
    table=span(off,count*40)
    sections=[struct.unpack_from("<10I",table,i*40) for i in range(count)]
    ns=sections[names]
    if ns[1]!=3: fail()
    strings=span(ns[4],ns[5]);found=[]
    for s in sections:
        if s[1]!=8: span(s[4],s[5])
        name=string(strings,s[0])
        if name==".app_manifest": found.append(s)
    if not found: return None
    if len(found)!=1: fail()
    s=found[0]
    if s[1]!=1 or s[2]&1 or s[5]!=128: fail()
    values=struct.unpack("<6I32s64s2I",span(s[4],s[5]))
    magic,size,major,minor,inputs,flags,appid,name,r0,r1=values
    if (magic,size,major)!=(0x32505041,128,2) or minor>0 or inputs&~1 or flags&~1 or r0 or r1: fail()
    appid=string(appid,0);name=string(name,0)
    if not re.fullmatch(r"[a-z0-9_-]{1,31}",appid) or not name or any(not 32<=ord(c)<=126 for c in name): fail()
    symbols=False
    for s in sections:
        if s[1]!=11: continue
        symbols=True
        if s[6]>=count or s[9]!=16 or s[5]%16: fail()
        st=sections[s[6]]
        if st[1]!=3: fail()
        strings=span(st[4],st[5]);syms=span(s[4],s[5])
        for i in range(0,len(syms),16):
            k,_,_,_,_,index=struct.unpack_from("<IIIBBH",syms,i)
            name_=string(strings,k)
            if index==0 and name_ and name_ not in ALLOWED: fail()
    if not symbols: fail()
    return dict(id=appid,name=name,abi=major,minor=minor,inputs=inputs,flags=flags)
