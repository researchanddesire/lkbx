# Contributing to Lockbox

## Daily development

Normal firmware work merges to **`main`**. OSS structural prep happens on **`opensource-prep`** (or `Lockbox-OSS` when the org fork is available).

## Developer documentation

Edit **`developer-docs/`** in this repo in the same PR as firmware changes when behavior changes. Published site: [dev.researchanddesire.com/lockbox](https://dev.researchanddesire.com/lockbox/) (aggregated by the [`dev-docs`](https://github.com/researchanddesire/dev-docs) repo).

Cross-product docs (rad-json, MQTT ecosystem): PR to [`dev-docs`](https://github.com/researchanddesire/dev-docs).

User-facing guides: [`simple-docs`](https://github.com/researchanddesire/simple-docs).

## Build setup

```bash
cp platformio_private.ini.example platformio_private.ini
# Set RAD_SERVER, MQTT_SERVER for your environment
pio run -e development
```

Never commit `platformio_private.ini`.

> **Note (for AJ):** As of 2026-06, only `sdkconfig.defaults` is tracked. The
> per-environment `sdkconfig.development` / `.staging` / `.production` files are
> ESP-IDF build artifacts (regenerated from the defaults on every build, and
> toolchain-version dependent), so they're gitignored. Edit `sdkconfig.defaults`
> for intentional config changes; never commit a regenerated `sdkconfig.<env>`.

## Pull requests

- Run `pio test -e test` and `pio check` when touching firmware
- Update `developer-docs/` if you change APIs, states, or integration behavior
- Follow existing code style (`.clang-format`)

## Hardware BOMs

`hardware/<product>/bom.csv` follows the [BOM standard](https://dev.researchanddesire.com/meta/bom-standard/) — fixed columns, closed category codes, `–` for empty cells. **Anyone is welcome to edit `bom.csv` directly and push/PR — as long as `bom-lint` passes, it's good.** Only the schema is vendored read-only at `.github/workflows/scripts/bom.schema.json` (do not hand-edit — synced from [`dev-docs`](https://github.com/researchanddesire/dev-docs)).

## Licenses

See [LICENSE](LICENSE). Firmware is **MPL 2.0**; hardware paths are **CERN-OHL-S v2**.
