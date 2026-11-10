#!/usr/bin/env python3
"""Convert locally owned PC regional car assets to the native Xbox layout.

Inputs and generated retail assets must remain private. This module validates
each chunk, vertex and index before producing a separate optional pack.
"""
from pathlib import Path
import argparse
import importlib.util
import math
import struct
import functools
import hashlib
import json

ROOT=Path(__file__).resolve().parents[1]
_spec=importlib.util.spec_from_file_location('regional_codecs',ROOT/'src/community/vendor/assettools/texture_codecs.py')
codec=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(codec)


def u32(b, at=0):
    return struct.unpack_from('<I', b, at)[0]


def put32(b, at, value):
    struct.pack_into('<I', b, at, value)


def align(n, a):
    return (n+a-1)&-a


def chunks(b, begin=0, end=None):
    end=len(b) if end is None else end
    if begin<0 or begin>end or end>len(b):
        raise ValueError('Invalid chunk bounds')
    while begin<end:
        if end-begin<8:
            raise ValueError('Truncated chunk header')
        tag,n=struct.unpack_from('<II',b,begin)
        if n>end-begin-8:
            raise ValueError('Truncated chunk payload')
        yield tag,begin+8,n
        begin+=8+n


def chunk(tag, payload):
    return struct.pack('<II',tag,len(payload))+payload


def aligned_chunk(tag, payload, at, alignment):
    return chunk(tag,b'\x11'*(align(at+8,alignment)-(at+8))+payload)


def mesh(b, at, size, target):
    children=list(chunks(b,at,at+size))
    # Exporters may place the vertex stream after the strips and indices.
    # Chunk tags define their roles; require exactly one of each known stream.
    order={0x134900:0,0x134b01:1,0x134b02:2,0x134b03:3}
    children.sort(key=lambda c:order.get(c[0],99))
    if [c[0] for c in children]!=[0x134900,0x134b01,0x134b02,0x134b03]:
        raise ValueError('Unsupported PC car mesh chunks')
    _,ha,hn=children[0];h=align(ha,16)
    if ha+hn-h!=68:
        raise ValueError('Unexpected PC mesh header')
    key,nstrips,nverts=u32(b,h+12),u32(b,h+16),u32(b,h+52)
    if key not in (0x4080,0x4180) or not 0<nstrips<=256 or not 0<nverts<=65535:
        raise ValueError('Unsupported car vertex layout')
    _,va,vn=children[1];v=align(va,128)
    _,sa,sn=children[2];s=align(sa,16)
    _,ia,ins=children[3];indices=align(ia,16)
    if va+vn-v!=nverts*36 or sa+sn-s!=nstrips*60 or (ia+ins-indices)%2:
        raise ValueError('Invalid vertex/strip/index lengths')
    vertices=bytearray()
    for i in range(nverts):
        p=v+i*36
        values=struct.unpack_from('<6f',b,p)+struct.unpack_from('<2f',b,p+28)
        if not all(math.isfinite(x) and abs(x)<10000 for x in values):
            raise ValueError('Invalid car vertex')
        # Xbox stores position, UV, normal; PC adds a per-vertex colour.
        vertices+=b[p:p+12]+b[p+28:p+36]+b[p+12:p+24]
    index_data=b[indices:ia+ins]
    if any(x[0]>=nverts for x in struct.iter_unpack('<H',index_data)):
        raise ValueError('Car index exceeds vertex range')
    strips=bytearray()
    for i in range(nstrips):
        p=s+i*60;count,texture,material,first,flags=(u32(b,p+o) for o in (12,28,32,52,56))
        if count%3 or (first+count)*2>len(index_data) or texture>127 or material>127:
            raise ValueError('Invalid car strip')
        if any(b[p+36:p+52]):
            raise ValueError('Unexpected runtime pointer in source strip')
        out=bytearray(64);out[:12]=b[p:p+12];out[12:24]=b[p+16:p+28]
        for off,value in ((24,count),(28,texture),(32,flags),(36,first),(44,material)):
            put32(out,off,value)
        strips+=out
    header=bytearray(52);put32(header,0,15)
    struct.pack_into('<4H',header,4,key,nverts,nstrips,32)
    out=bytearray()
    for tag,payload,a in ((0x134b00,header,16),(0x134b01,vertices,128),(0x134b02,strips,16),(0x134b03,index_data,16)):
        out+=aligned_chunk(tag,payload,target+len(out),a)
    return out


