# First-Time Setup

After flashing the ESP32 Macropad firmware, follow these steps to get your device up and running.

## Step 1: Connect to the Device's Wi-Fi

When the device boots for the first time (or after a factory reset), it starts in **AP mode** — broadcasting its own Wi-Fi network.

1. On your phone or computer, open the Wi-Fi settings
2. Look for a network named **`esp32-macropad-XXXXXX`** (where `XXXXXX` is a unique chip identifier)
3. Connect to it (no password required)
4. A **captive portal** should open automatically — if it doesn't, open a browser and navigate to `http://192.168.4.1`

## Step 2: Configure Wi-Fi

On first boot the portal lands on a one-page **setup wizard** that collects only what's needed to get the device on your network:

1. Enter your **Wi-Fi SSID** (network name) and **password** (leave empty for open networks)
2. *(Optional)* Give your device a **friendly name** — used as the mDNS hostname so you can reach it at `http://<name>.local`
3. *(Optional)* Tick **Require login for the portal** to set a basic-auth username and password
4. *(Optional)* Tick **Use a static IP address** to configure IP, subnet, gateway, and DNS
5. Click **Save & Connect**

The device reboots and connects to your Wi-Fi. MQTT, Home Assistant integration, pads, and other settings are configured later from the full portal (Step 3).

## Step 3: Access the Web Portal

Once connected to Wi-Fi, you can access the full configuration portal:

- **Via mDNS**: `http://<device-name>.local` (e.g., `http://living-room-pad.local`)
- **Via IP address**: Check your router's DHCP client list for the device's IP

> **Tip**: The device's IP address and mDNS name are also shown on the device's info screen (if your board has a display).

The portal has four pages:

| Page | What it does |
|------|--------------|
| **Home** | Welcome overview, operating mode, display settings, and sensors |
| **Pads** | Design and configure your pad layouts and buttons |
| **Network** | Manage Wi-Fi, MQTT, device name, static IP, and security settings |
| **Firmware** | Update firmware over Wi-Fi (OTA) or via file upload, and factory reset |

## Step 4: Set Up Your Pads

If your device has a display, head to the **Pads** page to configure your pad layout:

