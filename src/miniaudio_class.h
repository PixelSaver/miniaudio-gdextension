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
	static constexpr size_t MAX_BUFFER_SIZE = 48000 * 2;
	
    ma_device device = {};
    ma_context context = {};
    bool capturing = false;
    
    std::vector<float> buffer;
    size_t write_index = 0;
    bool buffer_wrapped = false;
    std::mutex buffer_mutex;
    
    // PFFFT state — cached per fft_size to avoid alloc every frame
    PFFFT_Setup* fft_setup = nullptr;
    int fft_setup_size = 0;
    float* fft_input = nullptr;
    float* fft_output = nullptr;
    float* fft_work = nullptr;
    
    std::vector<float> fft_window;
    std::vector<float> local_chunk;

#ifndef _WIN32
    ma_device_id selectedDeviceId = {};
    bool deviceSelected = false;
    void select_system_audio_device();
#endif

    static void data_callback(
        ma_device* device,
        void* output,
        const void* input,
        ma_uint32 frame_count
    );
    void push_samples(const float* data, int count);
    void ensure_fft_setup(int fft_size);

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
