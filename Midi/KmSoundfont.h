#ifndef KMSOUNDFONT_H
#define KMSOUNDFONT_H

// KaraMixer protected soundfont (.kmsf): a normal .sf2 whose bytes are encrypted (ChaCha20, random access),
// so other programs (Polyphone, Viena, ...) cannot open or edit it. KaraMixer decrypts it on the fly while
// BASSMIDI reads it (BASS_MIDI_FontInitUser), nothing is ever written back to disk in clear.
//
// File layout: "KMSF1\0\0\0" (8) | nonce (12) | original size, little endian (8) | reserved (4) | ciphertext
//
// Header-only. Include after <bass.h> / <bassmidi.h>.

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QByteArray>
#include <QRandomGenerator>
#include <memory>
#include <cstdint>
#include <cstring>

#include <bass.h>
#include <bassmidi.h>

namespace KmSf {

static const int kHeader = 32;
static const char kMagic[8] = { 'K', 'M', 'S', 'F', '1', 0, 0, 0 };

inline bool isProtectedFile(const QString &path)
{
    return path.endsWith(QLatin1String(".kmsf"), Qt::CaseInsensitive);
}

// ---- ChaCha20 (RFC 8439 block function) ----
inline uint32_t rotl(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }

inline void chachaBlock(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t out[64])
{
    uint32_t s[16] = { 0x61707865, 0x3320646e, 0x79622d32, 0x6b206574 };
    for (int i = 0; i < 8; i++)
        s[4 + i] = uint32_t(key[i * 4]) | uint32_t(key[i * 4 + 1]) << 8 | uint32_t(key[i * 4 + 2]) << 16 | uint32_t(key[i * 4 + 3]) << 24;
    s[12] = counter;
    for (int i = 0; i < 3; i++)
        s[13 + i] = uint32_t(nonce[i * 4]) | uint32_t(nonce[i * 4 + 1]) << 8 | uint32_t(nonce[i * 4 + 2]) << 16 | uint32_t(nonce[i * 4 + 3]) << 24;
    uint32_t x[16];
    std::memcpy(x, s, sizeof(x));
#define KM_QR(a, b, c, d) x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 16); x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 12); \
                          x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 8);  x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 7);
    for (int r = 0; r < 10; r++) {
        KM_QR(0, 4, 8, 12) KM_QR(1, 5, 9, 13) KM_QR(2, 6, 10, 14) KM_QR(3, 7, 11, 15)
        KM_QR(0, 5, 10, 15) KM_QR(1, 6, 11, 12) KM_QR(2, 7, 8, 13) KM_QR(3, 4, 9, 14)
    }
#undef KM_QR
    for (int i = 0; i < 16; i++) {
        const uint32_t v = x[i] + s[i];
        out[i * 4] = uint8_t(v); out[i * 4 + 1] = uint8_t(v >> 8); out[i * 4 + 2] = uint8_t(v >> 16); out[i * 4 + 3] = uint8_t(v >> 24);
    }
}

// The key is rebuilt at run time from a few constants (no 32-byte literal sitting in the executable).
inline void masterKey(uint8_t key[32])
{
    uint64_t z = 0x4b61726154ULL ^ 0x9e3779b97f4a7c15ULL;   // arbitrary seeds
    for (int i = 0; i < 4; i++) {
        z += 0x9e3779b97f4a7c15ULL;
        uint64_t t = z;
        t = (t ^ (t >> 30)) * 0xbf58476d1ce4e5b9ULL;
        t = (t ^ (t >> 27)) * 0x94d049bb133111ebULL;
        t ^= (t >> 31);
        t ^= 0x6d69784d65646958ULL * uint64_t(i + 1);
        for (int b = 0; b < 8; b++)
            key[i * 8 + b] = uint8_t(t >> (8 * b));
    }
}

// XOR `len` bytes at plaintext offset `pos` (random access: the counter is derived from the offset)
inline void crypt(const uint8_t key[32], const uint8_t nonce[12], uint64_t pos, uint8_t *data, size_t len)
{
    uint8_t ks[64];
    uint64_t block = pos / 64;
    size_t skip = size_t(pos % 64);
    size_t done = 0;
    while (done < len) {
        chachaBlock(key, nonce, uint32_t(block), ks);   // 2^32 blocks = 256 GB: far above any soundfont
        const size_t n = qMin<size_t>(64 - skip, len - done);
        for (size_t i = 0; i < n; i++)
            data[done + i] ^= ks[skip + i];
        done += n;
        skip = 0;
        block++;
    }
}

