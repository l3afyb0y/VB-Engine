use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::Path;

pub fn render_stereo_wav(
    path: impl AsRef<Path>,
    sample_rate_hz: u32,
    frames: &[[f32; 2]],
) -> std::io::Result<()> {
    let file = File::create(path.as_ref())?;
    let mut writer = BufWriter::new(file);

    let bytes_per_sample = 2u32;
    let channels = 2u32;
    let data_len = frames.len() as u32 * channels * bytes_per_sample;
    let riff_len = 36u32 + data_len;
    let byte_rate = sample_rate_hz * channels * bytes_per_sample;
    let block_align = (channels * bytes_per_sample) as u16;

    writer.write_all(b"RIFF")?;
    writer.write_all(&riff_len.to_le_bytes())?;
    writer.write_all(b"WAVE")?;
    writer.write_all(b"fmt ")?;
    writer.write_all(&16u32.to_le_bytes())?;
    writer.write_all(&1u16.to_le_bytes())?;
    writer.write_all(&(channels as u16).to_le_bytes())?;
    writer.write_all(&sample_rate_hz.to_le_bytes())?;
    writer.write_all(&byte_rate.to_le_bytes())?;
    writer.write_all(&block_align.to_le_bytes())?;
    writer.write_all(&16u16.to_le_bytes())?;
    writer.write_all(b"data")?;
    writer.write_all(&data_len.to_le_bytes())?;

    for [left, right] in frames {
        writer.write_all(&float_to_pcm_i16(*left).to_le_bytes())?;
        writer.write_all(&float_to_pcm_i16(*right).to_le_bytes())?;
    }

    writer.flush()?;
    Ok(())
}

fn float_to_pcm_i16(sample: f32) -> i16 {
    let clamped = sample.clamp(-1.0, 1.0);
    (clamped * i16::MAX as f32).round() as i16
}
