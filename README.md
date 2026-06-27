# ESP32 BT Trigger + Find My (dual mode)

One classic ESP32 doing **two things at once**:

1. **Classic Bluetooth HID trigger** — pairs with your iPhone as a mouse; the act
   of connecting fires an iOS **Shortcuts** automation. Auto-reconnects. (Same as
   the [esp32-bt-trigger](../esp32-bt-trigger) project.)
2. **BLE "Find My" beacon** — broadcasts a public key like a lost AirTag, so nearby
   Apple devices anonymously report its encrypted location. You retrieve and decrypt
   those locations yourself.

> ⚠️ **This is the unofficial, reverse-engineered route ([OpenHaystack](https://github.com/seemoo-lab/openhaystack)).**
> It is **not** a certified "Works with Find My" accessory and won't appear in the
> stock *Find My* app. Use it to locate **your own** property. The beacon uses a
> **static key** (it doesn't rotate like a real AirTag), so it may trigger
> "unknown accessory" anti-stalking alerts on other people's iPhones.

---

## Step 1 — Generate your keys

```bash
pip install cryptography
python tools/generate_keys.py
```

It prints two things:
- a **C array** → paste it into [`src/main.cpp`](src/main.cpp), replacing
  `findmy_public_key[28]`;
- a **key block** (Private / Advertisement / Hashed) → save it as `my-tag.keys`
  (you'll import it later). **Keep the private key secret.**

## Step 2 — Build & flash

```bash
pio run -t erase     # recommended on first flash
pio run -t upload
pio device monitor    # you should see "Find My beacon: advertising"
```

Pair the Classic side from the iPhone and set up the Shortcuts automation exactly
like the base project (Settings → Bluetooth → `ESP32-BT-Trigger`).

## Step 3 — Read the location ("associate it to your account")

There is **no pairing of the ESP32 to your Apple ID**. The ESP32 only broadcasts a
public key. "Association" happens on the **retrieval** side, with
**[macless-haystack](https://github.com/dchristl/macless-haystack)** (the no-Mac
successor of OpenHaystack):

1. Deploy macless-haystack (Docker): it runs an **anisette** server + a fetch
   endpoint, plus an app (web / desktop / Android) to show tags on a map.
2. **Log in with an Apple ID** — this is the part tied to *your account*. The tool
   uses it to query Apple's Find My network endpoint. **Use a secondary Apple ID**
   (with 2FA) rather than your main one: the script polls Apple's servers and some
   people prefer to keep that off their primary account.
3. **Import `my-tag.keys`** into the app (the tag you generated in Step 1).
4. The app fetches reports for your tag's *hashed* key, decrypts them with your
   *private* key, and shows the location + history on a map.

Reports appear only after **other** Apple devices have passed near the beacon (it
can take from minutes to hours depending on foot traffic). The beacon must be
**powered** to be found — for theft-tracking a small battery is needed so it keeps
broadcasting when the scooter is off.

---

## How it works (short)

| Layer | What happens |
|------|--------------|
| ESP32 | Broadcasts the 28-byte public key in a BLE advertisement shaped like a lost AirTag (key split between the BLE random address and the payload). |
| Nearby iPhones | Encrypt their own GPS with your public key (ECDH/ECIES) and upload it to Apple, indexed by `SHA256(public key)`. |
| You | Query Apple with that hash (via macless-haystack + your Apple ID), decrypt with the private key. |

Crypto: elliptic curve **P-224 (secp224r1)**. Tunables in `src/main.cpp`:
advertising interval (`findmy_adv_params`), Classic name (`DEVICE_NAME`).

## Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Dual-mode firmware: Classic HID trigger + BLE Find My beacon |
| `tools/generate_keys.py` | Generates the P-224 key pair (public for firmware, private for retrieval) |
| `sdkconfig.defaults` | Enables dual mode (BTDM) + Classic HID Device + BLE |
| `partitions.csv` | Larger app partition (the dual-mode stack needs > 1 MB) |

## Credits

Find My offline-finding protocol reverse-engineered by
[OpenHaystack / SEEMOO Lab](https://github.com/seemoo-lab/openhaystack);
Mac-less retrieval by [macless-haystack](https://github.com/dchristl/macless-haystack).



Easy script to convert ASCII to HEX to replace in a  Find my advertisement key function given by https://dchristl.github.io/macless-haystack/ site:

import base64
k = base64.b64decode("INCOLLA_QUI_LA_ADVERTISEMENT_KEY")
assert len(k) == 28, f"Errore: {len(k)} byte invece di 28 (forse hai copiato la chiave sbagliata)"
print("static uint8_t findmy_public_key[28] = {")
print("    " + ",\n    ".join(", ".join(f"0x{b:02x}" for b in k[i:i+8]) for i in range(0,28,8)))
print("};")
