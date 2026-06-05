#include "miniaudio_class.h"
#include "godot_cpp/variant/utility_functions.hpp"
#include <cmath>
#include <algorithm>

using namespace godot;

// ─── Godot binding ────────────────────────────────────────────────────────────

void MiniaudioClass::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start"),        &MiniaudioClass::start);
    ClassDB::bind_method(D_METHOD("stop"),         &MiniaudioClass::stop);
    ClassDB::bind_method(D_METHOD("get_samples"),  &MiniaudioClass::get_samples);
    ClassDB::bind_method(D_METHOD("is_capturing"), &MiniaudioClass::is_capturing);
    ClassDB::bind_method(
        D_METHOD("get_fft", "fft_size", "apply_window", "log_scale", "channel"),
        &MiniaudioClass::get_fft);
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

MiniaudioClass::MiniaudioClass() {
    buffer.resize(MAX_BUFFER_SIZE, 0.0f);
}

MiniaudioClass::~MiniaudioClass() {
    stop();
    if (fft_setup) {
        pffft_destroy_setup(fft_setup);
        pffft_aligned_free(fft_input);
        pffft_aligned_free(fft_output);
        pffft_aligned_free(fft_work);
    }
}

// ─── Device lifecycle ─────────────────────────────────────────────────────────

void MiniaudioClass::start() {
    if (capturing) return;

    UtilityFunctions::print("1: context init...");
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        UtilityFunctions::printerr("Context init failed");
        return;
    }
    UtilityFunctions::print("2: context ok");

#ifdef _WIN32
    ma_device_config config = ma_device_config_init(ma_device_type_loopback);
#else
	UtilityFunctions::print("3: selecting device...");
    select_system_audio_device();
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    UtilityFunctions::print("4: device selected");
#endif

    config.capture.format   = ma_format_f32;
    config.capture.channels = 2;
    config.sampleRate       = 48000;
    config.dataCallback     = data_callback;
    config.pUserData        = this;

#ifndef _WIN32
    if (deviceSelected)
        config.capture.pDeviceID = &selectedDeviceId;
#endif

	UtilityFunctions::print("5: device init...");
    if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
        UtilityFunctions::printerr("Device init failed");
        ma_context_uninit(&context);
        return;
    }

    UtilityFunctions::print("6: device start...");
    if (ma_device_start(&device) != MA_SUCCESS) {
        UtilityFunctions::printerr("Device start failed");
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        return;
    }

    UtilityFunctions::print("7: capturing!");
    capturing = true;
}

void MiniaudioClass::stop() {
    if (!capturing) return;
    ma_device_uninit(&device);
    ma_context_uninit(&context);
    capturing = false;
}

// ─── Audio callback ───────────────────────────────────────────────────────────

void MiniaudioClass::data_callback(ma_device* device, void* /*output*/,
                                   const void* input, ma_uint32 frame_count) {
    auto* self = static_cast<MiniaudioClass*>(device->pUserData);
    self->push_samples(static_cast<const float*>(input),
                       static_cast<int>(frame_count) * 2);
}

// ─── Ring buffer write ────────────────────────────────────────────────────────

void MiniaudioClass::push_samples(const float* data, int count) {
    std::lock_guard<std::mutex> lock(buffer_mutex);

    // If incoming chunk is larger than the whole buffer, keep only the tail
    if (static_cast<size_t>(count) > MAX_BUFFER_SIZE) {
        data += count - static_cast<int>(MAX_BUFFER_SIZE);
        count = static_cast<int>(MAX_BUFFER_SIZE);
    }

    const size_t space_to_end = MAX_BUFFER_SIZE - write_index;

    if (static_cast<size_t>(count) <= space_to_end) {
        std::copy(data, data + count, buffer.data() + write_index);
        write_index += count;
        if (write_index == MAX_BUFFER_SIZE) {
            write_index    = 0;
            buffer_wrapped = true;
        }
    } else {
        // Split across the wrap point
        std::copy(data,                    data + space_to_end,
                  buffer.data() + write_index);
        std::copy(data + space_to_end,     data + count,
                  buffer.data());
        write_index    = static_cast<size_t>(count) - space_to_end;
        buffer_wrapped = true;
    }
}

