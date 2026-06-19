// Native unit tests for WMBus library.
// Compiled with PlatformIO native platform; no hardware required.
//
// Including .cpp files directly (rather than linking as a library) puts all
// definitions in a single translation unit, which is needed because Frame.cpp
// depends on AES.cpp definitions.

#include "Arduino.h"  // stub — must come before any wmbus headers

#include "../../lib/wmbus/src/WMBus/Frame.cpp"
#include "../../lib/wmbus/src/WMBus/DataRecord.cpp"
#include "../../lib/wmbus/src/WMBus/Crypto/AES.cpp"
#include "../../lib/wmbus/src/WMBus/Crypto/CRC.cpp"
#include "../../lib/wmbus/src/WMBus/Crypto/Decrypt.cpp"

#include <unity.h>

// ── AES-CBC encrypt helper (forward direction, to build test vectors) ──────

static void testAesCbcEncrypt(const uint8_t* key, const uint8_t* iv,
                               const uint8_t* in,  uint8_t* out, uint8_t len) {
    uint8_t rk[176];
    WMBus::Crypto::AES::keyExpansion(key, rk);
    uint8_t prev[16];
    memcpy(prev, iv, 16);
    for (uint8_t i = 0; i < len; i += 16) {
        uint8_t tmp[16];
        for (int j = 0; j < 16; j++) tmp[j] = in[i + j] ^ prev[j];
        WMBus::Crypto::AES::encryptBlock(tmp, out + i, rk);
        memcpy(prev, out + i, 16);
    }
}

