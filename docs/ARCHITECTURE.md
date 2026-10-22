# Architecture

## Data flow

1. The user's existing joycon2cpp-1.3.8 instance connects to the Switch 2 Pro Controller.
2. That process exposes a virtual Xbox 360 controller through ViGEmBus.
3. IMPCS2 polls the virtual controller using XInput.
4. The mapping engine converts stick/button states to normalized screen coordinates.
5. IMPCS2 starts the separately installed iUsbBridge.exe --usb --udid <UDID> --rate-hz 120 process.
6. IMPCS2 waits for a ready event and sends iphoneMirror.touch.v2 messages as a 4-byte little-endian JSON length followed by UTF-8 JSON bytes.
7. iUsbBridge uses Apple's USBMux/CoreDevice/Universal HID path to send touch events to iPadOS.

## Touch model

- Pointer ID 0 is reserved for the virtual left stick.
- Other pointer IDs represent button contacts.
- The bridge supports up to five concurrent contacts; IMPCS2 prioritizes the virtual stick and the first available button contacts.
- A contact is emitted as down, repeated move while it changes position, and up when released.
- Shutdown and XInput disconnect must release every active pointer.
- The stick contact is created only while the stick exceeds the configured deadzone. Its initial point is the configured center; subsequent points offset from center according to stick direction and radius.

## Why this is touch emulation

The iPad does not enumerate a Switch 2 Pro Controller as a native gamepad. IMPCS2 sends touch events over the bridge. This means no native controller semantics, haptics, or analog trigger values are exposed to the game. Each game's on-screen controls must be calibrated.

## IPC

Protocol schema: iphoneMirror.touch.v2.

Frame format:
- uint32 little-endian JSON byte length
- UTF-8 JSON body

Touch message example:

    {"schema":"iphoneMirror.touch.v2","kind":"touch_batch","seq":1,"timestampNs":0,"points":[{"pointerId":0,"action":"down","normalizedX":0.16,"normalizedY":0.78}]}

Wait for the bridge's ready event before sending input. Read stdout and stderr continuously. The bridge's standard output is JSON Lines; diagnostic text belongs on stderr.

## Initial scope

The first milestone supports one XInput device, configurable normalized coordinates, and a console UI. It does not copy iPhoneMirror or iUsbBridge source. GUI profile editing, hotplug polish, and per-game calibration UI are later milestones.
