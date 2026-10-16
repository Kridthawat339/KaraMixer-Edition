#include "Remote/QrCode.h"

#include <cstdint>
#include <algorithm>

namespace qr {

namespace {

typedef std::vector<uint8_t> Bytes;

// ECC level M, versions 1..10 (index = version)
const int ECC_PER_BLOCK[11] = { 0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
const int NUM_BLOCKS[11]    = { 0,  1,  1,  1,  2,  2,  4,  4,  4,  5,  5 };

int rawDataModules(int ver)
{
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7)
            result -= 36;
    }
    return result;
}

int dataCodewords(int ver)
{
    return rawDataModules(ver) / 8 - ECC_PER_BLOCK[ver] * NUM_BLOCKS[ver];
}

// ---- Reed-Solomon over GF(256), polynomial 0x11D ----
uint8_t gfMul(uint8_t x, uint8_t y)
{
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

Bytes rsDivisor(int degree)
{
    Bytes result(degree, 0);
    result[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree; j++) {
            result[j] = gfMul(result[j], root);
            if (j + 1 < degree)
                result[j] ^= result[j + 1];
        }
        root = gfMul(root, 0x02);
    }
    return result;
}

Bytes rsRemainder(const Bytes &data, const Bytes &divisor)
{
    Bytes result(divisor.size(), 0);
    for (size_t i = 0; i < data.size(); i++) {
        uint8_t factor = data[i] ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t j = 0; j < result.size(); j++)
            result[j] ^= gfMul(divisor[j], factor);
    }
    return result;
}

Bytes addEccAndInterleave(const Bytes &data, int ver)
{
    int numBlocks = NUM_BLOCKS[ver];
    int blockEccLen = ECC_PER_BLOCK[ver];
    int rawCodewords = rawDataModules(ver) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;

    std::vector<Bytes> blocks;
    Bytes divisor = rsDivisor(blockEccLen);
    size_t k = 0;
    for (int i = 0; i < numBlocks; i++) {
        int datLen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        Bytes dat(data.begin() + k, data.begin() + k + datLen);
        k += datLen;
        Bytes ecc = rsRemainder(dat, divisor);
        if (i < numShortBlocks)
            dat.push_back(0);
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(dat);
    }

    Bytes result;
    for (size_t i = 0; i < blocks[0].size(); i++) {
        for (size_t j = 0; j < blocks.size(); j++) {
            if (i != (size_t)(shortBlockLen - blockEccLen) || j >= (size_t)numShortBlocks)
                result.push_back(blocks[j][i]);
        }
    }
    return result;
}

// ---- matrix ----
struct Matrix
{
    int size;
    std::vector<std::vector<bool> > mod;
    std::vector<std::vector<bool> > func;

    explicit Matrix(int s)
        : size(s), mod(s, std::vector<bool>(s, false)), func(s, std::vector<bool>(s, false)) {}

    void setFunc(int x, int y, bool dark)
    {
        mod[y][x] = dark;
        func[y][x] = true;
    }
};

void drawFinder(Matrix &m, int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int dist = std::max(std::abs(dx), std::abs(dy));
            int x = cx + dx, y = cy + dy;
            if (x >= 0 && x < m.size && y >= 0 && y < m.size)
                m.setFunc(x, y, dist != 2 && dist != 4);
        }
    }
}

void drawAlign(Matrix &m, int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            m.setFunc(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

std::vector<int> alignPositions(int ver, int size)
{
    std::vector<int> result;
    if (ver == 1)
        return result;
    int numAlign = ver / 7 + 2;
    int step = (ver * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step)
        result.insert(result.begin(), pos);
    result.insert(result.begin(), 6);
    return result;
}

void drawFormatBits(Matrix &m, int mask)
{
    int data = (0 << 3) | mask;                 // ECC M = 0
    int rem = data;
    for (int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;
    int size = m.size;

    for (int i = 0; i <= 5; i++)  m.setFunc(8, i, (bits >> i) & 1);
    m.setFunc(8, 7, (bits >> 6) & 1);
    m.setFunc(8, 8, (bits >> 7) & 1);
    m.setFunc(7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++)  m.setFunc(14 - i, 8, (bits >> i) & 1);

    for (int i = 0; i < 8; i++)   m.setFunc(size - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++)  m.setFunc(8, size - 15 + i, (bits >> i) & 1);
    m.setFunc(8, size - 8, true);
}

void drawVersion(Matrix &m, int ver)
{
    if (ver < 7)
        return;
    int rem = ver;
    for (int i = 0; i < 12; i++)
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    long bits = ((long)ver << 12) | rem;
    for (int i = 0; i < 18; i++) {
        bool bit = (bits >> i) & 1;
        int a = m.size - 11 + i % 3, b = i / 3;
        m.setFunc(a, b, bit);
        m.setFunc(b, a, bit);
    }
}

void drawFunctionPatterns(Matrix &m, int ver)
{
    int size = m.size;
    for (int i = 0; i < size; i++) {
        m.setFunc(6, i, i % 2 == 0);
        m.setFunc(i, 6, i % 2 == 0);
    }
    drawFinder(m, 3, 3);
    drawFinder(m, size - 4, 3);
    drawFinder(m, 3, size - 4);

    std::vector<int> pos = alignPositions(ver, size);
    int n = (int)pos.size();
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)))
                drawAlign(m, pos[i], pos[j]);

    drawFormatBits(m, 0);
    drawVersion(m, ver);
}

