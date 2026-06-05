# miniaudio-gdextension

A GDExtension for Godot 4 that captures system audio on Windows and Linux using [miniaudio](https://miniaud.io/).

On Windows it uses WASAPI loopback. On Linux it automatically detects the active PulseAudio/PipeWire sink monitor.

## AI Disclaimer

All code in this repository was written with the help of generative artificial intelligence. The original code comes from the [godot-cpp template](https://github.com/godotengine/godot-cpp-template/tree/main)

## Usage

Add the node `MiniaudioClass` to your scene, then call it from GDScript:

```gdscript
var miniaudio: MiniaudioClass

func _ready():
    miniaudio = MiniaudioClass.new()
    add_child(miniaudio)
    miniaudio.start()

func _process(_delta):
    if miniaudio.is_capturing():
        var samples: PackedFloat32Array = miniaudio.get_samples()
        # samples is interleaved stereo f32 at 48000hz
```

### API

| Method | Returns | Description |
|---|---|---|
| `start()` | `void` | Begin capturing system audio |
| `stop()` | `void` | Stop capturing and free resources |
| `is_capturing()` | `bool` | Whether capture is currently active |
| `get_samples()` | `PackedFloat32Array` | Returns buffered interleaved stereo f32 samples at 48kHz, up to ~1 second |

## Installation

Download the latest release zip and extract it into your project's `addons/` folder:

```
your-godot-project/
  addons/
    miniaudio/
      miniaudio.gdextension
      bin/
        libminiaudio.linux.debug.x86_64.so
        libminiaudio.windows.debug.x86_64.dll
        ...
```

Godot will automatically load the extension on next launch.

## Building from Source

### Prerequisites

**All platforms:**
- Python 3.6+
- SCons 4.0+

**Linux only:**
```shell
sudo apt-get install libasound2-dev libpulse-dev
```

### Build

```shell
git clone --recurse-submodules https://github.com/you/miniaudio-gdextension
cd miniaudio-gdextension
scons platform=linux target=template_debug   # Linux
scons platform=windows target=template_debug # Windows (cross-compile or native)
```

Compiled binaries are output to `bin/`.

## CI / Releases

This repository uses GitHub Actions to build for all supported platforms. To trigger a release build:

1. Go to the **Actions** tab on GitHub
2. Select **Make a GDExtension build for all supported platforms**
3. Click **Run workflow**

After it completes, download `godot-cpp-template.zip` from the **Artifacts** section of the workflow run.

## Platform Support

| Platform | Architecture | Status |
|---|---|---|
| Linux | x86_64, x86_32, arm64, arm32 | ✅ |
| Windows | x86_64, x86_32, arm64 | ✅ |
| macOS | universal | ⚠️ untested |
| Android | x86_64, x86_32, arm64, arm32 | ❌ not supported |
| iOS | arm64 | ❌ not supported |
| Web | wasm32 | ❌ not supported |

System audio capture requires OS-level loopback support. Mobile, web, and sandboxed platforms do not support this.

## How It Works

- **Windows** — uses `ma_device_type_loopback` via WASAPI, which natively captures all system audio
- **Linux** — enumerates PulseAudio/PipeWire capture devices, queries the active running sink via `pactl`, and selects its monitor source automatically

## License

MIT
