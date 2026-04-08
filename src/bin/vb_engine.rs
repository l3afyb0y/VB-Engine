use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = std::env::args().skip(1);
    match args.next().as_deref() {
        Some("render-showcase") => {
            let output = args
                .next()
                .map(PathBuf::from)
                .unwrap_or_else(|| PathBuf::from("Samples"));
            run_bin("render_showcase", &[output.to_string_lossy().as_ref()])?;
        }
        Some("render-scale") => {
            let output = args
                .next()
                .map(PathBuf::from)
                .unwrap_or_else(|| PathBuf::from("target/render-scale.wav"));
            run_bin("render_scale", &[output.to_string_lossy().as_ref()])?;
        }
        Some("check-wav-health") => {
            let dir = args
                .next()
                .map(PathBuf::from)
                .unwrap_or_else(|| PathBuf::from("Samples"));
            check_wav_health(&dir)?;
        }
        Some("benchmark") => {
            let iterations = args
                .next()
                .and_then(|value| value.parse::<usize>().ok())
                .unwrap_or(3);
            run_benchmark(iterations)?;
        }
        Some("compare-wavs") => {
            let baseline = args
                .next()
                .map(PathBuf::from)
                .ok_or("compare-wavs requires a baseline directory")?;
            let candidate = args
                .next()
                .map(PathBuf::from)
                .unwrap_or_else(|| PathBuf::from("Samples"));
            compare_wav_dirs(&baseline, &candidate)?;
        }
        Some("verify") => {
            run_full_verification()?;
        }
        Some(other) => {
            eprintln!("unknown subcommand: {other}");
            print_usage();
            std::process::exit(2);
        }
        None => {
            print_usage();
        }
    }
    Ok(())
}

fn run_bin(bin: &str, args: &[&str]) -> Result<(), Box<dyn std::error::Error>> {
    let status = Command::new("cargo")
        .arg("run")
        .arg("--bin")
        .arg(bin)
        .arg("--")
        .args(args)
        .status()?;
    if !status.success() {
        return Err(format!("{bin} exited with status {status}").into());
    }
    Ok(())
}

fn run_full_verification() -> Result<(), Box<dyn std::error::Error>> {
    let test_status = Command::new("cargo").arg("test").status()?;
    if !test_status.success() {
        return Err(format!("cargo test failed with status {test_status}").into());
    }

    let showcase_status = Command::new("cargo")
        .arg("run")
        .arg("--bin")
        .arg("render_showcase")
        .arg("--")
        .arg("Samples")
        .status()?;
    if !showcase_status.success() {
        return Err(format!("render_showcase failed with status {showcase_status}").into());
    }

    check_wav_health(Path::new("Samples"))?;
    run_benchmark(3)?;
    Ok(())
}

fn check_wav_health(dir: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let mut checked_any = false;
    let mut failed = false;

    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.extension().and_then(|ext| ext.to_str()) != Some("wav") {
            continue;
        }

        checked_any = true;
        let bytes = fs::read(&path)?;
        let metrics = analyze_wav(&bytes)?;
        println!(
            "{}: peak={:.6} rms={:.6} dc={:.6} max_jump={:.6} clip_samples={}",
            path.file_name()
                .and_then(|n| n.to_str())
                .unwrap_or("<unknown>"),
            metrics.peak,
            metrics.rms,
            metrics.dc,
            metrics.max_jump,
            metrics.clip_samples
        );

        if metrics.peak > 0.98
            || metrics.clip_samples > 0
            || metrics.dc.abs() > 0.02
            || metrics.max_jump > 0.98
        {
            failed = true;
        }
    }

    if !checked_any {
        return Err(format!("no wav files found in {}", dir.display()).into());
    }
    if failed {
        return Err("FAIL wav health gate".into());
    }

    println!("PASS wav health gate");
    Ok(())
}

fn run_benchmark(iterations: usize) -> Result<(), Box<dyn std::error::Error>> {
    use vb_engine::{Engine, EngineConfig, RenderEvent, render_event_sequence};

    let benchmark_events = vec![
        RenderEvent::SustainPedal {
            frame: 0,
            value: 127,
        },
        RenderEvent::NoteOn {
            frame: 0,
            note: 48,
            velocity: 110,
        },
        RenderEvent::NoteOn {
            frame: 0,
            note: 55,
            velocity: 96,
        },
        RenderEvent::NoteOn {
            frame: 0,
            note: 60,
            velocity: 104,
        },
        RenderEvent::NoteOff {
            frame: 24_000,
            note: 48,
        },
        RenderEvent::NoteOff {
            frame: 24_000,
            note: 55,
        },
        RenderEvent::NoteOff {
            frame: 24_000,
            note: 60,
        },
        RenderEvent::SustainPedal {
            frame: 28_000,
            value: 0,
        },
    ];

    let total_frames = 48_000usize;
    let mut rtfs = Vec::with_capacity(iterations);
    for _ in 0..iterations {
        let mut engine = Engine::new(EngineConfig::default())?;
        let start = Instant::now();
        let _ = render_event_sequence(&mut engine, total_frames, 128, &benchmark_events)?;
        let elapsed = start.elapsed().as_secs_f64();
        let rendered_seconds = total_frames as f64 / 48_000.0;
        rtfs.push(rendered_seconds / elapsed.max(1.0e-6));
    }

    rtfs.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
    let median = rtfs[rtfs.len() / 2];
    println!("benchmark median_rtf={median:.4}");

    let min_median_rtf = std::env::var("MIN_MEDIAN_RTF")
        .ok()
        .and_then(|value| value.parse::<f64>().ok())
        .unwrap_or(1.0);
    if median < min_median_rtf {
        return Err(format!(
            "benchmark median_rtf {median:.4} below threshold {min_median_rtf:.4}"
        )
        .into());
    }

    Ok(())
}

