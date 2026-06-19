#include <Arduino.h>
#include <LittleFS.h>
#include <WMBus.h>

// ── Pin assignments ───────────────────────────────────────────────────────
#if defined(ARDUINO_ARCH_RP2040)
// Pico / Pico 2  (SPI1)
//  CC1101   Signal   GPIO   Pico 2 pin
//  1  VCC   3.3V     —      36
//  2  GND   GND      —      38
//  3  MOSI  SPI1 TX  GPIO27 32
//  4  SCK   SPI1 SCK GPIO26 31
//  5  MISO  SPI1 RX  GPIO28 34
//  6  CSN   CS       GPIO29 35
//  7  GDO0  IRQ      GPIO22 29
//  8  GDO2  (unused) —      —
constexpr uint8_t CC1101_MISO = 28;
constexpr uint8_t CC1101_SCK  = 26;
constexpr uint8_t CC1101_MOSI = 27;
constexpr uint8_t CC1101_CS   = 29;
constexpr uint8_t CC1101_GDO0 = 22;
#define CC1101_SPI SPI1

#elif defined(ARDUINO_ARCH_ESP32)
// ESP32  (VSPI)
//  CC1101   Signal   GPIO
//  3  MOSI  VSPI MOSI GPIO23
//  4  SCK   VSPI SCK  GPIO18
//  5  MISO  VSPI MISO GPIO19
//  6  CSN   CS        GPIO5
//  7  GDO0  IRQ       GPIO4
constexpr uint8_t CC1101_MISO = 19;
constexpr uint8_t CC1101_SCK  = 18;
constexpr uint8_t CC1101_MOSI = 23;
constexpr uint8_t CC1101_CS   =  5;
constexpr uint8_t CC1101_GDO0 =  4;
#define CC1101_SPI SPI

#else
#error "Unsupported platform — add pin definitions for your board"
#endif

// ── Radio + receiver instances ────────────────────────────────────────────
WMBus::Radio::CC1101 radio;
WMBus::Receiver      wmbus;

// ── Key store ─────────────────────────────────────────────────────────────

struct KeyEntry { uint32_t id; uint8_t key[16]; };
static KeyEntry s_keys[8];
static uint8_t  s_keyCount = 0;

