#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

//! Thin VST3 wrapper around the safe Rust VB-Engine piano core.

mod parameters;

use std::cell::RefCell;
use std::ffi::{CString, c_char, c_void};
use std::mem::MaybeUninit;
use std::ptr;
use std::slice;
use std::str::FromStr;

use parameters::{
    PARAM_COUNT, ParameterId, default_parameter_values, midi_mapping, spec_by_id, spec_by_index,
    specs,
};
use vb_engine::{Engine, EngineConfig, ProcessEvent};
use vst3::{Class, ComRef, ComWrapper, Steinberg::Vst::*, Steinberg::*, uid};

const PLUGIN_NAME: &str = "VB-Engine Piano";
const PLUGIN_VENDOR: &str = "VB-Engine";
const PLUGIN_URL: &str = "https://example.invalid/vb-engine";
const PLUGIN_EMAIL: &str = "noreply@example.invalid";
const STATE_MAGIC: [u8; 4] = *b"VBP1";
const STATE_VERSION: u32 = 2;
const LEGACY_PARAM_COUNT_V1: usize = 9;

fn copy_cstring<const N: usize>(src: &str, dst: &mut [c_char; N]) {
    let c_string = CString::new(src).unwrap_or_else(|_| CString::default());
    let bytes = c_string.as_bytes_with_nul();

    for (src, dst) in bytes.iter().zip(dst.iter_mut()) {
        *dst = *src as c_char;
    }

    if bytes.len() > dst.len()
        && let Some(last) = dst.last_mut()
    {
        *last = 0;
    }
}

fn copy_wstring<const N: usize>(src: &str, dst: &mut [TChar; N]) {
    let mut len = 0;
    for (src, dst) in src.encode_utf16().zip(dst.iter_mut()) {
        *dst = src as TChar;
        len += 1;
    }

    if len < dst.len() {
        dst[len] = 0;
    } else if let Some(last) = dst.last_mut() {
        *last = 0;
    }
}

unsafe fn len_wstring(string: *const TChar) -> usize {
    let mut len = 0;
    // SAFETY: caller guarantees a valid NUL-terminated UTF-16 string.
    unsafe {
        while *string.add(len) != 0 {
            len += 1;
        }
    }
    len
}

#[derive(Clone)]
struct SerializedState {
    values: [f64; PARAM_COUNT],
}

impl SerializedState {
    fn new() -> Self {
        Self {
            values: default_parameter_values(),
        }
    }
}

unsafe fn stream_write_all(stream: &ComRef<IBStream>, bytes: &[u8]) -> tresult {
    let mut written = 0;
    // SAFETY: stream is a valid IBStream reference, bytes points to initialized memory.
    let result = unsafe {
        stream.write(
            bytes.as_ptr() as *mut c_void,
            bytes.len().try_into().unwrap_or(i32::MAX),
            &mut written,
        )
    };
    if result == kResultOk && written == bytes.len() as i32 {
        kResultOk
    } else {
        kResultFalse
    }
}

unsafe fn stream_read_exact(stream: &ComRef<IBStream>, bytes: &mut [u8]) -> tresult {
    let mut read = 0;
    // SAFETY: stream is a valid IBStream reference, bytes points to writable memory.
    let result = unsafe {
        stream.read(
            bytes.as_mut_ptr() as *mut c_void,
            bytes.len().try_into().unwrap_or(i32::MAX),
            &mut read,
        )
    };
    if result == kResultOk && read == bytes.len() as i32 {
        kResultOk
    } else {
        kResultFalse
    }
}

unsafe fn write_state(stream: *mut IBStream, state: &SerializedState) -> tresult {
    let Some(stream) = (unsafe { ComRef::from_raw(stream) }) else {
        return kInvalidArgument;
    };

    // SAFETY: validated stream above, writing fixed-size state payload.
    unsafe {
        if stream_write_all(&stream, &STATE_MAGIC) != kResultOk {
            return kResultFalse;
        }
        if stream_write_all(&stream, &STATE_VERSION.to_le_bytes()) != kResultOk {
            return kResultFalse;
        }
        for value in state.values {
            if stream_write_all(&stream, &value.to_le_bytes()) != kResultOk {
                return kResultFalse;
            }
        }
    }

    kResultOk
}

