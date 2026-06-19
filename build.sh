#!/usr/bin/env bash
set -e

PIO="$HOME/.platformio/penv/bin/pio"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
PYTHON="$HOME/.platformio/penv/bin/python"
BOOT_APP="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

BUILD_DIR=".pio/build"
DIST_DIR="build"
RELEASES_DIR="releases"
TIMESTAMP=$(date +"%Y%m%d-%H%M")
RELEASE_ZIP="$RELEASES_DIR/wmbus-debug-$TIMESTAMP.zip"
INDEX_PHP="$RELEASES_DIR/index.php"

echo "=== Build ==="
"$PIO" run -e rpipico -e rpipico2 -e esp32

echo ""
echo "=== Package ==="
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR" "$RELEASES_DIR"

# RP boards — UF2 only
cp "$BUILD_DIR/rpipico/firmware.uf2"  "$DIST_DIR/wmbus-pico.uf2"
cp "$BUILD_DIR/rpipico2/firmware.uf2" "$DIST_DIR/wmbus-pico2.uf2"

# ESP32 — combined flash image
"$PYTHON" "$ESPTOOL" --chip esp32 merge_bin \
  --output "$DIST_DIR/wmbus-esp32-combined.bin" \
  0x1000  "$BUILD_DIR/esp32/bootloader.bin"  \
  0x8000  "$BUILD_DIR/esp32/partitions.bin"  \
  0xe000  "$BOOT_APP"                         \
  0x10000 "$BUILD_DIR/esp32/firmware.bin"

cp HOWTO.md "$DIST_DIR/"

# Release ZIP
zip -j "$RELEASE_ZIP" "$DIST_DIR"/*

echo ""
echo "=== index.php ==="
cat > "$INDEX_PHP" << 'EOF'
<?php
$files = array_filter(scandir(__DIR__), fn($f) => is_file(__DIR__ . '/' . $f) && $f !== 'index.php');
usort($files, fn($a, $b) => strcmp($b, $a));
?><!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<title>wMBus Firmware</title>
<style>
  body { font-family: monospace; padding: 2em; background: #111; color: #ccc; }
  h1 { color: #fff; font-size: 1.2em; }
  table { border-collapse: collapse; width: 100%; }
  tr:hover td { background: #1e1e1e; }
  td { padding: .4em 1em; border-bottom: 1px solid #222; }
  td:last-child { text-align: right; }
  a { color: #6af; text-decoration: none; }
  a:hover { text-decoration: underline; }
</style>
</head>
<body>
<h1>wMBus Firmware Downloads</h1>
<table>
<?php foreach ($files as $f):
  $size = number_format(filesize(__DIR__ . '/' . $f) / 1024, 1) . ' KB';
  $mtime = date('Y-m-d H:i', filemtime(__DIR__ . '/' . $f));
?>
<tr>
  <td><a href="<?= htmlspecialchars($f) ?>"><?= htmlspecialchars($f) ?></a></td>
  <td><?= $mtime ?></td>
  <td><?= $size ?></td>
</tr>
<?php endforeach; ?>
</table>
</body>
</html>
EOF

echo ""
echo "=== Done ==="
echo "Release: $RELEASE_ZIP"
ls -lh "$RELEASES_DIR/"