// .sf2 -> .kmsf
inline bool protectFile(const QString &src, const QString &dst, QString *error = nullptr)
{
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read the source file");
        return false;
    }
    char riff[12] = {};
    if (in.read(riff, 12) != 12 || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "sfbk", 4) != 0) {
        if (error) *error = QStringLiteral("not a .sf2 soundfont");
        return false;
    }
    in.seek(0);

    QFile out(dst);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot write the output file");
        return false;
    }

    uint8_t key[32], nonce[12];
    masterKey(key);
    for (int i = 0; i < 12; i++)
        nonce[i] = uint8_t(QRandomGenerator::global()->generate() & 0xff);

    const uint64_t size = uint64_t(in.size());
    uint8_t head[kHeader] = {};
    std::memcpy(head, kMagic, 8);
    std::memcpy(head + 8, nonce, 12);
    for (int b = 0; b < 8; b++)
        head[20 + b] = uint8_t(size >> (8 * b));
    out.write(reinterpret_cast<const char *>(head), kHeader);

    QByteArray buf(1 << 20, 0);
    uint64_t pos = 0;
    while (true) {
        const qint64 n = in.read(buf.data(), buf.size());
        if (n <= 0)
            break;
        crypt(key, nonce, pos, reinterpret_cast<uint8_t *>(buf.data()), size_t(n));
        if (out.write(buf.constData(), n) != n) {
            if (error) *error = QStringLiteral("disk full?");
            out.close();
            QFile::remove(dst);
            return false;
        }
        pos += uint64_t(n);
    }
    out.close();
    return pos == size;
}

// ---- BASS user file callbacks: decrypt while BASSMIDI reads ----
struct Reader
{
    QFile file;
    uint8_t key[32];
    uint8_t nonce[12];
    uint64_t size = 0;
    uint64_t pos = 0;
    std::shared_ptr<bool> closed = std::make_shared<bool>(false);
};

inline void CALLBACK procClose(void *user)
{
    Reader *r = static_cast<Reader *>(user);
    *r->closed = true;
    delete r;
}

inline QWORD CALLBACK procLength(void *user)
{
    return static_cast<Reader *>(user)->size;
}

inline DWORD CALLBACK procRead(void *buffer, DWORD length, void *user)
{
    Reader *r = static_cast<Reader *>(user);
    if (r->pos >= r->size)
        return 0;
    const uint64_t want = qMin<uint64_t>(length, r->size - r->pos);
    if (!r->file.seek(qint64(kHeader + r->pos)))
        return 0;
    const qint64 got = r->file.read(static_cast<char *>(buffer), qint64(want));
    if (got <= 0)
        return 0;
    crypt(r->key, r->nonce, r->pos, static_cast<uint8_t *>(buffer), size_t(got));
    r->pos += uint64_t(got);
    return DWORD(got);
}

inline BOOL CALLBACK procSeek(QWORD offset, void *user)
{
    Reader *r = static_cast<Reader *>(user);
    if (offset > r->size)
        return FALSE;
    r->pos = offset;
    return TRUE;
}

// Opens a .kmsf as a BASSMIDI soundfont (0 = failed).
inline HSOUNDFONT openFont(const QString &path, DWORD flags)
{
    static const BASS_FILEPROCS procs = { procClose, procLength, procRead, procSeek };

    Reader *r = new Reader;
    r->file.setFileName(path);
    uint8_t head[kHeader];
    if (!r->file.open(QIODevice::ReadOnly) || r->file.read(reinterpret_cast<char *>(head), kHeader) != kHeader
        || std::memcmp(head, kMagic, 8) != 0) {
        delete r;
        return 0;
    }
    std::memcpy(r->nonce, head + 8, 12);
    for (int b = 0; b < 8; b++)
        r->size |= uint64_t(head[20 + b]) << (8 * b);
    if (r->size == 0 || r->size + kHeader > uint64_t(r->file.size())) {
        delete r;
        return 0;
    }
    masterKey(r->key);

    std::shared_ptr<bool> closed = r->closed;
    HSOUNDFONT f = BASS_MIDI_FontInitUser(&procs, r, flags);
    if (!f && !*closed)   // BASS did not call close on failure: the reader is still ours
        delete r;
    return f;
}

} // namespace KmSf

#endif // KMSOUNDFONT_H