def geometry(b):
    if len(b)>32*1024*1024:
        raise ValueError('Oversized car geometry')
    top=list(chunks(b))
    if not top or top[0][0]!=0x80134000:
        raise ValueError('Missing solid list')
    _,begin,size=top[0];children=list(chunks(b,begin,begin+size))
    out=bytearray(8);index_at=None;index_count=0;locations={};header_at=None
    for tag,at,n in children:
        if tag==0:
            if index_at is None:out+=chunk(0,b[at:at+n])
            continue
        if tag==0x80134001:
            # Keep the hash list and path metadata, rewrite offsets below.
            if index_at is not None:
                raise ValueError('Duplicate solid-list metadata')
            for k,a,nn in chunks(b,at,at+n):
                if k==0x134002:
                    if header_at is not None or nn!=144:
                        raise ValueError('Invalid solid-list header')
                    header_at=len(out)+8+(a-at)
                if k==0x134004:
                    if nn%24:
                        raise ValueError('Invalid solid offset table')
                    index_at=len(out)+8+(a-at)
                    index_count=nn//24
            out+=chunk(tag,b[at:at+n])
        elif tag==0x80134010:
            pad=align(len(out)+8,128)-len(out)
            out+=chunk(0,bytes(pad-8))
            start=len(out);inner=bytearray();name_hash=None
            for k,a,nn in chunks(b,at,at+n):
                if k==0x134011:
                    if nn<0xb0 or nn>256:
                        raise ValueError('Invalid car solid header')
                    name_hash=u32(b,a+16)
                if k==0x80134100:
                    inner+=chunk(k,mesh(b,a,nn,start+8+len(inner)+8))
                else:
                    inner+=chunk(k,b[a:a+nn])
            if name_hash is None or name_hash in locations:
                raise ValueError('Missing or duplicate solid hash')
            out+=chunk(tag,inner);locations[name_hash]=(start,len(out)-start)
        else:
            raise ValueError(f'Unsupported solid-list chunk {tag:x}')
    if header_at is None or index_at is None or index_count!=len(locations):
        raise ValueError('Missing solid offset table')
    # The source table has one record per solid, sorted by hash.
    for i in range(len(locations)):
        p=index_at+i*24;key=u32(out,p)
        if key not in locations or u32(out,p+16) or u32(out,p+20):
            raise ValueError('Unsupported compressed/unknown solid record')
        start,n=locations[key];put32(out,p+4,start);put32(out,p+8,n);put32(out,p+12,n)
    put32(out,0,0x80134000);put32(out,4,len(out)-8)
    animation=None
    for tag,at,n in top[1:]:
        if tag==0:
            continue
        if tag!=0x80034020:
            raise ValueError(f'Unsupported geometry tail {tag:x}')
        if animation is not None or (u32(out,header_at+0x68),u32(out,header_at+0x6c))!=(at-8,n+8):
            raise ValueError('Invalid animation location in solid-list header')
        # Preserve the animation payload's alignment modulo 128.
        pad=(at-8-len(out))%128
        if pad:
            if pad<8:pad+=128
            out+=chunk(0,bytes(pad-8))
        animation=len(out)
        put32(out,header_at+0x68,animation)
        out+=chunk(tag,b[at:at+n])
    if animation is None and (u32(out,header_at+0x68) or u32(out,header_at+0x6c)):
        raise ValueError('Missing referenced animation chunk')
    return bytes(out)