fn compare_wav_dirs(baseline: &Path, candidate: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let mut compared_any = false;
    let mut failed = false;
    let diff_threshold = std::env::var("VB_MAX_MEAN_ABS_DIFF")
        .ok()
        .and_then(|value| value.parse::<f64>().ok())
        .unwrap_or(0.08);

    for entry in fs::read_dir(baseline)? {
        let entry = entry?;
        let baseline_path = entry.path();
        if baseline_path.extension().and_then(|ext| ext.to_str()) != Some("wav") {
            continue;
        }

        let candidate_path = candidate.join(
            baseline_path
                .file_name()
                .ok_or("baseline file missing name")?,
        );
        if !candidate_path.exists() {
            return Err(format!("missing candidate wav {}", candidate_path.display()).into());
        }

        let baseline_metrics = analyze_wav(&fs::read(&baseline_path)?)?;
        let candidate_metrics = analyze_wav(&fs::read(&candidate_path)?)?;
        let diff = mean_abs_difference(&fs::read(&baseline_path)?, &fs::read(&candidate_path)?)?;
        compared_any = true;

        println!(
            "{}: mean_abs_diff={diff:.6} baseline_peak={:.6} candidate_peak={:.6}",
            baseline_path
                .file_name()
                .and_then(|name| name.to_str())
                .unwrap_or("<unknown>"),
            baseline_metrics.peak,
            candidate_metrics.peak
        );
        if diff > diff_threshold {
            failed = true;
        }
    }

    if !compared_any {
        return Err(format!("no wav files found in {}", baseline.display()).into());
    }
    if failed {
        return Err("FAIL audio quality gate".into());
    }
    println!("PASS audio quality gate");
    Ok(())
}

struct WavMetrics {
    peak: f64,
    rms: f64,
    dc: f64,
    max_jump: f64,
    clip_samples: usize,
}

fn mean_abs_difference(
    baseline: &[u8],
    candidate: &[u8],
) -> Result<f64, Box<dyn std::error::Error>> {
    let baseline_data = wav_data_chunk(baseline)?;
    let candidate_data = wav_data_chunk(candidate)?;
    let sample_count = baseline_data.len().min(candidate_data.len()) / 2;
    if sample_count == 0 {
        return Err("empty wav data".into());
    }

    let mut total = 0.0_f64;
    for index in 0..sample_count {
        let offset = index * 2;
        let baseline_sample =
            i16::from_le_bytes([baseline_data[offset], baseline_data[offset + 1]]) as f64 / 32768.0;
        let candidate_sample =
            i16::from_le_bytes([candidate_data[offset], candidate_data[offset + 1]]) as f64
                / 32768.0;
        total += (baseline_sample - candidate_sample).abs();
    }

    Ok(total / sample_count as f64)
}

fn wav_data_chunk(bytes: &[u8]) -> Result<&[u8], Box<dyn std::error::Error>> {
    if bytes.len() < 44 || &bytes[0..4] != b"RIFF" || &bytes[8..12] != b"WAVE" {
        return Err("invalid wav header".into());
    }

    let mut cursor = 12usize;
    while cursor + 8 <= bytes.len() {
        let chunk_id = &bytes[cursor..cursor + 4];
        let chunk_size = u32::from_le_bytes(bytes[cursor + 4..cursor + 8].try_into()?) as usize;
        cursor += 8;
        if cursor + chunk_size > bytes.len() {
            break;
        }
        if chunk_id == b"data" {
            return Ok(&bytes[cursor..cursor + chunk_size]);
        }
        cursor += chunk_size;
    }

    Err("missing wav data chunk".into())
}

fn analyze_wav(bytes: &[u8]) -> Result<WavMetrics, Box<dyn std::error::Error>> {
    let data = wav_data_chunk(bytes)?;
    let mut peak = 0.0_f64;
    let mut sum_sq = 0.0_f64;
    let mut sum = 0.0_f64;
    let mut max_jump = 0.0_f64;
    let mut clip_samples = 0usize;
    let mut prev: Option<f64> = None;
    let mut count = 0usize;

    for chunk in data.chunks_exact(2) {
        let sample_i16 = i16::from_le_bytes([chunk[0], chunk[1]]);
        let sample = sample_i16 as f64 / 32768.0;
        peak = peak.max(sample.abs());
        sum_sq += sample * sample;
        sum += sample;
        if sample_i16.abs() >= 32766 {
            clip_samples += 1;
        }
        if let Some(previous) = prev {
            max_jump = max_jump.max((sample - previous).abs());
        }
        prev = Some(sample);
        count += 1;
    }

    if count == 0 {
        return Err("empty wav data".into());
    }

    Ok(WavMetrics {
        peak,
        rms: (sum_sq / count as f64).sqrt(),
        dc: sum / count as f64,
        max_jump,
        clip_samples,
    })
}

fn print_usage() {
    eprintln!(
        "vb_engine <render-showcase|render-scale|check-wav-health|benchmark|compare-wavs|verify> [args]"
    );
}
