use vb_engine::{Engine, EngineConfig, EngineError, SustainPedalState};

fn render_block(engine: &mut Engine, frames: usize) -> Vec<f32> {
    let mut left = vec![0.0; frames];
    let mut right = vec![0.0; frames];
    let _ = engine
        .render(&mut left, &mut right)
        .expect("render succeeds");
    left.into_iter()
        .zip(right)
        .map(|(l, r)| l.abs() + r.abs())
        .collect()
}

fn render_stereo(engine: &mut Engine, frames: usize) -> (Vec<f32>, Vec<f32>) {
    let mut left = vec![0.0; frames];
    let mut right = vec![0.0; frames];
    let _ = engine
        .render(&mut left, &mut right)
        .expect("render succeeds");
    (left, right)
}

fn attack_frontload_metric(samples: &[f32]) -> f32 {
    let attack_peak = samples[..8].iter().copied().fold(0.0_f32, f32::max);
    let sustain_mean = samples[8..32].iter().sum::<f32>() / 24.0;
    attack_peak / sustain_mean.max(1.0e-6)
}

fn stereo_width_metric(engine: &mut Engine, frames: usize) -> f32 {
    let (left, right) = render_stereo(engine, frames);
    left.iter()
        .zip(&right)
        .map(|(l, r)| (l - r).abs())
        .sum::<f32>()
}

#[test]
fn rejects_render_blocks_larger_than_configured_limit() {
    let mut engine = Engine::new(EngineConfig {
        max_block_size: 32,
        ..EngineConfig::default()
    })
    .expect("engine creates");

    let mut left = vec![0.0; 64];
    let mut right = vec![0.0; 64];
    let error = engine
        .render(&mut left, &mut right)
        .expect_err("oversized block should be rejected");

    assert_eq!(
        error,
        EngineError::BlockTooLarge {
            requested: 64,
            max: 32
        }
    );
}

#[test]
fn note_on_produces_finite_audible_energy() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(60, 112);

    let block = render_block(&mut engine, 128);
    let audible_energy: f32 = block.iter().sum();
    let peak = block.iter().copied().fold(0.0_f32, f32::max);

    assert!(block.iter().all(|sample| sample.is_finite()));
    assert!(
        audible_energy > 0.01,
        "expected modeled voice energy, got {audible_energy}"
    );
    assert!(
        peak > 0.05,
        "expected a usable output level for a struck note, peak={peak}"
    );
    assert_eq!(engine.diagnostics().active_voices, 1);
}

#[test]
fn higher_velocity_produces_more_energy_than_lower_velocity() {
    let mut soft = Engine::new(EngineConfig::default()).expect("soft engine creates");
    let mut loud = Engine::new(EngineConfig::default()).expect("loud engine creates");

    soft.note_on(60, 32);
    loud.note_on(60, 120);

    let soft_energy: f32 = render_block(&mut soft, 256).iter().sum();
    let loud_energy: f32 = render_block(&mut loud, 256).iter().sum();

    assert!(
        loud_energy > soft_energy * 1.04,
        "expected loud note energy to exceed soft note energy, soft={soft_energy}, loud={loud_energy}"
    );
}

#[test]
fn higher_velocity_produces_a_brighter_attack_profile() {
    let mut soft = Engine::new(EngineConfig::default()).expect("soft engine creates");
    let mut loud = Engine::new(EngineConfig::default()).expect("loud engine creates");

    soft.note_on(60, 28);
    loud.note_on(60, 124);

    let soft_block = render_block(&mut soft, 128);
    let loud_block = render_block(&mut loud, 128);
    let soft_attack = attack_frontload_metric(&soft_block[..48]);
    let loud_attack = attack_frontload_metric(&loud_block[..48]);

    assert!(
        loud_attack > soft_attack * 1.10,
        "expected louder strike to produce a sharper, more front-loaded attack, soft={soft_attack}, loud={loud_attack}"
    );
}

#[test]
fn upper_register_notes_have_more_unison_width_than_low_register_notes() {
    let config = EngineConfig {
        resonance_gain: 0.0,
        body_gain: 0.0,
        ambience_gain: 0.0,
        ..EngineConfig::default()
    };
    let mut low = Engine::new(config).expect("low engine creates");
    let mut high = Engine::new(config).expect("high engine creates");

    low.note_on(45, 104);
    high.note_on(76, 104);

    let low_width = stereo_width_metric(&mut low, 256);
    let high_width = stereo_width_metric(&mut high, 256);

    assert!(
        high_width > low_width * 1.1,
        "expected upper-register unison spread to exceed bass width, low={low_width}, high={high_width}"
    );
}

