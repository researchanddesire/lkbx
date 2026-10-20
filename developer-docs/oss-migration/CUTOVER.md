# Cutover checklist — Lockbox

Run during the coordinated RAD open-source cutover window. See the unified plan in the org `opensource-prep` documentation.

## Pre-cutover (OSS team)

- [ ] Final rebase `opensource-prep` ← upstream `main` on Lockbox-OSS
- [ ] Run `git filter-repo` per [FILTER_REPO.md](FILTER_REPO.md)
- [ ] Validate OTA at `https://researchanddesire.github.io/Lockbox-OSS/`
- [ ] Validate dev docs at `https://dev.researchanddesire.com/lockbox/`
- [ ] Validate user docs at `https://docs.researchanddesire.com/lockbox/`

## Cutover window

1. Rename `Lockbox` → `Lockbox-internal` (private)
2. Rename `Lockbox-OSS` → `Lockbox` (public)
3. Enable GitHub Pages on public `Lockbox` (OTA from `ota/`)
4. Enable simple-docs and dev-docs auto-deploy
5. Remove `dispatch_docs.yml` from public Lockbox (Mintlify retired)
6. Ship firmware release with updated doc URL constants
7. Rotate MQTT/JWT secrets
8. Archive `Lockbox-internal` when no longer needed

## Team remotes after cutover

```bash
git remote set-url origin https://github.com/researchanddesire/Lockbox.git
cp platformio_private.ini.example platformio_private.ini
```
