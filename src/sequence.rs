use crate::{Engine, EngineError, ProcessEvent};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RenderEvent {
    NoteOn {
        frame: usize,
        note: u8,
        velocity: u8,
    },
    NoteOff {
        frame: usize,
        note: u8,
    },
    SustainPedal {
        frame: usize,
        value: u8,
    },
    SoftPedal {
        frame: usize,
        value: u8,
    },
}

impl RenderEvent {
    fn frame(self) -> usize {
        match self {
            Self::NoteOn { frame, .. }
            | Self::NoteOff { frame, .. }
            | Self::SustainPedal { frame, .. }
            | Self::SoftPedal { frame, .. } => frame,
        }
    }
}

pub fn render_event_sequence(
    engine: &mut Engine,
    total_frames: usize,
    block_size: usize,
    events: &[RenderEvent],
) -> Result<Vec<[f32; 2]>, EngineError> {
    let mut sorted_events = events.to_vec();
    sorted_events.sort_by_key(|event| event.frame());

    let mut out = vec![[0.0_f32; 2]; total_frames];
    let mut cursor = 0usize;
    let mut event_index = 0usize;

    while cursor < total_frames {
        let chunk_end = total_frames.min(cursor + block_size.max(1));
        let chunk_len = chunk_end - cursor;
        if chunk_len == 0 {
            continue;
        }

        let mut left = vec![0.0_f32; chunk_len];
        let mut right = vec![0.0_f32; chunk_len];
        let mut chunk_events = Vec::new();
        while event_index < sorted_events.len() && sorted_events[event_index].frame() < chunk_end {
            let frame_offset = sorted_events[event_index].frame() - cursor;
            chunk_events.push(match sorted_events[event_index] {
                RenderEvent::NoteOn { note, velocity, .. } => ProcessEvent::NoteOn {
                    frame_offset,
                    note,
                    velocity,
                },
                RenderEvent::NoteOff { note, .. } => ProcessEvent::NoteOff { frame_offset, note },
                RenderEvent::SustainPedal { value, .. } => ProcessEvent::ControlChange {
                    frame_offset,
                    control: 64,
                    value,
                },
                RenderEvent::SoftPedal { value, .. } => ProcessEvent::ControlChange {
                    frame_offset,
                    control: 67,
                    value,
                },
            });
            event_index += 1;
        }
        engine.process_events(&mut left, &mut right, &chunk_events)?;
        for (frame_offset, (sample_left, sample_right)) in left.into_iter().zip(right).enumerate() {
            out[cursor + frame_offset] = [sample_left, sample_right];
        }
        cursor = chunk_end;
    }

    Ok(out)
}
