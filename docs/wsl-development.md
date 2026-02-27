# WSL Development Guide

This guide explains how to set up serial port access for ESP32 development when using WSL (Windows Subsystem for Linux) on Windows.

## Overview

When developing ESP32 applications in WSL, you need to connect Windows COM ports to WSL so the scripts can communicate with your ESP32 device. This requires using `usbipd-win` to share USB devices between Windows and WSL.

## Prerequisites

- Windows 10/11 with WSL2 installed
- ESP32 device connected via USB
- Administrator access on Windows

## Installation Steps

### 1. Install usbipd-win on Windows

Download and install usbipd-win from the official repository:

```powershell
# Open PowerShell as Administrator and run:
winget install --interactive --exact dorssel.usbipd-win
```

Or download the installer from: https://github.com/dorssel/usbipd-win/releases

### 2. Install USB/IP tools in WSL

Open your WSL terminal and install the required Linux tools:

```bash
sudo apt update
sudo apt install linux-tools-generic hwdata
sudo update-alternatives --install /usr/local/bin/usbip usbip /usr/lib/linux-tools/*-generic/usbip 20
```

## Connecting ESP32 to WSL

### Step 1: List USB Devices (Windows)

Open PowerShell as Administrator and list connected USB devices:

```powershell
usbipd list
```

Look for your ESP32 device. It typically shows as:
- `CP210x UART Bridge` (Silicon Labs)
- `CH340` (USB-SERIAL)
- `FT232R USB UART` (FTDI)

Note the `BUSID` (e.g., `1-4`).

### Step 2: Bind the Device (Windows - One Time)

Bind the device to make it shareable (only needed once per device):

```powershell
usbipd bind --busid <BUSID>
```

Example:
```powershell
usbipd bind --busid 1-4
```

### Step 3: Attach to WSL

Attach the device to your WSL instance:

```powershell
usbipd attach --wsl --busid <BUSID>
```

Example:
```powershell
usbipd attach --wsl --busid 1-4
```

### Step 4: Verify in WSL

In your WSL terminal, verify the device is connected:

```bash
ls -l /dev/ttyUSB*
# or
ls -l /dev/ttyACM*
```

You should see a device like `/dev/ttyUSB0`, `/dev/ttyUSB1`, `/dev/ttyACM0`, etc.

### Step 5: Set Permissions (if needed)

If you get permission errors, add your user to the `dialout` group:

```bash
sudo usermod -a -G dialout $USER
```

Then log out and log back in for the change to take effect.

## Using the Scripts

Once the device is attached, you can use the project scripts:

### Upload Firmware

```bash
# Single-board projects (port as first arg)
./upload.sh /dev/ttyUSB0

# Multi-board projects (this template’s default): specify board name
./upload.sh cyd-v2 /dev/ttyACM0

# Or just let the scripts auto-detect the port
./upload.sh cyd-v2
```

If your device is on a different port:

```bash
./upload.sh cyd-v2 /dev/ttyACM1
```

### Monitor Serial Output

```bash
./monitor.sh /dev/ttyUSB0
```

With custom baud rate:

```bash
./monitor.sh /dev/ttyUSB0 9600
```

## Troubleshooting

### Device Not Found

If the device doesn't appear in `/dev/`:

1. Check if it's attached: `usbipd list` (in Windows PowerShell)
2. Re-attach: `usbipd detach --busid <BUSID>` then `usbipd attach --wsl --busid <BUSID>`
3. Check WSL kernel has USB support: `uname -r` (should be 5.10 or higher)

### Permission Denied

```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Or use sudo temporarily
sudo ./upload.sh /dev/ttyUSB0
```

### Device Resets When Attaching

This is normal. The ESP32 will reset when attached to WSL. Simply run your upload script after attachment.

### Multiple WSL Instances

If you have multiple WSL distributions, specify which one:

```powershell
usbipd attach --wsl --busid <BUSID> --distribution Ubuntu
```

List your distributions:
```powershell
wsl --list
```

## Quick Reference

### Windows (PowerShell as Administrator)

```powershell
# List devices
usbipd list

# Bind device (one time)
usbipd bind --busid 1-4

# Attach to WSL
usbipd attach --wsl --busid 1-4

# Detach from WSL
usbipd detach --busid 1-4

# Unbind device
usbipd unbind --busid 1-4
```

### WSL (Linux)

```bash
# Check connected devices
ls -l /dev/ttyUSB* /dev/ttyACM*

# Check USB devices
lsusb

# Upload firmware
# Single-board projects
./upload.sh /dev/ttyUSB0

# Multi-board projects
./upload.sh cyd-v2

# Monitor serial output
./monitor.sh /dev/ttyUSB0 115200
```

## Automation Tips

### Auto-attach on WSL Startup

Create a Windows Task Scheduler task to automatically attach the device when WSL starts, or create a PowerShell script:

```powershell
# attach-esp32.ps1
$busid = "1-4"  # Change to your device's BUSID
usbipd attach --wsl --busid $busid
```

### WSL Alias

Add to your `~/.bashrc` or `~/.zshrc`:

```bash
alias esp-upload='./upload.sh /dev/ttyUSB0'
alias esp-monitor='./monitor.sh /dev/ttyUSB0'
alias esp-build='./build.sh'
```

## Additional Resources

- [usbipd-win GitHub](https://github.com/dorssel/usbipd-win)
- [WSL USB Support Documentation](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)
- [ESP32 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
