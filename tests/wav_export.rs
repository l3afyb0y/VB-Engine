use std::fs;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use vb_engine::{Engine, EngineConfig, render_stereo_wav};

fn temp_wav_path(name: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("time works")
        .as_nanos();
    std::env::temp_dir().join(format!("vb_engine_{name}_{nanos}.wav"))
}

#[test]
fn writes_a_valid_pcm_wav_header() {
    let path = temp_wav_path("header");
    render_stereo_wav(&path, 48_000, &[[0.25, -0.25], [0.0, 0.0], [0.5, -0.5]])
        .expect("wav render succeeds");

    let bytes = fs::read(&path).expect("wav exists");
    let _ = fs::remove_file(&path);

    assert_eq!(&bytes[0..4], b"RIFF");
    assert_eq!(&bytes[8..12], b"WAVE");
    assert_eq!(&bytes[12..16], b"fmt ");
    assert_eq!(&bytes[36..40], b"data");
    assert_eq!(u16::from_le_bytes([bytes[22], bytes[23]]), 2);
    assert_eq!(
        u32::from_le_bytes([bytes[24], bytes[25], bytes[26], bytes[27]]),
        48_000
    );
}

#[test]
fn rendered_engine_audio_can_be_written_to_wav() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(60, 112);

    let mut frames = Vec::with_capacity(1024);
    let mut left = vec![0.0; 128];
    let mut right = vec![0.0; 128];
    for _ in 0..8 {
        engine
            .render(&mut left, &mut right)
            .expect("render succeeds");
        frames.extend(left.iter().zip(&right).map(|(l, r)| [*l, *r]));
    }

    let path = temp_wav_path("rendered");
    render_stereo_wav(&path, 48_000, &frames).expect("wav render succeeds");
    let metadata = fs::metadata(&path).expect("wav exists");
    let _ = fs::remove_file(&path);

    assert!(metadata.len() > 44, "wav should include audio payload");
}
