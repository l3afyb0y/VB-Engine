# Risks and Tradeoffs

## Latency
- Risk: larger buffers reduce CPU spikes but increase feel latency.
- Mitigation: benchmark across 64/128/256 sample blocks and expose configurable block-size strategy.

## CPU Budget
- Risk: realism features (resonance, pedal interactions) can multiply per-voice computation.
- Mitigation: staged realism rollout, SIMD opportunities, and voice stealing policy tuned by profiling.

## Memory
- Risk: high-polyphony + resonance buffers can increase memory footprint.
- Mitigation: preallocated pools with explicit caps and telemetry for max usage.

## Licensing/Distribution
- Risk: plugin SDK constraints (notably AAX) and sample library licensing.
- Mitigation: keep core engine independent from restricted SDK code and use clearly licensed assets only.

## Cross-Host Compatibility
- Risk: wrapper behavior differences per host/format.
- Mitigation: host matrix, automated smoke tests, and tiered rollout (CLAP/VST3 -> LV2 -> AU/AAX).
