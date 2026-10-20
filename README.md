# Chastity Lockbox

Open-source ESP32-S3 smart lock firmware (MPL 2.0) and hardware designs (CERN-OHL-S v2).

| Resource | Location |
| -------- | -------- |
| User guides | [docs.researchanddesire.com/lockbox](https://docs.researchanddesire.com/lockbox/) |
| Developer docs | [dev.researchanddesire.com/lockbox](https://dev.researchanddesire.com/lockbox/) |
| Assembly docs | [assembly-docs/](assembly-docs/) |
| PCB hardware | [hardware/pcb/](hardware/pcb/) |
| OTA firmware | GitHub Pages (`ota/`) |

## Build

```bash
cp platformio_private.ini.example platformio_private.ini
pio run -e development
```

See [CONTRIBUTING.md](CONTRIBUTING.md) and [developer-docs/](developer-docs/).

## OSS staging

Daily development targets canonical **`main`**. OSS structural prep lives on **`opensource-prep`** / [`Lockbox-OSS`](https://github.com/researchanddesire/Lockbox-OSS) until cutover — see [developer-docs/oss-migration/STAGING.md](developer-docs/oss-migration/STAGING.md).

## License

See [LICENSE](LICENSE).
