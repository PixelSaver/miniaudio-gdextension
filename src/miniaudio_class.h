#pragma once
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "miniaudio.h"
#include "pffft.h"
#include <vector>
#include <mutex>

using namespace godot;

class MiniaudioClass : public Node {
	GDCLASS(MiniaudioClass, Node)

private:
    ma_device device;
    ma_context context;
    bool capturing = false;
    std::vector<float> buffer;
    std::mutex buffer_mutex;
    
    // PFFFT state — cached per fft_size to avoid alloc every frame
    PFFFT_Setup* fft_setup = nullptr;
    int fft_setup_size = 0;
    std::vector<float> fft_input;
    std::vector<float> fft_output;
    std::vector<float> fft_work;

#ifndef _WIN32
    ma_device_id selectedDeviceId;
    bool deviceSelected = false;
#endif

    static void data_callback(
        ma_device* device,
        void* output,
        const void* input,
        ma_uint32 frame_count
    );
    void push_samples(const float* data, int count);
    void ensure_fft_setup(int fft_size);

#ifndef _WIN32
    void select_system_audio_device();
#endif

protected:
    static void _bind_methods();

public:
    MiniaudioClass();
    ~MiniaudioClass() override;

    void start();
    void stop();
    bool is_capturing() const;
    PackedFloat32Array get_samples();
    PackedFloat32Array get_fft(int fft_size, bool apply_window, bool log_scale, int channel);
};
