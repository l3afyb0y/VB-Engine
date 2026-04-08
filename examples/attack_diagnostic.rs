/// Isolate each audio component individually to find the thump source.
///
/// Runs the same C4 note with only one gain stage active at a time:
///   strings, mechanical (hammer noise), resonance, body, ambience, and full mix.
use vb_engine::{Engine, EngineConfig};

const SR: u32 = 48_000;
const NOTE: u8 = 60;
const VEL: u8 = 100;
const FRAMES: usize = 512;

struct ComponentResult {
    name: &'static str,
    peak: f32,
    peak_idx: usize,
    rms: f32,
    dc_mean: f32,
    first_neg: Option<usize>,
    first_512: Vec<f32>,
}

fn run_isolated(
    name: &'static str,
    string_gain: f32,
    mechanical_gain: f32,
    resonance_gain: f32,
    body_gain: f32,
    ambience_gain: f32,
) -> ComponentResult {
    let config = EngineConfig {
        sample_rate_hz: SR,
        string_gain,
        mechanical_gain,
        resonance_gain,
        body_gain,
        ambience_gain,
        ..EngineConfig::default()
    };
    let mut engine = Engine::new(config).unwrap();

    // Stabilise.
    let mut sl = vec![0.0f32; 512];
    let mut sr = vec![0.0f32; 512];
    engine.render(&mut sl, &mut sr).unwrap();

    engine.note_on(NOTE, VEL);

    let mut left = vec![0.0f32; FRAMES];
    let mut right = vec![0.0f32; FRAMES];
    engine.render(&mut left, &mut right).unwrap();

    let mono: Vec<f32> = left
        .iter()
        .zip(&right)
        .map(|(l, r)| (l + r) * 0.5)
        .collect();
    let peak = mono.iter().map(|s| s.abs()).fold(0.0f32, f32::max);
    let peak_idx = mono.iter().position(|s| s.abs() == peak).unwrap_or(0);
    let rms = (mono.iter().map(|s| s * s).sum::<f32>() / FRAMES as f32).sqrt();
    let dc_mean = mono.iter().sum::<f32>() / FRAMES as f32;
    let first_neg = mono.iter().position(|&s| s < 0.0);

    ComponentResult {
        name,
        peak,
        peak_idx,
        rms,
        dc_mean,
        first_neg,
        first_512: mono[..FRAMES].to_vec(),
    }
}

fn main() {
    let scenarios = vec![
        // name,                    str   mech  res   body  amb
        ("STRINGS-only", 1.25, 0.0, 0.0, 0.0, 0.0),
        ("MECHANICAL-only", 0.0, 0.24, 0.0, 0.0, 0.0),
        ("RESONANCE-only", 0.0, 0.0, 0.26, 0.0, 0.0),
        ("BODY-only", 0.0, 0.0, 0.0, 0.22, 0.0),
        ("AMBIENCE-only", 0.0, 0.0, 0.0, 0.0, 0.14),
        ("STR+MECH (no post)", 1.25, 0.24, 0.0, 0.0, 0.0),
        ("STR+MECH+RES (no body)", 1.25, 0.24, 0.26, 0.0, 0.0),
        ("FULL MIX", 1.25, 0.24, 0.26, 0.22, 0.14),
    ];

    let mut results = Vec::new();
    for (name, sg, mg, rg, bg, ag) in &scenarios {
        results.push(run_isolated(name, *sg, *mg, *rg, *bg, *ag));
    }

    // Summary table.
    eprintln!(
        "{:<28} {:>8} {:>6} {:>10} {:>10} {:>10}",
        "Component", "Peak", "PkIdx", "RMS", "DC_mean", "1st_neg"
    );
    eprintln!("{:-<78}", "");
    for r in &results {
        eprintln!(
            "{:<28} {:>8.5} {:>6} {:>10.6} {:>10.6} {:>10?}",
            r.name, r.peak, r.peak_idx, r.rms, r.dc_mean, r.first_neg
        );
    }

    // Dump per-component first-512 samples as TSV on stdout.
    // Header.
    print!("sample");
    for r in &results {
        print!("\t{}", r.name);
    }
    println!();
    for i in 0..FRAMES {
        print!("{i}");
        for r in &results {
            print!("\t{:.8}", r.first_512[i]);
        }
        println!();
    }

    // Per-component energy breakdown in 64-sample windows (attack region).
    eprintln!("\n--- Energy per 64-sample window (first 512 samples) ---");
    for r in &results {
        eprintln!("\n  {}", r.name);
        for w in 0..8 {
            let start = w * 64;
            let end = (start + 64).min(r.first_512.len());
            if start >= r.first_512.len() {
                break;
            }
            let slice = &r.first_512[start..end];
            let energy: f32 = slice.iter().map(|s| s * s).sum();
            let dc: f32 = slice.iter().sum::<f32>() / slice.len() as f32;
            let pos_count = slice.iter().filter(|&&s| s > 0.0).count();
            eprintln!(
                "    [{start:>4}..{end:>4}]  energy={energy:.6}  dc={dc:+.6}  pos={pos_count}/{}",
                slice.len()
            );
        }
    }
}
