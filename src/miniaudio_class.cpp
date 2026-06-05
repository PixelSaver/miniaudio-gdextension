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
	if (capturing) {
		return;
	}
	
	ma_backend backends[] = {
		ma_backend_wasapi,
		ma_backend_pulseaudio,
		ma_backend_alsa
	};
	
	if (ma_context_init(backends, 4, NULL, &context) != MA_SUCCESS) {
		UtilityFunctions::printerr("Context init failed");
		return;
	}

	ma_result result = ma_context_init(
		backends,
		4,
		NULL,
		&context
	);

	#ifdef _WIN32
	ma_device_config config =
		ma_device_config_init(ma_device_type_loopback);
	#else // Linux
	select_system_audio_device();

	ma_device_config config =
		ma_device_config_init(ma_device_type_capture);
	#endif
	config.capture.format = ma_format_f32;
	config.capture.channels = 2;
	config.sampleRate = 48000;

	config.dataCallback = data_callback;
	config.pUserData = this;
	
	#ifndef _WIN32
	if (deviceSelected) {
		config.capture.pDeviceID = &selectedDeviceId;
	}
	#endif

	if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
  UtilityFunctions::printerr("Device init failed");
  return;
	}
	
	if (ma_device_start(&device) != MA_SUCCESS) {
  UtilityFunctions::printerr("Device start failed");
  return;
	}
	
	capturing = true;
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
    ma_result result = ma_context_get_devices(
        &context,
        &pPlaybackDevices,
        &playbackDeviceCount,
        NULL,
        NULL
    );

    if (result != MA_SUCCESS) {
        UtilityFunctions::printerr("Failed to get devices");
        return;
    }

    for (ma_uint32 i = 0; i < playbackDeviceCount; i++) {
        const ma_device_info& info = pPlaybackDevices[i];

        String name = info.name;

        // LINUX: look for monitor devices
        if (name.find(".monitor") >= 0 ||
            name.find("Monitor") >= 0) {

            selectedDeviceId = info.id;
            deviceSelected = true;

            UtilityFunctions::print("Selected system audio monitor: " + name);
            return;
        }
    }

    UtilityFunctions::printerr("No system audio monitor found");
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