unsafe fn read_state(stream: *mut IBStream, state: &mut SerializedState) -> tresult {
    let Some(stream) = (unsafe { ComRef::from_raw(stream) }) else {
        return kInvalidArgument;
    };

    let mut magic = [0_u8; 4];
    let mut version = [0_u8; 4];
    // SAFETY: validated stream above, reading fixed-size state payload.
    unsafe {
        if stream_read_exact(&stream, &mut magic) != kResultOk || magic != STATE_MAGIC {
            return kResultFalse;
        }
        if stream_read_exact(&stream, &mut version) != kResultOk {
            return kResultFalse;
        }
        match u32::from_le_bytes(version) {
            STATE_VERSION => {
                for value in &mut state.values {
                    let mut bytes = [0_u8; 8];
                    if stream_read_exact(&stream, &mut bytes) != kResultOk {
                        return kResultFalse;
                    }
                    *value = f64::from_le_bytes(bytes);
                }
            }
            1 => {
                let mut legacy_values = [0.0_f64; LEGACY_PARAM_COUNT_V1];
                for value in &mut legacy_values {
                    let mut bytes = [0_u8; 8];
                    if stream_read_exact(&stream, &mut bytes) != kResultOk {
                        return kResultFalse;
                    }
                    *value = f64::from_le_bytes(bytes);
                }
                state.values[parameter_slot(ParameterId::MasterGain)] = legacy_values[0];
                state.values[parameter_slot(ParameterId::StringLevel)] = legacy_values[1];
                state.values[parameter_slot(ParameterId::MechanicalLevel)] = legacy_values[2];
                state.values[parameter_slot(ParameterId::HammerNoise)] = legacy_values[3];
                state.values[parameter_slot(ParameterId::Resonance)] = legacy_values[4];
                state.values[parameter_slot(ParameterId::Body)] = legacy_values[5];
                state.values[parameter_slot(ParameterId::Ambience)] = legacy_values[6];
                state.values[parameter_slot(ParameterId::SustainPedal)] = legacy_values[7];
                state.values[parameter_slot(ParameterId::SoftPedal)] = legacy_values[8];
            }
            _ => {
                return kResultFalse;
            }
        }
    }

    kResultOk
}

fn default_engine_config() -> EngineConfig {
    EngineConfig {
        max_voices: 64,
        ..EngineConfig::default()
    }
}

fn engine_from_parameter_values(
    sample_rate_hz: u32,
    max_block_size: usize,
    values: &[f64; PARAM_COUNT],
) -> Result<Engine, vb_engine::EngineError> {
    let mut config = EngineConfig {
        sample_rate_hz,
        max_block_size,
        ..default_engine_config()
    };

    for &spec in specs() {
        let normalized = values[parameter_slot(spec.id)];
        let plain = spec.normalized_to_plain(normalized) as f32;
        match spec.id {
            ParameterId::MasterGain => config.master_gain = plain,
            ParameterId::StringLevel => config.string_gain = plain,
            ParameterId::MechanicalLevel => config.mechanical_gain = plain,
            ParameterId::HammerHardness => config.hammer_hardness = plain,
            ParameterId::HammerNoise => config.hammer_noise_gain = plain,
            ParameterId::Resonance => config.resonance_gain = plain,
            ParameterId::Body => config.body_gain = plain,
            ParameterId::Ambience => config.ambience_gain = plain,
            ParameterId::BridgeFeedback => config.bridge_feedback_gain = plain,
            ParameterId::Downbearing => config.downbearing_preload = plain,
            ParameterId::PlateLeak => config.plate_leak = plain,
            ParameterId::SoundboardWidth => config.soundboard_width = plain,
            ParameterId::SustainPedal | ParameterId::SoftPedal => {}
        }
    }

    Engine::new(config)
}

