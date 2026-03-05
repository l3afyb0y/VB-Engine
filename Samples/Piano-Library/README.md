# Piano Library Assets

This folder is used by the sampled piano engine.

## Quick start

1. Download a redistributable SFZ piano library:
```bash
bash scripts/fetch-piano-assets.sh salamander
```
Or fetch both supported libraries in one shot:
```bash
bash scripts/fetch-piano-assets.sh all
```

2. Render showcase samples with that library:
```bash
bash scripts/generate-samples.sh
```

By default, the engine loads `Samples/Piano-Library/default.sfz` when present.
You can override via:
```bash
VB_PIANO_SFZ_PATH=/absolute/path/to/library.sfz bash scripts/generate-samples.sh
```

## Included download options

- `salamander`: Salamander Grand Piano v3 (CC BY 3.0)
- `upright-kw`: Upright Piano KW (CC0)

## Notes

- Libraries are not checked into git due size.
- `scripts/fetch-piano-assets.sh` is idempotent: it skips re-download when the library is already present (use `--force` to refresh).
- `default.sfz` is expected to point to a valid SFZ with relative sample paths.
- Current engine loader supports WAV/AIFF/FLAC sample files referenced from SFZ.
- Mechanical hammer/pedal noise layers are intentionally not added by VB-Engine in this design pass.
