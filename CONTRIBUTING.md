# Contributing to Lockbox

## Daily development

Branch feature work from **`staging`** and open the pull request back into `staging`. `staging` is promoted to `main` by a same-repository pull request. Only urgent production fixes target `main` directly; their pull request title starts with the word `hotfix`.

## Developer documentation

Edit **`developer-docs/`** in this repo in the same PR as firmware changes when behavior changes. Published site: [dev.researchanddesire.com/lkbx](https://dev.researchanddesire.com/lkbx) (aggregated by the [`dev-docs`](https://github.com/researchanddesire/dev-docs) repo).

Cross-product docs (rad-json, MQTT ecosystem): PR to [`dev-docs`](https://github.com/researchanddesire/dev-docs).

User-facing guides: [`simple-docs`](https://github.com/researchanddesire/simple-docs).

## Build setup

Software lives in `software/`: the PlatformIO project in `software/firmware/`, tooling in `software/scripts/`, and local fixture services in `software/dev/`.

```bash
python software/dev/dev_services.py configure --lan-ip <private-IP>   # local fixture services
cd software/firmware
pio run -e development-r8                                             # or development-r2
```

Development profiles talk only to the local fixture services; their generated credentials stay in the ignored `software/dev/.dev/` directory.

> **Note (for AJ):** As of 2026-06, only `sdkconfig.defaults` is tracked. The
> per-environment `sdkconfig.development` / `.staging` / `.production` files are
> ESP-IDF build artifacts (regenerated from the defaults on every build, and
> toolchain-version dependent), so they're gitignored. Edit `sdkconfig.defaults`
> for intentional config changes; never commit a regenerated `sdkconfig.<env>`.

## Pull requests

- Run `pio test -e test` and `pio check` when touching firmware
- Update `developer-docs/` if you change APIs, states, or integration behavior
- Follow existing code style (`software/.clang-format`)

## Hardware BOMs

`hardware/<product>/bom.csv` follows the [BOM standard](https://dev.researchanddesire.com/meta/bom-standard/) — fixed columns, closed category codes, `–` for empty cells. **Anyone is welcome to edit `bom.csv` directly and push/PR — as long as `bom-lint` passes, it's good.** Only the schema is vendored read-only at `.github/workflows/scripts/bom.schema.json` (do not hand-edit — synced from [`dev-docs`](https://github.com/researchanddesire/dev-docs)).

## Licenses

See [LICENSE](LICENSE). Firmware is **MPL 2.0**; hardware paths are **CERN-OHL-S v2**.
