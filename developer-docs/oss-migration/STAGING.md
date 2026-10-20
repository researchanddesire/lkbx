# OSS staging — Lockbox-OSS

Canonical **`main`** on `researchanddesire/Lockbox` is never frozen. Daily firmware development continues there unchanged.

OSS structural prep lives on **`opensource-prep`** in this repo and on [`Lockbox-OSS`](https://github.com/researchanddesire/Lockbox-OSS) until cutover.

## Remote setup

OSS prep is pushed to **`Lockbox-OSS`** (write), and rebased on canonical **`Lockbox`** (read-only). Configure two remotes so you never push prep work at canonical `main`:

```bash
git remote set-url origin https://github.com/researchanddesire/Lockbox-OSS.git
git remote add upstream https://github.com/researchanddesire/Lockbox.git   # if not already set
```

| Remote | Repo | Use |
| --- | --- | --- |
| `origin` | `Lockbox-OSS` | push/pull `opensource-prep` |
| `upstream` | `Lockbox` | fetch/rebase canonical `main` (do not push) |

Pushing `opensource-prep` at canonical `Lockbox` is denied (403) — that's expected.

## Weekly rebase (minimum)

```bash
git fetch upstream main
git checkout opensource-prep
git rebase upstream/main
# Resolve OSS-only conflicts: LICENSE, ota/, developer-docs/, .github/pages.yml
git push origin opensource-prep
```

## What belongs on the OSS branch only

- `docs/` → `ota/` rename and OTA-only Pages workflow
- Removal of `Documentation/` (Mintlify source)
- `git filter-repo` before going public
- LICENSE path map and secrets externalization

## Safe to merge to canonical main early (optional)

- `developer-docs/` folder
- `CONTRIBUTING.md`, issue templates
- `repository_dispatch` workflow for dev-docs rebuild

See [CUTOVER.md](CUTOVER.md) for the coordinated switch window.
