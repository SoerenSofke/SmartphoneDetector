#if 0
1//1 or """
#endif
/*
 * PDR — Predictive Delta Rice
 * Lossless 16-bit audio compression for sample playback
 *
 * Encoder (Python, embedded):
 *   python3 pdr.h input.wav
 *   WAV input:  mono or stereo (mixed to mono), PCM only
 *               16 or 24 bit (reduced to 16 bit via predictive rounding with lookahead)
 *               any sample rate (preserved, not resampled)
 *   Output:     input.h   C header with static const uint8_t input[] = {...};
 *               input.pdr raw binary in .pdr format (see below)
 *
 * Decoder (C99, header-only):
 *   #include "pdr.h"
 *   #include "kick.h"
 *   pdr_t d = {0};
 *   int16_t sample = pdr_decode(&d, kick, 1);  // first / retrigger
 *   int16_t sample = pdr_decode(&d, kick, 0);  // next sample
 *   // returns 0 (silence) after last sample, forever
 *
 * .pdr format (12 + n bytes):
 *   [0..2]  magic           'PDR'
 *   [3]     seed_k          context rice parameter
 *   [4..7]  sample count    uint32 little-endian (4-byte aligned)
 *   [8..9]  sample 0        int16 little-endian  (2-byte aligned)
 *   [10..11] sample 1       int16 little-endian  (2-byte aligned)
 *   [12..]  rice bitstream  delta2 + zigzag + context-adaptive rice
 *
 * Pipeline:
 *   encode: WAV -> mono mix -> predictive rounding with lookahead -> delta2 -> zigzag -> rice
 *   decode: rice -> zigzag undo -> delta2 undo -> PCM16
 *
 * Decoder footprint:
 *   sizeof(pdr_t) RAM per voice, no heap allocation
 *   integer arithmetic only, no division, no lookup tables
 *   polyphonic: N independent pdr_t instances
 */
#ifndef PDR_H
#define PDR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const uint8_t *src;
    uint32_t buf, A, N, count, pos;
    int      nb;
    int16_t  p1, p2;
} pdr_t;

static inline int16_t pdr_decode(pdr_t *d, const uint8_t *data, bool retrigger)
{
    if (retrigger) {
        uint8_t sk = data[3];
        d->count = (uint32_t)data[4] | ((uint32_t)data[5]<<8) | ((uint32_t)data[6]<<16) | ((uint32_t)data[7]<<24);
        d->p2  = (int16_t)(data[8]  | (data[9]  << 8));
        d->p1  = (int16_t)(data[10] | (data[11] << 8));
        d->pos = 0;
        d->src = data + 12;
        d->buf = 0;
        d->nb  = 0;
        d->A   = 2u << (sk & 0x1F);
        d->N   = 2;
    }
    if (d->pos >= d->count) return 0;
    if (d->pos++ < 2) return d->pos == 1 ? d->p2 : d->p1;

    while (d->nb <= 24) {
        d->buf |= (uint32_t)*d->src++ << (24 - d->nb);
        d->nb  += 8;
    }
    int k = 0;
    while (k < 31 && (d->N << (k + 1)) <= d->A) k++;
    int q = 0;
    while ((d->buf >> 31) & 1) {
        d->buf <<= 1; d->nb--; q++;
        if (d->nb < 8)
            while (d->nb <= 24) {
                d->buf |= (uint32_t)*d->src++ << (24 - d->nb);
                d->nb  += 8;
            }
    }
    d->buf <<= 1; d->nb--;
    uint32_t z = (uint32_t)q;
    if (k) {
        if (d->nb < k)
            while (d->nb <= 24) {
                d->buf |= (uint32_t)*d->src++ << (24 - d->nb);
                d->nb  += 8;
            }
        z = (z << k) | (d->buf >> (32 - k));
        d->buf <<= k; d->nb -= k;
    }
    int s = 2 * d->p1 - d->p2 + ((int)(z >> 1) ^ -(int)(z & 1));
    s = ((s + 32768) & 0xFFFF) - 32768;
    d->p2 = d->p1;
    d->p1 = (int16_t)s;
    d->A += z;
    if (++d->N > 64) { d->A >>= 1; d->N >>= 1; }
    return (int16_t)s;
}

#endif
#if 0
1//1 and """
import sys, os, re, struct, wave, math

def zigzag(v):
    return (v << 1) ^ (v >> 31) if v >= 0 else ((-v - 1) << 1 | 1)