static bool parseHexKey(const char* hex, uint8_t* out) {
    if (strlen(hex) < 32) return false;
    for (uint8_t i = 0; i < 16; i++) {
        char h[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        char* end;
        out[i] = (uint8_t)strtoul(h, &end, 16);
        if (*end) return false;
    }
    return true;
}

static void saveKeys() {
    File f = LittleFS.open("/keys.txt", "w");
    if (!f) { Serial.println("FS write error"); return; }
    for (uint8_t i = 0; i < s_keyCount; i++) {
        f.printf("%08lX:", (unsigned long)s_keys[i].id);
        for (uint8_t b = 0; b < 16; b++) f.printf("%02X", s_keys[i].key[b]);
        f.println();
    }
    f.close();
}

static void loadKeys() {
    s_keyCount = 0;
    File f = LittleFS.open("/keys.txt", "r");
    if (!f) return;
    while (f.available() && s_keyCount < 8) {
        String line = f.readStringUntil('\n');
        line.trim();
        int colon = line.indexOf(':');
        if (colon < 1 || (int)(line.length() - colon - 1) != 32) continue;
        uint32_t id = (uint32_t)strtoul(line.substring(0, colon).c_str(), nullptr, 16);
        if (parseHexKey(line.substring(colon + 1).c_str(), s_keys[s_keyCount].key)) {
            s_keys[s_keyCount].id = id;
            wmbus.setKey(id, s_keys[s_keyCount].key);
            s_keyCount++;
        }
    }
    f.close();
}

static void listKeys() {
    if (s_keyCount == 0) { Serial.println("  (no keys stored)"); return; }
    for (uint8_t i = 0; i < s_keyCount; i++) {
        Serial.printf("  [%u] %08lX : ", i, (unsigned long)s_keys[i].id);
        for (uint8_t b = 0; b < 16; b++) Serial.printf("%02X", s_keys[i].key[b]);
        Serial.println();
    }
}

// ── Meter table ───────────────────────────────────────────────────────────

struct MeterEntry {
    uint32_t id;
    char     vendor[4];
    uint8_t  devType;
    bool     encrypted;
    bool     decrypted;
    int8_t   rssi;
    uint32_t count;
};
static MeterEntry s_meters[16];
static uint8_t    s_meterCount = 0;

static void trackMeter(const WMBus::Frame& frame) {
    for (uint8_t i = 0; i < s_meterCount; i++) {
        if (s_meters[i].id == frame.meterId()) {
            s_meters[i].encrypted = frame.encrypted();
            s_meters[i].decrypted = frame.decrypted();
            s_meters[i].rssi      = frame.rssiDbm();
            s_meters[i].count++;
            return;
        }
    }
    if (s_meterCount >= 16) return;
    MeterEntry& e = s_meters[s_meterCount++];
    e.id        = frame.meterId();
    e.devType   = frame.devType();
    e.encrypted = frame.encrypted();
    e.decrypted = frame.decrypted();
    e.rssi      = frame.rssiDbm();
    e.count     = 1;
    strncpy(e.vendor, frame.vendorStr(), 3);
    e.vendor[3] = '\0';
}

static void listMeters() {
    if (s_meterCount == 0) { Serial.println("  (no meters seen yet)"); return; }
    Serial.println("  ID         Vendor  Type              Enc  AES    RSSI   Frames");
    for (uint8_t i = 0; i < s_meterCount; i++) {
        const MeterEntry& e = s_meters[i];
        const char* aes = !e.encrypted ? "N/A  " : (e.decrypted ? "OK   " : "FAIL ");
        Serial.printf("  %08lX   %-6s  %-16s  %-4s %-6s %4d   %lu\n",
            (unsigned long)e.id, e.vendor,
            WMBus::devTypeName(e.devType),
            e.encrypted ? "yes" : "no",
            aes, e.rssi, (unsigned long)e.count);
    }
}

// ── Radio config ──────────────────────────────────────────────────────────

static const WMBus::Radio::Gain GAIN_MAP[] = {
    WMBus::Radio::Gain::High,
    WMBus::Radio::Gain::Medium,
    WMBus::Radio::Gain::Low,
    WMBus::Radio::Gain::Min,
};
static const WMBus::Radio::Bandwidth BW_MAP[] = {
    WMBus::Radio::Bandwidth::Wide,
    WMBus::Radio::Bandwidth::Medium,
    WMBus::Radio::Bandwidth::Narrow,
};

struct RadioConfig { uint8_t gain; uint8_t bw; uint8_t pqt; int8_t freqOffset; uint8_t mode; };
static const RadioConfig RCFG_DEFAULT = { 0, 0, 0, 0, 0 };  // High, Wide, PQT off, no offset, T1+C1B
static RadioConfig s_rcfg = RCFG_DEFAULT;

static void saveRadioConfig() {
    File f = LittleFS.open("/radio.cfg", "w");
    if (!f) { Serial.println("FS write error"); return; }
    f.printf("gain=%u\nbw=%u\npqt=%u\nfreqoffset=%d\nmode=%u\n", s_rcfg.gain, s_rcfg.bw, s_rcfg.pqt, s_rcfg.freqOffset, s_rcfg.mode);
    f.close();
}

static void loadRadioConfig() {
    File f = LittleFS.open("/radio.cfg", "r");
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if      (line.startsWith("gain=")) s_rcfg.gain = (uint8_t)line.substring(5).toInt();
        else if (line.startsWith("bw="))   s_rcfg.bw   = (uint8_t)line.substring(3).toInt();
        else if (line.startsWith("pqt="))        s_rcfg.pqt        = (uint8_t)line.substring(4).toInt();
        else if (line.startsWith("freqoffset=")) s_rcfg.freqOffset = (int8_t) line.substring(11).toInt();
        else if (line.startsWith("mode="))       s_rcfg.mode       = (uint8_t)line.substring(5).toInt();
    }
    f.close();
}

