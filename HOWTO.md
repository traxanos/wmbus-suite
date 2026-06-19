# wMBus Receiver — Quickstart

## Verdrahtung CC1101 ↔ Mikrocontroller

> **Wichtig:** CC1101 läuft auf **3,3 V**. Niemals an 5 V anschließen.

### Raspberry Pi Pico / Pico 2 *(identisches Pinout)*

| CC1101 Pin | Signal    | GPIO   | Pico-Pin |
|-----------|-----------|--------|----------|
| 1 VCC     | 3,3 V     | —      | 36       |
| 2 GND     | GND       | —      | 38       |
| 3 MOSI    | SPI0 TX   | GPIO19 | 25       |
| 4 SCK     | SPI0 SCK  | GPIO18 | 24       |
| 5 MISO    | SPI0 RX   | GPIO16 | 21       |
| 6 CSN     | CS        | GPIO17 | 22       |
| 7 GDO0    | IRQ       | GPIO20 | 26       |
| 8 GDO2    | —         | —      | —        |

### ESP32 DevKit

| CC1101 Pin | Signal     | GPIO   |
|-----------|------------|--------|
| 1 VCC     | 3,3 V      | —      |
| 2 GND     | GND        | —      |
| 3 MOSI    | VSPI MOSI  | GPIO23 |
| 4 SCK     | VSPI SCK   | GPIO18 |
| 5 MISO    | VSPI MISO  | GPIO19 |
| 6 CSN     | CS         | GPIO5  |
| 7 GDO0    | IRQ        | GPIO4  |
| 8 GDO2    | —          | —      |

---

## Firmware flashen

### Pico / Pico 2 — `.uf2`-Datei

1. **BOOTSEL-Taste** auf dem Pico gedrückt halten.
2. USB-Kabel einstecken — der Pico erscheint als USB-Laufwerk namens **RPI-RP2**.
3. Die passende `.uf2`-Datei auf das Laufwerk kopieren:
   - Pico (RP2040): `wmbus-pico.uf2`
   - Pico 2 (RP2350): `wmbus-pico2.uf2`
4. Das Laufwerk trennt sich automatisch — fertig.

### ESP32 — mit dem Flash Download Tool (Windows)

1. [Flash Download Tool](https://www.espressif.com/en/support/download/other-tools) von Espressif herunterladen und starten.
2. Chip-Typ **ESP32** wählen.
3. Vier Dateien eintragen (jeweils Pfad und Adresse):

   | Datei                   | Adresse    |
   |-------------------------|------------|
   | `bootloader.bin`        | `0x1000`   |
   | `partitions.bin`        | `0x8000`   |
   | `boot_app0.bin`         | `0xe000`   |
   | `wmbus-esp32.bin`       | `0x10000`  |

4. COM-Port auswählen, Baudrate **921600**, **START** klicken.

### ESP32 — mit esptool (Mac / Linux)

```bash
esptool.py --port /dev/ttyUSB0 --baud 921600 write_flash \
  0x1000  bootloader.bin \
  0x8000  partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 wmbus-esp32.bin
```

---

## Serieller Monitor

Zum Bedienen wird ein serielles Terminalprogramm benötigt (**115200 Baud**):

| Betriebssystem | Empfehlung |
|----------------|-----------|
| Windows        | [PuTTY](https://www.putty.org) — Connection type: Serial, Speed: 115200 |
| Mac            | CoolTerm oder im Terminal: `screen /dev/tty.usbserial* 115200` |
| Linux          | `screen /dev/ttyUSB0 115200` |
| Alle           | Arduino IDE → Werkzeuge → Serieller Monitor |

---

## Bedienung

Alle Befehle werden durch einfaches Drücken der Taste ausgeführt — **kein Enter nötig**.
Ausnahme: Dateneingaben (Zähler-ID, Key) mit **Enter** bestätigen, **q** bricht ab.

| Taste | Funktion |
|-------|----------|
| `h`   | Hilfe anzeigen |
| `d`   | Debug-Ausgabe ein/aus |
| `l`   | Tabelle aller gesehenen Zähler anzeigen |
| `t`   | Empfangsmode T1 (Standard — Wasserzähler) |
| `s`   | Empfangsmode S1 |
| `c`   | Empfangsmode C1A |
| `k`   | Gespeicherte AES-Keys auflisten |
| `a`   | AES-Key hinzufügen / aktualisieren |
| `x`   | AES-Key löschen |
| `g`   | Empfangsempfindlichkeit einstellen |
| `b`   | Kanalbreite einstellen |
| `p`   | Praeambel-Qualitätsschwelle einstellen |
| `r`   | Aktuelle Radio-Konfiguration anzeigen |

---

## AES-Key eingeben

Ohne Key werden verschlüsselte Telegramme nicht entschlüsselt.

1. `a` drücken
2. Zähler-ID (8 Hex-Zeichen) eingeben, z. B. `64095861`, Enter
3. AES-Key (32 Hex-Zeichen) eingeben, z. B. `092D7E2FD774F53F3289B70BB4DC7FC2`, Enter

Keys bleiben nach einem Neustart gespeichert.

---

## Ausgabe verstehen

```
[64095861] Water - 747.841 m3 - 0 m3/h - 18.1 C
[A1B2C3D4] Electricity
```

Mit `l` alle gesehenen Zähler auflisten — zeigt ob der eigene Zähler empfangen und
korrekt entschlüsselt wird (Spalte `AES = OK`).

---

## Empfang optimieren

Falls viele Fehler auftreten, können über `g`, `b` und `p` die Radio-Parameter angepasst
werden. Die jeweilige Taste zeigt eine Erklärung der verfügbaren Werte.
