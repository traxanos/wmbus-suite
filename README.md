# wmbus-suite

Development and demo environment for the **[WMBus Arduino library](https://github.com/traxanos/wmbus)**.

`src/main.cpp` is a demo application that receives wMBus telegrams, manages AES keys, and prints meter values via serial. The library itself lives in `lib/wmbus/` as a **git submodule** and is the primary artifact.

## Repository layout

```
src/main.cpp        Demo app — serial UI, AES key store, telegram output
lib/wmbus/          WMBus library (git submodule → github.com/traxanos/wmbus)
test/test_unit/     Native unit tests for the library (PlatformIO + Unity)
releases/           Pre-built .uf2 / .bin firmware images
platformio.ini      Build targets: rpipico, rpipico2, esp32
HOWTO.md            End-user guide (wiring, flashing, serial commands)
```

## Clone

```sh
git clone --recurse-submodules https://github.com/traxanos/wmbus-suite.git
```

If you already cloned without `--recurse-submodules`:

```sh
git submodule update --init
```

## Build

Requires [PlatformIO](https://platformio.org/).

```sh
pio run                         # compile (default: rpipico2)
pio run -e rpipico              # Pico 1 (RP2040)
pio run -e esp32                # ESP32 DevKit
pio run -t upload               # flash via picotool (USB, Pico only)
pio device monitor              # serial monitor, 115200 baud
```

## Unit tests

The native test suite runs on the host (no hardware required):

```sh
pio test -e native
```

## Library

The WMBus library (`lib/wmbus/`) is self-contained and can be used independently:

- **PlatformIO:** copy `lib/wmbus/` into your project's `lib/` folder, or reference it as a git submodule.
- **Arduino IDE:** install via Library Manager or copy the folder into your `libraries/` directory.

See [lib/wmbus/README.md](lib/wmbus/README.md) for the full API reference, wiring tables, quick-start sketch, and decryption details.

## End-user firmware

Pre-built firmware for the demo app is in `releases/`. See [HOWTO.md](HOWTO.md) for flashing instructions and serial command reference.