fn parameter_slot(parameter: ParameterId) -> usize {
    match parameter {
        ParameterId::MasterGain => 0,
        ParameterId::StringLevel => 1,
        ParameterId::MechanicalLevel => 2,
        ParameterId::HammerHardness => 3,
        ParameterId::HammerNoise => 4,
        ParameterId::Resonance => 5,
        ParameterId::Body => 6,
        ParameterId::Ambience => 7,
        ParameterId::BridgeFeedback => 8,
        ParameterId::Downbearing => 9,
        ParameterId::PlateLeak => 10,
        ParameterId::SoundboardWidth => 11,
        ParameterId::SustainPedal => 12,
        ParameterId::SoftPedal => 13,
    }
}

fn sanitize_frame_offset(frame_offset: i32, block_len: usize) -> usize {
    if block_len == 0 {
        return 0;
    }
    frame_offset.clamp(0, block_len.saturating_sub(1) as i32) as usize
}

fn process_event_priority(event: &ProcessEvent) -> u8 {
    match event {
        ProcessEvent::ParameterChange { .. } | ProcessEvent::ControlChange { .. } => 0,
        ProcessEvent::NoteOff { .. } => 1,
        ProcessEvent::NoteOn { .. } => 2,
    }
}

fn should_insert_before(new_event: &ProcessEvent, existing: &ProcessEvent) -> bool {
    let new_key = (new_event.frame_offset(), process_event_priority(new_event));
    let existing_key = (existing.frame_offset(), process_event_priority(existing));
    new_key < existing_key
}

fn push_sorted_event(events: &mut Vec<ProcessEvent>, event: ProcessEvent) {
    let mut insert_at = events.len();
    while insert_at > 0 && should_insert_before(&event, &events[insert_at - 1]) {
        insert_at -= 1;
    }
    events.insert(insert_at, event);
}

struct ProcessorState {
    engine: Engine,
    parameter_values: [f64; PARAM_COUNT],
    event_buffer: Vec<ProcessEvent>,
    sample_rate_hz: u32,
    max_block_size: usize,
}

impl ProcessorState {
    fn new() -> Self {
        let parameter_values = default_parameter_values();
        let sample_rate_hz = 48_000;
        let max_block_size = 512;
        let engine =
            engine_from_parameter_values(sample_rate_hz, max_block_size, &parameter_values)
                .expect("default engine config should always be valid");

        Self {
            engine,
            parameter_values,
            event_buffer: Vec::with_capacity(256),
            sample_rate_hz,
            max_block_size,
        }
    }

    fn rebuild_engine(&mut self, sample_rate_hz: u32, max_block_size: usize) -> tresult {
        match engine_from_parameter_values(sample_rate_hz, max_block_size, &self.parameter_values) {
            Ok(engine) => {
                self.engine = engine;
                self.sample_rate_hz = sample_rate_hz;
                self.max_block_size = max_block_size;
                if self.event_buffer.capacity() < max_block_size.saturating_mul(2) {
                    self.event_buffer
                        .reserve(max_block_size.saturating_mul(2) - self.event_buffer.capacity());
                }
                kResultOk
            }
            Err(_) => kResultFalse,
        }
    }

    fn set_parameter_value(&mut self, parameter_id: ParameterId, normalized: f64) {
        self.parameter_values[parameter_slot(parameter_id)] = normalized.clamp(0.0, 1.0);
    }

    fn serialized_state(&self) -> SerializedState {
        SerializedState {
            values: self.parameter_values,
        }
    }

    fn restore_serialized_state(&mut self, state: &SerializedState) -> tresult {
        self.parameter_values = state.values;
        self.rebuild_engine(self.sample_rate_hz, self.max_block_size)
    }

