#include "miniaudio_class.h"

#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

// Registering methods such that GDScript sees them
void MiniaudioClass::_bind_methods() {
	ClassDB::bind_method(
		D_METHOD("start"),
		&MiniaudioClass::start
	);

	ClassDB::bind_method(
		D_METHOD("stop"),
		&MiniaudioClass::stop
	);

	ClassDB::bind_method(
		D_METHOD("get_samples"),
		&MiniaudioClass::get_samples
	);

	ClassDB::bind_method(
		D_METHOD("is_capturing"),
		&MiniaudioClass::is_capturing
	);

	ClassDB::bind_method(
	    D_METHOD("get_fft", "fft_size", "apply_window", "log_scale", "channel"),
	    &MiniaudioClass::get_fft
	);
}

MiniaudioClass::MiniaudioClass() {

}

MiniaudioClass::~MiniaudioClass() {
	stop();
	if (fft_setup) {
	    pffft_destroy_setup(fft_setup);
	    fft_setup = nullptr;
	}
}

// Begins system audio capture
void MiniaudioClass::start() {
	if (capturing) return;

	UtilityFunctions::print("Initializing context...");
	
	// Use PipeWire directly — PulseAudio compat layer can deadlock on init
	// ma_backend backends[] = { ma_backend_pipewire };
	// ma_result result = ma_context_init(backends, 1, NULL, &context);
	// UtilityFunctions::print("Context OK");
	if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
		UtilityFunctions::printerr("Context init failed");
		return;
	}
	
	// if (result != MA_SUCCESS) {
	// 	// Fallback: let miniaudio auto-select
	// 	UtilityFunctions::print("PipeWire failed, trying auto...");
	// 	result = ma_context_init(NULL, 0, NULL, &context);
	// 	if (result != MA_SUCCESS) {
	// 		UtilityFunctions::printerr("Context init failed");
	// 		return;
	// 	}
	// }
	UtilityFunctions::print("Context OK");

#ifdef _WIN32
	ma_device_config config = ma_device_config_init(ma_device_type_loopback);
#else
	UtilityFunctions::print("Selecting device...");
	select_system_audio_device();
	UtilityFunctions::print("Device selected: " + String(deviceSelected ? "yes" : "no"));
	ma_device_config config = ma_device_config_init(ma_device_type_capture);
#endif

	config.capture.format   = ma_format_f32;
	config.capture.channels = 2;
	config.sampleRate       = 48000;
	config.dataCallback     = data_callback;
	config.pUserData        = this;

#ifndef _WIN32
	if (deviceSelected) {
		config.capture.pDeviceID = &selectedDeviceId;
	}
#endif

	UtilityFunctions::print("Initializing device...");
	if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
		UtilityFunctions::printerr("Device init failed");
		ma_context_uninit(&context);
		return;
	}
	UtilityFunctions::print("Device OK");

	if (ma_device_start(&device) != MA_SUCCESS) {
		UtilityFunctions::printerr("Device start failed");
		ma_device_uninit(&device);
		ma_context_uninit(&context);
		return;
	}

	capturing = true;
	UtilityFunctions::print("Capture started");
}

// Halt system audio capture
void MiniaudioClass::stop() {
	if (!capturing) {
		return;
	}

	ma_device_uninit(&device);
	ma_context_uninit(&context);

	capturing = false;
}

// audio callback
void MiniaudioClass::data_callback(
	ma_device* device,
	void* output,
	const void* input,
	ma_uint32 frame_count
) {
	// UtilityFunctions::print("Callback fired: " + String::num_int64(frame_count)); // add this
    auto* self = static_cast<MiniaudioClass*>(device->pUserData);
    const float* samples = static_cast<const float*>(input);
    self->push_samples(samples, frame_count * 2);
}

#ifndef _WIN32
void MiniaudioClass::select_system_audio_device() {
    ma_device_info* pCaptureDevices = nullptr;
    ma_uint32 captureDeviceCount = 0;

    ma_context_get_devices(&context, NULL, NULL, &pCaptureDevices, &captureDeviceCount);

    // Get the running sink ALSA name
    String runningSinkName;
    FILE* pipe = popen("pactl list sinks short | awk '/RUNNING/ {print $2}'", "r");
    if (pipe) {
        char buf[256] = {};
        if (fgets(buf, sizeof(buf), pipe)) {
            runningSinkName = String(buf).strip_edges();
        }
        pclose(pipe);
    }
    UtilityFunctions::print("Running sink: " + runningSinkName);

    // Extract the suffix after "pro-output-" if present (e.g. "7")
    // For non-pro-output sinks, we fall back to substring matching
    String outputSuffix;
    int proIdx = runningSinkName.find("pro-output-");
    if (proIdx >= 0) {
        outputSuffix = runningSinkName.substr(proIdx + 11); // after "pro-output-"
        UtilityFunctions::print("Looking for monitor with suffix: " + outputSuffix);
    }

    ma_uint32 fallbackIndex = UINT32_MAX;

    for (ma_uint32 i = 0; i < captureDeviceCount; i++) {
        String name = String(pCaptureDevices[i].name);
        String lower = name.to_lower();
        if (lower.find("monitor") < 0 || lower.find("webcam") >= 0) continue;

        // Match "Monitor of ... Pro 7" by checking the suffix number at end of name
        if (!outputSuffix.is_empty() && name.ends_with(" " + outputSuffix)) {
            selectedDeviceId = pCaptureDevices[i].id;
            deviceSelected = true;
            UtilityFunctions::print("Selected (suffix match): " + name);
            return;
        }

        if (fallbackIndex == UINT32_MAX) fallbackIndex = i;
    }

    // Fallback
    if (fallbackIndex != UINT32_MAX) {
        selectedDeviceId = pCaptureDevices[fallbackIndex].id;
        deviceSelected = true;
        UtilityFunctions::print("Selected (fallback): " + String(pCaptureDevices[fallbackIndex].name));
    }
}
#endif

