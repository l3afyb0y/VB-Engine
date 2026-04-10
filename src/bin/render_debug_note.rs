use std::fs::{self, File};
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};

use vb_engine::{Engine, EngineConfig, RenderDebugFrame, RenderMode, render_stereo_wav};

const SAMPLE_RATE_HZ: u32 = 48_000;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let output_dir = std::env::args()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("target/debug-note"));
    fs::create_dir_all(&output_dir)?;

    let note = std::env::var("VB_DEBUG_NOTE")
        .ok()
        .and_then(|value| value.parse::<u8>().ok())
        .unwrap_or(60);
    let velocity = std::env::var("VB_DEBUG_VELOCITY")
        .ok()
        .and_then(|value| value.parse::<u8>().ok())
        .unwrap_or(112);
    let hold_frames = std::env::var("VB_DEBUG_HOLD_FRAMES")
        .ok()
        .and_then(|value| value.parse::<usize>().ok())
        .unwrap_or(SAMPLE_RATE_HZ as usize);
    let release_frames = std::env::var("VB_DEBUG_RELEASE_FRAMES")
        .ok()
        .and_then(|value| value.parse::<usize>().ok())
        .unwrap_or((SAMPLE_RATE_HZ as usize) * 3);

    let specs = [
        StemSpec {
            file_stem: "strings_only",
            render_mode: RenderMode::StringsOnly,
            low_frequency_body_mono_override: None,
            write_trace: false,
        },
        StemSpec {
            file_stem: "strings_plus_bridge",
            render_mode: RenderMode::StringsPlusBridge,
            low_frequency_body_mono_override: None,
            write_trace: false,
        },
        StemSpec {
            file_stem: "strings_plus_bridge_plus_body",
            render_mode: RenderMode::StringsPlusBridgePlusBody,
            low_frequency_body_mono_override: None,
            write_trace: false,
        },
        StemSpec {
            file_stem: "full_mix",
            render_mode: RenderMode::FullMix,
            low_frequency_body_mono_override: None,
            write_trace: true,
        },
        StemSpec {
            file_stem: "full_mix_body_low_mono",
            render_mode: RenderMode::FullMix,
            low_frequency_body_mono_override: Some(1.0),
            write_trace: true,
        },
    ];

    for spec in specs {
        let render = render_note_take(spec, note, velocity, hold_frames, release_frames)?;
        render_stereo_wav(
            output_dir.join(format!("{}.wav", spec.file_stem)),
            SAMPLE_RATE_HZ,
            &render.frames,
        )?;
        if spec.write_trace {
            write_trace_csv(
                &output_dir.join(format!("{}_trace.csv", spec.file_stem)),
                &render.trace,
            )?;
        }
    }

    eprintln!(
        "wrote debug note renders for MIDI note {} to {}",
        note,
        output_dir.display()
    );
    Ok(())
}

#[derive(Clone, Copy)]
struct StemSpec {
    file_stem: &'static str,
    render_mode: RenderMode,
    low_frequency_body_mono_override: Option<f32>,
    write_trace: bool,
}

struct RenderCapture {
    frames: Vec<[f32; 2]>,
    trace: Vec<RenderDebugFrame>,
}

fn render_note_take(
    spec: StemSpec,
    note: u8,
    velocity: u8,
    hold_frames: usize,
    release_frames: usize,
) -> Result<RenderCapture, Box<dyn std::error::Error>> {
    let mut engine = Engine::new(EngineConfig::default())?;
    engine.set_render_mode(spec.render_mode);
    engine.set_low_frequency_body_mono_override(spec.low_frequency_body_mono_override);
    engine.note_on(note, velocity);

    let total_frames = hold_frames + release_frames;
    let mut frames = Vec::with_capacity(total_frames);
    let mut trace = Vec::with_capacity(total_frames);
    let mut left = [0.0_f32; 1];
    let mut right = [0.0_f32; 1];

    for frame_index in 0..total_frames {
        if frame_index == hold_frames {
            engine.note_off(note);
        }
        let _ = engine.render(&mut left, &mut right)?;
        frames.push([left[0], right[0]]);
        trace.push(engine.last_debug_frame());
    }

    Ok(RenderCapture { frames, trace })
}

fn write_trace_csv(path: &Path, trace: &[RenderDebugFrame]) -> std::io::Result<()> {
    let file = File::create(path)?;
    let mut writer = BufWriter::new(file);
    writer.write_all(
        b"frame,strings_left,strings_right,mechanical_left,mechanical_right,bridge_left,bridge_right,body_left,body_right,ambience_left,ambience_right,bridge_drive,bridge_reflection,bridge_energy,projection_energy,low_board_motion,mid_board_motion,air_board_motion,side_board_motion,pre_dc_left,pre_dc_right,post_dc_left,post_dc_right,output_left,output_right\n",
    )?;

    for (frame_index, frame) in trace.iter().enumerate() {
        writeln!(
            writer,
            "{frame_index},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}",
            frame.strings_left,
            frame.strings_right,
            frame.mechanical_left,
            frame.mechanical_right,
            frame.bridge_left,
            frame.bridge_right,
            frame.body_left,
            frame.body_right,
            frame.ambience_left,
            frame.ambience_right,
            frame.bridge_drive,
            frame.bridge_reflection,
            frame.bridge_energy,
            frame.projection_energy,
            frame.low_board_motion,
            frame.mid_board_motion,
            frame.air_board_motion,
            frame.side_board_motion,
            frame.pre_dc_left,
            frame.pre_dc_right,
            frame.post_dc_left,
            frame.post_dc_right,
            frame.output_left,
            frame.output_right,
        )?;
    }

    writer.flush()?;
    Ok(())
}
