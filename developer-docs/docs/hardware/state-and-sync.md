# State and Sync

Firmware details of non-volatile state persistence, dashboard synchronization, timer storage, and motor calibration. For the user-facing behavior, see [Offline Mode](https://docs.researchanddesire.com/lockbox/technical/offline-mode/), [Timer Duration Limits](https://docs.researchanddesire.com/lockbox/technical/timer-duration-limits/), and [Calibration](https://docs.researchanddesire.com/lockbox/device-states/calibration/).

## NVS persistence

State is preserved across restarts, power cycles, WiFi disconnections, and deep sleep using non-volatile storage (NVS):

| Data | Storage |
|------|---------|
| Lock state (locked/unlocked) | Non-volatile memory |
| Lock end time | Non-volatile memory |
| Session settings | Non-volatile memory |
| WiFi credentials | Non-volatile memory |
| Calibration data | Non-volatile memory |

After a complete power loss the device restores its locked state on boot.

## Dashboard sync (MQTT)

Sync happens over MQTT for real-time responsiveness (subscribe/publish on the device MAC, QoS 2, TLS in production). Payload categories:

### Outgoing (device → dashboard)

- Lock state changes
- Button presses
- Sensor readings
- Emergency unlock usage
- Break timing

### Incoming (dashboard → device)

- Lock commands
- Unlock commands
- Break schedule updates
- Session setting changes
- Time display preferences

## WiFi reconnection

When WiFi drops during operation the device keeps running locally and attempts automatic reconnection every **30 seconds**. On reconnection it syncs state with the dashboard.

## Timer storage and unit conversion

Lock durations are stored internally in **milliseconds**. Unit → millisecond conversion constants:

| Unit | Milliseconds |
|------|--------------|
| 1 second | 1,000 |
| 1 minute | 60,000 |
| 1 hour | 3,600,000 |
| 1 day | 86,400,000 |
| 1 week | 604,800,000 |
| 1 month | 2,629,746,000 (~30.44 days) |
| 1 year | 31,556,952,000 (~365.24 days) |

Month and year use average lengths (`2629746000` ms and `31556952000` ms) to account for varying month lengths and leap years.

### Encoder unit-range mapping

The encoder maps to a time unit based on the current value; crossing a range boundary switches the active unit and resets the arc indicator:

| Unit | Range | Increment |
|------|-------|-----------|
| Seconds | 30–120 s | 1 second |
| Minutes | 1–120 min | 1 minute |
| Hours | 2–24 h | 1 hour |
| Days | 1–30 days | 1 day |
| Weeks | 4–8 weeks | 1 week |
| Months | 2–12 months | 1 month |
| Years | 1–1000 years | 1 year |

The encoder has acceleration enabled: slow turns produce single-increment changes; fast turns jump in larger steps. Default starting value is 10 minutes.

## Motor calibration (firmware)

On first boot (or after a reflash/factory reset that clears NVS), the device auto-detects hardware orientation and persists the result to NVS:

- `isMotorInverted` — whether the motor drive direction must be inverted for this hardware
- `isButtonInverted` — whether the Enter/Back button pins are swapped

Calibration runs a sensor-verified lock/unlock cycle to detect motor inversion:

1. Motor attempts to lock
2. Sensors verify the lock position
3. Motor attempts to unlock
4. Sensors verify the unlock position

The resulting `isMotorInverted` / `isButtonInverted` flags are written to NVS so calibration does not re-run unless NVS is cleared. Sensor verification uses the same TMR/Hall logic described in [Sensor System](/lockbox/hardware/sensor-system/).

## Related developer pages

- [Sensor System](/lockbox/hardware/sensor-system/) — TMR/Hall detection logic used during calibration
- [Power Management](/lockbox/hardware/power-management/) — sleep states and timer-based wake
- [Architecture](/lockbox/architecture/) — MQTT/HTTP/BLE communication and NVS usage
