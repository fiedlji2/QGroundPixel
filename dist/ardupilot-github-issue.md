# Title: 4.7 MAVn_OPTIONS "Don't forward" silently suppresses all STATUSTEXT on the link — no indication to the user

## Bug report

**Issue details**

After upgrading a vehicle from Copter 4.6.3 to 4.7.0, the GCS connected over the RC link (ExpressLRS MAVLink, RadioMaster AX12) stopped receiving **all STATUSTEXT messages** — no PreArm failures, no arming messages, no calibration instructions. Everything else on the link kept working normally: telemetry streams, full parameter download, commands, missions. The same vehicle's other telemetry link (Rpanion/LTE on a different serial port) still received STATUSTEXT fine, which made this very hard to diagnose — it looks like a GCS or radio bug, not a vehicle configuration issue.

Root cause (verified in source): in 4.7 the `SERIALn_OPTIONS` bit 10 ("Don't forward MAVLink to/from") was migrated to the new per-channel `MAVn_OPTIONS` bit 1 (`NO_FORWARD`). `GCS_MAVLINK::init()` calls `set_channel_private()` for a channel with that option, and `GCS::statustext_send_channel_mask()` masks private channels out (`ret &= ~GCS_MAVLINK::private_channel_mask()`), so the channel receives **no statustexts at all** while every other protocol on it works. Clearing bit 1 of the relevant `MAVn_OPTIONS` immediately restored statustexts.

Problems this raises:

1. **Silent failure with no feedback.** A single option bit disables all pilot-facing messages on a link with zero indication on either side. A pre-arm warning ("statustext disabled on active GCS link MAVn") or at least a parameter-doc warning would prevent long debugging sessions. (4.7 already added a pre-arm check for the *old* leftover `SERIALn_OPTIONS` bit — the new bit deserves the same care.)
2. **The old-bit → new-parameter conversion changes addressing.** The old bit was indexed by *serial port*, the new one by *MAVLink channel instance* (MAV0 = first MAVLink channel/USB, MAV1 = first MAVLink UART, ...). On vehicles with several MAVLink ports it is easy for the converted bit to land on (or be later attributed to) a different physical link than intended. Worth auditing `options_were_converted` handling for multi-port configs.
3. **Doc discoverability.** The `MAVn_OPTIONS` bit 1 description ("don't forward MAVLink data to or from this device") does not mention that it also suppresses STATUSTEXT/broadcasts to that device. Suggest amending the parameter description to state this explicitly.

**Steps to reproduce**

1. Vehicle with two MAVLink serial links (e.g. RC-link MAVLink + companion/telemetry radio), Copter 4.6.3, statustexts visible on both.
2. Upgrade to 4.7.0 (in-place, params carried over).
3. Set (or have migrated) `MAVn_OPTIONS` bit 1 = 1 on one link.
4. On that link: STATUSTEXT never arrives (confirm via MAVLink Inspector); telemetry and param download work normally. Other links unaffected.

**Version**: Copter 4.7.0 (working baseline: 4.6.3)
**Platform**: Copter
**Airframe type**: quad
**Hardware**: [FILL IN — flight controller board]
**GCS**: QGroundControl 5.x (Android, RadioMaster AX12 over ExpressLRS MAVLink); also reproduced with second QGC build; Rpanion+LTE link on same vehicle unaffected
**Logs**: parameter file before/after available on request