    fn collect_parameter_changes(&mut self, changes: *mut IParameterChanges, block_len: usize) {
        let Some(changes) = (unsafe { ComRef::from_raw(changes) }) else {
            return;
        };

        // SAFETY: `changes` is a valid host-provided interface for the duration of `process`.
        unsafe {
            let parameter_count = changes.getParameterCount();
            for queue_index in 0..parameter_count {
                let Some(queue) = ComRef::from_raw(changes.getParameterData(queue_index)) else {
                    continue;
                };
                let Some(spec) = spec_by_id(queue.getParameterId()) else {
                    continue;
                };
                let point_count = queue.getPointCount();
                for point_index in 0..point_count {
                    let mut sample_offset = 0;
                    let mut normalized = 0.0;
                    if queue.getPoint(point_index, &mut sample_offset, &mut normalized)
                        != kResultTrue
                    {
                        continue;
                    }
                    self.set_parameter_value(spec.id, normalized);
                    let frame_offset = sanitize_frame_offset(sample_offset, block_len);
                    push_sorted_event(
                        &mut self.event_buffer,
                        spec.to_process_event(frame_offset, normalized),
                    );
                }
            }
        }
    }

    fn collect_note_events(&mut self, input_events: *mut IEventList, block_len: usize) {
        let Some(input_events) = (unsafe { ComRef::from_raw(input_events) }) else {
            return;
        };

        // SAFETY: `input_events` is a valid host-provided interface for the duration of `process`.
        unsafe {
            let event_count = input_events.getEventCount();
            for event_index in 0..event_count {
                let mut event = MaybeUninit::<Event>::zeroed();
                if input_events.getEvent(event_index, event.as_mut_ptr()) != kResultOk {
                    continue;
                }
                let event = event.assume_init();
                let frame_offset = sanitize_frame_offset(event.sampleOffset, block_len);

                match event.r#type as u32 {
                    Event_::EventTypes_::kNoteOnEvent => {
                        let note_on = event.__field0.noteOn;
                        let pitch = note_on.pitch.clamp(0, 127) as u8;
                        let velocity =
                            ((note_on.velocity.clamp(0.0, 1.0) * 127.0).round() as u8).max(1);
                        push_sorted_event(
                            &mut self.event_buffer,
                            ProcessEvent::NoteOn {
                                frame_offset,
                                note: pitch,
                                velocity,
                            },
                        );
                    }
                    Event_::EventTypes_::kNoteOffEvent => {
                        let note_off = event.__field0.noteOff;
                        let pitch = note_off.pitch.clamp(0, 127) as u8;
                        push_sorted_event(
                            &mut self.event_buffer,
                            ProcessEvent::NoteOff {
                                frame_offset,
                                note: pitch,
                            },
                        );
                    }
                    _ => {}
                }
            }
        }
    }
}

struct PianoProcessor {
    state: RefCell<ProcessorState>,
}

impl PianoProcessor {
    const CID: TUID = uid(0xD9B12B2A, 0x7A9D4C41, 0xA4639D17, 0x4CE4E291);

    fn new() -> Self {
        Self {
            state: RefCell::new(ProcessorState::new()),
        }
    }
}

impl Class for PianoProcessor {
    type Interfaces = (IComponent, IAudioProcessor, IProcessContextRequirements);
}

impl IPluginBaseTrait for PianoProcessor {
    unsafe fn initialize(&self, _context: *mut FUnknown) -> tresult {
        kResultOk
    }

    unsafe fn terminate(&self) -> tresult {
        kResultOk
    }
}

impl IComponentTrait for PianoProcessor {
    unsafe fn getControllerClassId(&self, class_id: *mut TUID) -> tresult {
        // SAFETY: host provides writable storage for the class ID.
        unsafe {
            *class_id = PianoController::CID;
        }
        kResultOk
    }

    unsafe fn setIoMode(&self, _mode: IoMode) -> tresult {
        kResultOk
    }

    unsafe fn getBusCount(&self, media_type: MediaType, dir: BusDirection) -> i32 {
        match media_type as MediaTypes {
            MediaTypes_::kAudio => match dir as BusDirections {
                BusDirections_::kInput => 0,
                BusDirections_::kOutput => 1,
                _ => 0,
            },
            MediaTypes_::kEvent => match dir as BusDirections {
                BusDirections_::kInput => 1,
                BusDirections_::kOutput => 0,
                _ => 0,
            },
            _ => 0,
        }
    }