// Derive Kenc for mode 7 (mirrors Frame::decrypt ENC_OMS_AES branch)
static void testDeriveKenc(const uint8_t masterKey[16], const uint8_t mcr[4],
                            uint32_t meterId, uint8_t kenc[16]) {
    uint8_t kdfInput[16];
    kdfInput[0] = 0x00;
    memcpy(kdfInput + 1, mcr, 4);
    kdfInput[5] = (meterId >>  0) & 0xFF;
    kdfInput[6] = (meterId >>  8) & 0xFF;
    kdfInput[7] = (meterId >> 16) & 0xFF;
    kdfInput[8] = (meterId >> 24) & 0xFF;
    memset(kdfInput + 9, 0x07, 7);
    WMBus::Crypto::AES::cmac16(masterKey, kdfInput, kenc);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3of6 decode tests
// ═══════════════════════════════════════════════════════════════════════════

void test_decode3of6_byte_zero() {
    uint8_t raw[] = {0x59, 0x60};
    uint8_t out[4];
    TEST_ASSERT_EQUAL(1, WMBus::Frame::decode3of6(raw, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
}

void test_decode3of6_byte_ff() {
    uint8_t raw[] = {0xA6, 0x90};
    uint8_t out[4];
    TEST_ASSERT_EQUAL(1, WMBus::Frame::decode3of6(raw, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);
}

void test_decode3of6_two_bytes() {
    // [0x3B,0x47,0x1C] → [0x2C, 0x44]  (L=0x2C, C=0x44 from a real T1 frame)
    uint8_t raw[] = {0x3B, 0x47, 0x1C};
    uint8_t out[4];
    TEST_ASSERT_EQUAL(2, WMBus::Frame::decode3of6(raw, 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x2C, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x44, out[1]);
}

void test_decode3of6_two_ff() {
    uint8_t raw[] = {0xA6, 0x9A, 0x69};
    uint8_t out[4];
    TEST_ASSERT_EQUAL(2, WMBus::Frame::decode3of6(raw, 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[1]);
}

void test_decode3of6_invalid_symbol_stops_early() {
    uint8_t raw[] = {0xFF, 0xFF};
    uint8_t out[4];
    TEST_ASSERT_EQUAL(0, WMBus::Frame::decode3of6(raw, 2, out, sizeof(out)));
}

void test_decode3of6_output_capped() {
    uint8_t raw[] = {0xA6, 0x9A, 0x69};
    uint8_t out[4];
    TEST_ASSERT_EQUAL(1, WMBus::Frame::decode3of6(raw, 3, out, 1));
}

// ═══════════════════════════════════════════════════════════════════════════
// CRC tests (EN 13757-4: poly 0x3D65, init 0x0000, output XOR 0xFFFF)
// ═══════════════════════════════════════════════════════════════════════════

void test_crc_empty_input() {
    uint8_t dummy = 0;
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, WMBus::Crypto::CRC::compute(&dummy, 0));
}

void test_crc_self_consistent() {
    uint8_t block[12] = {0x2C, 0x44, 0xA5, 0x11, 0x61, 0x58, 0x09, 0x64, 0x70, 0x07};
    uint16_t c = WMBus::Crypto::CRC::compute(block, 10);
    block[10] = (uint8_t)(c >> 8);
    block[11] = (uint8_t)(c & 0xFF);
    TEST_ASSERT_TRUE(WMBus::Crypto::CRC::check(block, 10));
}

void test_crc_detects_corruption() {
    uint8_t block[12] = {0x2C, 0x44, 0xA5, 0x11, 0x61, 0x58, 0x09, 0x64, 0x70, 0x07};
    uint16_t c = WMBus::Crypto::CRC::compute(block, 10);
    block[10] = (uint8_t)(c >> 8);
    block[11] = (uint8_t)(c & 0xFF);
    block[3] ^= 0x01;
    TEST_ASSERT_FALSE(WMBus::Crypto::CRC::check(block, 10));
}

void test_crc_too_short() {
    uint8_t block[1] = {0x00};
    TEST_ASSERT_FALSE(WMBus::Crypto::CRC::check(block, 1));
}

// ═══════════════════════════════════════════════════════════════════════════
// Frame parsing tests
// ═══════════════════════════════════════════════════════════════════════════

// Simple EN 13757-3 frame: CI=0x7A, mode 5, 1 encrypted block.
//   [0]  L=0x11   [1]  C=0x44
//   [2-3] M=0xA5,0x11 (mfr)
//   [4-7] ID=0x61,0x58,0x09,0x64  (meterId 0x64095861, LE)
//   [8] version=0x70  [9] devType=0x07
//   [10] CI=0x7A  [11] accessNo=0x01  [12] status=0x00
//   [13] cfgLo=0x10 (encBlocks=1, high nibble)  [14] cfgHi=0x05 (encMode=5, bits 8-12)
//   [15..30] 16 bytes of ciphertext (filled by decrypt tests)
static uint8_t FRAME_MODE5[31] = {
    0x11, 0x44, 0xA5, 0x11, 0x61, 0x58, 0x09, 0x64, 0x70, 0x07,
    0x7A, 0x01, 0x00, 0x10, 0x05,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
    0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00
};

void test_parse_mode5_basic_fields() {
    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE5, sizeof(FRAME_MODE5));
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_EQUAL_HEX32(0x64095861, f.meterId());
    TEST_ASSERT_EQUAL_HEX8(0x7A, f.ci());
    TEST_ASSERT_EQUAL_HEX8(0x01, f.accessNo());
    TEST_ASSERT_EQUAL_HEX8(0x00, f.status());
}

void test_parse_mode5_encryption_fields() {
    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE5, sizeof(FRAME_MODE5));
    TEST_ASSERT_EQUAL(5, f.encMode());
    TEST_ASSERT_EQUAL(1, f.encBlocks());
    // payload = FRAME_MODE5[15..30], payloadLen = 16
    TEST_ASSERT_EQUAL(16, f.payloadLen());
    TEST_ASSERT_TRUE(f.encrypted());
}

void test_parse_too_short_returns_invalid() {
    uint8_t tiny[5] = {0x01, 0x02, 0x03, 0x04, 0x05};
    WMBus::Frame f = WMBus::Frame::parse(tiny, sizeof(tiny));
    TEST_ASSERT_FALSE(f.valid());
}

// Regression: real-world mode-5 config words from waterstarm (cfgHi=0x25) and
// sensostar (cfgHi=0x05) meters. Both carry cfgLo=0x90 → 9 encrypted blocks in
// the HIGH nibble. The mode lives in config-word bits 8-12 (cfgHi & 0x1f), not
// (cfgHi >> 2) & 7. Captured telegrams decoded as Security Mode 5 by wmbusmeters.
static void buildRealMode5(uint8_t* buf, uint8_t cfgHi) {
    // L large enough that parse() treats the frame as complete; 9 blocks = 144 B
    buf[0] = 0xA0; buf[1] = 0x44; buf[2] = 0xFA; buf[3] = 0x12;
    buf[4] = 0x47; buf[5] = 0x97; buf[6] = 0x25; buf[7] = 0x23;
    buf[8] = 0x02; buf[9] = 0x07;
    buf[10] = 0x7A; buf[11] = 0x9E; buf[12] = 0x00;
    buf[13] = 0x90; buf[14] = cfgHi;   // cfgLo=0x90 (9 blocks), cfgHi=mode
}

void test_parse_real_waterstarm_mode5() {
    uint8_t buf[160] = {0};
    buildRealMode5(buf, 0x25);
    WMBus::Frame f = WMBus::Frame::parse(buf, sizeof(buf));
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_EQUAL(5, f.encMode());
    TEST_ASSERT_EQUAL(9, f.encBlocks());
    TEST_ASSERT_TRUE(f.encrypted());
}

void test_parse_real_sensostar_mode5() {
    uint8_t buf[160] = {0};
    buildRealMode5(buf, 0x05);
    WMBus::Frame f = WMBus::Frame::parse(buf, sizeof(buf));
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_EQUAL(5, f.encMode());
    TEST_ASSERT_EQUAL(9, f.encBlocks());
}

// Layered OMS frame: CI=0x8C (ELL-I) + CI=0x90 (AFL) + CI=0x7A (TPL).
// mode 7, 4 encrypted blocks.
//   [10] CI=0x8C  [11] CC=0x00  [12] ACC=0x00
//   [13] AFL CI=0x90  [14] AFL len=0x06
//   [15-16] FCL=0x00,0x08 → MCR present
//   [17-20] MCR=0x01,0x02,0x03,0x04
//   [21] CI=0x7A  [22] accessNo=0x05  [23] status=0x00
//   [24] cfgLo=0x40 (encBlocks=4)  [25] cfgHi=0x07 (encMode=7)  [26] cfgByte3=0x00
//   [27..90] 64 bytes of ciphertext (filled by decrypt tests)
static uint8_t FRAME_MODE7[91];

static void buildMode7Frame() {
    memset(FRAME_MODE7, 0, sizeof(FRAME_MODE7));
    FRAME_MODE7[0]  = 0x5A;  FRAME_MODE7[1]  = 0x44;
    FRAME_MODE7[2]  = 0xA5;  FRAME_MODE7[3]  = 0x11;
    FRAME_MODE7[4]  = 0x61;  FRAME_MODE7[5]  = 0x58;
    FRAME_MODE7[6]  = 0x09;  FRAME_MODE7[7]  = 0x64;
    FRAME_MODE7[8]  = 0x70;  FRAME_MODE7[9]  = 0x07;
    FRAME_MODE7[10] = 0x8C;
    FRAME_MODE7[11] = 0x00;  FRAME_MODE7[12] = 0x00;
    FRAME_MODE7[13] = 0x90;
    FRAME_MODE7[14] = 0x06;
    FRAME_MODE7[15] = 0x00;  FRAME_MODE7[16] = 0x08;  // FCL = 0x0800
    FRAME_MODE7[17] = 0x01;  FRAME_MODE7[18] = 0x02;
    FRAME_MODE7[19] = 0x03;  FRAME_MODE7[20] = 0x04;  // MCR
    FRAME_MODE7[21] = 0x7A;
    FRAME_MODE7[22] = 0x05;  FRAME_MODE7[23] = 0x00;
    FRAME_MODE7[24] = 0x40;  FRAME_MODE7[25] = 0x07;  FRAME_MODE7[26] = 0x00;
}

void test_parse_mode7_basic_fields() {
    buildMode7Frame();
    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE7, sizeof(FRAME_MODE7));
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_EQUAL_HEX32(0x64095861, f.meterId());
    TEST_ASSERT_EQUAL_HEX8(0x8C, f.ci());
    TEST_ASSERT_EQUAL_HEX8(0x05, f.accessNo());
}

void test_parse_mode7_encryption_fields() {
    buildMode7Frame();
    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE7, sizeof(FRAME_MODE7));
    TEST_ASSERT_EQUAL(7, f.encMode());
    TEST_ASSERT_EQUAL(4, f.encBlocks());
    TEST_ASSERT_EQUAL(64, f.payloadLen());
}

void test_parse_mode7_mcr() {
    buildMode7Frame();
    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE7, sizeof(FRAME_MODE7));
    TEST_ASSERT_EQUAL_HEX8(0x01, f.mcr()[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, f.mcr()[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, f.mcr()[2]);
    TEST_ASSERT_EQUAL_HEX8(0x04, f.mcr()[3]);
}

void test_parse_ell_bit_error_treated_as_ell() {
    buildMode7Frame();
    FRAME_MODE7[10] = 0x8E;  // CI_ELL_I_BITERR — must be handled identically to 0x8C
    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE7, sizeof(FRAME_MODE7));
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_EQUAL(7, f.encMode());
    TEST_ASSERT_EQUAL(4, f.encBlocks());
}

// ═══════════════════════════════════════════════════════════════════════════
// CRC strip tests
// ═══════════════════════════════════════════════════════════════════════════

void test_stripCRC_removes_block1_crc() {
    uint8_t in[12] = {0x0A, 0x44, 0xA5, 0x11, 0x61, 0x58, 0x09, 0x64, 0x70, 0x07,
                      0xDE, 0xAD};
    uint8_t out[16];
    uint8_t n = WMBus::Frame::stripCRC(in, 12, out, sizeof(out));
    TEST_ASSERT_EQUAL(10, n);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x07, out[9]);
}

