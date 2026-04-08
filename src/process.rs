use crate::{Engine, EngineError, EngineParameter, RenderStats};

/// A sample-offset event to apply within a single render block.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ProcessEvent {
    NoteOn {
        frame_offset: usize,
        note: u8,
        velocity: u8,
    },
    NoteOff {
        frame_offset: usize,
        note: u8,
    },
    ControlChange {
        frame_offset: usize,
        control: u8,
        value: u8,
    },
    ParameterChange {
        frame_offset: usize,
        parameter: EngineParameter,
        value: f32,
    },
}

impl ProcessEvent {
    pub fn frame_offset(self) -> usize {
        match self {
            Self::NoteOn { frame_offset, .. }
            | Self::NoteOff { frame_offset, .. }
            | Self::ControlChange { frame_offset, .. }
            | Self::ParameterChange { frame_offset, .. } => frame_offset,
        }
    }
}

impl Engine {
    /// Renders a block while applying sorted sample-offset events at exact frame boundaries.
    ///
    /// The `events` slice must already be sorted by `frame_offset` and each offset must be within
    /// the current output buffer length.
    pub fn process_events(
        &mut self,
        left: &mut [f32],
        right: &mut [f32],
        events: &[ProcessEvent],
    ) -> Result<RenderStats, EngineError> {
        validate_process_events(left.len(), events)?;

        let mut cursor = 0usize;
        let mut stats = RenderStats::default();
        let mut event_index = 0usize;

        while cursor < left.len() {
            while event_index < events.len() && events[event_index].frame_offset() == cursor {
                match events[event_index] {
                    ProcessEvent::NoteOn { note, velocity, .. } => self.note_on(note, velocity),
                    ProcessEvent::NoteOff { note, .. } => self.note_off(note),
                    ProcessEvent::ControlChange { control, value, .. } => {
                        self.control_change(control, value);
                    }
                    ProcessEvent::ParameterChange {
                        parameter, value, ..
                    } => {
                        let _ = self.set_parameter(parameter, value);
                    }
                }
                event_index += 1;
            }

            let next_frame = events
                .get(event_index)
                .map(|event| event.frame_offset())
                .unwrap_or(left.len());
            let chunk_end = next_frame.min(left.len());
            if chunk_end == cursor {
                continue;
            }

            let chunk_stats =
                self.render(&mut left[cursor..chunk_end], &mut right[cursor..chunk_end])?;
            stats.peak_abs_sample = stats.peak_abs_sample.max(chunk_stats.peak_abs_sample);
            stats.rendered_frames += chunk_stats.rendered_frames;
            cursor = chunk_end;
        }

        Ok(stats)
    }
}

fn validate_process_events(block_len: usize, events: &[ProcessEvent]) -> Result<(), EngineError> {
    let mut last_offset = 0usize;
    for (index, event) in events.iter().copied().enumerate() {
        let frame_offset = event.frame_offset();
        if frame_offset >= block_len {
            return Err(EngineError::EventOutOfRange {
                frame_offset,
                block_len,
            });
        }
        if index > 0 && frame_offset < last_offset {
            return Err(EngineError::EventsNotSorted);
        }
        last_offset = frame_offset;
    }
    Ok(())
}
