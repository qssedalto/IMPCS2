# Test plan

## A. Controller-only validation

Run `IMPCS2.exe --dry-run --controller-index 0` with joycon2cpp already running.

- Confirm a virtual Xbox/XInput controller is visible to Windows.
- Press each face button and confirm the button bitmask changes.
- Move the left stick and confirm LX/LY values change and return near zero.
- Test disconnect/reconnect and verify the app resumes polling.

If no XInput state appears, verify that the joycon2cpp profile creates an Xbox 360 target rather than only a DualShock 4 target.

## B. Bridge startup

Run with a valid iPad UDID and the full iUsbBridge path.

- iPad is unlocked and trusted.
- Developer Mode is enabled.
- Wait for the bridge `ready` event.
- If startup fails, record the complete bridge stdout/stderr message before changing settings.

## C. Single-point test

Before testing the game, use a temporary profile where A points to a safe, obvious iPad UI target. Press A and confirm a single down/up cycle. Do not test with the uncalibrated example coordinates on an important screen.

## D. Stick test

- Calibrate virtual-stick center while the game is open.
- Move in four cardinal directions and diagonals.
- Release the stick and verify the touch is released.
- Confirm a held button plus stick does not exceed five active contacts.

## E. Recovery

- Release all buttons before stopping.
- Stop with Ctrl+C.
- Disconnect the controller while a touch is active and verify all contacts are released.
- Disconnect USB or terminate the bridge and confirm IMPCS2 stops sending input.

## Acceptance boundary

Compilation and dry-run do not prove iPad compatibility. Final acceptance requires a real iPad, matching iPadOS/DDI support in iUsbBridge, the user's virtual XInput controller, and in-game coordinate calibration.
