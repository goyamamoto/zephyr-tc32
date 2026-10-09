#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Apache NimBLE's recorded Secure Connections pairing (nimble/host/test/src/ble_sm_sc_test.c,
ble_sm_sc_peer_jw_iio3_rio3_b1_iat0_rat0_ik5_rk7: the peer is the initiator, we are the responder)
re-derived with an independent P-256 and AES-CMAC (the cryptography package): our public key from
our private key, the DHKey, the confirm (f4), MacKey and LTK (f5) and both DHKey checks (f6) must
come out as the recording has them. Prints the values the engine test (tlsr_pke_test.c) compares
against, as little-endian C arrays.

Usage: nimble_sc_vectors.py [--c]   (needs the cryptography package)
"""
import sys

from cryptography.hazmat.primitives import cmac
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers import algorithms

# All as in the C file (little-endian, the order of the SMP PDUs).
init_addr = bytes.fromhex("ca61a06794e0")
resp_addr = bytes.fromhex("33221100450a")
pair_req = bytes([0x01, 0x03, 0x00, 0x09, 0x10, 0x0d, 0x0f])
pair_rsp = bytes([0x02, 0x03, 0x00, 0x09, 0x10, 0x05, 0x07])
our_priv = bytes.fromhex("548d20b8970bbc439aad106f6074d46a55c17a178b60e0b45ae658f1ea12d9fb")
pka_x = bytes.fromhex("bcf2d8a5dba3956c99f9110d4d2ef0bdee9b69b6cd8874be40e8e5ccdc884453")
pka_y = bytes.fromhex("bfa9820e187a14f877fd8e922af85d39d16d921f387499dc6c2c9423f97256ab")
pkb_x = bytes.fromhex("728cd188d7be49b2c55c95b364e01232b6c9476337385b9c1e1b1a0609e23185")
pkb_y = bytes.fromhex("193a296962d630e7e84863dc00730a707d2e29cc917771b175b8f7dcb0e29110")
confirm_rsp = bytes.fromhex("82edd062913d967f13c50d022b5e4316")
na = bytes.fromhex("a4345fb3af734364cd191b5b87583166")
nb = bytes.fromhex("c091fbb377a2020bc6cd6c0451454539")
dhkey_check_req = bytes.fromhex("82651d02ed891344041a147c329a1e7d")
dhkey_check_rsp = bytes.fromhex("063c284ae5484b51654e145e2fddfa22")
ltk = bytes.fromhex("63598a14094b946effae5e538602a36c")


def be(b):
    return int.from_bytes(b, "little")


def point(x, y):
    return ec.EllipticCurvePublicNumbers(be(x), be(y), ec.SECP256R1()).public_key()


def aes_cmac(k, m):
    c = cmac.CMAC(algorithms.AES(k))
    c.update(m)
    return c.finalize()


# Core 6.0 Vol 3 Part H 2.2.6-2.2.8, big-endian inside; the PDUs carry little-endian.
def f4(u, v, x, z):
    return aes_cmac(x[::-1], u[::-1] + v[::-1] + bytes([z]))[::-1]


def f5(w, n1, n2, a1, a2):
    salt = bytes.fromhex("6C888391AAF5A53860370BDB5A6083BE")
    t = aes_cmac(salt, w[::-1])
    key_id = b"btle"
    mac = aes_cmac(t, bytes([0]) + key_id + n1[::-1] + n2[::-1] + a1 + a2 + bytes([1, 0]))
    lt = aes_cmac(t, bytes([1]) + key_id + n1[::-1] + n2[::-1] + a1 + a2 + bytes([1, 0]))
    return mac[::-1], lt[::-1]


def f6(w, n1, n2, r, io_cap, a1, a2):
    return aes_cmac(w[::-1], n1[::-1] + n2[::-1] + r[::-1] + io_cap + a1 + a2)[::-1]


def main():
    priv = ec.derive_private_key(be(our_priv), ec.SECP256R1())
    nums = priv.public_key().public_numbers()
    ok = True
    ok &= nums.x == be(pkb_x) and nums.y == be(pkb_y)
    print("our public key = our private key . G:", "ok" if ok else "DIFFERS")
    dh = priv.exchange(ec.ECDH(), point(pka_x, pka_y))  # big-endian x
    dhkey = dh[::-1]
    cb = f4(pkb_x, pka_x, nb, 0)
    c_ok = cb == confirm_rsp
    print("Cb = f4(PKbx, PKax, Nb, 0):", "ok" if c_ok else "DIFFERS")
    a1 = bytes([0]) + init_addr[::-1]  # 7 octets, big-endian: type then the address
    a2 = bytes([0]) + resp_addr[::-1]
    mackey, lt = f5(dhkey, na, nb, a1, a2)
    l_ok = lt == ltk
    print("LTK = f5(DHKey, Na, Nb, A, B):", "ok" if l_ok else "DIFFERS")
    r = bytes(16)
    io_a = bytes([pair_req[3], pair_req[2], pair_req[1]])  # AuthReq, OOB, IOcap (big-endian)
    io_b = bytes([pair_rsp[3], pair_rsp[2], pair_rsp[1]])
    ea = f6(mackey, na, nb, r, io_a, a1, a2)
    eb = f6(mackey, nb, na, r, io_b, a2, a1)
    e_ok = ea == dhkey_check_req and eb == dhkey_check_rsp
    print("Ea, Eb = f6(MacKey, ...):", "ok" if e_ok else "DIFFERS")
    ok = ok and c_ok and l_ok and e_ok
    if "--c" in sys.argv:
        def arr(name, b):
            body = ", ".join(f"0x{x:02x}" for x in b)
            print(f"static const uint8_t {name}[32] = {{{body}}};")
        arr("nimble_db", our_priv)
        arr("nimble_bx", pkb_x)
        arr("nimble_by", pkb_y)
        arr("nimble_ax", pka_x)
        arr("nimble_ay", pka_y)
        arr("nimble_dhkey", dhkey)
    else:
        print("DHKey (LE):", dhkey.hex())
        print("MacKey (LE):", mackey.hex())
    print("all as recorded" if ok else "NOT as recorded")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