static void applyRadioConfig() {
    // Only apply values that deviate from the mode-table defaults.
    // gain=0 (High), bw=0 (Wide), pqt=0, freqOffset=0 are all already
    // set correctly by radio.begin() — calling the setters anyway would
    // trigger unnecessary SIDLE/startRx cycles that can disturb the chip.
    // if (s_rcfg.gain > 0)           radio.setGain(GAIN_MAP[s_rcfg.gain]);
    // if (s_rcfg.bw   > 0)           radio.setBandwidth(BW_MAP[s_rcfg.bw]);
    // if (s_rcfg.pqt  > 0)           radio.setPreambleQuality(s_rcfg.pqt);
    // if (s_rcfg.freqOffset != 0)    radio.setFrequencyOffset(s_rcfg.freqOffset);
}

static void resetRadioConfig() {
    s_rcfg = RCFG_DEFAULT;
    applyRadioConfig();
    saveRadioConfig();
}

static void setMode(uint8_t modeIdx) {
    if (modeIdx > 4) { Serial.println("Invalid (0-4)."); return; }
    static const WMBus::Mode modes[] = { WMBus::Mode::T1C1B, WMBus::Mode::T1, WMBus::Mode::C1A, WMBus::Mode::C1B, WMBus::Mode::S1 };
    static const char* MODE_NAMES[] = { "T1+C1B", "T1", "C1A", "C1B", "S1" };
    s_rcfg.mode = modeIdx;
    wmbus.setMode(modes[modeIdx]);
    saveRadioConfig();
    Serial.printf("Mode set to %u = %s. Receiver restarted.\n", modeIdx, MODE_NAMES[modeIdx]);
}

static void printRadioConfig() {
    static const char* GAIN_NAMES[] = { "High", "Medium", "Low", "Min" };
    static const char* BW_NAMES[]   = { "Wide", "Medium", "Narrow" };
    static const char* MODE_NAMES[] = { "T1+C1B", "T1", "C1A", "C1B", "S1" };
    Serial.printf("  Mode:        %u = %s\n", s_rcfg.mode, MODE_NAMES[s_rcfg.mode]);
    Serial.printf("  Gain:        %u = %s\n", s_rcfg.gain, GAIN_NAMES[s_rcfg.gain]);
    Serial.printf("  Bandwidth:   %u = %s\n", s_rcfg.bw,   BW_NAMES[s_rcfg.bw]);
    Serial.printf("  PQT:         %u\n",      s_rcfg.pqt);
    Serial.printf("  FreqOffset:  %d LSB (~%d kHz)\n", s_rcfg.freqOffset, (int)s_rcfg.freqOffset * 159 / 100);
}

// ── Serial input ──────────────────────────────────────────────────────────
// s_menuStep: 0=idle
//             1=add:id  2=add:key  3=del:id
//             4=set gain  5=set bw  6=set pqt  7=set freqoffset

static uint8_t  s_menuStep  = 0;
static char     s_lineBuf[40];
static uint8_t  s_lineLen   = 0;
static uint32_t s_pendingId = 0;