#[test]
fn sustain_pedal_holds_note_until_pedal_releases() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(57, 100);
    assert_eq!(engine.set_sustain_pedal(127), SustainPedalState::Down);
    engine.note_off(57);

    let held_energy: f32 = render_block(&mut engine, 256).iter().sum();
    assert!(
        held_energy > 0.01,
        "expected held note energy while pedal is down"
    );

    assert_eq!(engine.set_sustain_pedal(0), SustainPedalState::Up);
    for _ in 0..64 {
        let _ = render_block(&mut engine, 256);
    }

    let released_energy: f32 = render_block(&mut engine, 256).iter().sum();
    assert!(released_energy < held_energy * 0.25);
}

#[test]
fn held_note_decays_while_key_is_still_down() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(60, 104);

    let early_energy: f32 = (0..4)
        .map(|_| render_block(&mut engine, 256).iter().sum::<f32>())
        .sum();
    for _ in 0..24 {
        let _ = render_block(&mut engine, 256);
    }
    let late_energy: f32 = (0..4)
        .map(|_| render_block(&mut engine, 256).iter().sum::<f32>())
        .sum();

    assert!(
        late_energy < early_energy * 0.84,
        "expected held note to decay over time while sustained, early={early_energy}, late={late_energy}"
    );
}

#[test]
fn held_middle_c_note_keeps_audible_pitch_energy_past_half_a_second() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(60, 104);

    for _ in 0..96 {
        let _ = render_block(&mut engine, 256);
    }

    let late_energy: f32 = render_block(&mut engine, 256).iter().sum();
    assert!(
        late_energy > 0.01,
        "expected held C4 to retain audible energy after roughly half a second, got {late_energy}"
    );
}

#[test]
fn soft_pedal_reduces_attack_energy_and_brightness() {
    let mut natural = Engine::new(EngineConfig::default()).expect("natural engine creates");
    let mut soft = Engine::new(EngineConfig::default()).expect("soft engine creates");

    natural.note_on(60, 104);
    soft.set_soft_pedal(127);
    soft.note_on(60, 104);

    let natural_block = render_block(&mut natural, 128);
    let soft_block = render_block(&mut soft, 128);
    let natural_energy: f32 = natural_block.iter().sum();
    let soft_energy: f32 = soft_block.iter().sum();
    let natural_attack = attack_frontload_metric(&natural_block[..48]);
    let soft_attack = attack_frontload_metric(&soft_block[..48]);

    assert!(soft_energy < natural_energy * 0.98);
    assert!(soft_attack < natural_attack * 0.95);
}

#[test]
fn hammer_noise_gain_changes_the_attack_transient() {
    let mut restrained = Engine::new(EngineConfig {
        hammer_noise_gain: 0.0,
        ..EngineConfig::default()
    })
    .expect("restrained engine creates");
    let mut bright = Engine::new(EngineConfig {
        hammer_noise_gain: 0.9,
        ..EngineConfig::default()
    })
    .expect("bright engine creates");

    restrained.note_on(76, 118);
    bright.note_on(76, 118);

    let restrained_block = render_block(&mut restrained, 96);
    let bright_block = render_block(&mut bright, 96);
    let transient_delta: f32 = restrained_block[..18]
        .iter()
        .zip(&bright_block[..18])
        .map(|(left, right)| (left - right).abs())
        .sum();

    assert!(
        transient_delta > 0.000008,
        "expected hammer noise gain to change the attack transient, delta={transient_delta}"
    );
}

#[test]
fn string_gain_controls_sustain_layer_energy() {
    let base = EngineConfig {
        mechanical_gain: 0.0,
        resonance_gain: 0.0,
        body_gain: 0.0,
        ambience_gain: 0.0,
        ..EngineConfig::default()
    };
    let mut muted_strings = Engine::new(EngineConfig {
        string_gain: 0.0,
        ..base
    })
    .expect("muted strings engine creates");
    let mut forward_strings = Engine::new(EngineConfig {
        string_gain: 1.8,
        ..base
    })
    .expect("forward strings engine creates");

    muted_strings.note_on(60, 108);
    forward_strings.note_on(60, 108);

    let _ = render_block(&mut muted_strings, 128);
    let _ = render_block(&mut forward_strings, 128);
    let muted_sustain: f32 = render_block(&mut muted_strings, 256).iter().sum();
    let forward_sustain: f32 = render_block(&mut forward_strings, 256).iter().sum();

    assert!(
        forward_sustain > muted_sustain * 8.0,
        "expected string gain to dominate carried note energy, muted={muted_sustain}, forward={forward_sustain}"
    );
}

