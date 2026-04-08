use std::collections::HashSet;
use std::ffi::CString;
use std::sync::atomic::{AtomicBool, AtomicU8, AtomicU32, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use alsa::Direction;
use alsa::seq::{
    Addr, ClientIter, EvCtrl, EvNote, EventType, PortCap, PortIter, PortSubscribe, PortType, Seq,
};
use sdl2::audio::{AudioCallback, AudioSpecDesired};
use sdl2::event::{Event, WindowEvent};
use sdl2::keyboard::Keycode;
use sdl2::pixels::Color;
use sdl2::rect::Rect;
use vb_engine::{Engine, EngineConfig};

const NOTE_LOW: u8 = 45; // A2
const NOTE_HIGH: u8 = 72; // C5
const WINDOW_WIDTH: u32 = 1100;
const WINDOW_HEIGHT: u32 = 360;
const TOP_BAR_HEIGHT: i32 = 52;
const PIANO_MARGIN: i32 = 18;
const PEDAL_CIRCLE_RADIUS: i32 = 12;
const SETTINGS_WINDOW_WIDTH: u32 = 520;
const SETTINGS_WINDOW_HEIGHT: u32 = 280;
const SETTINGS_BUTTON_X: i32 = 452;
const SETTINGS_BUTTON_Y: i32 = 12;
const SETTINGS_BUTTON_WIDTH: u32 = 136;
const SETTINGS_BUTTON_HEIGHT: u32 = 28;
const RESET_BUTTON_X: i32 = 20;
const RESET_BUTTON_Y: i32 = 220;
const RESET_BUTTON_WIDTH: u32 = 124;
const RESET_BUTTON_HEIGHT: u32 = 36;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let sample_rate = std::env::var("VB_TESTER_SAMPLE_RATE")
        .ok()
        .and_then(|v| v.parse::<u32>().ok())
        .unwrap_or(48_000);
    let buffer = std::env::var("VB_TESTER_BUFFER")
        .ok()
        .and_then(|v| v.parse::<u16>().ok())
        .unwrap_or(256);
    let max_voices = std::env::var("VB_TESTER_MAX_VOICES")
        .ok()
        .and_then(|v| v.parse::<usize>().ok())
        .unwrap_or(48);
    let mut tone_controls = ToneControls {
        master_gain: std::env::var("VB_TESTER_MASTER_GAIN")
            .ok()
            .and_then(|v| v.parse::<f32>().ok())
            .unwrap_or(0.16),
        hammer_noise_gain: std::env::var("VB_TESTER_HAMMER_NOISE")
            .ok()
            .and_then(|v| v.parse::<f32>().ok())
            .unwrap_or(0.08),
        resonance_gain: std::env::var("VB_TESTER_RESONANCE")
            .ok()
            .and_then(|v| v.parse::<f32>().ok())
            .unwrap_or(0.78),
        body_gain: std::env::var("VB_TESTER_BODY")
            .ok()
            .and_then(|v| v.parse::<f32>().ok())
            .unwrap_or(0.55),
        ambience_gain: std::env::var("VB_TESTER_AMBIENCE")
            .ok()
            .and_then(|v| v.parse::<f32>().ok())
            .unwrap_or(0.10),
    };

    let sdl = sdl2::init()?;
    let video = sdl.video()?;
    let audio = sdl.audio()?;

    let window = video
        .window("VB-Engine Piano Tester", WINDOW_WIDTH, WINDOW_HEIGHT)
        .position_centered()
        .resizable()
        .build()?;
    let mut canvas = window.into_canvas().present_vsync().build()?;

    let engine = Arc::new(Mutex::new(Engine::new(EngineConfig {
        sample_rate_hz: sample_rate,
        max_block_size: buffer as usize,
        max_voices,
        master_gain: tone_controls.master_gain,
        hammer_noise_gain: tone_controls.hammer_noise_gain,
        resonance_gain: tone_controls.resonance_gain,
        body_gain: tone_controls.body_gain,
        ambience_gain: tone_controls.ambience_gain,
        ..EngineConfig::default()
    })?));
    let shared_tone_controls = Arc::new(SharedToneControls::new(tone_controls));
    let ui_state = Arc::new(UiState::new());
    let midi_selector = std::env::var("VB_TESTER_MIDI_PORT").ok();
    let mut midi_worker =
        MidiWorker::start(Arc::clone(&engine), Arc::clone(&ui_state), midi_selector);

    let desired = AudioSpecDesired {
        freq: Some(sample_rate as i32),
        channels: Some(2),
        samples: Some(buffer),
    };

    let device = audio.open_playback(None, &desired, |_spec| AudioEngine {
        engine: Arc::clone(&engine),
        tone_controls: Arc::clone(&shared_tone_controls),
        left: vec![0.0; buffer as usize],
        right: vec![0.0; buffer as usize],
        fallback_left: vec![0.0; buffer as usize],
        fallback_right: vec![0.0; buffer as usize],
    })?;
    device.resume();

    let mut event_pump = sdl.event_pump()?;
    let mut held_mouse_notes = HashSet::new();
    let mut keyboard_notes = HashSet::new();
    let mut settings_canvas: Option<sdl2::render::Canvas<sdl2::video::Window>> = None;
    let mut settings_window_id: Option<u32> = None;
    let mut active_slider: Option<SliderKind> = None;
    let mut selected_pedal = PedalTarget::Sustain;

    'running: loop {
        for event in event_pump.poll_iter() {
            match event {
                Event::Quit { .. } => break 'running,
                Event::Window {
                    window_id,
                    win_event: WindowEvent::Close,
                    ..
                } if settings_window_id == Some(window_id) => {
                    settings_canvas = None;
                    settings_window_id = None;
                    active_slider = None;
                }
                Event::KeyDown {
                    keycode: Some(Keycode::Escape),
                    ..
                } => break 'running,
                Event::KeyDown {
                    keycode: Some(Keycode::Space),
                    repeat: false,
                    ..
                } => {
                    toggle_selected_pedal(&engine, &ui_state, selected_pedal);
                }
                Event::KeyDown {
                    keycode: Some(Keycode::Tab),
                    repeat: false,
                    ..
                } => {
                    selected_pedal = match selected_pedal {
                        PedalTarget::Sustain => PedalTarget::Soft,
                        PedalTarget::Soft => PedalTarget::Sustain,
                    };
                }
                Event::KeyDown {
                    keycode: Some(code),
                    repeat: false,
                    ..
                } => {
                    if let Some(note) = computer_key_to_note(code)
                        && keyboard_notes.insert(note)
                    {
                        trigger_note_on(&engine, &ui_state, note, 108);
                    }
                }
                Event::KeyUp {
                    keycode: Some(code),
                    repeat: false,
                    ..
                } => {
                    if let Some(note) = computer_key_to_note(code)
                        && keyboard_notes.remove(&note)
                    {
                        trigger_note_off(&engine, &ui_state, note);
                    }
                }
                Event::MouseButtonDown {
                    window_id, x, y, ..
                } if settings_window_id == Some(window_id) => {
                    if let Some(slider) = slider_at_point(x, y) {
                        apply_slider_from_position(
                            &shared_tone_controls,
                            &mut tone_controls,
                            slider,
                            x,
                        );
                        active_slider = Some(slider);
                    } else if point_in_rect(
                        Rect::new(
                            RESET_BUTTON_X,
                            RESET_BUTTON_Y,
                            RESET_BUTTON_WIDTH,
                            RESET_BUTTON_HEIGHT,
                        ),
                        x,
                        y,
                    ) {
                        tone_controls = ToneControls::tester_defaults();
                        shared_tone_controls.store(tone_controls);
                    }
                }
                Event::MouseButtonDown { x, y, .. } => {
                    let sustain_rect = Rect::new(16, 12, 180, 28);
                    let soft_rect = Rect::new(212, 12, 180, 28);
                    if point_in_rect(
                        Rect::new(
                            SETTINGS_BUTTON_X,
                            SETTINGS_BUTTON_Y,
                            SETTINGS_BUTTON_WIDTH,
                            SETTINGS_BUTTON_HEIGHT,
                        ),
                        x,
                        y,
                    ) {
                        toggle_settings_window(
                            &video,
                            &mut settings_canvas,
                            &mut settings_window_id,
                        )?;
                        continue;
                    }
                    if point_in_rect(sustain_rect, x, y) {
                        selected_pedal = PedalTarget::Sustain;
                        continue;
                    }
                    if point_in_rect(soft_rect, x, y) {
                        selected_pedal = PedalTarget::Soft;
                        continue;
                    }
                    if point_in_circle(x, y, 420, 26, PEDAL_CIRCLE_RADIUS) {
                        toggle_selected_pedal(&engine, &ui_state, selected_pedal);
                        continue;
                    }
                    if let Some(note) = note_at_position(x, y, canvas.window().size())
                        && held_mouse_notes.insert(note)
                    {
                        trigger_note_on(&engine, &ui_state, note, 112);
                    }
                }
                Event::MouseMotion { window_id, x, .. }
                    if settings_window_id == Some(window_id) =>
                {
                    if let Some(slider) = active_slider {
                        apply_slider_from_position(
                            &shared_tone_controls,
                            &mut tone_controls,
                            slider,
                            x,
                        );
                    }
                }
                Event::MouseButtonUp { window_id, .. } if settings_window_id == Some(window_id) => {
                    active_slider = None;
                }
                Event::MouseButtonUp { .. } => {
                    let notes: Vec<u8> = held_mouse_notes.drain().collect();
                    for note in notes {
                        trigger_note_off(&engine, &ui_state, note);
                    }
                    active_slider = None;
                }
                _ => {}
            }
        }

        let title = format!(
            "VB-Engine Piano Tester | Pedal: {} | MIDI: {}",
            if ui_state.sustain_down() { "ON" } else { "OFF" },
            ui_state.midi_label(),
        );
        let _ = canvas.window_mut().set_title(&title);

        let active_notes = ui_state.active_notes();
        draw_ui(
            &mut canvas,
            &active_notes,
            ui_state.sustain_down(),
            ui_state.soft_down(),
            selected_pedal,
            settings_canvas.is_some(),
        )?;

        if let Some(settings_canvas) = settings_canvas.as_mut() {
            draw_settings_window(settings_canvas, tone_controls)?;
        }
    }

    midi_worker.stop();
    Ok(())
}