// ─── Ring buffer read ─────────────────────────────────────────────────────────

PackedFloat32Array MiniaudioClass::get_samples() {
    std::lock_guard<std::mutex> lock(buffer_mutex);

    const size_t total = buffer_wrapped ? MAX_BUFFER_SIZE : write_index;
    if (total == 0) return {};

    PackedFloat32Array out;
    out.resize(static_cast<int>(total));
    float* dst = out.ptrw();

    if (!buffer_wrapped) {
        std::copy(buffer.data(), buffer.data() + write_index, dst);
    } else {
        // Oldest samples start at write_index
        const size_t part1 = MAX_BUFFER_SIZE - write_index;
        std::copy(buffer.data() + write_index, buffer.data() + MAX_BUFFER_SIZE, dst);
        std::copy(buffer.data(),               buffer.data() + write_index,     dst + part1);
    }

    return out;
}

bool MiniaudioClass::is_capturing() const {
    return capturing;
}

// ─── Platform: Linux/macOS device selection ───────────────────────────────────

#ifndef _WIN32
void MiniaudioClass::select_system_audio_device() {
    ma_device_info* capture_devices = nullptr;
    ma_uint32       capture_count   = 0;
    ma_context_get_devices(&context, NULL, NULL, &capture_devices, &capture_count);

    // Ask PulseAudio which sink is RUNNING
    String running_sink;
    if (FILE* pipe = popen("pactl list sinks short | awk '/RUNNING/ {print $2}'", "r")) {
        char buf[256] = {};
        if (fgets(buf, sizeof(buf), pipe))
            running_sink = String(buf).strip_edges();
        pclose(pipe);
    }

    // Extract the output-index suffix from "pro-output-N"
    String output_suffix;
    int idx = running_sink.find("pro-output-");
    if (idx >= 0)
        output_suffix = running_sink.substr(idx + 11);

    ma_uint32 fallback = UINT32_MAX;

    for (ma_uint32 i = 0; i < capture_count; ++i) {
        const String name  = String(capture_devices[i].name);
        const String lower = name.to_lower();

        // Only consider monitor sources (loopback), skip webcams
        if (lower.find("monitor") < 0 || lower.find("webcam") >= 0)
            continue;

        if (!output_suffix.is_empty() && name.ends_with(" " + output_suffix)) {
            selectedDeviceId = capture_devices[i].id;
            deviceSelected   = true;
            return;
        }

        if (fallback == UINT32_MAX)
            fallback = i;
    }

    if (fallback != UINT32_MAX) {
        selectedDeviceId = capture_devices[fallback].id;
        deviceSelected   = true;
    }
}
#endif

// ─── FFT setup (cached per size) ─────────────────────────────────────────────

void MiniaudioClass::ensure_fft_setup(int fft_size) {
    if (fft_setup && fft_setup_size == fft_size) return; // already ready

    // Tear down old state
    if (fft_setup) {
        pffft_destroy_setup(fft_setup);
        pffft_aligned_free(fft_input);
        pffft_aligned_free(fft_output);
        pffft_aligned_free(fft_work);
    }

    fft_setup      = pffft_new_setup(fft_size, PFFFT_REAL);
    fft_setup_size = fft_size;

    // pffft REQUIRES aligned allocations — never use plain new/vector here
    fft_input  = static_cast<float*>(pffft_aligned_malloc(fft_size * sizeof(float)));
    fft_output = static_cast<float*>(pffft_aligned_malloc(fft_size * sizeof(float)));
    fft_work   = static_cast<float*>(pffft_aligned_malloc(fft_size * sizeof(float)));

    // Staging buffer: interleaved stereo frames needed for one FFT pass
    local_chunk.resize(fft_size * 2);

    // Hann window — computed once, reused every frame
    fft_window.resize(fft_size);
    const float inv = 1.0f / static_cast<float>(fft_size - 1);
    for (int i = 0; i < fft_size; ++i)
        fft_window[i] = 0.5f * (1.0f - cosf(6.28318530718f * i * inv));
}