static void handleInput(char ch) {
    if (s_menuStep == 0) {
        if (ch == 'd' || ch == 'D') {
            wmbus.setDebug(!wmbus.isDebug());
            Serial.printf("Debug: %s\n", wmbus.isDebug() ? "ON" : "OFF");
        } else if (ch == 'l' || ch == 'L') {
            listMeters();
        } else if (ch == 'k' || ch == 'K') {
            listKeys();
        } else if (ch == 'a' || ch == 'A') {
            Serial.print("Meter ID (hex): ");
            s_menuStep = 1; s_lineLen = 0;
        } else if (ch == 'x' || ch == 'X') {
            Serial.print("Meter ID to delete (hex): ");
            s_menuStep = 3; s_lineLen = 0;
        } else if (ch == 'T') {
            setMode(0);                 // T1+C1B (combined, auto-detect)
        } else if (ch == 't') {
            setMode(1);                 // T1 (pure 3of6)
        } else if (ch == 'c') {
            setMode(2);                 // C1A (Frame A, sync 0x54CD)
        } else if (ch == 'C') {
            setMode(3);                 // C1B (Frame B, sync 0x543D)
        } else if (ch == 's' || ch == 'S') {
            setMode(4);
        } else if (ch == 'g' || ch == 'G') {
            Serial.println("Empfangsverstaerkung (LNA):");
            Serial.println("  0 = High    volle Empfindlichkeit — hoert alles (Standard)");
            Serial.println("  1 = Medium  ca. -7 dB  — schwache/entfernte Geraete fallen weg");
            Serial.println("  2 = Low     ca. -15 dB — nur starke Signale in der Naehe");
            Serial.println("  3 = Min     -15 dB LNA + DVGA reduziert — taubstes Setting");
            Serial.printf( "Current: %u  > ", s_rcfg.gain);
            s_menuStep = 4; s_lineLen = 0;
        } else if (ch == 'b' || ch == 'B') {
            Serial.println("Kanalbreite (Bandpassfilter):");
            Serial.println("  0 = Wide    325 kHz — tolerant bei Frequenzabweichungen (Standard)");
            Serial.println("  1 = Medium  203 kHz");
            Serial.println("  2 = Narrow  135 kHz — filtert Stoerer auf Nachbarkanaelen heraus");
            Serial.printf( "Current: %u  > ", s_rcfg.bw);
            s_menuStep = 5; s_lineLen = 0;
        } else if (ch == 'p' || ch == 'P') {
            Serial.println("Praeambel-Qualitaetsschwelle (0-7):");
            Serial.println("  0 = aus     jedes Sync-Wort wird akzeptiert (Standard)");
            Serial.println("  2-3 = leicht  reduziert Fehlausloeser durch Rauschen");
            Serial.println("  4-6 = mittel  benoetigt saubere 0xAA-Praeambel vor dem Sync");
            Serial.println("  7 = strikt  nur sehr saubere Signale passieren");
            Serial.printf( "Current: %u  > ", s_rcfg.pqt);
            s_menuStep = 6; s_lineLen = 0;
        } else if (ch == 'f' || ch == 'F') {
            Serial.println("Statischer Frequenzoffset (-128..127 LSB, ~1.59 kHz/LSB):");
            Serial.println("  0   = kein Offset (Standard)");
            Serial.println("  >0  = Empfaenger hoeher verschieben (FREQEST war positiv)");
            Serial.println("  <0  = Empfaenger tiefer verschieben (FREQEST war negativ)");
            Serial.printf( "Current: %d  > ", s_rcfg.freqOffset);
            s_menuStep = 7; s_lineLen = 0;
        } else if (ch == 'r' || ch == 'R') {
            printRadioConfig();
        } else if (ch == 'z' || ch == 'Z') {
            resetRadioConfig();
            Serial.println("Radio-Konfiguration auf Standard zurueckgesetzt:");
            printRadioConfig();
        } else if (ch == 'h' || ch == 'H') {
            Serial.println("  d  Debug-Ausgabe ein/aus");
            Serial.println("  l  Tabelle gesehener Zaehler anzeigen");
            Serial.println("  T  Mode T1+C1B (kombiniert, faengt T1 + C1B gleichzeitig)");
            Serial.println("  t  Mode T1  (3of6, Standard)");
            Serial.println("  c  Mode C1A (Frame A, sync 0x54CD)");
            Serial.println("  C  Mode C1B (Frame B, sync 0x543D)");
            Serial.println("  s  Mode S1");
            Serial.println("  k  AES-Keys auflisten");
            Serial.println("  a  AES-Key hinzufuegen / aktualisieren");
            Serial.println("  x  AES-Key loeschen");
            Serial.println("  g  Empfangsverstaerkung einstellen");
            Serial.println("  b  Kanalbreite einstellen");
            Serial.println("  p  Praeambel-Qualitaetsschwelle einstellen");
            Serial.println("  f  Frequenzoffset einstellen (FREQEST-Wert aus Debug)");
            Serial.println("  r  aktuelle Radio-Konfiguration anzeigen");
            Serial.println("  z  Radio-Konfiguration auf Standard zuruecksetzen");
            Serial.println("  h  diese Hilfe");
        } else if (ch == '\n') {
            Serial.println();
        }
        return;
    }

    // Data input: q cancels, Enter confirms
    if (ch == 'q' || ch == 'Q') {
        Serial.println("\nCancelled.");
        s_menuStep = 0; s_lineLen = 0;
        return;
    }
    if (ch == '\r' || ch == '\n') {
        if (s_lineLen == 0) return;  // skip empty lines from double CR/LF
        s_lineBuf[s_lineLen] = '\0';
        s_lineLen = 0;
        Serial.println();

        if (s_menuStep == 1) {
            s_pendingId = (uint32_t)strtoul(s_lineBuf, nullptr, 16);
            Serial.print("AES key (32 hex chars): ");
            s_menuStep = 2;
            return;
        }

        if (s_menuStep == 2) {
            uint8_t key[16];
            if (!parseHexKey(s_lineBuf, key)) {
                Serial.println("Invalid — must be exactly 32 hex characters.");
            } else {
                bool updated = false;
                for (uint8_t i = 0; i < s_keyCount; i++) {
                    if (s_keys[i].id == s_pendingId) {
                        memcpy(s_keys[i].key, key, 16);
                        wmbus.setKey(s_pendingId, key);
                        saveKeys();
                        Serial.printf("Key for %08lX updated.\n", (unsigned long)s_pendingId);
                        updated = true;
                        break;
                    }
                }
                if (!updated) {
                    if (s_keyCount >= 8) {
                        Serial.println("Key table full (max 8).");
                    } else {
                        s_keys[s_keyCount].id = s_pendingId;
                        memcpy(s_keys[s_keyCount].key, key, 16);
                        wmbus.setKey(s_pendingId, key);
                        s_keyCount++;
                        saveKeys();
                        Serial.printf("Key for %08lX added.\n", (unsigned long)s_pendingId);
                    }
                }
            }
            s_menuStep = 0;
            return;
        }

        if (s_menuStep == 3) {
            uint32_t id = (uint32_t)strtoul(s_lineBuf, nullptr, 16);
            bool found = false;
            for (uint8_t i = 0; i < s_keyCount; i++) {
                if (s_keys[i].id == id) {
                    for (uint8_t j = i; j < s_keyCount - 1; j++) s_keys[j] = s_keys[j + 1];
                    s_keyCount--;
                    saveKeys();
                    Serial.printf("Key for %08lX deleted (restart to deregister).\n", (unsigned long)id);
                    found = true;
                    break;
                }
            }
            if (!found) Serial.printf("No key found for %08lX.\n", (unsigned long)id);
            s_menuStep = 0;
            return;
        }

        if (s_menuStep == 4) {
            uint8_t v = (uint8_t)atoi(s_lineBuf);
            if (v > 3) { Serial.println("Invalid (0-3)."); }
            else {
                s_rcfg.gain = v;
                radio.setGain(GAIN_MAP[v]);
                saveRadioConfig();
                Serial.printf("Gain set to %u.\n", v);
            }
            s_menuStep = 0;
            return;
        }

        if (s_menuStep == 5) {
            uint8_t v = (uint8_t)atoi(s_lineBuf);
            if (v > 2) { Serial.println("Invalid (0-2)."); }
            else {
                s_rcfg.bw = v;
                radio.setBandwidth(BW_MAP[v]);
                saveRadioConfig();
                Serial.printf("Bandwidth set to %u.\n", v);
            }
            s_menuStep = 0;
            return;
        }

        if (s_menuStep == 6) {
            uint8_t v = (uint8_t)atoi(s_lineBuf);
            if (v > 7) { Serial.println("Invalid (0-7)."); }
            else {
                s_rcfg.pqt = v;
                radio.setPreambleQuality(v);
                saveRadioConfig();
                Serial.printf("PQT set to %u.\n", v);
            }
            s_menuStep = 0;
            return;
        }

        if (s_menuStep == 7) {
            int v = atoi(s_lineBuf);
            if (v < -128 || v > 127) { Serial.println("Invalid (-128..127)."); }
            else {
                s_rcfg.freqOffset = (int8_t)v;
                radio.setFrequencyOffset((int8_t)v);
                saveRadioConfig();
                Serial.printf("FreqOffset set to %d LSB (~%d kHz).\n", v, v * 159 / 100);
            }
            s_menuStep = 0;
            return;
        }

    }

    // Accumulate + echo
    if (s_lineLen < sizeof(s_lineBuf) - 1) {
        s_lineBuf[s_lineLen++] = ch;
        Serial.print(ch);
    }
}