@functools.lru_cache(maxsize=24)
def swizzle_indices(w,h):
    import numpy as np
    x=np.arange(w,dtype=np.uint32)[None,:];y=np.arange(h,dtype=np.uint32)[:,None]
    address=np.zeros((h,w),dtype=np.uint32);outbit=0
    for bit in range(max(w,h).bit_length()-1):
        if 1<<bit<w:address|=((x>>bit)&1)<<outbit;outbit+=1
        if 1<<bit<h:address|=((y>>bit)&1)<<outbit;outbit+=1
    return address.ravel()


def texture_block(raw):
    import numpy as np
    if len(raw)<156 or len(raw)>8*1024*1024:
        raise ValueError('Invalid source texture size')
    info=bytearray(raw[-156:-32]);pc=raw[-32:];payload=raw[:-156]
    w,h=struct.unpack_from('<HH',info,0x44);levels=info[0x4e]
    if not 0<w<=2048 or not 0<h<=2048 or w&(w-1) or h&(h-1) or not 0<levels<=12:
        raise ValueError('Invalid texture dimensions')
    flags,address,filtering,fmt=struct.unpack_from('<4I',pc,8)
    if flags not in (0,1) or address!=5 or filtering not in (2,6):
        raise ValueError('Unsupported texture sampler')
    formats={0x31545844:12,0x33545844:14,0x35545844:15,41:11,21:11}
    if fmt not in formats:raise ValueError('Unsupported PC texture format')
    pixel_size,palette_size,base_size=struct.unpack_from('<3I',info,0x38)
    palette_offset=u32(info,0x34)-u32(info,0x30)
    palette=payload[palette_offset:palette_offset+palette_size] if palette_size else b''
    indexed=fmt in (21,41);sizes=[]
    for level in range(levels):
        width=max(1,w>>level);height=max(1,h>>level)
        sizes.append(width*height*(4 if fmt==21 else 1) if indexed else max(1,(width+3)//4)*max(1,(height+3)//4)*(8 if fmt==0x31545844 else 16))
    if pixel_size!=sum(sizes) or pixel_size>len(payload) or base_size!=sizes[0]:
        raise ValueError('Texture mip sizes disagree with metadata')
    if fmt==41 and (palette_size!=1024 or palette_offset<pixel_size or palette_offset+1024>len(payload)):
        raise ValueError('Invalid indexed palette')
    pixels=payload[:pixel_size]
    if fmt==21:
        from PIL import Image
        rgba=np.frombuffer(pixels,dtype=np.uint8).reshape((-1,4))[:,[2,1,0,3]].copy()
        source=Image.frombytes('RGBA',(len(rgba),1),rgba.tobytes())
        q=source.quantize(colors=256,method=Image.Quantize.FASTOCTREE,dither=Image.Dither.NONE)
        pixels=q.tobytes();pal=q.getpalette('RGBA');pal=pal+([0]*(1024-len(pal)))
        palette=np.array(pal,dtype=np.uint8).reshape((256,4))[:,[2,1,0,3]].tobytes()
        sizes=[n//4 for n in sizes];pixel_size=sum(sizes);base_size=sizes[0];palette_size=1024
        info[0x4a]=128 if flags else 8;info[0x4d]=1
    if indexed:
        result=bytearray();offset=0
        for level,n in enumerate(sizes):
            width=max(1,w>>level);height=max(1,h>>level);data=np.frombuffer(pixels,dtype=np.uint8,count=n,offset=offset)
            out=np.empty_like(data);out[swizzle_indices(width,height)]=data;result+=out.tobytes();offset+=n
        pixels=bytes(result)
    # Xbox TextureInfo and platform header at the tail; offsets are relative
    # to the decompressed block. Preserve the complete palette including alpha.
    palette_offset=align(len(pixels),128)
    result=pixels+bytes(palette_offset-len(pixels))+palette
    if not palette_size:result=pixels+bytes(align(len(pixels),128)-len(pixels))
    put32(info,0x30,0);put32(info,0x34,palette_offset)
    put32(info,0x38,pixel_size);put32(info,0x3c,palette_size);put32(info,0x40,base_size)
    platform=bytearray(60);struct.pack_into('<4I',platform,0,flags,0x302,1 if filtering==2 else 0x303,formats[fmt])
    return result+info+platform


def textures(b):
    if len(b)>64*1024*1024 or u32(b)!=0xb3300000 or u32(b,4)!=len(b)-8:
        raise ValueError('Unsupported texture pack')
    layout={};parents={}
    for tag,at,n in chunks(b,8,len(b)):
        if tag in (0xb3310000,0xb3320000):
            parents[tag]=(at,n)
            for k,a,nn in chunks(b,at,at+n):
                if k and k in layout:raise ValueError('Duplicate texture chunk')
                if k:layout[k]=(a,nn)
        elif tag:raise ValueError('Unsupported texture pack child')
    if not all(k in layout for k in (0x33310002,0x33310003,0x33320002)):
        raise ValueError('Missing compressed texture index')
    table,ts=layout[0x33310003];hashes,hs=layout[0x33310002];data,ds=layout[0x33320002]
    if ts%24 or ts//24!=hs//8 or hs%8 or not 0<ts//24<=4096 or data+ds!=len(b):
        raise ValueError('Invalid texture index lengths')
    out=bytearray(b[:align(data,128)]);previous=-1
    for i in range(ts//24):
        key,at,packed,unpacked,flags,reserved=struct.unpack_from('<6I',b,table+i*24)
        if key<=previous or u32(b,hashes+i*8)!=key or u32(b,hashes+i*8+4) or flags!=0x100 or reserved or at<data or at+packed>len(b) or packed<16 or not 0<unpacked<=8*1024*1024:
            raise ValueError('Invalid compressed texture record')
        previous=key;blob=b[at:at+packed]
        if u32(blob,8)!=unpacked:raise ValueError('Mismatched decompression size')
        raw=codec.huff(blob,8*1024*1024) if blob[:4]==b'HUFF' else codec.jdlz_game(blob) if blob[:4]==b'JDLZ' else None
        if raw is None or len(raw)!=unpacked or u32(raw,len(raw)-156+0x24)!=key:
            raise ValueError('Invalid decompressed PC texture')
        converted=texture_block(raw);encoded=codec.jdlz_pack(converted)
        if codec.jdlz_game(encoded)!=converted:raise ValueError('JDLZ round trip failed')
        out+=bytes(align(len(out),128)-len(out));start=len(out);out+=encoded
        struct.pack_into('<6I',out,table+i*24,key,start,len(encoded),len(converted),0x100,0)
        if i%100==0:print(f'Textures: {i+1}/{ts//24}',flush=True)
    put32(out,4,len(out)-8);put32(out,parents[0xb3320000][0]-4,len(out)-parents[0xb3320000][0]);put32(out,data-4,len(out)-data)
    return bytes(out)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to(Path(__file__).resolve().parents[1]/'build'):
        parser.error('Use a private output inside build/.')
    if args.input.is_dir():
        manifest={'format':'NREG1','files':{}}
        for model in ('PEUGOT106','CORSA'):
            for filename in ('GEOMETRY.BIN','TEXTURES.BIN','VINYLS.BIN'):
                name=f'CARS/{model}/{filename}'
                source=(args.input/name).read_bytes()
                data=geometry(source) if filename=='GEOMETRY.BIN' else textures(source)
                path=output/name;path.parent.mkdir(parents=True,exist_ok=True)
                temporary=path.with_suffix('.BIN.tmp');temporary.write_bytes(data);temporary.replace(path)
                manifest['files'][name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
                    'source_sha256':hashlib.sha256(source).hexdigest()}
        output.mkdir(parents=True,exist_ok=True)
        (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
        print('Prepared both regional cars; stage with install-community-regional.py')
        return
    source=args.input.read_bytes();data=geometry(source) if u32(source)==0x80134000 else textures(source)
    output.parent.mkdir(parents=True,exist_ok=True)
    temporary=output.with_suffix(output.suffix+'.tmp');temporary.write_bytes(data);temporary.replace(output)
    print(f'Converted asset: {len(data)} bytes')


if __name__=='__main__':main()
