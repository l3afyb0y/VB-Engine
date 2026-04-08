/// Renders a C4 note with each audio component soloed to separate WAV files.
/// Listen to each file to identify which component produces the "thump".
///
/// Output goes to ~/Videos/component-isolation/ for easy access.
use std::path::PathBuf;

use vb_engine::{Engine, EngineConfig, RenderEvent, render_event_sequence, render_stereo_wav};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let output_dir = dirs();
    std::fs::create_dir_all(&output_dir)?;

    let sample_rate = 48_000u32;
    let total_frames = sample_rate as usize * 3; // 3 seconds
    let note = 60; // C4
    let velocity = 100;

    let events = vec![
        RenderEvent::NoteOn {
            frame: 0,
            note,
            velocity,
        },
        RenderEvent::NoteOff {
            frame: sample_rate as usize, // hold for 1 second
            note,
        },
    ];

    // Each config zeroes out everything except the component being tested.
    // "full-mix" uses default gains for comparison.
    let configs: Vec<(&str, EngineConfig)> = vec![
        ("01-full-mix", EngineConfig::default()),
        (
            "02-strings-only",
            EngineConfig {
                mechanical_gain: 0.0,
                hammer_noise_gain: 0.0,
                resonance_gain: 0.0,
                body_gain: 0.0,
                ambience_gain: 0.0,
                ..EngineConfig::default()
            },
        ),
        (
            "03-resonance-only",
            EngineConfig {
                string_gain: 0.0,
                mechanical_gain: 0.0,
                hammer_noise_gain: 0.0,
                body_gain: 0.0,
                ambience_gain: 0.0,
                ..EngineConfig::default()
            },
        ),
        (
            "04-body-only",
            EngineConfig {
                string_gain: 0.0,
                mechanical_gain: 0.0,
                hammer_noise_gain: 0.0,
                resonance_gain: 0.0,
                ambience_gain: 0.0,
                ..EngineConfig::default()
            },
        ),
        (
            "05-mechanical-only",
            EngineConfig {
                string_gain: 0.0,
                resonance_gain: 0.0,
                body_gain: 0.0,
                ambience_gain: 0.0,
                ..EngineConfig::default()
            },
        ),
        (
            "06-ambience-only",
            EngineConfig {
                string_gain: 0.0,
                mechanical_gain: 0.0,
                hammer_noise_gain: 0.0,
                resonance_gain: 0.0,
                body_gain: 0.0,
                ..EngineConfig::default()
            },
        ),
        (
            "07-strings-plus-resonance",
            EngineConfig {
                mechanical_gain: 0.0,
                hammer_noise_gain: 0.0,
                body_gain: 0.0,
                ambience_gain: 0.0,
                ..EngineConfig::default()
            },
        ),
        (
            "08-no-resonance",
            EngineConfig {
                resonance_gain: 0.0,
                ..EngineConfig::default()
            },
        ),
        (
            "09-no-body",
            EngineConfig {
                body_gain: 0.0,
                ..EngineConfig::default()
            },
        ),
    ];

    for (name, config) in &configs {
        let mut engine = Engine::new(*config)?;
        let frames = render_event_sequence(&mut engine, total_frames, 128, &events)?;
        let path = output_dir.join(format!("{name}.wav"));
        render_stereo_wav(&path, sample_rate, &frames)?;

        // Quick peak measurement
        let peak: f32 = frames
            .iter()
            .flat_map(|f| [f[0].abs(), f[1].abs()])
            .fold(0.0f32, f32::max);
        eprintln!("  {name}.wav  peak={peak:.4}");
    }

    eprintln!("\nWAV files written to: {}", output_dir.display());
    eprintln!("Listen to each file to identify the thump source.");
    eprintln!("Key comparisons:");
    eprintln!("  01-full-mix vs 08-no-resonance  → does removing resonance kill the thump?");
    eprintln!("  02-strings-only                 → is the thump in the raw strings?");
    eprintln!("  03-resonance-only               → is the thump in the resonance bank?");
    eprintln!("  04-body-only                    → body contributes? (likely silent alone)");
    Ok(())
}

fn dirs() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    PathBuf::from(home).join("Videos/component-isolation")
}