struct AudioEngine {
    engine: Arc<Mutex<Engine>>,
    tone_controls: Arc<SharedToneControls>,
    left: Vec<f32>,
    right: Vec<f32>,
    fallback_left: Vec<f32>,
    fallback_right: Vec<f32>,
}

impl AudioCallback for AudioEngine {
    type Channel = f32;

    fn callback(&mut self, out: &mut [f32]) {
        let frames = out.len() / 2;
        if self.left.len() != frames {
            self.left.resize(frames, 0.0);
            self.right.resize(frames, 0.0);
            self.fallback_left.resize(frames, 0.0);
            self.fallback_right.resize(frames, 0.0);
        }

        if let Ok(mut engine) = self.engine.try_lock() {
            let tone_controls = self.tone_controls.load();
            engine.set_master_gain(tone_controls.master_gain);
            engine.set_hammer_noise_gain(tone_controls.hammer_noise_gain);
            engine.set_resonance_gain(tone_controls.resonance_gain);
            engine.set_body_gain(tone_controls.body_gain);
            engine.set_ambience_gain(tone_controls.ambience_gain);
            if engine.render(&mut self.left, &mut self.right).is_ok() {
                for frame in 0..frames {
                    out[frame * 2] = self.left[frame];
                    out[(frame * 2) + 1] = self.right[frame];
                    self.fallback_left[frame] = self.left[frame];
                    self.fallback_right[frame] = self.right[frame];
                }
                return;
            }
        }

        for frame in 0..frames {
            out[frame * 2] = self.fallback_left[frame];
            out[(frame * 2) + 1] = self.fallback_right[frame];
        }
    }
}