    unsafe fn getBusInfo(
        &self,
        media_type: MediaType,
        dir: BusDirection,
        index: i32,
        bus: *mut BusInfo,
    ) -> tresult {
        if bus.is_null() {
            return kInvalidArgument;
        }

        match (media_type as MediaTypes, dir as BusDirections, index) {
            (MediaTypes_::kAudio, BusDirections_::kOutput, 0) => {
                // SAFETY: validated non-null above.
                let bus = unsafe { &mut *bus };
                bus.mediaType = MediaTypes_::kAudio as MediaType;
                bus.direction = BusDirections_::kOutput as BusDirection;
                bus.channelCount = 2;
                copy_wstring("Output", &mut bus.name);
                bus.busType = BusTypes_::kMain as BusType;
                bus.flags = BusInfo_::BusFlags_::kDefaultActive;
                kResultOk
            }
            (MediaTypes_::kEvent, BusDirections_::kInput, 0) => {
                // SAFETY: validated non-null above.
                let bus = unsafe { &mut *bus };
                bus.mediaType = MediaTypes_::kEvent as MediaType;
                bus.direction = BusDirections_::kInput as BusDirection;
                bus.channelCount = 16;
                copy_wstring("MIDI Input", &mut bus.name);
                bus.busType = BusTypes_::kMain as BusType;
                bus.flags = BusInfo_::BusFlags_::kDefaultActive;
                kResultOk
            }
            _ => kInvalidArgument,
        }
    }

    unsafe fn getRoutingInfo(
        &self,
        _in_info: *mut RoutingInfo,
        _out_info: *mut RoutingInfo,
    ) -> tresult {
        kNotImplemented
    }

    unsafe fn activateBus(
        &self,
        _media_type: MediaType,
        _dir: BusDirection,
        _index: i32,
        _state: TBool,
    ) -> tresult {
        kResultOk
    }

    unsafe fn setActive(&self, _state: TBool) -> tresult {
        kResultOk
    }

    unsafe fn setState(&self, state: *mut IBStream) -> tresult {
        let mut serialized = SerializedState::new();
        // SAFETY: host owns the stream pointer and passes it for this call only.
        if unsafe { read_state(state, &mut serialized) } != kResultOk {
            return kResultFalse;
        }
        self.state
            .borrow_mut()
            .restore_serialized_state(&serialized)
    }

    unsafe fn getState(&self, state: *mut IBStream) -> tresult {
        let serialized = self.state.borrow().serialized_state();
        // SAFETY: host owns the stream pointer and passes it for this call only.
        unsafe { write_state(state, &serialized) }
    }
}

impl IAudioProcessorTrait for PianoProcessor {
    unsafe fn setBusArrangements(
        &self,
        _inputs: *mut SpeakerArrangement,
        num_ins: i32,
        outputs: *mut SpeakerArrangement,
        num_outs: i32,
    ) -> tresult {
        if num_ins != 0 || num_outs != 1 || outputs.is_null() {
            return kResultFalse;
        }
        // SAFETY: validated non-null above and host passed one arrangement.
        if unsafe { *outputs } != SpeakerArr::kStereo {
            return kResultFalse;
        }
        kResultTrue
    }

    unsafe fn getBusArrangement(
        &self,
        dir: BusDirection,
        index: i32,
        arr: *mut SpeakerArrangement,
    ) -> tresult {
        if arr.is_null() {
            return kInvalidArgument;
        }

        match (dir as BusDirections, index) {
            (BusDirections_::kOutput, 0) => {
                // SAFETY: validated non-null above.
                unsafe {
                    *arr = SpeakerArr::kStereo;
                }
                kResultOk
            }
            _ => kInvalidArgument,
        }
    }

    unsafe fn canProcessSampleSize(&self, symbolic_sample_size: i32) -> tresult {
        match symbolic_sample_size as SymbolicSampleSizes {
            SymbolicSampleSizes_::kSample32 => kResultOk,
            SymbolicSampleSizes_::kSample64 => kNotImplemented,
            _ => kInvalidArgument,
        }
    }