// Stores samples
void MiniaudioClass::push_samples(
	const float* data,
	int count
) {
	std::lock_guard<std::mutex> lock(buffer_mutex);

	for (int i = 0; i < count; i++) {
		buffer.push_back(data[i]);
	}

	const size_t max_size = 48000 * 2; // ~1s

	if (buffer.size() > max_size) {
		buffer.erase(
			buffer.begin(),
			buffer.begin() + (buffer.size() - max_size)
		);
	}
}

// Returns samples
PackedFloat32Array MiniaudioClass::get_samples() {
	std::lock_guard<std::mutex> lock(buffer_mutex);

	PackedFloat32Array out;

	out.resize(buffer.size());

	for (size_t i = 0; i < buffer.size(); i++) {
		out[i] = buffer[i];
	}

	return out;
}

bool MiniaudioClass::is_capturing() const {
	return capturing;
}

void MiniaudioClass::ensure_fft_setup(int fft_size) {
    if (fft_setup && fft_setup_size == fft_size) return;

    if (fft_setup) {
        pffft_destroy_setup(fft_setup);
    }

    fft_setup = pffft_new_setup(fft_size, PFFFT_REAL);
    fft_setup_size = fft_size;

    fft_input.resize(fft_size);
    fft_output.resize(fft_size);
    fft_work.resize(fft_size);
}

PackedFloat32Array MiniaudioClass::get_fft(
    int fft_size,
    bool apply_window,
    bool log_scale,
    int channel         // 0 = mono mix, 1 = left, 2 = right
) {
    // fft_size must be a multiple of 32 for pffft
    // valid: 256, 512, 1024, 2048
    ensure_fft_setup(fft_size);

    // --- copy from ring buffer ---
    {
        std::lock_guard<std::mutex> lock(buffer_mutex);

        int needed = fft_size * 2; // stereo frames
        if ((int)buffer.size() < needed) {
            // not enough data yet — return empty
            return PackedFloat32Array();
        }

        int offset = (int)buffer.size() - needed; // take the most recent samples

        for (int i = 0; i < fft_size; i++) {
            float l = buffer[offset + i * 2];
            float r = buffer[offset + i * 2 + 1];
            if (channel == 1)
                fft_input[i] = l;
            else if (channel == 2)
                fft_input[i] = r;
            else
                fft_input[i] = (l + r) * 0.5f;
        }
    }

    // --- Hann window ---
    if (apply_window) {
        for (int i = 0; i < fft_size; i++) {
	        float w = 0.5f * (1.0f - cosf(
	            6.28318530717958647f * i / (float)(fft_size - 1)
	        ));	
            fft_input[i] *= w;
        }
    }

    // --- pffft real forward transform ---
    // pffft requires 16-byte aligned buffers — pffft_aligned_malloc handles this,
    // but std::vector is fine on most platforms; use pffft_aligned_malloc if you
    // see corruption on ARM
    pffft_transform_ordered(
        fft_setup,
        fft_input.data(),
        fft_output.data(),
        fft_work.data(),
        PFFFT_FORWARD
    );

    // --- compute magnitudes for bins 1..N/2-1, skip DC (bin 0) ---
    int num_bins = fft_size / 2;
    PackedFloat32Array magnitudes;
    magnitudes.resize(num_bins);

    // pffft output layout for real transform (ordered):
    // [0]       = DC (bin 0), purely real
    // [1]       = Nyquist, purely real
    // [2k], [2k+1] = real/imag for bin k, k=1..N/2-1
    magnitudes[0] = 0.0f; // skip DC

    for (int k = 1; k < num_bins; k++) {
        float re = fft_output[k * 2];
        float im = fft_output[k * 2 + 1];
        float mag = sqrtf(re * re + im * im) / (float)fft_size; // normalize by N
        magnitudes[k] = log_scale ? log2f(mag + 1.0f) : mag;
    }

    return magnitudes;
}