struct UiState {
    note_counts: [AtomicU8; 128],
    sustain_cc: AtomicU8,
    soft_cc: AtomicU8,
    midi_label: Mutex<String>,
}

impl UiState {
    fn new() -> Self {
        Self {
            note_counts: std::array::from_fn(|_| AtomicU8::new(0)),
            sustain_cc: AtomicU8::new(0),
            soft_cc: AtomicU8::new(0),
            midi_label: Mutex::new(String::from("Scanning...")),
        }
    }

    fn note_on(&self, note: u8) {
        let index = note as usize;
        let current = self.note_counts[index].load(Ordering::Acquire);
        if current < u8::MAX {
            self.note_counts[index].store(current + 1, Ordering::Release);
        }
    }

    fn note_off(&self, note: u8) {
        let index = note as usize;
        let current = self.note_counts[index].load(Ordering::Acquire);
        if current > 0 {
            self.note_counts[index].store(current - 1, Ordering::Release);
        }
    }

    fn active_notes(&self) -> HashSet<u8> {
        self.note_counts
            .iter()
            .enumerate()
            .filter_map(|(note, count)| (count.load(Ordering::Acquire) > 0).then_some(note as u8))
            .collect()
    }

    fn set_control(&self, control: u8, value: u8) {
        match control {
            64 => self.sustain_cc.store(value, Ordering::Release),
            67 => self.soft_cc.store(value, Ordering::Release),
            _ => {}
        }
    }

    fn sustain_down(&self) -> bool {
        self.sustain_cc.load(Ordering::Acquire) >= 64
    }

    fn soft_down(&self) -> bool {
        self.soft_cc.load(Ordering::Acquire) >= 64
    }

    fn set_midi_label(&self, label: impl Into<String>) {
        if let Ok(mut value) = self.midi_label.lock() {
            *value = label.into();
        }
    }

    fn midi_label(&self) -> String {
        self.midi_label
            .lock()
            .map(|value| value.clone())
            .unwrap_or_else(|_| String::from("Unavailable"))
    }
}

#[derive(Clone, Copy)]
struct ToneControls {
    master_gain: f32,
    hammer_noise_gain: f32,
    resonance_gain: f32,
    body_gain: f32,
    ambience_gain: f32,
}

impl ToneControls {
    fn tester_defaults() -> Self {
        Self {
            master_gain: 0.16,
            hammer_noise_gain: 0.08,
            resonance_gain: 0.78,
            body_gain: 0.55,
            ambience_gain: 0.10,
        }
    }

    fn settings_title(&self) -> String {
        format!(
            "VB-Engine Settings | G {:.2} | H {:.2} | R {:.2} | B {:.2} | A {:.2}",
            self.master_gain,
            self.hammer_noise_gain,
            self.resonance_gain,
            self.body_gain,
            self.ambience_gain
        )
    }
}

struct SharedToneControls {
    master_gain: AtomicU32,
    hammer_noise_gain: AtomicU32,
    resonance_gain: AtomicU32,
    body_gain: AtomicU32,
    ambience_gain: AtomicU32,
}

impl SharedToneControls {
    fn new(initial: ToneControls) -> Self {
        Self {
            master_gain: AtomicU32::new(initial.master_gain.to_bits()),
            hammer_noise_gain: AtomicU32::new(initial.hammer_noise_gain.to_bits()),
            resonance_gain: AtomicU32::new(initial.resonance_gain.to_bits()),
            body_gain: AtomicU32::new(initial.body_gain.to_bits()),
            ambience_gain: AtomicU32::new(initial.ambience_gain.to_bits()),
        }
    }

    fn load(&self) -> ToneControls {
        ToneControls {
            master_gain: f32::from_bits(self.master_gain.load(Ordering::Acquire)),
            hammer_noise_gain: f32::from_bits(self.hammer_noise_gain.load(Ordering::Acquire)),
            resonance_gain: f32::from_bits(self.resonance_gain.load(Ordering::Acquire)),
            body_gain: f32::from_bits(self.body_gain.load(Ordering::Acquire)),
            ambience_gain: f32::from_bits(self.ambience_gain.load(Ordering::Acquire)),
        }
    }