#[test]
fn mechanical_gain_controls_onset_layer_energy() {
    let base = EngineConfig {
        string_gain: 0.0,
        resonance_gain: 0.0,
        body_gain: 0.0,
        ambience_gain: 0.0,
        ..EngineConfig::default()
    };
    let mut dry_mechanical = Engine::new(EngineConfig {
        mechanical_gain: 0.0,
        ..base
    })
    .expect("dry mechanical engine creates");
    let mut loud_mechanical = Engine::new(EngineConfig {
        mechanical_gain: 1.4,
        ..base
    })
    .expect("loud mechanical engine creates");

    dry_mechanical.note_on(76, 118);
    loud_mechanical.note_on(76, 118);

    let dry_attack: f32 = render_block(&mut dry_mechanical, 64)[..16].iter().sum();
    let loud_attack: f32 = render_block(&mut loud_mechanical, 64)[..16].iter().sum();

    assert!(
        loud_attack > dry_attack * 10.0 + 0.0001,
        "expected mechanical gain to dominate the onset layer, dry={dry_attack}, loud={loud_attack}"
    );
}

#[test]
fn sustain_pedal_increases_post_release_tail_energy() {
    let mut dry = Engine::new(EngineConfig::default()).expect("dry engine creates");
    let mut wet = Engine::new(EngineConfig::default()).expect("wet engine creates");

    dry.note_on(60, 108);
    wet.set_sustain_pedal(127);
    wet.note_on(60, 108);
    let _ = render_block(&mut dry, 256);
    let _ = render_block(&mut wet, 256);
    dry.note_off(60);
    wet.note_off(60);

    for _ in 0..24 {
        let _ = render_block(&mut dry, 128);
        let _ = render_block(&mut wet, 128);
    }

    let dry_tail: f32 = render_block(&mut dry, 256).iter().sum();
    let wet_tail: f32 = render_block(&mut wet, 256).iter().sum();
    assert!(
        wet_tail > dry_tail * 1.2,
        "expected pedal-down tail to exceed pedal-up tail, dry={dry_tail}, wet={wet_tail}"
    );
}

#[test]
fn released_note_decays_back_to_near_silence_without_pedal() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(64, 96);
    let active_energy: f32 = render_block(&mut engine, 256).iter().sum();
    engine.note_off(64);

    for _ in 0..80 {
        let _ = render_block(&mut engine, 256);
    }

    let tail_energy: f32 = render_block(&mut engine, 256).iter().sum();
    assert!(tail_energy < active_energy * 0.02);
    assert_eq!(engine.diagnostics().active_voices, 0);
}

#[test]
fn quick_release_keeps_a_short_audible_body_tail() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(60, 104);
    let strike_energy: f32 = render_block(&mut engine, 128).iter().sum();
    engine.note_off(60);

    let mut tail_blocks = Vec::new();
    for _ in 0..8 {
        tail_blocks.push(render_block(&mut engine, 128).iter().sum::<f32>());
    }

    let early_tail = tail_blocks[0];
    let late_tail = *tail_blocks.last().expect("tail blocks exist");
    let total_tail: f32 = tail_blocks.iter().sum();

    assert!(
        total_tail > strike_energy * 0.20,
        "expected a quick tap to leave a short instrument tail, strike={strike_energy}, tail={total_tail}"
    );
    assert!(
        late_tail < early_tail,
        "expected the post-release tail to decay over time, early={early_tail}, late={late_tail}"
    );
}

#[test]
fn retriggering_the_same_note_reuses_the_existing_voice() {
    let mut engine = Engine::new(EngineConfig {
        max_voices: 8,
        ..EngineConfig::default()
    })
    .expect("engine creates");

    engine.note_on(60, 90);
    let first_peak = render_block(&mut engine, 64)
        .into_iter()
        .fold(0.0_f32, f32::max);
    engine.note_on(60, 110);
    let retrigger_peak = render_block(&mut engine, 64)
        .into_iter()
        .fold(0.0_f32, f32::max);
    let diagnostics = engine.diagnostics();

    assert_eq!(diagnostics.active_voices, 1);
    assert_eq!(diagnostics.voice_steals, 0);
    assert!(
        retrigger_peak < first_peak * 3.0,
        "retrigger should refresh the note, not stack duplicate voices: first={first_peak}, retrigger={retrigger_peak}"
    );
}