// ── Frame callback ────────────────────────────────────────────────────────

static void onFrame(const WMBus::Frame& frame,
                    const WMBus::DataRecord* records, uint8_t count) {

    bool valid = frame.complete() && frame.badBlocks() == 0
                 && (!frame.encrypted() || frame.decrypted());

    if (wmbus.isDebug()) {
        Serial.printf("Vendor: %s - Type: %s - SN: %08lX - RSSI: %d dBm - Status: ",
            frame.vendorStr(), WMBus::devTypeName(frame.devType()),
            (unsigned long)frame.meterId(), frame.rssiDbm());
        if (!frame.complete())
            Serial.printf("Invalid length (%u/%u B)\n", frame.receivedLen(), frame.expectedLen());
        else if (frame.badBlocks() > 0)
            Serial.printf("Bad CRC (%u/%u blocks)\n", frame.badBlocks(), frame.totalBlocks());
        else if (frame.encrypted() && !frame.decrypted())
            Serial.println("Missing AES");
        else
            Serial.println("Valid");
    }

    if (frame.complete() && frame.badBlocks() == 0) trackMeter(frame);

    if (!valid) return;

    if (wmbus.isDebug()) {
        for (uint8_t i = 0; i < count; i++) {
            const WMBus::DataRecord& r = records[i];
            Serial.printf("  [%2u] DIF=%02X VIF=%02X %-12s %-4s st%u -> ",
                i, r.dif, r.vif, r.quantityStr(), r.functionStr(), r.storage);
            if (r.quantity == WMBus::Quantity::Date) {
                Serial.printf("%04u-%02u-%02u\n", r.year, r.month, r.day);
            } else if (r.quantity == WMBus::Quantity::DateTime) {
                Serial.printf("%04u-%02u-%02u %02u:%02u\n",
                    r.year, r.month, r.day, r.hour, r.minute);
            } else {
                bool isTemp = (r.quantity == WMBus::Quantity::FlowTemperature       ||
                               r.quantity == WMBus::Quantity::ReturnTemperature     ||
                               r.quantity == WMBus::Quantity::ExternalTemperature   ||
                               r.quantity == WMBus::Quantity::TemperatureDifference);
                Serial.printf("%.*f %s (raw=%ld exp=%d)\n",
                    isTemp ? 1 : 3, r.value(), r.unitStr(), (long)r.rawValue, r.exponent);
            }
        }
    }

    // Valid frame one-liner: "[64095861] Water - 747.797 m3 - 0 m3/h - 23.3 C"
    // For non-water device types only the identification line is printed.
    uint8_t dt = frame.devType();
    bool isWater = (dt == 0x06 || dt == 0x07 || dt == 0x16 || dt == 0x17);
    bool isHeat  = (dt == 0x04 || dt == 0x05 || dt == 0x0A ||
                    dt == 0x0B || dt == 0x0C || dt == 0x0D);
    Serial.printf("[%08lX] %s", (unsigned long)frame.meterId(), WMBus::devTypeName(dt));
    if (isHeat) {
        // Heat one-liner: energy, power, flow rate (l/h), temperature difference —
        // in that fixed order regardless of the record order in the telegram.
        // Find the first current (storage 0, tariff 0, instantaneous) record of a
        // given quantity; returns nullptr if the meter does not send it.
        auto findCurrent = [&](WMBus::Quantity q) -> const WMBus::DataRecord* {
            for (uint8_t i = 0; i < count; i++) {
                const WMBus::DataRecord& r = records[i];
                if (r.storage == 0 && r.tariff == 0
                 && r.function == WMBus::Function::Instantaneous
                 && r.quantity == q)
                    return &records[i];
            }
            return nullptr;
        };
        if (const WMBus::DataRecord* r = findCurrent(WMBus::Quantity::Energy))
            Serial.printf(" - %.3f kWh", r->value() / 1000.0f);
        if (const WMBus::DataRecord* r = findCurrent(WMBus::Quantity::Power))
            Serial.printf(" - %.3f W", r->value());
        if (const WMBus::DataRecord* r = findCurrent(WMBus::Quantity::VolumeFlow))
            Serial.printf(" - %.0f l/h", r->value() * 1000.0f);
        if (const WMBus::DataRecord* r = findCurrent(WMBus::Quantity::TemperatureDifference))
            Serial.printf(" - %.1f K", r->value());
    } else if (isWater) {
        for (uint8_t i = 0; i < count; i++) {
            const WMBus::DataRecord& r = records[i];
            if (r.storage  != 0)                              continue;
            if (r.function != WMBus::Function::Instantaneous) continue;
            if (r.quantity == WMBus::Quantity::Unknown)        continue;
            if (r.quantity == WMBus::Quantity::Date
             || r.quantity == WMBus::Quantity::DateTime)       continue;
            bool isTemp = (r.quantity == WMBus::Quantity::FlowTemperature       ||
                           r.quantity == WMBus::Quantity::ReturnTemperature     ||
                           r.quantity == WMBus::Quantity::ExternalTemperature   ||
                           r.quantity == WMBus::Quantity::TemperatureDifference);
            Serial.printf(" - %.*f %s", isTemp ? 1 : 3, r.value(), r.unitStr());
        }
    }
    Serial.println();
}