// ═══════════════════════════════════════════════════════════════════════════
// Decrypt tests
// ═══════════════════════════════════════════════════════════════════════════

void test_decrypt_null_key_returns_false() {
    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE5, sizeof(FRAME_MODE5));
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_FALSE(f.decrypt(nullptr));
}

void test_decrypt_mode5_round_trip() {
    // Build IV matching Frame::decrypt ENC_AES branch for FRAME_MODE5
    //   iv[0..1]  = mfr  = {0xA5, 0x11}
    //   iv[2..5]  = ID LE = {0x61, 0x58, 0x09, 0x64}
    //   iv[6]=version=0x70  iv[7]=devType=0x07
    //   iv[8..15] = accessNo (0x01) × 8  (OMS / EN 13757-3 §9 mode-5 IV)
    uint8_t iv[16] = {0xA5,0x11, 0x61,0x58,0x09,0x64, 0x70,0x07, 0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01};

    const uint8_t key[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F
    };

    uint8_t plaintext[16] = {0x2F,0x2F,0x0C,0x13,0x00,0x00,0x00,0x00,
                              0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    uint8_t ciphertext[16];
    testAesCbcEncrypt(key, iv, plaintext, ciphertext, 16);

    // Place ciphertext at payload offset 15 in a copy of FRAME_MODE5
    uint8_t buf[sizeof(FRAME_MODE5)];
    memcpy(buf, FRAME_MODE5, sizeof(buf));
    memcpy(buf + 15, ciphertext, 16);

    WMBus::Frame f = WMBus::Frame::parse(buf, sizeof(buf));
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_TRUE(f.encrypted());

    TEST_ASSERT_TRUE(f.decrypt(key));
    TEST_ASSERT_EQUAL_HEX8(0x2F, f.payload()[0]);
    TEST_ASSERT_EQUAL_HEX8(0x2F, f.payload()[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, f.payload()[2]);
}

void test_decrypt_mode5_wrong_key_returns_false() {
    uint8_t iv[16] = {0xA5,0x11, 0x61,0x58,0x09,0x64, 0x70,0x07, 0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01};

    const uint8_t correctKey[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    const uint8_t wrongKey[16]   = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    uint8_t plaintext[16] = {0x2F,0x2F,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8_t ciphertext[16];
    testAesCbcEncrypt(correctKey, iv, plaintext, ciphertext, 16);

    uint8_t buf[sizeof(FRAME_MODE5)];
    memcpy(buf, FRAME_MODE5, sizeof(buf));
    memcpy(buf + 15, ciphertext, 16);

    WMBus::Frame f = WMBus::Frame::parse(buf, sizeof(buf));
    TEST_ASSERT_FALSE(f.decrypt(wrongKey));
}

void test_decrypt_mode7_round_trip() {
    buildMode7Frame();

    const uint8_t masterKey[16] = {
        0x09,0x2D,0x7E,0x2F,0xD7,0x74,0xF5,0x3F,
        0x32,0x89,0xB7,0x0B,0xB4,0xDC,0x7F,0xC2
    };
    const uint8_t mcr[4]    = {0x01,0x02,0x03,0x04};
    const uint32_t meterId  = 0x64095861;

    uint8_t kenc[16];
    testDeriveKenc(masterKey, mcr, meterId, kenc);

    uint8_t plaintext[64] = {};
    plaintext[0] = 0x2F; plaintext[1] = 0x2F;
    plaintext[2] = 0x0C; plaintext[3] = 0x13;

    uint8_t ciphertext[64];
    const uint8_t iv0[16] = {};
    testAesCbcEncrypt(kenc, iv0, plaintext, ciphertext, 64);

    // Place 64-byte ciphertext at payload offset 27
    memcpy(FRAME_MODE7 + 27, ciphertext, 64);

    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE7, sizeof(FRAME_MODE7));
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_EQUAL(7, f.encMode());
    TEST_ASSERT_EQUAL(4, f.encBlocks());

    TEST_ASSERT_TRUE(f.decrypt(masterKey));
    TEST_ASSERT_EQUAL_HEX8(0x2F, f.payload()[0]);
    TEST_ASSERT_EQUAL_HEX8(0x2F, f.payload()[1]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, f.payload()[2]);
    TEST_ASSERT_EQUAL_HEX8(0x13, f.payload()[3]);
}

void test_decrypt_mode7_wrong_mcr_returns_false() {
    buildMode7Frame();

    const uint8_t masterKey[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F
    };
    const uint8_t mcr_correct[4] = {0x01,0x02,0x03,0x04};
    const uint32_t meterId       = 0x64095861;

    uint8_t kenc[16];
    testDeriveKenc(masterKey, mcr_correct, meterId, kenc);

    uint8_t plaintext[64] = {};
    plaintext[0] = 0x2F; plaintext[1] = 0x2F;
    uint8_t ciphertext[64];
    const uint8_t iv0[16] = {};
    testAesCbcEncrypt(kenc, iv0, plaintext, ciphertext, 64);

    memcpy(FRAME_MODE7 + 27, ciphertext, 64);

    // Corrupt MCR in the frame buffer — Frame::decrypt will derive wrong Kenc
    FRAME_MODE7[17] = 0xFF; FRAME_MODE7[18] = 0xFF;
    FRAME_MODE7[19] = 0xFF; FRAME_MODE7[20] = 0xFF;

    WMBus::Frame f = WMBus::Frame::parse(FRAME_MODE7, sizeof(FRAME_MODE7));
    TEST_ASSERT_FALSE(f.decrypt(masterKey));
}

// ─────────────────────────────────────────────────────────────────────────
// Key store test (Crypto::Decrypt)
// ─────────────────────────────────────────────────────────────────────────

void test_keystore_set_get_remove() {
    const uint8_t key[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    WMBus::Crypto::Decrypt keys;
    keys.setKey(0xDEADBEEF, key);
    const uint8_t* stored = keys.getKey(0xDEADBEEF);
    TEST_ASSERT_NOT_NULL(stored);
    TEST_ASSERT_EQUAL_HEX8(1, stored[0]);
    keys.removeKey(0xDEADBEEF);
    TEST_ASSERT_NULL(keys.getKey(0xDEADBEEF));
}

// ═══════════════════════════════════════════════════════════════════════════
// DataRecord / parseRecords tests
// ═══════════════════════════════════════════════════════════════════════════

// Actual decrypted payload from meter 64095861 (OMS Mode 7, 2026-06-11).
// Leading 2F 2F are the fill/check bytes.
static const uint8_t REAL_PAYLOAD[] = {
    0x2F, 0x2F,
    0x0C, 0x13, 0x43, 0x77, 0x74, 0x00,  // inst  volume  = 747743 × 10^-3 = 747.743 m³
    0x4C, 0x13, 0x57, 0x42, 0x69, 0x00,  // min   volume  = 694257 × 10^-3 = 694.257 m³
    0x42, 0x6C, 0x3F, 0x3C,              // min   date    = 31.12.2025
    0x0B, 0x3B, 0x00, 0x00, 0x00,        // inst  volflow = 0 × 10^-3 m³/h
    0x02, 0x5A, 0xBF, 0x00,              // inst  flowtemp= 191 × 10^-1 = 19.1 °C
};

void test_parse_records_volume_instantaneous() {
    WMBus::DataRecord recs[WMBus::MAX_DATA_RECORDS];
    uint8_t n = WMBus::parseRecords(REAL_PAYLOAD, sizeof(REAL_PAYLOAD), recs, WMBus::MAX_DATA_RECORDS);
    TEST_ASSERT_TRUE(n >= 1);
    TEST_ASSERT_EQUAL((uint8_t)WMBus::Quantity::Volume, (uint8_t)recs[0].quantity);
    TEST_ASSERT_EQUAL((uint8_t)WMBus::Function::Instantaneous, (uint8_t)recs[0].function);
    TEST_ASSERT_EQUAL(0, recs[0].storage);
    TEST_ASSERT_EQUAL(747743L, recs[0].rawValue);
    TEST_ASSERT_EQUAL(-3, recs[0].exponent);
}

void test_parse_records_volume_minimum() {
    WMBus::DataRecord recs[WMBus::MAX_DATA_RECORDS];
    uint8_t n = WMBus::parseRecords(REAL_PAYLOAD, sizeof(REAL_PAYLOAD), recs, WMBus::MAX_DATA_RECORDS);
    TEST_ASSERT_TRUE(n >= 2);
    TEST_ASSERT_EQUAL((uint8_t)WMBus::Quantity::Volume, (uint8_t)recs[1].quantity);
    TEST_ASSERT_EQUAL((uint8_t)WMBus::Function::Minimum, (uint8_t)recs[1].function);
    TEST_ASSERT_EQUAL(694257L, recs[1].rawValue);
    TEST_ASSERT_EQUAL(-3, recs[1].exponent);
}

void test_parse_records_billing_date() {
    WMBus::DataRecord recs[WMBus::MAX_DATA_RECORDS];
    uint8_t n = WMBus::parseRecords(REAL_PAYLOAD, sizeof(REAL_PAYLOAD), recs, WMBus::MAX_DATA_RECORDS);
    TEST_ASSERT_TRUE(n >= 3);
    TEST_ASSERT_EQUAL((uint8_t)WMBus::Quantity::Date, (uint8_t)recs[2].quantity);
    TEST_ASSERT_EQUAL(31,   recs[2].day);
    TEST_ASSERT_EQUAL(12,   recs[2].month);
    TEST_ASSERT_EQUAL(2025, (int)recs[2].year);
}

void test_parse_records_flow_temperature() {
    WMBus::DataRecord recs[WMBus::MAX_DATA_RECORDS];
    uint8_t n = WMBus::parseRecords(REAL_PAYLOAD, sizeof(REAL_PAYLOAD), recs, WMBus::MAX_DATA_RECORDS);
    TEST_ASSERT_TRUE(n >= 5);
    TEST_ASSERT_EQUAL((uint8_t)WMBus::Quantity::FlowTemperature, (uint8_t)recs[4].quantity);
    TEST_ASSERT_EQUAL(191L, recs[4].rawValue);
    TEST_ASSERT_EQUAL(-1, recs[4].exponent);
}

void test_parse_records_volume_flow_zero() {
    WMBus::DataRecord recs[WMBus::MAX_DATA_RECORDS];
    WMBus::parseRecords(REAL_PAYLOAD, sizeof(REAL_PAYLOAD), recs, WMBus::MAX_DATA_RECORDS);
    TEST_ASSERT_EQUAL((uint8_t)WMBus::Quantity::VolumeFlow, (uint8_t)recs[3].quantity);
    TEST_ASSERT_EQUAL(0L, recs[3].rawValue);
}

void test_parse_records_skips_fill_bytes() {
    // Payload with extra fill bytes at the start
    uint8_t p[] = {0x2F, 0x2F, 0x2F, 0x0C, 0x13, 0x01, 0x00, 0x00, 0x00};
    WMBus::DataRecord recs[4];
    uint8_t n = WMBus::parseRecords(p, sizeof(p), recs, 4);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL((uint8_t)WMBus::Quantity::Volume, (uint8_t)recs[0].quantity);
    TEST_ASSERT_EQUAL(1L, recs[0].rawValue);
}

void test_parse_records_empty_payload() {
    WMBus::DataRecord recs[4];
    uint8_t n = WMBus::parseRecords(nullptr, 0, recs, 4);
    TEST_ASSERT_EQUAL(0, n);
}

void test_parse_records_value_helper() {
    // rawValue=191, exponent=-1 → value() = 19.1
    WMBus::DataRecord r{};
    r.rawValue = 191;
    r.exponent = -1;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.1f, r.value());
}

void setUp()    {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_decode3of6_byte_zero);
    RUN_TEST(test_decode3of6_byte_ff);
    RUN_TEST(test_decode3of6_two_bytes);
    RUN_TEST(test_decode3of6_two_ff);
    RUN_TEST(test_decode3of6_invalid_symbol_stops_early);
    RUN_TEST(test_decode3of6_output_capped);

    RUN_TEST(test_crc_empty_input);
    RUN_TEST(test_crc_self_consistent);
    RUN_TEST(test_crc_detects_corruption);
    RUN_TEST(test_crc_too_short);

    RUN_TEST(test_parse_mode5_basic_fields);
    RUN_TEST(test_parse_mode5_encryption_fields);
    RUN_TEST(test_parse_real_waterstarm_mode5);
    RUN_TEST(test_parse_real_sensostar_mode5);
    RUN_TEST(test_parse_too_short_returns_invalid);
    RUN_TEST(test_parse_mode7_basic_fields);
    RUN_TEST(test_parse_mode7_encryption_fields);
    RUN_TEST(test_parse_mode7_mcr);
    RUN_TEST(test_parse_ell_bit_error_treated_as_ell);

    RUN_TEST(test_stripCRC_removes_block1_crc);

    RUN_TEST(test_decrypt_null_key_returns_false);
    RUN_TEST(test_decrypt_mode5_round_trip);
    RUN_TEST(test_decrypt_mode5_wrong_key_returns_false);
    RUN_TEST(test_decrypt_mode7_round_trip);
    RUN_TEST(test_decrypt_mode7_wrong_mcr_returns_false);

    RUN_TEST(test_keystore_set_get_remove);

    RUN_TEST(test_parse_records_volume_instantaneous);
    RUN_TEST(test_parse_records_volume_minimum);
    RUN_TEST(test_parse_records_billing_date);
    RUN_TEST(test_parse_records_flow_temperature);
    RUN_TEST(test_parse_records_volume_flow_zero);
    RUN_TEST(test_parse_records_skips_fill_bytes);
    RUN_TEST(test_parse_records_empty_payload);
    RUN_TEST(test_parse_records_value_helper);

    return UNITY_END();
}
