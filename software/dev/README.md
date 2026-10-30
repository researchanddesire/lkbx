# LKBX development services

Local HTTPS and MQTT services for `development-r2` and `development-r8` builds.
Development firmware talks only to services on this machine, never to cloud dashboards.

- `dev_services.py configure --lan-ip <private-IP>` writes per-machine settings,
  broker credentials, certificates and the firmware header to the ignored
  `.dev/` directory here. `up` and `down` start and stop `compose.yaml`.
- `up` picks one backend for the same generated endpoints and certificate, so
  the firmware never changes between them:
  - `rad-app`: when a local RAD App answers on `:3000` and its EMQX broker on
    `:1883` (`pnpm dev:web` in rad-app), `fixture/rad_app_front.py` terminates
    TLS with the local certificate and forwards to them. Devices pair, sync and
    take lock/unlock commands from that RAD App with its seeded test lockbox
    (`AA:BB:CC:DD:EE:FF`). Firmware checks are answered locally with no update,
    so a local dashboard can never offer a cloud artifact.
  - `fixture`: otherwise, the Mosquitto broker and HTTPS fixture in `fixture/`.
  `--backend` or `LKBX_DEV_BACKEND` (`auto`, `fixture`, `rad-app`) overrides
  the choice; under CI, `auto` always uses `fixture`.
- `fixture/` holds the HTTP fixture, the RAD App front and the Mosquitto
  configuration that `compose.yaml` mounts read-only into each container.
- `../firmware/pre_build_script.py` reconfigures these services for every
  development build, force-includes `.dev/local/include/lkbx_dev_config.h`, and
  runs `up` before an upload or device test.

Tests: `python -m unittest discover -s software/dev -p 'test_*.py'`,
`python software/dev/fixture/test_http_fixture.py`,
`python software/dev/fixture/test_rad_app_front.py`, and, after
`dev_services.py up --backend fixture --lan-ip 127.0.0.1 --device-id AA:BB:CC:DD:EE:FF --device-id AA:BB:CC:DD:EE:FE`,
`python software/dev/fixture/test_compose_integration.py`.