    unsafe fn getLatencySamples(&self) -> u32 {
        0
    }

    unsafe fn setupProcessing(&self, setup: *mut ProcessSetup) -> tresult {
        if setup.is_null() {
            return kInvalidArgument;
        }
        // SAFETY: validated non-null above.
        let setup = unsafe { &*setup };
        let sample_rate_hz = setup.sampleRate.max(1.0).round() as u32;
        let max_block_size = usize::try_from(setup.maxSamplesPerBlock.max(1)).unwrap_or(512);
        self.state
            .borrow_mut()
            .rebuild_engine(sample_rate_hz, max_block_size)
    }

    unsafe fn setProcessing(&self, _state: TBool) -> tresult {
        kResultOk
    }

    unsafe fn process(&self, data: *mut ProcessData) -> tresult {
        if data.is_null() {
            return kInvalidArgument;
        }
        // SAFETY: validated non-null above.
        let process_data = unsafe { &*data };
        if process_data.numOutputs != 1 || process_data.outputs.is_null() {
            return kResultOk;
        }

        let num_samples = usize::try_from(process_data.numSamples.max(0)).unwrap_or(0);
        if num_samples == 0 {
            return kResultOk;
        }

        // SAFETY: host provides one output bus because numOutputs == 1.
        let output_buses = unsafe { slice::from_raw_parts(process_data.outputs, 1) };
        if output_buses[0].numChannels != 2 {
            return kResultFalse;
        }

        // SAFETY: VST3 host provides valid channel buffer pointers for the current process call.
        let output_channels = unsafe {
            slice::from_raw_parts(
                output_buses[0].__field0.channelBuffers32,
                output_buses[0].numChannels as usize,
            )
        };
        let output_left = output_channels[0];
        let output_right = output_channels[1];
        if output_left.is_null() || output_right.is_null() {
            return kResultFalse;
        }

        // SAFETY: host guarantees writable buffers for `num_samples` frames.
        let left = unsafe { slice::from_raw_parts_mut(output_left, num_samples) };
        let right = unsafe { slice::from_raw_parts_mut(output_right, num_samples) };
        left.fill(0.0);
        right.fill(0.0);

        let mut state = self.state.borrow_mut();
        state.event_buffer.clear();
        state.collect_parameter_changes(process_data.inputParameterChanges, num_samples);
        state.collect_note_events(process_data.inputEvents, num_samples);

        let ProcessorState {
            engine,
            event_buffer,
            ..
        } = &mut *state;
        match engine.process_events(left, right, event_buffer.as_slice()) {
            Ok(_) => kResultOk,
            Err(_) => kResultFalse,
        }
    }

    unsafe fn getTailSamples(&self) -> u32 {
        kInfiniteTail
    }
}

impl IProcessContextRequirementsTrait for PianoProcessor {
    unsafe fn getProcessContextRequirements(&self) -> u32 {
        0
    }
}

struct PianoController {
    values: RefCell<[f64; PARAM_COUNT]>,
}

impl PianoController {
    const CID: TUID = uid(0x4F0A1C82, 0xE4D34B37, 0x95EF8E5A, 0xC2B90D74);

    fn new() -> Self {
        Self {
            values: RefCell::new(default_parameter_values()),
        }
    }
}

impl Class for PianoController {
    type Interfaces = (IEditController, IMidiMapping);
}

impl IPluginBaseTrait for PianoController {
    unsafe fn initialize(&self, _context: *mut FUnknown) -> tresult {
        kResultOk
    }

    unsafe fn terminate(&self) -> tresult {
        kResultOk
    }
}

impl IEditControllerTrait for PianoController {
    unsafe fn setComponentState(&self, state: *mut IBStream) -> tresult {
        unsafe { self.setState(state) }
    }

