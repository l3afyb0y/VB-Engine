use std::env;
use std::path::PathBuf;

use vb_engine::{Engine, EngineConfig, render_stereo_wav};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let output = env::args()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("target/render-scale.wav"));

    let mut engine = Engine::new(EngineConfig::default())?;
    let melody = [(60u8, 112u8), (64u8, 104u8), (67u8, 108u8), (72u8, 116u8)];

    let mut frames = Vec::new();
    let mut left = vec![0.0; 128];
    let mut right = vec![0.0; 128];

    for (note, velocity) in melody {
        engine.note_on(note, velocity);
        for _ in 0..6 {
            engine.render(&mut left, &mut right)?;
            frames.extend(left.iter().zip(&right).map(|(l, r)| [*l, *r]));
        }
        engine.note_off(note);
        for _ in 0..3 {
            engine.render(&mut left, &mut right)?;
            frames.extend(left.iter().zip(&right).map(|(l, r)| [*l, *r]));
        }
    }

    for _ in 0..24 {
        engine.render(&mut left, &mut right)?;
        frames.extend(left.iter().zip(&right).map(|(l, r)| [*l, *r]));
    }

    render_stereo_wav(&output, 48_000, &frames)?;
    eprintln!("wrote {}", output.display());
    Ok(())
}
