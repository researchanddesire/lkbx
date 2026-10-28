# LKBX development services

Local HTTPS and MQTT fixtures for `development-r2` and `development-r8` builds.
Development firmware talks only to these services, never to cloud dashboards.

- `dev_services.py configure --lan-ip <private-IP>` writes per-machine settings,
  broker credentials, certificates and the firmware header to the ignored
  `.dev/` directory here. `up` and `down` start and stop `compose.yaml`.
- `fixture/` holds the HTTP fixture and Mosquitto configuration that
  `compose.yaml` mounts read-only into each container.
- `../firmware/pre_build_script.py` reconfigures these services for every
  development build and force-includes `.dev/local/include/lkbx_dev_config.h`.

Tests: `python -m unittest discover -s software/dev -p 'test_*.py'`,
`python software/dev/fixture/test_http_fixture.py`, and, with the services up,
`python software/dev/fixture/test_compose_integration.py`.
