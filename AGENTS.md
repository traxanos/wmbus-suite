# wMBus Water — PlatformIO test project

`src/main.cpp` is a demo application that uses the WMBus library.
The library itself lives in `lib/wmbus/` and is the primary artifact.
Development platform: Raspberry Pi Pico 2 (RP2350), PlatformIO + earlephilhower/arduino-pico 5.6.0.

## Project layout

```
src/main.cpp        Full demo app: Serial menu, key store (LittleFS), meter table,
                    radio tuning (gain/BW/PQT/freq-offset), mode switching (T1/C1/S1)
lib/wmbus/          WMBus Arduino library as git submodule (github.com/traxanos/wmbus)
test/test_unit/     Native PlatformIO unit tests (Unity framework, no hardware needed)
platformio.ini      Build targets: rpipico, rpipico2, esp32, native (tests)
AGENTS.md           This file — includes lib/wmbus/AGENTS.md for library internals
CLAUDE.md           Claude Code entry point
```

## Serial menu commands (main.cpp)

| Key | Action |
|-----|--------|
| `d` | Toggle debug (library verbose log + app per-frame detail) |
| `l` | List seen meters |
| `k` | List stored AES keys |
| `a` | Add / update AES key |
| `x` | Delete AES key |
| `T` | Switch mode T1+C1B (combined — receives T1 and C1B at once) |
| `t` | Switch mode T1 (3of6) |
| `c` | Switch mode C1A (Frame A, sync 0x54CD) |
| `C` | Switch mode C1B (Frame B, sync 0x543D) |
| `s` | Switch mode S1 |
| `g` | Set LNA gain (0=High .. 3=Min) |
| `b` | Set channel bandwidth (0=Wide 325 kHz .. 2=Narrow 135 kHz) |
| `p` | Set preamble quality threshold (0=off .. 7=strict) |
| `f` | Set static frequency offset (LSB, ~1.59 kHz/LSB) |
| `r` | Show current radio config |
| `z` | Reset radio config to defaults |
| `h` | Help |

Keys and radio config persist to LittleFS (`/keys.txt`, `/radio.cfg`).

## Build

```
pio run                     # compile
pio run -t upload           # flash via picotool (USB)
pio device monitor          # 115200 baud, colorize + time filters
```

@lib/wmbus/AGENTS.md
