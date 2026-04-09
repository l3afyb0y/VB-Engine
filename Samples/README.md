# Samples

This directory contains Rust-generated showcase renders from the current VB-Engine rewrite.

Regenerate them with:

```bash
bash scripts/generate-samples.sh
```

That script writes the showcase WAVs into `Samples/` by running:

```bash
cargo run --bin render_showcase -- Samples
```
