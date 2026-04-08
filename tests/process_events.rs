use vb_engine::{Engine, EngineConfig, EngineError, EngineParameter, ProcessEvent};

fn summed_energy(samples: &[[f32; 2]]) -> f32 {
    samples
        .iter()
        .map(|[left, right]| left.abs() + right.abs())
        .sum()
}

#[test]
fn note_on_within_block_is_silent_before_offset_and_audible_after() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    let mut left = vec![0.0_f32; 128];
    let mut right = vec![0.0_f32; 128];

    let _ = engine
        .process_events(
            &mut left,
            &mut right,
            &[ProcessEvent::NoteOn {
                frame_offset: 48,
                note: 60,
                velocity: 112,
            }],
        )
        .expect("process succeeds");

    let frames: Vec<[f32; 2]> = left.into_iter().zip(right).map(|(l, r)| [l, r]).collect();
    assert!(summed_energy(&frames[..48]) < 1.0e-6);
    assert!(summed_energy(&frames[48..]) > 0.01);
}

#[test]
fn multiple_events_in_one_block_are_applied_at_sample_offsets() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    let mut left = vec![0.0_f32; 160];
    let mut right = vec![0.0_f32; 160];

    let _ = engine
        .process_events(
            &mut left,
            &mut right,
            &[
                ProcessEvent::NoteOn {
                    frame_offset: 16,
                    note: 60,
                    velocity: 108,
                },
                ProcessEvent::ControlChange {
                    frame_offset: 64,
                    control: 67,
                    value: 127,
                },
                ProcessEvent::NoteOn {
                    frame_offset: 96,
                    note: 67,
                    velocity: 108,
                },
            ],
        )
        .expect("process succeeds");

    let frames: Vec<[f32; 2]> = left.into_iter().zip(right).map(|(l, r)| [l, r]).collect();
    let first_segment = summed_energy(&frames[16..64]);
    let second_segment = summed_energy(&frames[96..160]);

    assert!(summed_energy(&frames[..16]) < 1.0e-6);
    assert!(first_segment > 0.01);
    assert!(second_segment > first_segment * 0.7);
}

#[test]
fn parameter_changes_apply_at_sample_offsets() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(60, 112);

    let mut left = vec![0.0_f32; 192];
    let mut right = vec![0.0_f32; 192];

    let _ = engine
        .process_events(
            &mut left,
            &mut right,
            &[ProcessEvent::ParameterChange {
                frame_offset: 96,
                parameter: EngineParameter::MasterGain,
                value: 0.05,
            }],
        )
        .expect("process succeeds");

    let frames: Vec<[f32; 2]> = left.into_iter().zip(right).map(|(l, r)| [l, r]).collect();
    let pre_change_energy = summed_energy(&frames[..96]);
    let post_change_energy = summed_energy(&frames[96..]);

    assert!(pre_change_energy > 0.01);
    assert!(post_change_energy < pre_change_energy * 0.7);
}

#[test]
fn unsorted_process_events_are_rejected() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    let mut left = vec![0.0_f32; 64];
    let mut right = vec![0.0_f32; 64];

    let error = engine
        .process_events(
            &mut left,
            &mut right,
            &[
                ProcessEvent::NoteOn {
                    frame_offset: 32,
                    note: 60,
                    velocity: 100,
                },
                ProcessEvent::NoteOff {
                    frame_offset: 16,
                    note: 60,
                },
            ],
        )
        .expect_err("unsorted events should fail");

    assert_eq!(error, EngineError::EventsNotSorted);
}

#[test]
fn out_of_range_process_events_are_rejected() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    let mut left = vec![0.0_f32; 64];
    let mut right = vec![0.0_f32; 64];

    let error = engine
        .process_events(
            &mut left,
            &mut right,
            &[ProcessEvent::NoteOn {
                frame_offset: 64,
                note: 60,
                velocity: 100,
            }],
        )
        .expect_err("out-of-range events should fail");

    assert_eq!(
        error,
        EngineError::EventOutOfRange {
            frame_offset: 64,
            block_len: 64,
        }
    );
}
