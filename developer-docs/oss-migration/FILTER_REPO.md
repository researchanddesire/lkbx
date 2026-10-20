# git filter-repo — before making Lockbox public

Run only on a **fresh clone** of `Lockbox-OSS`, never on canonical `main`.

## Install

```bash
pip install git-filter-repo
```

## Audit secrets

Search for hardcoded hosts, tokens, and IPs:

```bash
rg -i 'mqtt|jwt|password|secret|192\.168|emqx|vercel' --glob '!*.example' --glob '!FILTER_REPO.md'
```

## Example: strip historical platformio_private.ini if ever committed

```bash
git filter-repo --path platformio_private.ini --invert-paths
```

## Example: replace removed broker hostname in history

```bash
git filter-repo --replace-text expressions.txt
```

`expressions.txt`:

```
literal:old-broker.example.com==>REDACTED
```

## After filter-repo

Force-push to Lockbox-OSS only, re-validate OTA and CI, then proceed with cutover.
