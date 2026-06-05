#pragma once

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"

#include "miniaudio.h"

#include <vector>
#include <mutex>

using namespace godot;

class MiniaudioClass : public Node {
	GDCLASS(MiniaudioClass, Node)

private:
	ma_device device;
	ma_context context;
#ifndef _WIN32
	void select_system_audio_device();
    ma_device_id selectedDeviceId;
    bool deviceSelected = false;
#endif
	bool capturing = false;

	std::vector<float> buffer;
	std::mutex buffer_mutex;

	static void data_callback(
		ma_device* device,
		void* output,
		const void* input,
		ma_uint32 frame_count
	);

	void push_samples(const float* data, int count);
	
protected:
	static void _bind_methods();

public:
	MiniaudioClass();
	~MiniaudioClass() override;
	
	void start();
	void stop();

	bool is_capturing() const;

	PackedFloat32Array get_samples();
};
