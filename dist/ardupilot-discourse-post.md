# Title: No PreArm / STATUSTEXT messages in GCS after 4.6 → 4.7 upgrade? Check MAVn_OPTIONS "Don't forward"

Posting this because it cost me days of debugging and the symptom is very misleading.

**Symptom:** after upgrading from Copter 4.6.3 to 4.7.0, my GCS (QGroundControl on a RadioMaster AX12, connected over ExpressLRS MAVLink) stopped showing *any* vehicle messages — no PreArm errors, no "Arming motors", no calibration prompts, nothing. The messages icon in QGC never appears. Meanwhile **everything else works**: full telemetry, parameter download, missions, commands — so it looks like a GCS or radio problem, not an ArduPilot one. A second GCS connected via Rpanion/LTE on another serial port *did* receive messages, which confused things further.

**Cause:** in 4.7 the old `SERIALn_OPTIONS` bit 10 ("Don't forward MAVLink to/from") moved to the new per-channel parameter `MAVn_OPTIONS`, bit 1 ("Don't forward MAVLink data to or from this device"). What the description doesn't say: a channel with this bit set is treated as a **private channel**, and ArduPilot excludes private channels from **all STATUSTEXT broadcasts**. Streams and request/response protocols still work, so the link looks perfectly healthy — only the human-readable messages vanish.

Note the numbering when you look for it: `MAV0` = first MAVLink channel (usually USB), `MAV1` = first serial port with `SERIALx_PROTOCOL` 1/2, `MAV2` = the next one, etc. — it is **not** the SERIAL port number.

**Fix:** find the `MAVn_OPTIONS` for the affected link, clear bit 1 (in my case set it to 0), reboot. Statustexts return immediately (easy to verify: start an accel calibration — the instruction texts are statustexts; or unplug GPS to trigger PreArm).

Hopefully this saves someone the same hunt. It would be nice if a future release warned when an active GCS link has statustext suppressed — I've filed a GitHub issue suggesting that.
