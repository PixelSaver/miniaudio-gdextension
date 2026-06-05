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
	
	ma_device_config config =
		ma_device_config_init(ma_device_type_capture);
		// ma_device_config_init(ma_device_type_loopback);

	config.capture.format = ma_format_f32;
	config.capture.channels = 2;
	config.sampleRate = 48000;

	config.dataCallback = data_callback;
	config.pUserData = this;

	ma_result result =
		ma_device_init(NULL, &config, &device);

	if (result != MA_SUCCESS) {
		UtilityFunctions::printerr(
			vformat(
				"Miniaudio init failed: %d",
				result
			)
		);

		return;
	}

	ma_device_start(&device);

	capturing = true;
}

// Halt system audio capture
void MiniaudioClass::stop() {
	if (!capturing) {
		return;
	}

	ma_device_uninit(&device);

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