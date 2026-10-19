#ifndef ED25519_H
#define ED25519_H

// Ed25519 signature VERIFICATION only (no signing, no private key anywhere in the program).
// Port of the public-domain TweetNaCl (Bernstein, Lange, Schwabe et al.) crypto_sign_open, with the hash supplied by
// Qt (SHA-512). Header-only; the licence tool (license-tools/kmlicense.py) makes the signatures.

#include <cstdint>
#include <cstddef>
#include <vector>

#ifndef KM_ED25519_EXTERNAL_SHA512
#  include <QByteArray>
#  include <QCryptographicHash>
#endif

namespace Ed25519 {

typedef unsigned char u8;
typedef unsigned long long u64;
typedef long long i64;
typedef i64 gf[16];

#ifdef KM_ED25519_EXTERNAL_SHA512
void externalSha512(u8 out[64], const u8 *msg, size_t len);   // supplied by the test program
inline void sha512(u8 out[64], const u8 *msg, size_t len) { externalSha512(out, msg, len); }
#else
inline void sha512(u8 out[64], const u8 *msg, size_t len)
{
    const QByteArray h = QCryptographicHash::hash(QByteArray(reinterpret_cast<const char *>(msg), int(len)),
                                                  QCryptographicHash::Sha512);
    for (int i = 0; i < 64; i++)
        out[i] = u8(h.at(i));
}
#endif

namespace detail {

static const gf gf0 = { 0 };
static const gf gf1 = { 1 };
static const gf D  = { 0x78a3,0x1359,0x4dca,0x75eb,0xd8ab,0x4141,0x0a4d,0x0070,0xe898,0x7779,0x4079,0x8cc7,0xfe73,0x2b6f,0x6cee,0x5203 };
static const gf D2 = { 0xf159,0x26b2,0x9b94,0xebd6,0xb156,0x8283,0x149a,0x00e0,0xd130,0xeef3,0x80f2,0x198e,0xfce7,0x56df,0xd9dc,0x2406 };
static const gf X  = { 0xd51a,0x8f25,0x2d60,0xc956,0xa7b2,0x9525,0xc760,0x692c,0xdc5c,0xfdd6,0xe231,0xc0a4,0x53fe,0xcd6e,0x36d3,0x2169 };
static const gf Y  = { 0x6658,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666 };
static const gf I  = { 0xa0b0,0x4a0e,0x1b27,0xc4ee,0xe478,0xad2f,0x1806,0x2f43,0xd7a7,0x3dfb,0x0099,0x2b4d,0xdf0b,0x4fc1,0x2480,0x2b83 };
static const u64 L[32] = { 0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,
                           0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10 };

inline int vn32(const u8 *x, const u8 *y)
{
    unsigned d = 0;
    for (int i = 0; i < 32; i++)
        d |= x[i] ^ y[i];
    return (1 & ((d - 1) >> 8)) - 1;
}

inline void set25519(gf r, const gf a) { for (int i = 0; i < 16; i++) r[i] = a[i]; }

inline void car25519(gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        const i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

inline void sel25519(gf p, gf q, int b)
{
    const i64 c = ~(i64(b) - 1);
    for (int i = 0; i < 16; i++) {
        const i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

inline void pack25519(u8 *o, const gf n)
{
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = int((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = u8(t[i] & 0xff);
        o[2 * i + 1] = u8(t[i] >> 8);
    }
}

inline int neq25519(const gf a, const gf b)
{
    u8 c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return vn32(c, d);
}

inline u8 par25519(const gf a)
{
    u8 d[32];
    pack25519(d, a);
    return d[0] & 1;
}

inline void unpack25519(gf o, const u8 *n)
{
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + (i64(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

inline void A(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
inline void Z(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

inline void M(gf o, const gf a, const gf b)
{
    i64 t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o);
    car25519(o);
}

inline void S(gf o, const gf a) { M(o, a, a); }

inline void pow2523(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 250; a >= 0; a--) {
        S(c, c);
        if (a != 1) M(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

inline void inv25519(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        S(c, c);
        if (a != 2 && a != 4) M(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

inline void add(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;
    Z(a, p[1], p[0]);
    Z(t, q[1], q[0]);
    M(a, a, t);
    A(b, p[0], p[1]);
    A(t, q[0], q[1]);
    M(b, b, t);
    M(c, p[3], q[3]);
    M(c, c, D2);
    M(d, p[2], q[2]);
    A(d, d, d);
    Z(e, b, a);
    Z(f, d, c);
    A(g, d, c);
    A(h, b, a);
    M(p[0], e, f);
    M(p[1], h, g);
    M(p[2], g, f);
    M(p[3], e, h);
}

inline void cswap(gf p[4], gf q[4], u8 b)
{
    for (int i = 0; i < 4; i++) sel25519(p[i], q[i], b);
}

inline void pack(u8 *r, gf p[4])
{
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= u8(par25519(tx) << 7);
}

inline void scalarmult(gf p[4], gf q[4], const u8 *s)
{
    set25519(p[0], gf0);
    set25519(p[1], gf1);
    set25519(p[2], gf1);
    set25519(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const u8 b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b);
        add(q, p);
        add(p, p);
        cswap(p, q, b);
    }
}

inline void scalarbase(gf p[4], const u8 *s)
{
    gf q[4];
    set25519(q[0], X);
    set25519(q[1], Y);
    set25519(q[2], gf1);
    M(q[3], X, Y);
    scalarmult(p, q, s);
}

inline void modL(u8 *r, i64 x[64])
{
    i64 carry, i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * i64(L[j - (i - 32)]);
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * i64(L[j]);
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; j++) x[j] -= carry * i64(L[j]);
    for (i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = u8(x[i] & 255);
    }
}

inline void reduce(u8 *r)
{
    i64 x[64];
    for (int i = 0; i < 64; i++) x[i] = i64(u64(r[i]));
    for (int i = 0; i < 64; i++) r[i] = 0;
    modL(r, x);
}

inline int unpackneg(gf r[4], const u8 p[32])
{
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    S(num, r[1]);
    M(den, num, D);
    Z(num, num, r[2]);
    A(den, r[2], den);

    S(den2, den);
    S(den4, den2);
    M(den6, den4, den2);
    M(t, den6, num);
    M(t, t, den);

    pow2523(t, t);
    M(t, t, num);
    M(t, t, den);
    M(t, t, den);
    M(r[0], t, den);

    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num)) M(r[0], r[0], I);

    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num)) return -1;

    if (par25519(r[0]) == (p[31] >> 7)) Z(r[0], gf0, r[0]);

    M(r[3], r[0], r[1]);
    return 0;
}

} // namespace detail

// true when `sig` (64 bytes: R | S) is a valid Ed25519 signature of msg[0..len) made by the owner of public key `pk`
inline bool verify(const u8 sig[64], const u8 *msg, size_t len, const u8 pk[32])
{
    using namespace detail;
    u8 t[32], h[64];
    gf p[4], q[4];

    // the scalar S must be < L (no malleable signatures)
    {
        bool less = false;
        for (int i = 31; i >= 0; i--) {
            if (sig[32 + i] < L[i]) { less = true; break; }
            if (sig[32 + i] > L[i]) { less = false; break; }
        }
        if (!less)
            return false;
    }
    if (unpackneg(q, pk))
        return false;

    std::vector<u8> buf(len + 64);
    for (int i = 0; i < 32; i++) buf[i] = sig[i];
    for (int i = 0; i < 32; i++) buf[32 + i] = pk[i];
    for (size_t i = 0; i < len; i++) buf[64 + i] = msg[i];
    sha512(h, buf.data(), buf.size());
    reduce(h);

    scalarmult(p, q, h);
    scalarbase(q, sig + 32);
    add(p, q);
    pack(t, p);
    return vn32(sig, t) == 0;
}

} // namespace Ed25519

#endif // ED25519_H
