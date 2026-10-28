# Testing

Unit and integration tests use Unity on the native `test` PlatformIO environment with ArduinoFake. Run them from `software/firmware/`.

```bash
pio test -e test
```

Integration tests live under `software/firmware/test/test_integration/` (state machine, MQTT dispatch, BLE, HTTP, breaks, hardware mocks).

See [`software/firmware/test/README.md`](https://github.com/researchanddesire/Open-Source-Chastity-Lockbox_LKBX/blob/main/software/firmware/test/README.md).
