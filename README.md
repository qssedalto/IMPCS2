# IMPCS2

IMPCS2 is a Windows input adapter prototype for controlling an iPhone/iPad over USB with a Nintendo Switch 2 Pro Controller.

It does not install an iPadOS driver and does not make iPadOS enumerate the Switch 2 controller as a native gamepad. Instead, it reads a virtual XInput controller created by a separate controller adapter (for example, a compatible joycon2cpp build), maps buttons/sticks to touch coordinates, and sends touch events through the separately obtained iUsbBridge executable.

## Current milestone

- Reads a virtual Xbox/XInput controller through Windows XInput.
- Starts iUsbBridge in USB mode and waits for its \`ready\` event.
- Sends framed JSON touch events using the documented \`iphoneMirror.touch.v2\` IPC.
- Maps the left stick to a continuous virtual joystick touch.
- Maps face buttons, shoulders, triggers, Start/Back, and stick clicks to configurable screen points.
- Limits concurrent contacts to iUsbBridge's five-touch capacity and releases active touches on shutdown or controller disconnect.
- Includes a dry-run mode to verify XInput before connecting the iPad.

This is an early CLI prototype. The sample Cult of the Lamb coordinates are placeholders and have not been calibrated on the user's iPad/game. A GUI and calibration wizard are future work.

## Requirements

- Windows 10/11 x64.
- Visual Studio 2022 or newer with Desktop development with C++ and a Windows SDK.
- CMake 3.24 or newer.
- A working Switch 2 Pro Controller setup that exposes a virtual Xbox/XInput controller to Windows. The user's custom joycon2cpp XInput build is the intended input source.
- A separately obtained iUsbBridge.exe from the official upstream project. IMPCS2 does not bundle or modify iUsbBridge.
- iPhone/iPad running iOS/iPadOS 18 or later, unlocked and trusted by this computer, with Developer Mode enabled. First use may need a matching personalized Developer Disk Image and network access.

## Build

Run these as a single line in an x64 Native Tools command prompt for Visual Studio:

    cmake -S . -B build -A x64 && cmake --build build --config Release

The executable is written to build/Release/IMPCS2.exe. CMake copies the example profile beside it under config/.

## First: verify the controller only

Run:

    .\build\Release\IMPCS2.exe --dry-run --controller-index 0

Press buttons and move the sticks. The console should show changing XInput state. If no controller is detected, verify that joycon2cpp is running and that its selected output is Xbox 360/XInput, not only a virtual DualShock 4 device.

## Connect to the iPad

Get the iPad UDID from your device-management tool or iPhoneMirror device details. Then run:

    .\build\Release\IMPCS2.exe --udid YOUR_DEVICE_UDID --bridge "C:\Program Files\iPhoneMirror\iUsbBridge.exe" --profile "config\cult-of-the-lamb.example.ini"

If iUsbBridge.exe is in a different location, change the --bridge path. The program also checks common iPhoneMirror install locations when --bridge is omitted.

iUsbBridge performs its own USB device, Developer Mode, DDI, and Universal HID checks. Wait until it reports ready before controller input is sent.

## Coordinate profile

The profile uses normalized screen coordinates from 0.0 to 1.0:

- x = 0 is the left edge; x = 1 is the right edge.
- y = 0 is the top edge; y = 1 is the bottom edge.
- Coordinates follow the current iPad screen orientation.
- The left-stick center is where the virtual joystick should be touched; radius is the maximum movement offset from that center.
- Button coordinates are touch targets, not physical controller coordinates.

Copy the example profile to a new file and pass it with --profile. Calibrate coordinates while the game is open on the iPad. The included values are illustrative only; they may not match your in-game control layout, orientation, or UI scaling.

## Safety and recovery

- Press Ctrl+C to stop. IMPCS2 sends touch-up events for active contacts before closing the bridge.
- If the controller disconnects, active touches are released.
- iUsbBridge supports at most five simultaneous touch contacts. Additional button presses are deferred until a contact is available.
- Start with dry-run, then test one mapped button before attempting movement plus multiple buttons.
- Do not run multiple programs that simultaneously control the same iPad over USB.

## Architecture

Switch 2 Pro Controller → existing joycon2cpp adapter → virtual XInput controller → IMPCS2 mapping engine → iUsbBridge IPC → iPadOS Universal HID touch service.

IMPCS2 intentionally does not copy the iPhoneMirror repository or bundle third-party executables. See docs/ARCHITECTURE.md and THIRD_PARTY_NOTICES.md.

## Known limitations

- CLI only; no graphical profile editor yet.
- XInput output only in this milestone.
- Example coordinates are unverified.
- Touch emulation cannot provide native gamepad semantics, controller vibration, or true analog triggers. Triggers are treated as configurable digital touch buttons.
- Some games may ignore simulated touch input in certain screens or may change their touch layout by orientation.
- The project has not yet been tested on the user's physical iPad from this repository; the Windows CI build only validates compilation.
