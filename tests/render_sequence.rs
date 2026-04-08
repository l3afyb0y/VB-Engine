use vb_engine::{Engine, EngineConfig, RenderEvent, render_event_sequence};

#[test]
fn scheduled_events_render_expected_frame_count_with_audio() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    let events = [
        RenderEvent::NoteOn {
            frame: 0,
            note: 60,
            velocity: 112,
        },
        RenderEvent::NoteOff {
            frame: 256,
            note: 60,
        },
    ];

    let frames =
        render_event_sequence(&mut engine, 1024, 128, &events).expect("sequence render succeeds");
    let total_energy: f32 = frames
        .iter()
        .map(|[left, right]| left.abs() + right.abs())
        .sum();

    assert_eq!(frames.len(), 1024);
    assert!(total_energy > 0.01);
}

#[test]
fn scheduled_soft_pedal_changes_later_note_energy() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    let events = [
        RenderEvent::NoteOn {
            frame: 0,
            note: 60,
            velocity: 104,
        },
        RenderEvent::NoteOff {
            frame: 192,
            note: 60,
        },
        RenderEvent::SoftPedal {
            frame: 256,
            value: 127,
        },
        RenderEvent::NoteOn {
            frame: 256,
            note: 60,
            velocity: 104,
        },
        RenderEvent::NoteOff {
            frame: 448,
            note: 60,
        },
    ];

    let frames =
        render_event_sequence(&mut engine, 1024, 128, &events).expect("sequence render succeeds");
    let first_note_energy: f32 = frames[0..192]
        .iter()
        .map(|[left, right]| left.abs() + right.abs())
        .sum();
    let soft_note_energy: f32 = frames[256..448]
        .iter()
        .map(|[left, right]| left.abs() + right.abs())
        .sum();

    assert!(soft_note_energy < first_note_energy * 0.95);
}

#[test]
fn repeated_pedal_held_notes_do_not_run_away_into_hard_jumps() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    let mut events = vec![RenderEvent::SustainPedal {
        frame: 0,
        value: 127,
    }];

    for index in 0..18 {
        let frame = index * 768;
        events.push(RenderEvent::NoteOn {
            frame,
            note: 60 + (index % 5) as u8,
            velocity: 104,
        });
        events.push(RenderEvent::NoteOff {
            frame: frame + 160,
            note: 60 + (index % 5) as u8,
        });
    }
    events.push(RenderEvent::SustainPedal {
        frame: 18 * 768,
        value: 0,
    });

    let frames = render_event_sequence(&mut engine, 18 * 768 + 2048, 128, &events)
        .expect("sequence render succeeds");
    let peak: f32 = frames
        .iter()
        .map(|[left, right]| left.abs().max(right.abs()))
        .fold(0.0, f32::max);
    let diagnostics = engine.diagnostics();

    assert!(
        peak < 0.95,
        "expected sustained repetition to remain bounded, peak={peak}"
    );
    assert!(
        diagnostics.hard_jump_events < 8,
        "expected sustained repetition to avoid runaway discontinuities, hard_jumps={}",
        diagnostics.hard_jump_events
    );
}
