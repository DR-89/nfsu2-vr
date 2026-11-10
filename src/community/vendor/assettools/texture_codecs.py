# MIT, copyright (c) 2026 th0mk. See LICENSE and NOTICE.txt.
import struct

class _Bits:
    """big-endian bit reader for HUFF"""
    def __init__(self, data):
        self.data, self.at, self.value, self.count = data, 0, 0, 0

    def peek(self, size):
        while self.count < size and self.at < len(self.data):
            self.value = (self.value << 8) | self.data[self.at]; self.count += 8; self.at += 1
        return self.value << (size - self.count) if self.count < size else self.value >> (self.count - size)

    def get(self, size):
        r = self.peek(size)
        if self.count < size: raise ValueError('HUFF: truncated')
        self.count -= size; self.value &= (1 << self.count) - 1
        return r

    def number(self):
        width = 2
        while not self.get(1):
            width += 1
            if width > 30: raise ValueError('HUFF: unbounded number')
        return self.get(width) + (1 << width) - 4


def huff(data, max_output_bytes=1 << 20):
    """EA's HUFF ('HUFF' 01 10: canonical Huffman with a clue symbol for runs and literals, optional delta passes;
    most of CARS/<car>/VINYLS.BIN's textures).  Every size and table is bounded; a bad block raises ValueError."""
    if len(data) < 16 or data[:6] != b'HUFF\x01\x10': raise ValueError('HUFF: unknown header')
    expected, packed = struct.unpack_from('<II', data, 8)
    if not 0 < expected <= max_output_bytes or not 0 < packed <= len(data) - 16: raise ValueError('HUFF: sizes')
    bits = _Bits(data[16:16 + packed])
    kind = bits.get(16)
    if kind & 0x100: bits.get(32 if kind & 0x8000 else 24)
    kind &= ~0x100
    if kind not in (0x30FB, 0x32FB, 0x34FB, 0xB0FB, 0xB2FB, 0xB4FB): raise ValueError(f'HUFF: coding {kind:04x}')
    if bits.get(32 if kind & 0x8000 else 24) != expected: raise ValueError('HUFF: inner size')
    clue = bits.get(8)
    entries, base, total = [], 0, 0
    for width in range(1, 17):
        base <<= 1
        n = bits.number()
        if total + n > 256 or base + n > 1 << width: raise ValueError('HUFF: code table')
        entries.append((width, base, n)); base += n; total += n
        if base == 1 << width: break
    else:
        raise ValueError('HUFF: incomplete code table')
    used, symbols, cur = set(), [], 255
    for _ in range(total):
        skip = bits.number() + 1
        if skip > 256 - len(used): raise ValueError('HUFF: symbol order')
        while skip:
            cur = (cur + 1) & 255
            if cur not in used: skip -= 1
        used.add(cur); symbols.append(cur)
    quick, full, k = [None] * 256, {}, 0
    for width, start, n in entries:
        for code in range(start, start + n):
            sym = symbols[k]; k += 1; full[width, code] = sym
            if width <= 8:
                for i in range(code << (8 - width), (code + 1) << (8 - width)): quick[i] = width, sym
    out = bytearray()
    while True:
        e = quick[bits.peek(8)]
        if e is None:
            for width in range(9, 17):
                sym = full.get((width, bits.peek(width)))
                if sym is not None: bits.get(width); break
            else:
                raise ValueError('HUFF: unknown code')
        else:
            width, sym = e; bits.get(width)
        if sym != clue:
            out.append(sym)
        else:
            run = bits.number()
            if run:
                if not out or len(out) + run > expected: raise ValueError('HUFF: run')
                out.extend(bytes([out[-1]]) * run)
            elif bits.get(1):
                break
            else:
                out.append(bits.get(8))
        if len(out) > expected: raise ValueError('HUFF: overrun')
    if len(out) != expected: raise ValueError('HUFF: short')
    if kind in (0x32FB, 0xB2FB, 0x34FB, 0xB4FB):
        v = v2 = 0
        for i, d in enumerate(out):
            v = (v + d) & 255; v2 = (v2 + v) & 255
            out[i] = v2 if kind in (0x34FB, 0xB4FB) else v
    return bytes(out)


