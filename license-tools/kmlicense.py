#!/usr/bin/env python3
"""KaraMixer PRO licence tool (KEEP THIS FILE AND km_private.key PRIVATE: never ship them, never commit them).

Licences are Ed25519 signatures: the program only contains the PUBLIC key, so nobody can make a licence
from the program itself; only the owner of km_private.key can.

One time:
    python kmlicense.py --genkey
        -> creates km_private.key (BACK IT UP, losing it means no new licences) and km_pubkey.h
        -> copy km_pubkey.h into the project's Widgets folder, commit, build. Done once for all customers.

Issue a licence (prints one long line to give to the customer; he pastes it in the PRO dialog):
    python kmlicense.py --usb 1A2B3C4D      licence for the flash drive whose USB ID is 1A2B3C4D
    python kmlicense.py --hw  556BAE3D      licence for the machine whose Hardware ID is 556BAE3D

Old short licences (XXXX-XXXX-XXXX-XXXX, still accepted by the program for customers who already have one):
    set KM_LIC_SECRET=...   python kmlicense.py --old-hw 556BAE3D     /     --old-usb 1A2B3C4D
"""
import base64
import getpass
import hashlib
import hmac
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KEY_FILE = os.path.join(HERE, "km_private.key")

# ---------------- Ed25519 (RFC 8032 reference implementation, public domain) ----------------
_p = 2 ** 255 - 19
_q = 2 ** 252 + 27742317777372353535851937790883648493
_d = -121665 * pow(121666, _p - 2, _p) % _p
_I = pow(2, (_p - 1) // 4, _p)


def _inv(x):
    return pow(x, _p - 2, _p)


def _recover_x(y, sign):
    x2 = (y * y - 1) * _inv(_d * y * y + 1) % _p
    x = pow(x2, (_p + 3) // 8, _p)
    if (x * x - x2) % _p:
        x = x * _I % _p
    if x & 1 != sign:
        x = _p - x
    return x


_By = 4 * _inv(5) % _p
_Bx = _recover_x(_By, 0)
_B = (_Bx, _By, 1, _Bx * _By % _p)


def _add(P, Q):
    A = (P[1] - P[0]) * (Q[1] - Q[0]) % _p
    B = (P[1] + P[0]) * (Q[1] + Q[0]) % _p
    C = 2 * P[3] * Q[3] * _d % _p
    D = 2 * P[2] * Q[2] % _p
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F % _p, G * H % _p, F * G % _p, E * H % _p)


def _mul(s, P):
    Q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            Q = _add(Q, P)
        P = _add(P, P)
        s >>= 1
    return Q


def _compress(P):
    zi = _inv(P[2])
    x, y = P[0] * zi % _p, P[1] * zi % _p
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _h(m):
    return hashlib.sha512(m).digest()


def _clamp(seed):
    h = _h(seed)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed):
    a, _ = _clamp(seed)
    return _compress(_mul(a, _B))


def sign(seed, msg):
    a, prefix = _clamp(seed)
    A = _compress(_mul(a, _B))
    r = int.from_bytes(_h(prefix + msg), "little") % _q
    R = _compress(_mul(r, _B))
    k = int.from_bytes(_h(R + A + msg), "little") % _q
    S = (r + k * a) % _q
    return R + int.to_bytes(S, 32, "little")


# ---------------- licence text ----------------
def _b32(sig):
    s = base64.b32encode(sig).decode().rstrip("=")      # 103 characters
    s = "KM3" + s
    return "-".join(s[i:i + 6] for i in range(0, len(s), 6))


def licence(kind, ident, seed):
    msg = ("KMPRO3|%s|%s" % (kind, ident.strip().upper())).encode("latin-1")
    return _b32(sign(seed, msg))


def old_licence(prefix, ident, passphrase):
    secret = hashlib.sha256(passphrase).digest()
    mac = hmac.new(secret, prefix + ident.strip().upper().encode("latin-1"), hashlib.sha256).digest()
    h = mac[:8].hex().upper()
    return "-".join(h[i:i + 4] for i in range(0, 16, 4))


def load_seed():
    if not os.path.exists(KEY_FILE):
        sys.exit("km_private.key not found. Run:  python kmlicense.py --genkey")
    seed = bytes.fromhex(open(KEY_FILE).read().strip())
    if len(seed) != 32:
        sys.exit("km_private.key is damaged")
    return seed


def genkey():
    if os.path.exists(KEY_FILE):
        sys.exit("km_private.key already exists - not overwritten (a new key would invalidate every licence made so far).")
    seed = os.urandom(32)
    with open(KEY_FILE, "w") as f:
        f.write(seed.hex() + "\n")
    try:
        os.chmod(KEY_FILE, 0o600)
    except OSError:
        pass
    pk = public_key(seed)
    arr = ", ".join("0x%02x" % b for b in pk)
    hdr = ("// Public key of the KaraMixer licence (Ed25519). Not secret: it only verifies licences.\n"
           "// Made by kmlicense.py --genkey. The matching private key is km_private.key (never commit it).\n"
           "#ifndef KM_PUBKEY_H\n#define KM_PUBKEY_H\n"
           "static const unsigned char kmPublicKey[32] = { %s };\n"
           "#define KM_HAVE_PUBKEY 1\n#endif\n" % arr)
    with open(os.path.join(HERE, "km_pubkey.h"), "w") as f:
        f.write(hdr)
    print("Created km_private.key (KEEP IT SECRET AND BACK IT UP) and km_pubkey.h")
    print("Copy km_pubkey.h into the project's Widgets folder and build.")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a == ["--genkey"]:
        genkey()
    elif len(a) == 2 and a[0] in ("--usb", "--hw"):
        print(licence("USB" if a[0] == "--usb" else "HW", a[1], load_seed()))
    elif len(a) == 2 and a[0] in ("--old-usb", "--old-hw"):
        pw = os.environ.get("KM_LIC_SECRET") or getpass.getpass("Old licence secret: ")
        if not pw:
            sys.exit("No secret given.")
        print(old_licence(b"KMPRO|USB|" if a[0] == "--old-usb" else b"KMPRO|", a[1], pw.encode("utf-8")))
    else:
        print(__doc__)
        sys.exit(1)