// ── Setup ─────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    delay(1500);

    Serial.println("\n=== wMBus receiver ===");

#if defined(ARDUINO_ARCH_RP2040)
    CC1101_SPI.setRX(CC1101_MISO);
    CC1101_SPI.setTX(CC1101_MOSI);
    CC1101_SPI.setSCK(CC1101_SCK);
    CC1101_SPI.begin();
#elif defined(ARDUINO_ARCH_ESP32)
    CC1101_SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI);
#else
    CC1101_SPI.begin();
#endif

    bool cc1101ok = radio.begin(CC1101_CS, CC1101_GDO0, CC1101_SPI, WMBus::Mode::T1C1B);
    uint8_t ver  = radio.chipVersion();
    uint8_t part = radio.partNumber();

    Serial.printf("CC1101 VERSION=0x%02X  PARTNUM=0x%02X — ", ver, part);
    if (ver == 0x00 || ver == 0xFF) {
        Serial.println("FAIL: no response, check wiring/power");
        while (true) delay(1000);
    } else if (cc1101ok) {
        if (part == 0x00) Serial.println("OK");
        else Serial.println("OK (SPI works, but PARTNUM!=0x00 — clone or marginal read?)");
    } else {
        Serial.println("WARN: chip not configured");
    }

    wmbus.setDebug(false);
    wmbus.begin(radio);
    wmbus.setLogger([](const char* msg){ Serial.println(msg); });
    wmbus.setCallback(onFrame);

