# Piano Realism Iteration 8 - Polyphony Headroom + Loudness Trim (2026-02-16)

## Scope
Address repeated low-register crackle reports during dense note onsets and reduce overall loudness.

## Implemented Changes
- Added polyphony-aware output trim in `VoicePool::render_sample`:
  - applies a gentle active-voice dependent gain reduction,
  - plus fixed master trim for safer headroom.
- Preserved prior low-register stabilization and soft crackle guard improvements.

## Verification
- `bash scripts/run-full-verification.sh`: pass
- Debug/soak: pass
- ASan/UBSan: pass
- TSan: pass
- WAV health: pass (`clip_samples=0` across generated showcase files)

Performance:
- 7-run benchmark median realtime factor: `x18.7649`
- Full pipeline 3-run median realtime factor: `x19.4274`

## Outcome
This pass reduces dense-onset loudness stress and increases practical headroom for 20-note pedal-down usage while retaining realtime performance margins.

## Research Notes
- Underruns/xruns and callback timing pressure are canonical causes of realtime crackles in low-latency systems.
- Relevant references used:
  - PortAudio underflow/overflow guidance: https://www.portaudio.com/docs/proposals/001-UnderflowOverflowHandling.html
  - JUCE callback underrun context: https://docs.juce.com/master/classjuce_1_1AudioIODeviceCallback.html
