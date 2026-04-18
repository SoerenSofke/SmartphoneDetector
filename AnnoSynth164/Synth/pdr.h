#if 0
""""
#endif

/*
 * pdr.h — PDR decoder for FAUST ffunction() (C++ only)
 *
 * Samples embedded via inline-asm (GCC/Clang, ELF platforms — Linux/ESP32).
 *
 * Usage in FAUST:
 *
 *   pdr_play = ffunction(float pdr_play(int, int, int), <pdr.h>, "");
 *   tick     = +(1) ~ _;
 *   kick     = pdr_play(0, button("kick") * (1 + nentry("kick_var", 0, 0, 3, 1)) : int, tick);
 *   process  = kick;
 *
 * Configuration syntax:
 *   PDR_VOICE(n, "f0.pdr", "f1.pdr", ...);   // 1-8 variations
 *   PDR_VOICE(n);                            // empty slot
 *
 * Trigger encoding:
 *   0       → no trigger, continue current playback
 *   1 .. N  → rising edge triggers variation (value - 1)
 */

#ifndef PDR_H
#define PDR_H

#ifndef __cplusplus
#error "pdr.h requires C++."
#endif
#if !defined(__GNUC__) && !defined(__clang__)
#error "pdr.h requires GCC or Clang (uses __asm__ .incbin)."
#endif

#include <stdint.h>
#include <stddef.h>

/* ==========================================================================
 * USER CONFIGURATION — only section users need to edit
 * ==========================================================================*/

#define PDR_CONFIG                                                                                    \
    PDR_VOICE(0, "audio/kick3.pdr", "audio/snare3.pdr", "audio/ride3.pdr", "audio/hihatClosed3.pdr"); \
    PDR_VOICE(1);                                                                                     \
    PDR_VOICE(2);                                                                                     \
    PDR_VOICE(3);                                                                                     \
    PDR_VOICE(4);                                                                                     \
    PDR_VOICE(5);                                                                                     \
    PDR_VOICE(6);                                                                                     \
    PDR_VOICE(7);                                                                                     \
    PDR_VOICE(8);                                                                                     \
    PDR_VOICE(9);                                                                                     \
    PDR_VOICE(10);                                                                                    \
    PDR_VOICE(11)

/* ==========================================================================
 * Decoder — no user-editable code below
 * ==========================================================================*/

struct PdrEntry
{
    const uint8_t *data;
    const uint8_t *end;
};

/* Forward declarations — arrays defined at end of file via PDR_CONFIG. */
extern const PdrEntry pdr_voice_0[];
extern const PdrEntry pdr_voice_1[];
extern const PdrEntry pdr_voice_2[];
extern const PdrEntry pdr_voice_3[];
extern const PdrEntry pdr_voice_4[];
extern const PdrEntry pdr_voice_5[];
extern const PdrEntry pdr_voice_6[];
extern const PdrEntry pdr_voice_7[];
extern const PdrEntry pdr_voice_8[];
extern const PdrEntry pdr_voice_9[];
extern const PdrEntry pdr_voice_10[];
extern const PdrEntry pdr_voice_11[];

namespace
{

    constexpr int PDR_MAX_VOICES = 12;

    struct PdrState
    {
        const uint8_t *ptr; /* bitstream read pointer          */
        const uint8_t *end; /* bitstream end                   */
        uint32_t buf;       /* MSB-aligned bit accumulator     */
        uint32_t rsum;      /* Rice adaptive sum               */
        uint32_t rcnt;      /* Rice adaptive count             */
        uint32_t total;     /* total samples in .pdr file      */
        uint32_t idx;       /* current output sample index     */
        int nbits;          /* valid bits in buf               */
        int16_t prev1;      /* previous decoded sample         */
        int16_t prev2;      /* sample before that              */
        int prev_trig;      /* last trigger value (edge detect)*/
    };

    PdrState pdr_state[PDR_MAX_VOICES] = {};

/* Dispatch: empty voice detected via arr[0].data == nullptr (sentinel).
 * Count via runtime sentinel search (arrays are forward-declared, size unknown
 * at this point, so sizeof is not available — trailing PDR_EMPTY_ENTRY in
 * populated voices lets us count by walking until the terminator). */
#define PDR_DISPATCH(N)                                \
    case N:                                            \
    {                                                  \
        if (pdr_voice_##N[0].data != nullptr)          \
        {                                              \
            int cnt = 0;                               \
            while (pdr_voice_##N[cnt].data != nullptr) \
                cnt++;                                 \
            if (var < cnt)                             \
            {                                          \
                vdata = pdr_voice_##N[var].data;       \
                vend = pdr_voice_##N[var].end;         \
            }                                          \
        }                                              \
        break;                                         \
    }

} // anonymous namespace

/* --------------------------------------------------------------------------
 * pdr_play — FAUST entry point, external C linkage
 * --------------------------------------------------------------------------*/

extern "C" inline float pdr_play(int voice, int trig, int tick)
{
    static_cast<void>(tick);

    if (voice < 0 || voice >= PDR_MAX_VOICES)
        return 0.0f;

    auto &s = pdr_state[voice];

    /* Rising edge detection → retrigger with encoded variation.
     * `prev <= 0` (not `== 0`) protects against negative trigger values. */
    const bool retrig = (trig > 0) && (s.prev_trig <= 0);
    s.prev_trig = trig;

    if (retrig)
    {
        const uint8_t *vdata = nullptr;
        const uint8_t *vend = nullptr;
        const int var = trig - 1;

        if (var >= 0)
            switch (voice)
            {
                PDR_DISPATCH(0)
                PDR_DISPATCH(1)
                PDR_DISPATCH(2)
                PDR_DISPATCH(3)
                PDR_DISPATCH(4)
                PDR_DISPATCH(5)
                PDR_DISPATCH(6)
                PDR_DISPATCH(7)
                PDR_DISPATCH(8)
                PDR_DISPATCH(9)
                PDR_DISPATCH(10)
                PDR_DISPATCH(11)
            default:
                break;
            }

        if (vdata && vend > vdata && static_cast<size_t>(vend - vdata) >= 12 && vdata[0] == 'P' && vdata[1] == 'D' && vdata[2] == 'R')
        {
            /* Cap seed to 30 to prevent overflow in (2u << seed). */
            uint8_t seed = vdata[3];
            if (seed > 30)
                seed = 30;

            s.total = static_cast<uint32_t>(vdata[4]) | (static_cast<uint32_t>(vdata[5]) << 8) | (static_cast<uint32_t>(vdata[6]) << 16) | (static_cast<uint32_t>(vdata[7]) << 24);
            s.prev2 = static_cast<int16_t>(vdata[8] | (vdata[9] << 8));
            s.prev1 = static_cast<int16_t>(vdata[10] | (vdata[11] << 8));
            s.idx = 0;
            s.ptr = vdata + 12;
            s.end = vend;
            s.buf = 0;
            s.nbits = 0;
            s.rsum = 2u << seed;
            s.rcnt = 2;
        }
        /* invalid / empty / bad magic → no re-init, continue current playback */
    }

    /* End of sample → silence. */
    if (s.idx >= s.total)
        return 0.0f;

    /* First two samples are stored verbatim in the header. */
    if (s.idx++ < 2)
    {
        const int16_t hdr = (s.idx == 1) ? s.prev2 : s.prev1;
        return static_cast<float>(hdr) / 32768.0f;
    }

    /* Refill bit buffer. */
    while (s.nbits <= 24 && s.ptr < s.end)
    {
        s.buf |= static_cast<uint32_t>(*s.ptr++) << (24 - s.nbits);
        s.nbits += 8;
    }

    /* Bitstream fully exhausted AND buffer empty → clean end-of-sample. */
    if (s.nbits <= 0 && s.ptr >= s.end)
    {
        s.idx = s.total;
        return 0.0f;
    }

    /* Rice decode: k from adaptive (rsum / rcnt). */
    int k = 0;
    while (k < 31 && (s.rcnt << (k + 1)) <= s.rsum)
        k++;

    /* Unary quotient. */
    int q = 0;
    while ((s.buf >> 31) & 1)
    {
        s.buf <<= 1;
        s.nbits--;
        q++;
        if (s.nbits < 8)
            while (s.nbits <= 24 && s.ptr < s.end)
            {
                s.buf |= static_cast<uint32_t>(*s.ptr++) << (24 - s.nbits);
                s.nbits += 8;
            }
    }
    s.buf <<= 1;
    s.nbits--;

    /* k binary remainder bits. */
    uint32_t code = static_cast<uint32_t>(q);
    if (k)
    {
        if (s.nbits < k)
            while (s.nbits <= 24 && s.ptr < s.end)
            {
                s.buf |= static_cast<uint32_t>(*s.ptr++) << (24 - s.nbits);
                s.nbits += 8;
            }
        code = (code << k) | (s.buf >> (32 - k));
        s.buf <<= k;
        s.nbits -= k;
    }

    /* Zigzag undo + delta2 undo. */
    int out = 2 * s.prev1 - s.prev2 + (static_cast<int>(code >> 1) ^ -static_cast<int>(code & 1));
    out = ((out + 32768) & 0xFFFF) - 32768;

    s.prev2 = s.prev1;
    s.prev1 = static_cast<int16_t>(out);

    /* Adaptive Rice parameter update. */
    s.rsum += code;
    if (++s.rcnt > 64)
    {
        s.rsum >>= 1;
        s.rcnt >>= 1;
    }

    return static_cast<float>(out) / 32768.0f;
}

/* ==========================================================================
 * ==========================================================================
 *
 *   I N T E R N A L   M A C H I N E R Y
 *
 *   Below this line: preprocessor plumbing. Nothing user-serviceable.
 *
 * ==========================================================================
 * ==========================================================================*/

/* --------------------------------------------------------------------------
 * EMBED — inline-asm binary include (GCC/Clang, ELF)
 * --------------------------------------------------------------------------*/

#define EMBED(name, file)                                 \
    __asm__(".section .rodata\n"                          \
            ".balign 4\n"                                 \
            ".global " #name "_start\n" #name "_start:\n" \
            ".incbin \"" file "\"\n"                      \
            ".global " #name "_end\n" #name "_end:\n"     \
            ".previous\n");                               \
    extern const uint8_t name##_start[];                  \
    extern const uint8_t name##_end[]

/* --------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------*/

#define PDR_SYM_(v, s) _pdr_v##v##_s##s
#define PDR_SYM(v, s) PDR_SYM_(v, s)

#define PDR_EMBED_I(name, file) EMBED(name, file)

#define PDR_ENTRY_I(name) {name##_start, name##_end}
#define PDR_ENTRY(name) PDR_ENTRY_I(name)
#define PDR_EMPTY_ENTRY {nullptr, nullptr}

#define PDR_CAT_(a, b) a##b
#define PDR_CAT(a, b) PDR_CAT_(a, b)

/* --------------------------------------------------------------------------
 * FOREACH-with-index (compresses what would otherwise be 8 arity macros)
 * --------------------------------------------------------------------------*/

#define PDR_INC_0 1
#define PDR_INC_1 2
#define PDR_INC_2 3
#define PDR_INC_3 4
#define PDR_INC_4 5
#define PDR_INC_5 6
#define PDR_INC_6 7
#define PDR_INC_7 8
#define PDR_INC_(x) PDR_INC_##x
#define PDR_INC(x) PDR_INC_(x)

#define PDR_FE_1(M, c, i, x) M(c, i, x)
#define PDR_FE_2(M, c, i, x, ...) M(c, i, x) PDR_FE_1(M, c, PDR_INC(i), __VA_ARGS__)
#define PDR_FE_3(M, c, i, x, ...) M(c, i, x) PDR_FE_2(M, c, PDR_INC(i), __VA_ARGS__)
#define PDR_FE_4(M, c, i, x, ...) M(c, i, x) PDR_FE_3(M, c, PDR_INC(i), __VA_ARGS__)
#define PDR_FE_5(M, c, i, x, ...) M(c, i, x) PDR_FE_4(M, c, PDR_INC(i), __VA_ARGS__)
#define PDR_FE_6(M, c, i, x, ...) M(c, i, x) PDR_FE_5(M, c, PDR_INC(i), __VA_ARGS__)
#define PDR_FE_7(M, c, i, x, ...) M(c, i, x) PDR_FE_6(M, c, PDR_INC(i), __VA_ARGS__)
#define PDR_FE_8(M, c, i, x, ...) M(c, i, x) PDR_FE_7(M, c, PDR_INC(i), __VA_ARGS__)

#define PDR_FE_PICK(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME
#define PDR_FOREACH(M, ctx, ...)                                     \
    PDR_FE_PICK(__VA_ARGS__, PDR_FE_8, PDR_FE_7, PDR_FE_6, PDR_FE_5, \
                PDR_FE_4, PDR_FE_3, PDR_FE_2, PDR_FE_1)              \
    (M, ctx, 0, __VA_ARGS__)

#define PDR_EMIT_EMBED(n, i, f) PDR_EMBED_I(PDR_SYM(n, i), f);
#define PDR_EMIT_ENTRY(n, i, _) PDR_ENTRY(PDR_SYM(n, i)),

/* --------------------------------------------------------------------------
 * PDR_VOICE — unified populated / empty dispatch via __VA_OPT__
 * --------------------------------------------------------------------------*/

#define PDR_ISEMPTY(...) PDR_ISEMPTY_I(__VA_OPT__(0, ) 1)
#define PDR_ISEMPTY_I(X, ...) X

#define PDR_VOICE(n, ...) \
    PDR_CAT(PDR_VOICE_IMPL_, PDR_ISEMPTY(__VA_ARGS__))(n __VA_OPT__(, ) __VA_ARGS__)

/* Arrays use `extern const` to match the forward declarations above.
 * Trailing PDR_EMPTY_ENTRY sentinel enables runtime size detection without
 * sizeof (required because arrays are incomplete at the pdr_play site). */

#define PDR_VOICE_IMPL_1(n) \
    extern const PdrEntry pdr_voice_##n[] = {PDR_EMPTY_ENTRY}

#define PDR_VOICE_IMPL_0(n, ...)                    \
    PDR_FOREACH(PDR_EMIT_EMBED, n, __VA_ARGS__)     \
    extern const PdrEntry pdr_voice_##n[] = {       \
        PDR_FOREACH(PDR_EMIT_ENTRY, n, __VA_ARGS__) \
            PDR_EMPTY_ENTRY}

/* --------------------------------------------------------------------------
 * Expand USER CONFIGURATION
 * --------------------------------------------------------------------------*/

PDR_CONFIG;

#endif /* PDR_H */

#if 0
"x"""

import sys, os, struct, wave, math

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

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f'usage: python3 {sys.argv[0]} input.wav')
        sys.exit(1)
    wav_path = sys.argv[1]
    pcm24, n = read_wav(wav_path)
    pdr = encode(pcm24)
    pdr_path = os.path.splitext(wav_path)[0] + '.pdr'
    with open(pdr_path, 'wb') as f:
        f.write(pdr)
    print(f'{n} samples -> {len(pdr)} bytes ({n*2/len(pdr):.1f}x) -> {pdr_path}')
#endif