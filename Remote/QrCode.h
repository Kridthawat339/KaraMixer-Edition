#ifndef QRCODE_H
#define QRCODE_H

// Tiny QR Code encoder (byte mode, error correction M, versions 1..10).
// No dependencies. Returns a square matrix, true = dark module.

#include <string>
#include <vector>

namespace qr {

// Returns an empty vector when the text is too long (> 213 bytes).
std::vector<std::vector<bool> > encode(const std::string &text);

}

#endif // QRCODE_H