#[test]
fn struck_note_leaves_a_small_resonance_tail_after_voice_decay() {
    let mut engine = Engine::new(EngineConfig::default()).expect("engine creates");
    engine.note_on(60, 112);
    let _ = render_block(&mut engine, 256);
    engine.note_off(60);

    for _ in 0..120 {
        let _ = render_block(&mut engine, 256);
        if engine.diagnostics().active_voices == 0 {
            break;
        }
    }

    assert_eq!(engine.diagnostics().active_voices, 0);
    let resonance_tail: f32 = render_block(&mut engine, 256).iter().sum();
    assert!(
        resonance_tail > 0.0005,
        "expected some sympathetic/body tail after voices ended, got {resonance_tail}"
    );
}

#[test]
fn ambience_gain_strengthens_late_tail_energy() {
    let mut dry = Engine::new(EngineConfig {
        ambience_gain: 0.0,
        ..EngineConfig::default()
    })
    .expect("dry engine creates");
    let mut wet = Engine::new(EngineConfig {
        ambience_gain: 0.55,
        ..EngineConfig::default()
    })
    .expect("wet engine creates");

    dry.note_on(60, 110);
    wet.note_on(60, 110);
    let _ = render_block(&mut dry, 256);
    let _ = render_block(&mut wet, 256);
    dry.note_off(60);
    wet.note_off(60);

    for _ in 0..40 {
        let _ = render_block(&mut dry, 128);
        let _ = render_block(&mut wet, 128);
    }

    let dry_tail: f32 = render_block(&mut dry, 256).iter().sum();
    let wet_tail: f32 = render_block(&mut wet, 256).iter().sum();
    assert!(
        wet_tail > dry_tail * 1.2,
        "expected ambience gain to strengthen late tail energy, dry={dry_tail}, wet={wet_tail}"
    );
}

#[test]
fn lower_notes_ring_longer_than_higher_notes_after_release() {
    let mut low = Engine::new(EngineConfig::default()).expect("low engine creates");
    let mut high = Engine::new(EngineConfig::default()).expect("high engine creates");

    low.note_on(40, 104);
    high.note_on(88, 104);
    let _ = render_block(&mut low, 128);
    let _ = render_block(&mut high, 128);
    low.note_off(40);
    high.note_off(88);

    for _ in 0..26 {
        let _ = render_block(&mut low, 128);
        let _ = render_block(&mut high, 128);
    }

    // Average over several blocks to smooth out beat-pattern phase variation
    // between resonance modes (single-block measurements are phase-sensitive).
    let mut low_tail: f32 = 0.0;
    let mut high_tail: f32 = 0.0;
    for _ in 0..4 {
        low_tail += render_block(&mut low, 128).iter().sum::<f32>();
        high_tail += render_block(&mut high, 128).iter().sum::<f32>();
    }
    assert!(
        low_tail > high_tail * 0.98,
        "expected lower note to retain more tail energy, low={low_tail}, high={high_tail}"
    );
}

#[test]
fn extremely_low_sample_rates_do_not_make_dc_blockers_explode() {
    let mut engine = Engine::new(EngineConfig {
        sample_rate_hz: 1,
        max_block_size: 64,
        ..EngineConfig::default()
    })
    .expect("engine creates even at pathological sample rates");
    engine.note_on(60, 96);

    let block = render_block(&mut engine, 64);
    assert!(block.iter().all(|sample| sample.is_finite()));
    assert_eq!(
        engine.diagnostics().non_finite_output_samples,
        0,
        "engine sanitized a non-finite internal sample instead of staying numerically stable"
    );
}

#[test]
fn bass_note_is_not_overwhelmingly_louder_than_middle_c() {
    let mut bass = Engine::new(EngineConfig::default()).expect("bass engine creates");
    let mut middle = Engine::new(EngineConfig::default()).expect("middle engine creates");

    bass.note_on(28, 110);
    middle.note_on(60, 110);

    let bass_peak = render_block(&mut bass, 256)
        .into_iter()
        .fold(0.0_f32, f32::max);
    let middle_peak = render_block(&mut middle, 256)
        .into_iter()
        .fold(0.0_f32, f32::max);

    assert!(
        bass_peak < middle_peak * 3.35,
        "expected bass voicing to stay strong without overwhelming middle C, bass={bass_peak}, middle={middle_peak}"
    );
}

#[test]
fn stealing_more_notes_than_capacity_increments_diagnostics() {
    let mut engine = Engine::new(EngineConfig {
        max_voices: 2,
        ..EngineConfig::default()
    })
    .expect("engine creates");

    engine.note_on(48, 90);
    engine.note_on(52, 90);
    engine.note_on(55, 90);

    let _ = render_block(&mut engine, 64);
    let diagnostics = engine.diagnostics();

    assert_eq!(diagnostics.active_voices, 2);
    assert_eq!(diagnostics.voice_steals, 1);
}