void drawCodewords(Matrix &m, const Bytes &data)
{
    int size = m.size;
    size_t i = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int vert = 0; vert < size; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? size - 1 - vert : vert;
                if (!m.func[y][x] && i < data.size() * 8) {
                    m.mod[y][x] = (data[i >> 3] >> (7 - (i & 7))) & 1;
                    i++;
                }
            }
        }
    }
}

bool maskBit(int mask, int x, int y)
{
    switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

void applyMask(Matrix &m, int mask)
{
    for (int y = 0; y < m.size; y++)
        for (int x = 0; x < m.size; x++)
            if (!m.func[y][x] && maskBit(mask, x, y))
                m.mod[y][x] = !m.mod[y][x];
}

int penalty(const Matrix &m)
{
    int size = m.size;
    int result = 0;

    // rule 1: runs of 5+ same colour (rows and columns)
    for (int pass = 0; pass < 2; pass++) {
        for (int a = 0; a < size; a++) {
            int run = 1;
            for (int b = 1; b < size; b++) {
                bool cur  = pass ? m.mod[b][a] : m.mod[a][b];
                bool prev = pass ? m.mod[b - 1][a] : m.mod[a][b - 1];
                if (cur == prev) {
                    run++;
                    if (run == 5) result += 3;
                    else if (run > 5) result++;
                } else {
                    run = 1;
                }
            }
        }
    }
    // rule 2: 2x2 blocks
    for (int y = 0; y + 1 < size; y++)
        for (int x = 0; x + 1 < size; x++) {
            bool c = m.mod[y][x];
            if (c == m.mod[y][x + 1] && c == m.mod[y + 1][x] && c == m.mod[y + 1][x + 1])
                result += 3;
        }
    // rule 4: dark/light balance
    int dark = 0;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            if (m.mod[y][x]) dark++;
    int total = size * size;
    int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
    result += k * 10;
    return result;
}

} // namespace

std::vector<std::vector<bool> > encode(const std::string &text)
{
    // choose smallest version that fits (byte mode)
    int ver = 0;
    for (int v = 1; v <= 10; v++) {
        int ccBits = (v <= 9) ? 8 : 16;
        int needBits = 4 + ccBits + (int)text.size() * 8;
        if (needBits <= dataCodewords(v) * 8) {
            ver = v;
            break;
        }
    }
    if (ver == 0)
        return std::vector<std::vector<bool> >();

    // bit stream
    std::vector<bool> bb;
    auto put = [&bb](unsigned val, int len) {
        for (int i = len - 1; i >= 0; i--)
            bb.push_back((val >> i) & 1);
    };
    put(0x4, 4);
    put((unsigned)text.size(), ver <= 9 ? 8 : 16);
    for (size_t i = 0; i < text.size(); i++)
        put((uint8_t)text[i], 8);

    size_t capBits = (size_t)dataCodewords(ver) * 8;
    for (int i = 0; i < 4 && bb.size() < capBits; i++)
        bb.push_back(false);
    while (bb.size() % 8 != 0)
        bb.push_back(false);
    for (uint8_t pad = 0xEC; bb.size() < capBits; pad ^= 0xEC ^ 0x11)
        put(pad, 8);

    Bytes data(bb.size() / 8, 0);
    for (size_t i = 0; i < bb.size(); i++)
        data[i >> 3] |= (uint8_t)((bb[i] ? 1 : 0) << (7 - (i & 7)));

    Bytes all = addEccAndInterleave(data, ver);

    int size = ver * 4 + 17;
    Matrix m(size);
    drawFunctionPatterns(m, ver);
    drawCodewords(m, all);

    int bestMask = 0, bestPen = 1 << 30;
    for (int mask = 0; mask < 8; mask++) {
        applyMask(m, mask);
        drawFormatBits(m, mask);
        int p = penalty(m);
        if (p < bestPen) {
            bestPen = p;
            bestMask = mask;
        }
        applyMask(m, mask);     // undo (xor)
    }
    applyMask(m, bestMask);
    drawFormatBits(m, bestMask);

    return m.mod;
}

} // namespace qr
