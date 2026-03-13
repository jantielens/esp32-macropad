# First-Time Setup

After flashing the ESP32 Macropad firmware, follow these steps to get your device up and running.

## Step 1: Connect to the Device's Wi-Fi

When the device boots for the first time (or after a factory reset), it starts in **AP mode** — broadcasting its own Wi-Fi network.

1. On your phone or computer, open the Wi-Fi settings
2. Look for a network named **`esp32-macropad-XXXXXX`** (where `XXXXXX` is a unique chip identifier)
3. Connect to it (no password required)
4. A **captive portal** should open automatically — if it doesn't, open a browser and navigate to `http://192.168.4.1`

## Step 2: Configure Wi-Fi

You'll land on the **Network** configuration page:

1. Enter your **Wi-Fi SSID** (network name)
2. Enter your **Wi-Fi password** (leave empty for open networks)
3. Give your device a **name** (e.g., "Living Room Pad") — this will also be used for mDNS discovery
4. *(Optional)* Configure a static IP address under **Network Configuration**
5. *(Optional)* Set up **MQTT** if you plan to use Home Assistant integration
6. Click **Save and Reboot**

The device will restart and connect to your Wi-Fi network.

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
   - **Background images** — load images from a URL (JPEG/PNG)
4. Click **Save Pad** when you're done

You can create up to **8 pads** and switch between them on the device or via Home Assistant.

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
- A screen select entity to switch pad pages remotely
- Button press events

For more details, see the [Home Assistant + MQTT guide](dev/home-assistant-mqtt.md).

## Troubleshooting

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