def read_wav(path):
    w = wave.open(path, 'r')
    nch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    if w.getcomptype() != 'NONE':
        w.close()
        raise ValueError(f'unsupported WAV compression: {w.getcomptype()}')
    if sw not in (2, 3):
        w.close()
        raise ValueError(f'unsupported bit depth: {sw * 8}')
    raw = w.readframes(n); w.close()
    if n < 2:
        raise ValueError(f'WAV too short: {n} samples (need >= 2)')
    pcm = []
    for i in range(n):
        if sw == 3:
            s = int.from_bytes(raw[i*nch*3:i*nch*3+3], 'little', signed=True)
            if nch > 1:
                r = int.from_bytes(raw[i*nch*3+3:i*nch*3+6], 'little', signed=True)
                s = (s + r) // 2
        else:
            s = struct.unpack_from('<h', raw, i*nch*2)[0]
            if nch > 1:
                r = struct.unpack_from('<h', raw, i*nch*2+2)[0]
                s = (s + r) // 2
            s <<= 8
        pcm.append(s)
    return pcm, n

def encode(pcm24):
    n = len(pcm24)
    pcm = [0] * n
    pcm[0] = max(-32768, min(32767, (pcm24[0] + 128) >> 8))
    if n > 1: pcm[1] = max(-32768, min(32767, (pcm24[1] + 128) >> 8))
    for i in range(2, n):
        pred = 2 * pcm[i-1] - pcm[i-2]
        lo = pcm24[i] >> 8
        hi = min(32767, lo + 1)
        if i + 1 < n:
            plo = 2 * lo - pcm[i-1]
            phi = 2 * hi - pcm[i-1]
            ln = pcm24[i+1] >> 8
            hn = min(32767, (pcm24[i+1] >> 8) + 1)
            cl = abs(((lo-pred+32768)%65536)-32768) + min(
                abs(((ln-plo+32768)%65536)-32768), abs(((hn-plo+32768)%65536)-32768))
            ch = abs(((hi-pred+32768)%65536)-32768) + min(
                abs(((ln-phi+32768)%65536)-32768), abs(((hn-phi+32768)%65536)-32768))
            pcm[i] = lo if cl <= ch else hi
        else:
            pcm[i] = lo if abs(((lo-pred+32768)%65536)-32768) <= abs(((hi-pred+32768)%65536)-32768) else hi
        pcm[i] = max(-32768, min(32767, pcm[i]))
    d2 = [((pcm[i]-2*pcm[i-1]+pcm[i-2]+32768)%65536)-32768 for i in range(2,n)]
    zz = [zigzag(v) for v in d2[:256]]
    sk = max(0, int(math.log2(sum(zz)/max(len(zz),1)))) if sum(zz) > 0 else 0
    bits = []
    A, N = max(1, (1 << sk) * 2), 2
    for v in d2:
        z = zigzag(v); k = 0
        while k < 31 and (N << (k+1)) <= A: k += 1
        q = z >> k if k > 0 else z
        bits.extend([1]*q + [0])
        if k > 0:
            for b in range(k-1, -1, -1): bits.append((z >> b) & 1)
        A += z; N += 1
        if N > 64: A >>= 1; N >>= 1
    data = bytearray()
    for i in range(0, len(bits), 8):
        b = 0
        for j in range(8):
            if i+j < len(bits): b |= bits[i+j] << (7-j)
        data.append(b)
    hdr = b'PDR' + struct.pack('<BI2h', sk, n, pcm[0], pcm[1])
    return hdr + bytes(data)

def sanitize(name):
    name = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    if not name or name[0].isdigit(): name = '_' + name
    return name

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f'usage: python3 {sys.argv[0]} input.wav')
        sys.exit(1)
    wav_path = sys.argv[1]
    name = sanitize(os.path.splitext(os.path.basename(wav_path))[0])
    h_path = os.path.splitext(wav_path)[0] + '.h'
    pcm24, n = read_wav(wav_path)
    pdr = encode(pcm24)
    with open(h_path, 'w') as f:
        f.write(f'static const uint8_t {name}[] __attribute__((aligned(4))) = {{\n   ')
        for i, b in enumerate(pdr):
            f.write(f' 0x{b:02X}{"," if i+1<len(pdr) else ""}')
            if (i+1) % 12 == 0 and i+1 < len(pdr): f.write('\n   ')
        f.write(f'\n}}; /* {len(pdr)} bytes, {n} samples */\n')
    pdr_path = os.path.splitext(wav_path)[0] + '.pdr'
    with open(pdr_path, 'wb') as f:
        f.write(pdr)
    print(f'{n} samples -> {len(pdr)} bytes ({n*2/len(pdr):.1f}x) -> {h_path}, {pdr_path}')
#endif
