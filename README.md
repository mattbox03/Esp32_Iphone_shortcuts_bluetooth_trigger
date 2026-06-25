# ESP32 BT Trigger

A tiny ESP32 firmware that acts as a **Bluetooth trigger for iPhone Shortcuts
automations**.

The ESP32 presents itself to your iPhone as a **Classic Bluetooth** device (a
mouse). It never sends any input — the **act of connecting** is the trigger: iOS
runs a Shortcuts automation set to *"When &lt;device&gt; connects"*. After a reboot
or a dropped link, the ESP32 **reconnects on its own** to the last paired iPhone.

### What you can do with it

Power the ESP32 from something you switch on (a scooter, bike, car, desk lamp,
3D-printer, door…). When it powers up it connects to your iPhone, and your
Shortcuts automation fires automatically — for example:

- open a navigation / dashboard app,
- toggle a Focus mode or Do Not Disturb,
- start a timer, set the volume, send a message, run any shortcut.

It's the "when this turns on, do that on my phone" building block, done over
plain Bluetooth with no extra app on the phone.

> **Why a mouse and not a keyboard?** While an HID *keyboard* is connected, iOS
> hides the on-screen keyboard. A pointing device does not, so your on-screen
> keyboard keeps working (iOS shows the AssistiveTouch pointer instead).
>
> **Why Classic Bluetooth and not BLE?** This project is for people who want a
> Classic-BT HID device recognized by iPhone. (A BLE HID would also work; this
> one deliberately uses Classic BT via ESP-IDF.) Note: Classic **SPP** (serial)
> does *not* work with iPhone — Classic **HID** does, which is what this uses.

---

## Change the name

The Bluetooth name is defined in one place, in [`src/main.cpp`](src/main.cpp):

```c
#define DEVICE_NAME  "ESP32-BT-Trigger"
```

Change it, rebuild, re-flash. After a name change, "Forget This Device" on the
iPhone and pair again.

---

## Hardware

- A **classic ESP32** with Classic Bluetooth — **not** an ESP32-C3/C6/H2 (those
  are BLE-only). Set `board` in `platformio.ini` (default `wemos_d1_mini32`; a
  generic module also works as `esp32dev`).
- A USB cable.

---

## Build & flash (PlatformIO)

```bash
pio run                 # build
pio run -t erase        # (recommended on first flash) wipe old pairings
pio run -t upload       # flash the ESP32
pio device monitor      # serial logs at 115200 baud
```

> The serial port is auto-detected; set `upload_port` / `monitor_port` in
> `platformio.ini` if you have several devices.
>
> This project uses **ESP-IDF 5.3.1** with **CMake 3.30** (see `platformio.ini`):
> the *Classic BT HID Device* role is disabled in the precompiled Arduino
> libraries, so ESP-IDF is required. The first build downloads the SDK and is slow.

---

## iPhone setup

### 1) Pair (once)

1. Flash the firmware and power the ESP32.
2. iPhone → **Settings → Bluetooth** → tap the device under **Other Devices**
   (pairs with no code, "Just Works") → it moves to **My Devices**.

### 2) Create the automation (Shortcuts app)

1. **Shortcuts** app → **Automation** tab → **+** → **Create Personal Automation**.
2. **Bluetooth** → **Device** → select your device → **Next**.
3. Add the actions you want → **Next** → turn **off** *"Ask Before Running"* → **Done**.

From now on, every time the ESP32 powers up and connects, the automation runs.
Very useful with Home Assistant Automations.
---

## Notes

- **Reconnect** is handled by the ESP32: it calls back the last paired iPhone
  (from the saved-devices list, which persists across reboots). Interval:
  `RECONNECT_PERIOD_S` in `src/main.cpp`.
- **On-screen pointer**: with a mouse iOS shows the AssistiveTouch cursor; the
  on-screen keyboard remains usable.
- The red/yellow `BT_BTM` / `BT_HCI` lines during connection are normal bluedroid
  log noise, not real errors.

---

## How it's built

| File | Purpose |
|------|---------|
| `src/main.cpp` | Firmware: Classic BT HID mouse + auto-reconnect |
| `sdkconfig.defaults` | Re-enables Classic BT + HID Device in the ESP-IDF stack |
| `platformio.ini` | ESP-IDF 5.3.1, CMake 3.30 |

## License

MIT — see [LICENSE](LICENSE).