#if defined(ARDUINO_ARCH_ESP32)
    bool fsOk = LittleFS.begin(true);  // format-on-fail
#else
    bool fsOk = LittleFS.begin();
    if (!fsOk) {                       // first boot: format, then mount
        LittleFS.format();
        fsOk = LittleFS.begin();
    }
#endif
    if (!fsOk)
        Serial.println("WARN: LittleFS unavailable — keys/config will NOT persist.");

    loadRadioConfig();
    applyRadioConfig();

    // Apply stored mode if different from default (T1+C1B = index 0)
    if (s_rcfg.mode != 0 && s_rcfg.mode <= 4) {
        static const WMBus::Mode modes[] = { WMBus::Mode::T1C1B, WMBus::Mode::T1, WMBus::Mode::C1A, WMBus::Mode::C1B, WMBus::Mode::S1 };
        wmbus.setMode(modes[s_rcfg.mode]);
    }

    loadKeys();

    Serial.println("Radio:");
    printRadioConfig();
    Serial.printf("Keys:  %u loaded  (press 'h' for help)\n", s_keyCount);
    static const char* MODE_NAMES[] = { "T1+C1B", "T1", "C1A", "C1B", "S1" };
    Serial.printf("Listening for %s frames...\n\n", MODE_NAMES[s_rcfg.mode]);
}

// ── Loop ─────────────────────────────────────────────────────────────────

void loop() {
    wmbus.loop();

    while (Serial.available()) {
        handleInput((char)Serial.read());
    }
}
