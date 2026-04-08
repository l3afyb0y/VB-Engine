use std::fs;
use std::path::{Path, PathBuf};

use vb_engine::{Engine, EngineConfig, RenderEvent, render_event_sequence, render_stereo_wav};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let output_dir = std::env::args()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("Samples"));
    fs::create_dir_all(&output_dir)?;

    for piece in showcase_pieces() {
        render_piece(
            &output_dir.join(piece.file_name),
            piece.frames,
            &piece.events,
        )?;
    }

    eprintln!("wrote showcase WAVs to {}", output_dir.display());
    Ok(())
}

struct ShowcasePiece {
    file_name: &'static str,
    frames: usize,
    events: Vec<RenderEvent>,
}

fn render_piece(
    path: &Path,
    total_frames: usize,
    events: &[RenderEvent],
) -> Result<(), Box<dyn std::error::Error>> {
    let mut engine = Engine::new(EngineConfig::default())?;
    let frames = render_event_sequence(&mut engine, total_frames, 128, events)?;
    render_stereo_wav(path, 48_000, &frames)?;
    Ok(())
}

fn showcase_pieces() -> Vec<ShowcasePiece> {
    vec![
        ShowcasePiece {
            file_name: "Piano-Fur-Elise-Excerpt.wav",
            frames: 33_600,
            events: phrase(&[
                (0, 76, 112, 2_100),
                (2_400, 75, 96, 1_800),
                (4_800, 76, 104, 1_900),
                (7_200, 75, 96, 1_700),
                (9_600, 76, 112, 2_200),
                (12_000, 71, 104, 2_000),
                (14_400, 74, 104, 2_000),
                (16_800, 72, 104, 2_300),
            ]),
        },
        ShowcasePiece {
            file_name: "Piano-Moonlight-Sonata-Excerpt.wav",
            frames: 48_000,
            events: pedal_phrase(
                &[
                    (0, 49, 104),
                    (0, 56, 88),
                    (0, 61, 92),
                    (4_800, 56, 84),
                    (4_800, 61, 88),
                    (4_800, 64, 96),
                    (9_600, 54, 88),
                    (9_600, 61, 88),
                    (9_600, 66, 100),
                    (14_400, 56, 84),
                    (14_400, 61, 88),
                    (14_400, 64, 96),
                ],
                4_200,
                19_200,
            ),
        },
        ShowcasePiece {
            file_name: "Piano-Turkish-March-Excerpt.wav",
            frames: 28_800,
            events: phrase(&[
                (0, 76, 116, 1_000),
                (1_200, 78, 112, 1_000),
                (2_400, 79, 108, 1_000),
                (3_600, 81, 112, 1_000),
                (4_800, 83, 116, 1_000),
                (6_000, 81, 108, 1_000),
                (7_200, 79, 108, 1_000),
                (8_400, 78, 108, 1_000),
                (9_600, 76, 112, 1_600),
            ]),
        },
        ShowcasePiece {
            file_name: "Piano-Clair-de-Lune-Excerpt.wav",
            frames: 43_200,
            events: pedal_phrase(
                &[
                    (0, 60, 92),
                    (0, 67, 88),
                    (0, 72, 96),
                    (7_200, 62, 92),
                    (7_200, 69, 88),
                    (7_200, 74, 96),
                    (14_400, 64, 90),
                    (14_400, 71, 88),
                    (14_400, 76, 94),
                    (21_600, 65, 90),
                    (21_600, 72, 88),
                    (21_600, 77, 94),
                ],
                5_800,
                28_800,
            ),
        },
        ShowcasePiece {
            file_name: "Piano-Chopin-Ballade-No1-Excerpt.wav",
            frames: 38_400,
            events: phrase(&[
                (0, 57, 110, 3_200),
                (4_000, 64, 96, 2_100),
                (7_200, 69, 104, 2_400),
                (10_800, 72, 112, 2_800),
                (14_400, 76, 118, 3_000),
                (18_000, 74, 100, 2_100),
                (21_600, 72, 102, 2_300),
            ]),
        },
        ShowcasePiece {
            file_name: "Piano-Creep-Transposed-Excerpt.wav",
            frames: 33_600,
            events: pedal_phrase(
                &[
                    (0, 55, 110),
                    (0, 59, 96),
                    (0, 62, 100),
                    (6_000, 52, 108),
                    (6_000, 57, 96),
                    (6_000, 60, 100),
                    (12_000, 48, 112),
                    (12_000, 55, 96),
                    (12_000, 59, 100),
                    (18_000, 50, 110),
                    (18_000, 57, 96),
                    (18_000, 60, 100),
                ],
                5_200,
                24_000,
            ),
        },
        ShowcasePiece {
            file_name: "Piano-Bach-Prelude-C-Excerpt.wav",
            frames: 36_000,
            events: phrase(&[
                (0, 60, 96, 1_400),
                (1_600, 64, 92, 1_400),
                (3_200, 67, 92, 1_400),
                (4_800, 72, 96, 1_400),
                (6_400, 76, 100, 1_400),
                (8_000, 72, 96, 1_400),
                (9_600, 67, 92, 1_400),
                (11_200, 64, 92, 1_400),
                (12_800, 60, 96, 1_800),
            ]),
        },
    ]
}

fn phrase(notes: &[(usize, u8, u8, usize)]) -> Vec<RenderEvent> {
    let mut events = Vec::with_capacity(notes.len() * 2);
    for &(frame, note, velocity, duration) in notes {
        events.push(RenderEvent::NoteOn {
            frame,
            note,
            velocity,
        });
        events.push(RenderEvent::NoteOff {
            frame: frame + duration,
            note,
        });
    }
    events
}

fn pedal_phrase(
    notes: &[(usize, u8, u8)],
    duration: usize,
    pedal_release: usize,
) -> Vec<RenderEvent> {
    let mut events = Vec::with_capacity((notes.len() * 2) + 2);
    events.push(RenderEvent::SustainPedal {
        frame: 0,
        value: 127,
    });
    for &(frame, note, velocity) in notes {
        events.push(RenderEvent::NoteOn {
            frame,
            note,
            velocity,
        });
        events.push(RenderEvent::NoteOff {
            frame: frame + duration,
            note,
        });
    }
    events.push(RenderEvent::SustainPedal {
        frame: pedal_release,
        value: 0,
    });
    events
}