// ─── Public FFT entry point ───────────────────────────────────────────────────

PackedFloat32Array MiniaudioClass::get_fft(int fft_size, bool apply_window,
                                           bool log_scale, int channel) {
    ensure_fft_setup(fft_size);

    const int samples_needed = fft_size * 2; // interleaved stereo

    // ── 1. Copy latest samples out of the ring buffer (short critical section) ──
    {
        std::lock_guard<std::mutex> lock(buffer_mutex);

        if (!buffer_wrapped && write_index < static_cast<size_t>(samples_needed))
            return {}; // not enough data yet

        const int read_start = static_cast<int>(write_index) - samples_needed;

        if (read_start >= 0) {
            // Contiguous — single copy
            std::copy(buffer.data() + read_start,
                      buffer.data() + write_index,
                      local_chunk.data());
        } else {
            // Wraps around the ring: [end part] ++ [start part]
            const int tail = -read_start;              // samples from the end
            const int head = samples_needed - tail;    // samples from the start
            std::copy(buffer.data() + MAX_BUFFER_SIZE - tail,
                      buffer.data() + MAX_BUFFER_SIZE,
                      local_chunk.data());
            std::copy(buffer.data(),
                      buffer.data() + head,
                      local_chunk.data() + tail);
        }
    }

    // ── 2. Deinterleave channel into fft_input ────────────────────────────────
    //
    // Pointer arithmetic once outside the loop is faster than index * 2 per iter.
    {
        const float* src = local_chunk.data();
        float*       dst = fft_input;

        if (channel == 1) {
            for (int i = 0; i < fft_size; ++i, src += 2)
                dst[i] = src[0];
        } else if (channel == 2) {
            for (int i = 0; i < fft_size; ++i, src += 2)
                dst[i] = src[1];
        } else {
            // Mix both channels
            for (int i = 0; i < fft_size; ++i, src += 2)
                dst[i] = (src[0] + src[1]) * 0.5f;
        }
    }

    // ── 3. Apply Hann window in-place ─────────────────────────────────────────
    if (apply_window) {
        const float* win = fft_window.data();
        float*       inp = fft_input;
        for (int i = 0; i < fft_size; ++i)
            inp[i] *= win[i];
    }

    // ── 4. Forward FFT ────────────────────────────────────────────────────────
    pffft_transform_ordered(fft_setup, fft_input, fft_output, fft_work, PFFFT_FORWARD);

    // ── 5. Compute magnitude spectrum ─────────────────────────────────────────
    //
    // Output layout from pffft (REAL, ordered):
    //   [0]           = DC  (purely real, no imaginary part stored)
    //   [1]           = Nyquist (purely real, packed into index 1)
    //   [2k], [2k+1]  = Re/Im of bin k, for k = 1 … N/2-1
    //
    // We skip DC (bin 0) — it's just the mean level, useless for visualisation.

    const int   num_bins = fft_size / 2;
    const float norm     = 1.0f / static_cast<float>(fft_size); // optional normalisation

    PackedFloat32Array magnitudes;
    magnitudes.resize(num_bins);
    float* mag_ptr = magnitudes.ptrw();

    mag_ptr[0] = 0.0f; // DC suppressed

    const float* out = fft_output;

    if (log_scale) {
        for (int k = 1; k < num_bins; ++k) {
            const float re  = out[k * 2];
            const float im  = out[k * 2 + 1];
            const float mag = (re * re + im * im) * norm;
            mag_ptr[k] = log2f(mag + 1.0f); // back to original behaviour
        }
    } else {
        // Linear magnitude (normalised)
        for (int k = 1; k < num_bins; ++k) {
            const float re = out[k * 2];
            const float im = out[k * 2 + 1];
            mag_ptr[k]     = sqrtf((re * re + im * im) * norm);
        }
    }

    return magnitudes;
}
