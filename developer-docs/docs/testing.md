# Testing

Unit and integration tests use Unity on the native `test` PlatformIO environment with ArduinoFake.

```bash
pio test -e test
```

Integration tests live under `test/test_integration/` (state machine, MQTT dispatch, BLE, HTTP, breaks, hardware mocks).

See [`test/README.md`](https://github.com/researchanddesire/Lockbox/blob/main/test/README.md) in the repository root.