1. Choose the number of **columns** and **rows** for your grid
2. Click any button to open the **button editor**
3. Configure each button with:
   - **Labels** (top, center, bottom) — supports live data via [MQTT bindings](#mqtt-bindings)
   - **Icons** — choose from emoji or Material Symbols
   - **Colors** — background, text, border
   - **Tap/long-press actions** — navigate screens or publish MQTT messages
   - **Background images and camera feeds** — load images or MJPEG streams from a URL (JPEG, PNG, or MJPEG)
4. Click **Save Pad** when you're done

You can create up to **16 pads** and switch between them on the device or via Home Assistant.

### MQTT Bindings

Labels can display live data using binding templates:

- **MQTT data**: `[mqtt:topic;json_path;format]` — e.g., `[mqtt:home/energy/solar;power;%.0f W]`
- **Device health**: `[health:key;format]` — e.g., `[health:cpu;%.0f%%]`
- **Date & time**: `[time:format;timezone]` — e.g., `[time:%H:%M;Europe/Amsterdam]`

A full binding reference is available in the pad editor's built-in help dialog.

## Step 5 (Optional): Connect to Home Assistant

If you have an MQTT broker and Home Assistant:

1. Go to the **Network** page and fill in your **MQTT Host**, **Port**, and credentials
2. On the **Home** page, configure the **Operating Mode** and **Transport Mode** settings
3. Save and reboot

The device will automatically register itself with Home Assistant via **MQTT Discovery** — no manual YAML needed. You'll see:

- Device health sensors (CPU, memory, temperature, Wi-Fi signal)
- A screen select entity to switch pads remotely
- Button press events

For more details, see the [Home Assistant + MQTT guide](dev/home-assistant-mqtt.md).

## Troubleshooting

### M5Stack StopWatch

The `m5stack-stopwatch` target provides the ordinary Macropad workflow on the
round AMOLED display. Implementation is based on
[M5Stack's documentation](https://docs.m5stack.com/en/core/StopWatch),
[factory reference](https://github.com/m5stack/M5StopWatch-UserDemo), and M5GFX
bring-up code, not physical validation.

**Record your hardware revision before connecting rear accessories.** On
v1.0, a rear pin marked BAT on some stickers is **5V IN**, not a battery pin:
never connect a battery. On v1.0.1, `*BAT` is a battery connection.

- **First flash/recovery:** connect a data-capable Type-C cable to a computer.
  Hold the power button for about two seconds until the green LED lights,
  then release to enter download mode (vendor procedure). Use a full USB flash
  so the firmware and partition table are installed together. Verify this
  procedure on your unit before relying on OTA.
- **Configuration recovery:** hold the **yellow** programmable button while
  powering on/resetting, and keep it held through the startup configuration-mode
  check. This enters configuration mode without erasing saved settings.
- **Normal power button:** the vendor documents a short press for power-on/reset
  and two quick presses for power-off. Firmware does not remap this button.
- **Programmable buttons:** configure yellow and blue tap/hold actions through
  the existing hardware-button controls in the portal.
- **Pad layout:** the grid uses a centered inscribed square so corner buttons
  remain visible. Pad insets and pixel-shift settings apply inside that area.
- **Brightness:** existing brightness and screen-saver controls send AMOLED
  commands; this device has no backlight GPIO. Start at low brightness and avoid
  leaving static content illuminated for long periods.
- **Storage and updates:** settings and pads use internal flash. The target uses
  two 4 MiB OTA application slots; after the first full flash, use the portal's
  Firmware controls for updates.

Before considering a unit ready, manually verify:

1. Cold boot on USB and battery; serial logs report 16 MiB flash and 8 MiB PSRAM.
2. Download-mode recovery and yellow-button configuration recovery.
3. Correct colors, framing, partial updates, and all four rotations. Check pads,
   splash/info screens, notifications, and buttons against the visible circle.
   Vendor examples disagree on 466/468-pixel geometry; do not infer success from
   a passing compilation.
4. Touch at the center and circle edges, valid release, idle recovery, and
   screen-saver sleep/wake without stuck presses; verify both hardware buttons.
5. Portal Wi-Fi setup, representative actions, and settings/pad persistence
   across reboot; successful OTA and boot into the updated firmware.
6. Internal free RAM, largest internal/DMA-capable block, and PSRAM headroom
   under simultaneous display, Wi-Fi, portal, and OTA traffic. Keep BLE, MCP,
   remote image fetching, and native Extensions disabled until measured.
7. For milestone 2, validate PMIC battery voltage and charging state against
   actual USB/battery conditions. Audio remains disabled by default: enable
   `HAS_AUDIO` in the board overrides only for a locally built test image, then
   verify speaker playback, silent amplifier startup/mute, and memory headroom
   with concurrent networking. See [audio architecture](dev/audio-architecture.md).

Microphone capture, IMU, hardware RTC, haptics, and advanced low-power modes are
deferred, not advertised firmware capabilities. No hardware revision, memory
measurement, or hardware acceptance result is recorded by this change.

### Can't find the device's Wi-Fi network
- Make sure the device is powered on and has finished booting (the display may show a splash screen)
- If the device was previously configured, it will connect to the saved Wi-Fi instead. To reset, use the **Factory Reset** option from the firmware page, or hold the boot button during startup to enter config mode

### Can't access http://device-name.local
- mDNS works on macOS, iOS, Android, and Linux (with avahi). On Windows, you may need Bonjour installed
- Try accessing the device by IP address instead (check your router)

### Device won't connect to Wi-Fi
- Double-check the SSID and password
- Make sure your network is 2.4 GHz (ESP32 does not support 5 GHz)
- Move the device closer to your router for initial setup

### Captive portal doesn't open automatically
- Open a browser manually and go to `http://192.168.4.1`