    unsafe fn setState(&self, state: *mut IBStream) -> tresult {
        let mut serialized = SerializedState::new();
        // SAFETY: host owns the stream pointer and passes it for this call only.
        if unsafe { read_state(state, &mut serialized) } != kResultOk {
            return kResultFalse;
        }
        *self.values.borrow_mut() = serialized.values;
        kResultOk
    }

    unsafe fn getState(&self, state: *mut IBStream) -> tresult {
        let serialized = SerializedState {
            values: *self.values.borrow(),
        };
        // SAFETY: host owns the stream pointer and passes it for this call only.
        unsafe { write_state(state, &serialized) }
    }

    unsafe fn getParameterCount(&self) -> i32 {
        specs().len() as i32
    }

    unsafe fn getParameterInfo(&self, param_index: i32, info: *mut ParameterInfo) -> tresult {
        if info.is_null() {
            return kInvalidArgument;
        }
        let Some(spec) = spec_by_index(param_index) else {
            return kInvalidArgument;
        };

        // SAFETY: validated non-null above.
        let info = unsafe { &mut *info };
        info.id = spec.id as u32;
        copy_wstring(spec.title, &mut info.title);
        copy_wstring(spec.short_title, &mut info.shortTitle);
        copy_wstring(spec.units, &mut info.units);
        info.stepCount = 0;
        info.defaultNormalizedValue = spec.default_normalized;
        info.unitId = 0;
        info.flags = spec.flags;
        kResultOk
    }

    unsafe fn getParamStringByValue(
        &self,
        id: u32,
        value_normalized: f64,
        string: *mut String128,
    ) -> tresult {
        if string.is_null() {
            return kInvalidArgument;
        }
        let Some(spec) = spec_by_id(id) else {
            return kInvalidArgument;
        };
        let display = spec.format_plain(spec.normalized_to_plain(value_normalized));
        // SAFETY: validated non-null above.
        unsafe {
            copy_wstring(&display, &mut *string);
        }
        kResultOk
    }

    unsafe fn getParamValueByString(
        &self,
        id: u32,
        string: *mut TChar,
        value_normalized: *mut f64,
    ) -> tresult {
        if string.is_null() || value_normalized.is_null() {
            return kInvalidArgument;
        }
        let Some(spec) = spec_by_id(id) else {
            return kInvalidArgument;
        };

        // SAFETY: host provides a valid NUL-terminated UTF-16 string for this call.
        let len = unsafe { len_wstring(string as *const TChar) };
        // SAFETY: `string` and `value_normalized` were validated above.
        unsafe {
            if let Ok(value_string) = String::from_utf16(slice::from_raw_parts(string, len))
                && let Ok(plain_value) = f64::from_str(value_string.trim())
            {
                *value_normalized = spec.plain_to_normalized(plain_value);
                return kResultOk;
            }
        }
        kInvalidArgument
    }

    unsafe fn normalizedParamToPlain(&self, id: u32, value_normalized: f64) -> f64 {
        spec_by_id(id)
            .map(|spec| spec.normalized_to_plain(value_normalized))
            .unwrap_or(0.0)
    }

    unsafe fn plainParamToNormalized(&self, id: u32, plain_value: f64) -> f64 {
        spec_by_id(id)
            .map(|spec| spec.plain_to_normalized(plain_value))
            .unwrap_or(0.0)
    }

    unsafe fn getParamNormalized(&self, id: u32) -> f64 {
        spec_by_id(id)
            .map(|spec| self.values.borrow()[parameter_slot(spec.id)])
            .unwrap_or(0.0)
    }

    unsafe fn setParamNormalized(&self, id: u32, value: f64) -> tresult {
        let Some(spec) = spec_by_id(id) else {
            return kInvalidArgument;
        };
        self.values.borrow_mut()[parameter_slot(spec.id)] = value.clamp(0.0, 1.0);
        kResultOk
    }

    unsafe fn setComponentHandler(&self, _handler: *mut IComponentHandler) -> tresult {
        kResultOk
    }

    unsafe fn createView(&self, _name: *const c_char) -> *mut IPlugView {
        ptr::null_mut()
    }
}

