import importlib.util
from pathlib import Path
import struct
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('regional',ROOT/'tools/prepare-community-regional.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)


class RegionalConversion(unittest.TestCase):
    @staticmethod
    def mesh_source(order):
        header=bytearray(68)
        struct.pack_into('<II',header,12,0x4180,1);r.put32(header,52,3)
        vertices=b''.join(struct.pack('<6fI2f',i,0,1,0,0,1,0xffffffff,i/2,1)for i in range(3))
        strip=bytearray(60);struct.pack_into('<3fI3fII',strip,0,0,0,1,3,2,0,1,1,2);r.put32(strip,56,0x14180)
        streams={0x134900:(header,16),0x134b01:(vertices,128),0x134b02:(strip,16),
                 0x134b03:(struct.pack('<3H',0,1,2),16)}
        out=bytearray(8)
        for tag in order:
            data,alignment=streams[tag]
            out+=r.aligned_chunk(tag,data,len(out),alignment)
        return out

    def test_mesh_chunk_order_is_independent_of_exporter(self):
        normal=self.mesh_source((0x134900,0x134b01,0x134b02,0x134b03))
        reordered=self.mesh_source((0x134900,0x134b02,0x134b03,0x134b01))
        self.assertEqual(r.mesh(normal,8,len(normal)-8,8),r.mesh(reordered,8,len(reordered)-8,8))
        duplicate=self.mesh_source((0x134900,0x134b01,0x134b01,0x134b02,0x134b03))
        for invalid in (duplicate,normal+r.chunk(0xdeadbeef,b'')):
            with self.assertRaisesRegex(ValueError,'Unsupported PC car mesh chunks'):
                r.mesh(invalid,8,len(invalid)-8,8)

    def test_chunk_bounds(self):
        for b in (b'x',struct.pack('<II',1,20)+b'x'):
            with self.assertRaises(ValueError):list(r.chunks(b))

    def test_mesh_format_and_ranges(self):
        b=bytearray(8);header=bytearray(68)
        struct.pack_into('<II',header,12,0x4180,1);r.put32(header,52,3)
        vertices=b''.join(struct.pack('<6fI2f',i,0,1,0,0,1,0xffffffff,i/2,1)for i in range(3))
        strip=bytearray(60);struct.pack_into('<3fI3fII',strip,0,0,0,1,3,2,0,1,1,2);r.put32(strip,56,0x14180)
        for tag,data,alignment in ((0x134900,header,16),(0x134b01,vertices,128),(0x134b02,strip,16),(0x134b03,struct.pack('<3H',0,1,2),16)):
            b+=r.aligned_chunk(tag,data,len(b),alignment)
        out=r.mesh(bytes(b),8,len(b)-8,8);out=bytes(8)+out
        c={k:(a,n)for k,a,n in r.chunks(out,8,len(out))}
        h=r.align(c[0x134b00][0],16);v=r.align(c[0x134b01][0],128);s=r.align(c[0x134b02][0],16)
        self.assertEqual(struct.unpack_from('<I4H',out,h),(15,0x4180,3,1,32))
        self.assertEqual(struct.unpack_from('<8f',out,v+32),(1,0,1,.5,1,0,0,1))
        self.assertEqual(struct.unpack_from('<3f',out,s+12),(2,0,1))
        self.assertEqual((r.u32(out,s+24),r.u32(out,s+28),r.u32(out,s+32),r.u32(out,s+44)),(3,1,0x14180,2))
        bad=bytearray(b);a,n=list(r.chunks(b,8,len(b)))[-1][1:];struct.pack_into('<H',bad,r.align(a,16),3)
        with self.assertRaises(ValueError):r.mesh(bad,8,len(bad)-8,8)

    def test_animation_relocated_after_geometry_shrinks(self):
        header=bytearray(144)
        metadata=r.chunk(0x80134001,r.chunk(0x134002,header)+r.chunk(0x134003,b'')+r.chunk(0x134004,b''))
        source=bytearray(r.chunk(0x80134000,metadata)+r.chunk(0,bytes(1000)))
        animation=len(source);payload=bytes(range(32))
        source+=r.chunk(0x80034020,payload)
        r.put32(source,24+0x68,animation);r.put32(source,24+0x6c,len(payload)+8)
        out=r.geometry(source);tail=[(a-8,n+8)for tag,a,n in r.chunks(out)if tag==0x80034020]
        self.assertEqual(len(tail),1)
        self.assertEqual(struct.unpack_from('<II',out,24+0x68),tail[0])
        self.assertLess(tail[0][0],animation)
        self.assertEqual(tail[0][0]%128,animation%128)
        self.assertEqual(out[tail[0][0]+8:],payload)
        r.put32(source,24+0x68,animation+1)
        with self.assertRaises(ValueError):r.geometry(source)

    @staticmethod
    def texture(fmt=41):
        info=bytearray(124);struct.pack_into('<HH',info,0x44,4,4);info[0x4e]=2
        n=20*(4 if fmt==21 else 1);pal=1024 if fmt==41 else 0
        struct.pack_into('<5I',info,0x30,0,r.align(n,128),n,pal,16*(4 if fmt==21 else 1))
        pc=bytearray(32);struct.pack_into('<4I',pc,8,1,5,6,fmt)
        data=bytes(range(20)) if fmt==41 else b'\x20\x40\x80\xff'*16+b'\x10\x20\x40\x80'*4
        palette=b''.join(struct.pack('<I',0xff000000|i)for i in range(256)) if pal else b''
        return data+bytes(r.align(n,128)-n)+palette+info+pc

    def test_indexed_mips_and_palette(self):
        raw=self.texture();out=r.texture_block(raw)
        self.assertEqual(out[:16],bytes([0,1,4,5,2,3,6,7,8,9,12,13,10,11,14,15]))
        self.assertEqual(out[16:20],bytes(range(16,20)))
        self.assertEqual(out[128:1152],raw[128:1152])
        self.assertEqual(struct.unpack_from('<4I',out,len(out)-60),(1,0x302,0x303,11))
        bad=bytearray(raw);r.put32(bad,len(bad)-156+0x38,3)
        with self.assertRaises(ValueError):r.texture_block(bad)
        blob=r.codec.jdlz_pack(out);self.assertEqual(r.codec.jdlz_game(blob),out)
        self.assertIsNone(r.codec.jdlz_game(blob[:-1]))

    def test_alpha_quantization(self):
        try:import PIL
        except ImportError:self.skipTest('Optional preparation dependency Pillow unavailable')
        out=r.texture_block(self.texture(21));info=out[-184:-60]
        self.assertEqual((info[0x4a],info[0x4d],r.u32(info,0x38),r.u32(info,0x3c)),(128,1,20,1024))
        palette=out[128:1152]
        self.assertEqual(palette[out[0]*4:out[0]*4+4],b'\x20\x40\x80\xff')
        self.assertEqual(palette[out[16]*4:out[16]*4+4],b'\x10\x20\x40\x80')


if __name__=='__main__':unittest.main()