def jdlz_game(b):
    """the game's unpack loop (0x573700, runtime/overrides.c jdlz_native): the bytes, or None where it would fail"""
    size, csize = struct.unpack_from('<II', b, 8)
    if csize != len(b) or csize < 0x12: return None
    out = bytearray(size)
    s, o, left = 0x12, 0, csize - 0x12
    f1, f2 = b[0x10] | 0x100, b[0x11] | 0x100
    while left:
        if f1 & 1:
            if s + 2 > csize: return None
            if f2 & 1: ln, dist = ((b[s] & 0xF0) << 4 | b[s + 1]) + 3, (b[s] & 0x0F) + 1
            else: dist, ln = ((b[s] & 0xE0) << 3 | b[s + 1]) + 17, (b[s] & 0x1F) + 3
            if o < dist or ln > size - o or left < 2: return None
            for k in range(ln): out[o + k] = out[o + k - dist]
            o += ln; s += 2; left -= 2; f2 >>= 1
        else:
            if o < size:
                if s >= csize: return None
                out[o] = b[s]; o += 1; s += 1
            left -= 1
        f1 >>= 1
        if f1 == 1:
            if not left or s >= csize: return None
            f1 = b[s] | 0x100; s += 1; left -= 1
        if f2 == 1:
            if not left or s >= csize: return None
            f2 = b[s] | 0x100; s += 1; left -= 1
    return bytes(out) if o == size else None


def match_len(d, i, j, cap):
    """how many bytes from i equal those from j (j < i; may overlap, as the copy does), at most cap"""
    n = 0
    step = 16
    while n < cap:
        k = min(step, cap - n)
        if d[i + n:i + n + k] == d[j + n:j + n + k]:
            n += k; step = min(step * 2, 4096)
        else:
            if k == 1: break
            step = max(1, k // 2)
    return n


def jdlz_pack(d):
    """d packed in JDLZ: greedy matches (distance 1-16 up to 4098 bytes, 17-2064 up to 34)"""
    n = len(d)
    out = bytearray(b'JDLZ\x02\x10\x00\x00' + struct.pack('<II', n, 0))
    fl = [len(out), 0, 0, len(out) + 1, 0, 0]            # f1: place, bits, count; f2: place, bits, count
    out += b'\0\0'
    heads = {}
    i = 0
    while i < n:
        best, bd = 0, 0
        for dist in range(1, min(16, i) + 1):
            if d[i] != d[i - dist]: continue
            L = match_len(d, i, i - dist, min(4098, n - i))
            if L > best: best, bd = L, dist
            if best >= 64: break
        if best < 34 and i + 3 <= n:
            for j in reversed(heads.get(d[i:i + 3], ())):
                if i - j > 2064: break
                if i - j <= 16: continue
                L = match_len(d, i, j, min(34, n - i))
                if L > best: best, bd = L, i - j
                if best == 34: break
        if best >= 3:
            short = bd <= 16
            if short: out += bytes(((((best - 3) >> 4) & 0xF0) | (bd - 1), (best - 3) & 0xFF))
            else: out += bytes((((((bd - 17) >> 8) & 7) << 5) | (best - 3), (bd - 17) & 0xFF))
            fl[1] |= 1 << fl[2]
            if short: fl[4] |= 1 << fl[5]
            fl[5] += 1
            step = best
        else:
            out.append(d[i]); step = 1
        fl[2] += 1
        for k in range(i, min(i + step, n - 2)):
            heads.setdefault(d[k:k + 3], []).append(k)
            if len(heads[d[k:k + 3]]) > 64: del heads[d[k:k + 3]][:32]
        i += step
        # the loop reads the next flag byte right after the token that used up the last bit: f1 first, then f2
        if fl[2] == 8:
            out[fl[0]] = fl[1]; fl[0:3] = [len(out), 0, 0]; out.append(0)
        if fl[5] == 8:
            out[fl[3]] = fl[4]; fl[3:6] = [len(out), 0, 0]; out.append(0)
    out[fl[0]] = fl[1]; out[fl[3]] = fl[4]
    struct.pack_into('<I', out, 12, len(out))
    return bytes(out)