impl IMidiMappingTrait for PianoController {
    unsafe fn getMidiControllerAssignment(
        &self,
        bus_index: i32,
        _channel: i16,
        midi_controller_number: CtrlNumber,
        id: *mut ParamID,
    ) -> tresult {
        if bus_index != 0 || id.is_null() {
            return kInvalidArgument;
        }
        let Some(parameter_id) = midi_mapping(midi_controller_number) else {
            return kResultFalse;
        };
        // SAFETY: validated non-null above.
        unsafe {
            *id = parameter_id as ParamID;
        }
        kResultTrue
    }
}

struct Factory {}

impl Class for Factory {
    type Interfaces = (IPluginFactory,);
}

impl IPluginFactoryTrait for Factory {
    unsafe fn getFactoryInfo(&self, info: *mut PFactoryInfo) -> tresult {
        if info.is_null() {
            return kInvalidArgument;
        }
        // SAFETY: validated non-null above.
        let info = unsafe { &mut *info };
        copy_cstring(PLUGIN_VENDOR, &mut info.vendor);
        copy_cstring(PLUGIN_URL, &mut info.url);
        copy_cstring(PLUGIN_EMAIL, &mut info.email);
        info.flags = PFactoryInfo_::FactoryFlags_::kUnicode as int32;
        kResultOk
    }

    unsafe fn countClasses(&self) -> i32 {
        2
    }

    unsafe fn getClassInfo(&self, index: i32, info: *mut PClassInfo) -> tresult {
        if info.is_null() {
            return kInvalidArgument;
        }
        // SAFETY: validated non-null above.
        let info = unsafe { &mut *info };
        match index {
            0 => {
                info.cid = PianoProcessor::CID;
                info.cardinality = PClassInfo_::ClassCardinality_::kManyInstances as int32;
                copy_cstring("Audio Module Class", &mut info.category);
                copy_cstring(PLUGIN_NAME, &mut info.name);
                kResultOk
            }
            1 => {
                info.cid = PianoController::CID;
                info.cardinality = PClassInfo_::ClassCardinality_::kManyInstances as int32;
                copy_cstring("Component Controller Class", &mut info.category);
                copy_cstring(PLUGIN_NAME, &mut info.name);
                kResultOk
            }
            _ => kInvalidArgument,
        }
    }

    unsafe fn createInstance(
        &self,
        cid: FIDString,
        iid: FIDString,
        obj: *mut *mut c_void,
    ) -> tresult {
        if cid.is_null() || iid.is_null() || obj.is_null() {
            return kInvalidArgument;
        }

        // SAFETY: validated non-null above, host provides 16-byte class IDs.
        let instance = unsafe {
            match *(cid as *const TUID) {
                PianoProcessor::CID => Some(
                    ComWrapper::new(PianoProcessor::new())
                        .to_com_ptr::<FUnknown>()
                        .unwrap(),
                ),
                PianoController::CID => Some(
                    ComWrapper::new(PianoController::new())
                        .to_com_ptr::<FUnknown>()
                        .unwrap(),
                ),
                _ => None,
            }
        };

        if let Some(instance) = instance {
            let ptr = instance.as_ptr();
            // SAFETY: `ptr` is a live COM instance, `iid` and `obj` are host-provided pointers.
            unsafe { ((*(*ptr).vtbl).queryInterface)(ptr, iid as *mut TUID, obj) }
        } else {
            kInvalidArgument
        }
    }
}

#[cfg(target_os = "linux")]
#[unsafe(no_mangle)]
extern "system" fn ModuleEntry(_library_handle: *mut c_void) -> bool {
    true
}

#[cfg(target_os = "linux")]
#[unsafe(no_mangle)]
extern "system" fn ModuleExit() -> bool {
    true
}

#[unsafe(no_mangle)]
extern "system" fn GetPluginFactory() -> *mut IPluginFactory {
    ComWrapper::new(Factory {})
        .to_com_ptr::<IPluginFactory>()
        .unwrap()
        .into_raw()
}