    fn store(&self, tone_controls: ToneControls) {
        self.master_gain
            .store(tone_controls.master_gain.to_bits(), Ordering::Release);
        self.hammer_noise_gain
            .store(tone_controls.hammer_noise_gain.to_bits(), Ordering::Release);
        self.resonance_gain
            .store(tone_controls.resonance_gain.to_bits(), Ordering::Release);
        self.body_gain
            .store(tone_controls.body_gain.to_bits(), Ordering::Release);
        self.ambience_gain
            .store(tone_controls.ambience_gain.to_bits(), Ordering::Release);
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum SliderKind {
    MasterGain,
    HammerNoise,
    Resonance,
    Body,
    Ambience,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum PedalTarget {
    Sustain,
    Soft,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct MidiPortInfo {
    addr: Addr,
    name: String,
}

struct MidiWorker {
    stop: Arc<AtomicBool>,
    thread: Option<JoinHandle<()>>,
}

impl MidiWorker {
    fn start(engine: Arc<Mutex<Engine>>, ui_state: Arc<UiState>, selector: Option<String>) -> Self {
        let stop = Arc::new(AtomicBool::new(false));
        let thread_stop = Arc::clone(&stop);
        let handle = thread::spawn(move || {
            if let Err(error) = run_midi_worker(thread_stop, engine, ui_state, selector) {
                eprintln!("piano tester MIDI worker error: {error}");
            }
        });

        Self {
            stop,
            thread: Some(handle),
        }
    }

    fn stop(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(handle) = self.thread.take() {
            let _ = handle.join();
        }
    }
}

impl Drop for MidiWorker {
    fn drop(&mut self) {
        self.stop();
    }
}

fn run_midi_worker(
    stop: Arc<AtomicBool>,
    engine: Arc<Mutex<Engine>>,
    ui_state: Arc<UiState>,
    selector: Option<String>,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let seq = Seq::open(None, Some(Direction::Capture), true)?;
    seq.set_client_name(&CString::new("VB Engine Piano Tester MIDI")?)?;
    let input_port = seq.create_simple_port(
        &CString::new("input")?,
        PortCap::WRITE | PortCap::SUBS_WRITE,
        PortType::APPLICATION,
    )?;
    let dest = Addr {
        client: seq.client_id()?,
        port: input_port,
    };

    let mut input = seq.input();
    let mut connected: Option<MidiPortInfo> = None;
    let mut last_refresh = Instant::now() - Duration::from_secs(2);

    while !stop.load(Ordering::Acquire) {
        if connected.is_none() || last_refresh.elapsed() >= Duration::from_secs(2) {
            let ports = enumerate_midi_inputs(&seq);
            let wanted = select_midi_port(&ports, selector.as_deref());

            if connected.as_ref() != wanted {
                if let Some(port) = connected.take() {
                    let _ = seq.unsubscribe_port(port.addr, dest);
                }

                if let Some(port) = wanted.cloned() {
                    let subscription = PortSubscribe::empty()?;
                    subscription.set_sender(port.addr);
                    subscription.set_dest(dest);
                    if seq.subscribe_port(&subscription).is_ok() {
                        ui_state.set_midi_label(port.name.clone());
                        connected = Some(port);
                    } else {
                        ui_state.set_midi_label("Connect failed");
                    }
                } else {
                    ui_state.set_midi_label("No MIDI input");
                }
            } else if let Some(port) = connected.as_ref() {
                ui_state.set_midi_label(port.name.clone());
            }

            last_refresh = Instant::now();
        }

        let pending = input.event_input_pending(true).unwrap_or(0);
        if pending == 0 {
            thread::sleep(Duration::from_millis(2));
            continue;
        }

        while let Ok(event) = input.event_input() {
            match event.get_type() {
                EventType::Noteon => {
                    if let Some(note) = event.get_data::<EvNote>() {
                        if note.velocity > 0 {
                            trigger_note_on(&engine, &ui_state, note.note, note.velocity);
                        } else {
                            trigger_note_off(&engine, &ui_state, note.note);
                        }
                    }
                }
                EventType::Noteoff => {
                    if let Some(note) = event.get_data::<EvNote>() {
                        trigger_note_off(&engine, &ui_state, note.note);
                    }
                }
                EventType::Controller => {
                    if let Some(control) = event.get_data::<EvCtrl>() {
                        let param = control.param.clamp(0, 127) as u8;
                        let value = control.value.clamp(0, 127) as u8;
                        if matches!(param, 64 | 67) {
                            apply_control_change(&engine, &ui_state, param, value);
                        }
                    }
                }
                _ => {}
            }
        }
    }

    if let Some(port) = connected.take() {
        let _ = seq.unsubscribe_port(port.addr, dest);
    }
    ui_state.set_midi_label("Stopped");
    Ok(())
}

fn enumerate_midi_inputs(seq: &Seq) -> Vec<MidiPortInfo> {
    let own_client = seq.client_id().unwrap_or(-1);
    let mut ports = Vec::new();

    for client in ClientIter::new(seq) {
        let client_id = client.get_client();
        if client_id == own_client {
            continue;
        }

        let client_name = client.get_name().unwrap_or("Unknown");
        for port in PortIter::new(seq, client_id) {
            let caps = port.get_capability();
            if !(caps.contains(PortCap::READ) && caps.contains(PortCap::SUBS_READ)) {
                continue;
            }

            let port_name = port.get_name().unwrap_or("Port");
            ports.push(MidiPortInfo {
                addr: port.addr(),
                name: format!("{client_name}:{port_name}"),
            });
        }
    }

    ports.sort_by(|left, right| left.name.cmp(&right.name));
    ports
}

fn select_midi_port<'a>(
    ports: &'a [MidiPortInfo],
    selector: Option<&str>,
) -> Option<&'a MidiPortInfo> {
    if let Some(selector) = selector {
        let selector = selector.trim();
        if selector.is_empty() {
            return ports.first();
        }

        if let Some(port) = ports.iter().find(|port| {
            port.name.contains(selector)
                || format!("{}:{}", port.addr.client, port.addr.port) == selector
        }) {
            return Some(port);
        }
    }

    ports.first()
}

fn trigger_note_on(engine: &Arc<Mutex<Engine>>, ui_state: &Arc<UiState>, note: u8, velocity: u8) {
    ui_state.note_on(note);
    if let Ok(mut engine) = engine.lock() {
        engine.note_on(note, velocity.max(1));
    }
}

fn trigger_note_off(engine: &Arc<Mutex<Engine>>, ui_state: &Arc<UiState>, note: u8) {
    ui_state.note_off(note);
    if let Ok(mut engine) = engine.lock() {
        engine.note_off(note);
    }
}

fn apply_control_change(
    engine: &Arc<Mutex<Engine>>,
    ui_state: &Arc<UiState>,
    control: u8,
    value: u8,
) {
    ui_state.set_control(control, value);
    if let Ok(mut engine) = engine.lock() {
        engine.control_change(control, value);
    }
}

fn toggle_selected_pedal(
    engine: &Arc<Mutex<Engine>>,
    ui_state: &Arc<UiState>,
    selected_pedal: PedalTarget,
) {
    match selected_pedal {
        PedalTarget::Sustain => {
            let next = if ui_state.sustain_down() { 0 } else { 127 };
            apply_control_change(engine, ui_state, 64, next);
        }
        PedalTarget::Soft => {
            let next = if ui_state.soft_down() { 0 } else { 127 };
            apply_control_change(engine, ui_state, 67, next);
        }
    }
}

fn apply_slider_from_position(
    shared_tone_controls: &Arc<SharedToneControls>,
    tone_controls: &mut ToneControls,
    slider: SliderKind,
    mouse_x: i32,
) {
    let rect = slider_rect(slider);
    let t = slider_ratio(rect, mouse_x);
    match slider {
        SliderKind::MasterGain => {
            tone_controls.master_gain = lerp(0.05, 0.40, t);
        }
        SliderKind::HammerNoise => {
            tone_controls.hammer_noise_gain = lerp(0.0, 1.0, t);
        }
        SliderKind::Resonance => {
            tone_controls.resonance_gain = lerp(0.0, 2.0, t);
        }
        SliderKind::Body => {
            tone_controls.body_gain = lerp(0.0, 2.0, t);
        }
        SliderKind::Ambience => {
            tone_controls.ambience_gain = lerp(0.0, 1.6, t);
        }
    }
    shared_tone_controls.store(*tone_controls);
}

fn toggle_settings_window(
    video: &sdl2::VideoSubsystem,
    settings_canvas: &mut Option<sdl2::render::Canvas<sdl2::video::Window>>,
    settings_window_id: &mut Option<u32>,
) -> Result<(), String> {
    if settings_canvas.is_some() {
        *settings_canvas = None;
        *settings_window_id = None;
        return Ok(());
    }

    let window = video
        .window(
            "VB-Engine Settings",
            SETTINGS_WINDOW_WIDTH,
            SETTINGS_WINDOW_HEIGHT,
        )
        .position_centered()
        .build()
        .map_err(|err| err.to_string())?;
    let canvas = window
        .into_canvas()
        .present_vsync()
        .build()
        .map_err(|err| err.to_string())?;
    *settings_window_id = Some(canvas.window().id());
    *settings_canvas = Some(canvas);
    Ok(())
}

fn draw_ui(
    canvas: &mut sdl2::render::Canvas<sdl2::video::Window>,
    active_notes: &HashSet<u8>,
    sustain_down: bool,
    soft_down: bool,
    selected_pedal: PedalTarget,
    settings_open: bool,
) -> Result<(), String> {
    let (width, height) = canvas.window().size();
    canvas.set_draw_color(Color::RGB(28, 29, 31));
    canvas.clear();

    let top_bar = Rect::new(0, 0, width, TOP_BAR_HEIGHT as u32);
    canvas.set_draw_color(Color::RGB(42, 45, 49));
    canvas.fill_rect(top_bar)?;

    let sustain_rect = Rect::new(16, 12, 180, 28);
    let soft_rect = Rect::new(212, 12, 180, 28);
    canvas.set_draw_color(match (selected_pedal, sustain_down) {
        (PedalTarget::Sustain, true) => Color::RGB(52, 148, 84),
        (PedalTarget::Sustain, false) => Color::RGB(61, 119, 181),
        _ => Color::RGB(90, 96, 104),
    });
    canvas.fill_rect(sustain_rect)?;
    canvas.set_draw_color(match (selected_pedal, soft_down) {
        (PedalTarget::Soft, true) => Color::RGB(52, 148, 84),
        (PedalTarget::Soft, false) => Color::RGB(61, 119, 181),
        _ => Color::RGB(90, 96, 104),
    });
    canvas.fill_rect(soft_rect)?;
    let settings_rect = Rect::new(
        SETTINGS_BUTTON_X,
        SETTINGS_BUTTON_Y,
        SETTINGS_BUTTON_WIDTH,
        SETTINGS_BUTTON_HEIGHT,
    );
    canvas.set_draw_color(if settings_open {
        Color::RGB(184, 140, 64)
    } else {
        Color::RGB(102, 88, 68)
    });
    canvas.fill_rect(settings_rect)?;
    canvas.set_draw_color(Color::RGB(26, 24, 22));
    canvas.draw_rect(settings_rect)?;
    draw_bitmap_text(
        canvas,
        "SUS",
        sustain_rect.x() + 18,
        sustain_rect.y() + 9,
        2,
        Color::RGB(236, 238, 242),
    )?;
    draw_bitmap_text(
        canvas,
        "SOFT",
        soft_rect.x() + 16,
        soft_rect.y() + 9,
        2,
        Color::RGB(236, 238, 242),
    )?;
    draw_settings_glyph(canvas, settings_rect, Color::RGB(232, 224, 214))?;
    draw_filled_circle(
        canvas,
        420,
        26,
        PEDAL_CIRCLE_RADIUS,
        match selected_pedal {
            PedalTarget::Sustain if sustain_down => Color::RGB(52, 148, 84),
            PedalTarget::Soft if soft_down => Color::RGB(52, 148, 84),
            _ => Color::RGB(61, 119, 181),
        },
    )?;

    let keys = build_keyboard_layout(width as i32, height as i32);
    for key in keys.iter().filter(|key| !key.black) {
        let active = active_notes.contains(&key.note);
        canvas.set_draw_color(if active {
            Color::RGB(210, 228, 255)
        } else {
            Color::RGB(236, 236, 236)
        });
        canvas.fill_rect(key.rect)?;
        canvas.set_draw_color(Color::RGB(30, 30, 30));
        canvas.draw_rect(key.rect)?;
    }

    for key in keys.iter().filter(|key| key.black) {
        let active = active_notes.contains(&key.note);
        canvas.set_draw_color(if active {
            Color::RGB(112, 149, 219)
        } else {
            Color::RGB(22, 22, 24)
        });
        canvas.fill_rect(key.rect)?;
    }

    canvas.present();
    Ok(())
}

fn draw_settings_window(
    canvas: &mut sdl2::render::Canvas<sdl2::video::Window>,
    tone_controls: ToneControls,
) -> Result<(), String> {
    canvas.set_draw_color(Color::RGB(24, 26, 29));
    canvas.clear();

    let panel = Rect::new(
        12,
        12,
        SETTINGS_WINDOW_WIDTH - 24,
        SETTINGS_WINDOW_HEIGHT - 24,
    );
    canvas.set_draw_color(Color::RGB(39, 42, 47));
    canvas.fill_rect(panel)?;
    canvas.set_draw_color(Color::RGB(64, 69, 75));
    canvas.draw_rect(panel)?;

    draw_slider_row(
        canvas,
        SliderKind::MasterGain,
        tone_controls.master_gain,
        0.05,
        0.40,
        Color::RGB(218, 176, 99),
        "GAIN",
    )?;
    draw_slider_row(
        canvas,
        SliderKind::HammerNoise,
        tone_controls.hammer_noise_gain,
        0.0,
        1.0,
        Color::RGB(203, 120, 78),
        "HAMMER",
    )?;
    draw_slider_row(
        canvas,
        SliderKind::Resonance,
        tone_controls.resonance_gain,
        0.0,
        2.0,
        Color::RGB(87, 175, 152),
        "RES",
    )?;
    draw_slider_row(
        canvas,
        SliderKind::Body,
        tone_controls.body_gain,
        0.0,
        2.0,
        Color::RGB(84, 136, 198),
        "BODY",
    )?;
    draw_slider_row(
        canvas,
        SliderKind::Ambience,
        tone_controls.ambience_gain,
        0.0,
        1.6,
        Color::RGB(94, 168, 122),
        "AIR",
    )?;

    let reset_rect = Rect::new(
        RESET_BUTTON_X,
        RESET_BUTTON_Y,
        RESET_BUTTON_WIDTH,
        RESET_BUTTON_HEIGHT,
    );
    canvas.set_draw_color(Color::RGB(78, 83, 90));
    canvas.fill_rect(reset_rect)?;
    canvas.set_draw_color(Color::RGB(30, 32, 36));
    canvas.draw_rect(reset_rect)?;
    draw_reset_glyph(canvas, reset_rect, Color::RGB(224, 226, 229))?;
    draw_bitmap_text(
        canvas,
        "RESET",
        reset_rect.x() + 50,
        reset_rect.y() + 12,
        2,
        Color::RGB(224, 226, 229),
    )?;

    canvas
        .window_mut()
        .set_title(&tone_controls.settings_title())
        .map_err(|err| err.to_string())?;
    canvas.present();
    Ok(())
}

fn draw_filled_circle(
    canvas: &mut sdl2::render::Canvas<sdl2::video::Window>,
    cx: i32,
    cy: i32,
    radius: i32,
    color: Color,
) -> Result<(), String> {
    canvas.set_draw_color(color);
    for y in -radius..=radius {
        let span = (((radius * radius) - (y * y)) as f32).sqrt() as i32;
        canvas.draw_line((cx - span, cy + y), (cx + span, cy + y))?;
    }
    Ok(())
}

fn draw_settings_glyph(
    canvas: &mut sdl2::render::Canvas<sdl2::video::Window>,
    rect: Rect,
    color: Color,
) -> Result<(), String> {
    canvas.set_draw_color(color);
    let x = rect.x();
    let y = rect.y();
    canvas.draw_line((x + 26, y + 8), (x + 110, y + 8))?;
    canvas.draw_line((x + 26, y + 14), (x + 110, y + 14))?;
    canvas.draw_line((x + 26, y + 20), (x + 110, y + 20))?;
    draw_filled_circle(canvas, x + 48, y + 8, 4, color)?;
    draw_filled_circle(canvas, x + 88, y + 14, 4, color)?;
    draw_filled_circle(canvas, x + 64, y + 20, 4, color)?;
    Ok(())
}

fn draw_slider_row(
    canvas: &mut sdl2::render::Canvas<sdl2::video::Window>,
    slider: SliderKind,
    value: f32,
    minimum: f32,
    maximum: f32,
    color: Color,
    label: &str,
) -> Result<(), String> {
    let rect = slider_rect(slider);
    canvas.set_draw_color(Color::RGB(55, 58, 64));
    canvas.fill_rect(rect)?;
    draw_bitmap_text(
        canvas,
        label,
        rect.x() + 10,
        rect.y() + 8,
        2,
        Color::RGB(232, 234, 238),
    )?;

    let ratio = ((value - minimum) / (maximum - minimum)).clamp(0.0, 1.0);
    let track_x = rect.x() + 150;
    let track_width = rect.width() as i32 - 164;
    let track = Rect::new(track_x, rect.y() + 4, track_width as u32, rect.height() - 8);
    canvas.set_draw_color(Color::RGB(73, 76, 82));
    canvas.fill_rect(track)?;
    let fill_width = ((track.width() as f32 - 8.0) * ratio).round().max(8.0) as u32;
    let fill = Rect::new(track.x() + 4, track.y() + 2, fill_width, track.height() - 4);
    canvas.set_draw_color(color);
    canvas.fill_rect(fill)?;
    canvas.set_draw_color(Color::RGB(24, 24, 26));
    canvas.draw_rect(rect)?;
    canvas.draw_rect(track)?;

    let knob_x = track.x() + 4 + ((track.width() as f32 - 8.0) * ratio).round() as i32;
    let knob_rect = Rect::new(knob_x - 5, track.y() - 2, 10, track.height() + 4);
    canvas.set_draw_color(Color::RGB(240, 240, 242));
    canvas.fill_rect(knob_rect)?;
    Ok(())
}

fn draw_reset_glyph(
    canvas: &mut sdl2::render::Canvas<sdl2::video::Window>,
    rect: Rect,
    color: Color,
) -> Result<(), String> {
    canvas.set_draw_color(color);
    let x = rect.x();
    let y = rect.y();
    canvas.draw_line((x + 24, y + 18), (x + 94, y + 18))?;
    canvas.draw_line((x + 24, y + 18), (x + 34, y + 10))?;
    canvas.draw_line((x + 24, y + 18), (x + 34, y + 26))?;
    canvas.draw_line((x + 40, y + 10), (x + 88, y + 10))?;
    canvas.draw_line((x + 88, y + 10), (x + 88, y + 26))?;
    canvas.draw_line((x + 88, y + 26), (x + 40, y + 26))?;
    Ok(())
}

fn draw_bitmap_text(
    canvas: &mut sdl2::render::Canvas<sdl2::video::Window>,
    text: &str,
    x: i32,
    y: i32,
    scale: i32,
    color: Color,
) -> Result<(), String> {
    let mut cursor_x = x;
    for ch in text.chars() {
        if ch == ' ' {
            cursor_x += scale * 4;
            continue;
        }
        if let Some(rows) = glyph_rows(ch) {
            canvas.set_draw_color(color);
            for (row_index, row) in rows.iter().enumerate() {
                for (col_index, pixel) in row.chars().enumerate() {
                    if pixel == '1' {
                        canvas.fill_rect(Rect::new(
                            cursor_x + (col_index as i32 * scale),
                            y + (row_index as i32 * scale),
                            scale as u32,
                            scale as u32,
                        ))?;
                    }
                }
            }
        }
        cursor_x += scale * 6;
    }
    Ok(())
}

fn glyph_rows(ch: char) -> Option<[&'static str; 5]> {
    match ch {
        'A' => Some(["01110", "10001", "11111", "10001", "10001"]),
        'B' => Some(["11110", "10001", "11110", "10001", "11110"]),
        'D' => Some(["11110", "10001", "10001", "10001", "11110"]),
        'E' => Some(["11111", "10000", "11110", "10000", "11111"]),
        'F' => Some(["11111", "10000", "11110", "10000", "10000"]),
        'G' => Some(["01111", "10000", "10111", "10001", "01110"]),
        'H' => Some(["10001", "10001", "11111", "10001", "10001"]),
        'I' => Some(["11111", "00100", "00100", "00100", "11111"]),
        'M' => Some(["10001", "11011", "10101", "10001", "10001"]),
        'O' => Some(["01110", "10001", "10001", "10001", "01110"]),
        'R' => Some(["11110", "10001", "11110", "10100", "10010"]),
        'S' => Some(["01111", "10000", "01110", "00001", "11110"]),
        'T' => Some(["11111", "00100", "00100", "00100", "00100"]),
        'U' => Some(["10001", "10001", "10001", "10001", "01110"]),
        'Y' => Some(["10001", "01010", "00100", "00100", "00100"]),
        _ => None,
    }
}

fn point_in_circle(x: i32, y: i32, cx: i32, cy: i32, radius: i32) -> bool {
    let dx = x - cx;
    let dy = y - cy;
    (dx * dx) + (dy * dy) <= radius * radius
}

fn point_in_rect(rect: Rect, x: i32, y: i32) -> bool {
    rect.contains_point((x, y))
}

fn slider_rect(slider: SliderKind) -> Rect {
    let row = match slider {
        SliderKind::MasterGain => 0,
        SliderKind::HammerNoise => 1,
        SliderKind::Resonance => 2,
        SliderKind::Body => 3,
        SliderKind::Ambience => 4,
    };
    Rect::new(20, 24 + (row * 36), 480, 26)
}

fn slider_at_point(x: i32, y: i32) -> Option<SliderKind> {
    let sliders = [
        SliderKind::MasterGain,
        SliderKind::HammerNoise,
        SliderKind::Resonance,
        SliderKind::Body,
        SliderKind::Ambience,
    ];

    sliders
        .into_iter()
        .find(|slider| point_in_rect(slider_rect(*slider), x, y))
}

fn slider_ratio(rect: Rect, mouse_x: i32) -> f32 {
    ((mouse_x - rect.x()) as f32 / rect.width() as f32).clamp(0.0, 1.0)
}

fn lerp(minimum: f32, maximum: f32, t: f32) -> f32 {
    minimum + ((maximum - minimum) * t.clamp(0.0, 1.0))
}

#[derive(Clone, Copy)]
struct KeyRect {
    note: u8,
    black: bool,
    rect: Rect,
}

fn build_keyboard_layout(width: i32, height: i32) -> Vec<KeyRect> {
    let mut white_count = 0;
    for note in NOTE_LOW..=NOTE_HIGH {
        if !is_black_key(note) {
            white_count += 1;
        }
    }

    let piano_top = TOP_BAR_HEIGHT + PIANO_MARGIN;
    let piano_height = height - piano_top - PIANO_MARGIN;
    let piano_width = width - (PIANO_MARGIN * 2);
    let white_width = (piano_width as f32 / white_count as f32).max(18.0);
    let black_width = (white_width * 0.62).round() as i32;
    let black_height = (piano_height as f32 * 0.62).round() as i32;

    let mut keys = Vec::new();
    let mut white_index = 0;
    for note in NOTE_LOW..=NOTE_HIGH {
        if !is_black_key(note) {
            let x = PIANO_MARGIN + (white_index as f32 * white_width).round() as i32;
            keys.push(KeyRect {
                note,
                black: false,
                rect: Rect::new(
                    x,
                    piano_top,
                    white_width.round() as u32,
                    piano_height.max(1) as u32,
                ),
            });
            white_index += 1;
        } else {
            let prev_white = (white_index - 1).max(0) as f32;
            let prev_x = PIANO_MARGIN + (prev_white * white_width).round() as i32;
            keys.push(KeyRect {
                note,
                black: true,
                rect: Rect::new(
                    prev_x + white_width.round() as i32 - (black_width / 2),
                    piano_top,
                    black_width.max(1) as u32,
                    black_height.max(1) as u32,
                ),
            });
        }
    }
    keys
}

fn note_at_position(x: i32, y: i32, size: (u32, u32)) -> Option<u8> {
    let keys = build_keyboard_layout(size.0 as i32, size.1 as i32);
    for key in keys.iter().filter(|key| key.black) {
        if key.rect.contains_point((x, y)) {
            return Some(key.note);
        }
    }
    for key in keys.iter().filter(|key| !key.black) {
        if key.rect.contains_point((x, y)) {
            return Some(key.note);
        }
    }
    None
}

fn is_black_key(note: u8) -> bool {
    matches!(note % 12, 1 | 3 | 6 | 8 | 10)
}

fn computer_key_to_note(keycode: Keycode) -> Option<u8> {
    let key_map = [
        (Keycode::Z, 48),
        (Keycode::S, 49),
        (Keycode::X, 50),
        (Keycode::D, 51),
        (Keycode::C, 52),
        (Keycode::V, 53),
        (Keycode::G, 54),
        (Keycode::B, 55),
        (Keycode::H, 56),
        (Keycode::N, 57),
        (Keycode::J, 58),
        (Keycode::M, 59),
        (Keycode::Q, 60),
        (Keycode::Num2, 61),
        (Keycode::W, 62),
        (Keycode::Num3, 63),
        (Keycode::E, 64),
        (Keycode::R, 65),
        (Keycode::Num5, 66),
        (Keycode::T, 67),
        (Keycode::Num6, 68),
        (Keycode::Y, 69),
        (Keycode::Num7, 70),
        (Keycode::U, 71),
        (Keycode::I, 72),
    ];
    key_map
        .into_iter()
        .find(|(candidate, _)| *candidate == keycode)
        .map(|(_, note)| note)
}
