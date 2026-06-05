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
}

MiniaudioClass::MiniaudioClass() {

}

MiniaudioClass::~MiniaudioClass() {
	stop();
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
	auto* self =
		static_cast<MiniaudioClass*>(device->pUserData);

	const float* samples =
		static_cast<const float*>(input);

	self->push_samples(samples, frame_count * 2);
}

void MiniaudioClass::select_system_audio_device() {
    ma_device_info* pCaptureDevices = nullptr;
    ma_uint32 captureDeviceCount = 0;

    // Note: local variables — do NOT store the pointer, copy the ID
    ma_result result = ma_context_get_devices(
        &context,
        NULL, NULL,                          // skip playback
        &pCaptureDevices, &captureDeviceCount
    );

    if (result != MA_SUCCESS) {
        UtilityFunctions::printerr("Failed to enumerate capture devices");
        return;
    }

    UtilityFunctions::print("Capture devices found: " + String::num_int64(captureDeviceCount));

    for (ma_uint32 i = 0; i < captureDeviceCount; i++) {
        // Safety check — name may not be null-terminated if malformed
        if (pCaptureDevices[i].name[0] == '\0') continue;

        String name = String(pCaptureDevices[i].name);
        UtilityFunctions::print("  [" + String::num_int64(i) + "] " + name);

        String lower = name.to_lower();
        if (lower.find("monitor") >= 0) {
            selectedDeviceId = pCaptureDevices[i].id;  // copy by value
            deviceSelected = true;
            UtilityFunctions::print("Selected monitor: " + name);
            return;
        }
    }

    UtilityFunctions::printerr("No .monitor device found — will use default capture");
    // Don't leave deviceSelected false if you want a fallback;
    // just let it fall through and miniaudio uses the default device
}

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
