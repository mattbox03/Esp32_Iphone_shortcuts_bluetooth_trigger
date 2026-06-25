# Precompiled firmware

`esp32-bt-trigger.factory.bin` is a ready-to-flash image for a **classic ESP32**
(4 MB flash). It already contains bootloader + partition table + app, merged so it
is written at offset **0x0**. The Bluetooth name baked in is **`ESP32-BT-Trigger`**
(to use a different name, build from source and change `DEVICE_NAME`).

## Flash it

### Option A — esptool (command line)

```bash
pip install esptool
esptool.py --chip esp32 --port <PORT> --baud 921600 write_flash 0x0 esp32-bt-trigger.factory.bin
```

Replace `<PORT>` with e.g. `COM5` (Windows), `/dev/ttyUSB0` (Linux) or
`/dev/cu.usbserial-XXXX` (macOS). Tip: run `esptool.py erase_flash` first for a
clean state.

### Option B — web flasher (nothing to install)

Open <https://espressif.github.io/esptool-js/>, click **Connect**, select the
board's serial port, then flash `esp32-bt-trigger.factory.bin` at offset **0x0**.

## After flashing

On the iPhone: **Settings → Bluetooth → `ESP32-BT-Trigger`** to pair, then create a
Shortcuts automation *"When ESP32-BT-Trigger connects"*. See the main
[README](../README.md).
