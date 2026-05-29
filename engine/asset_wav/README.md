# cd::asset_wav

**Purpose**: Minimal RIFF/WAVE PCM audio loader. Parses standard .wav files (44.1kHz to 48kHz PCM, mono/stereo) into raw audio buffers. No external codec libraries (no libvorbis, FLAC); PCM only.

**Namespace**: `cd::asset_wav`.

**Public Headers**:
- `cd/asset_wav/Wav.hpp` — WAVE parser; sample rate, channel count, PCM data.
- `cd/asset_wav/AssetLoader.hpp` — async WAV asset loader interface.

**Primary Types**:
- `WavHeader` — parsed RIFF/WAVE header (sample rate, bits per sample, channel count, frame count).
- `WavPcm` — in-memory PCM data (int16 or float32 samples; mono/stereo).
- `WavLoader` — async loader; file queue + worker threads.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_asset_wav
ctest --preset ninja-debug -R asset_wav --output-on-failure
```

**Dependencies**: cd::core.

**Format Support**:
- **Container**: RIFF/WAVE with fmt + data chunks.
- **PCM Formats**: 16-bit int (signed), 24-bit int, 32-bit float.
- **Channels**: Mono, stereo; multichannel support planned.
- **Sample Rates**: 44.1kHz, 48kHz, 96kHz typical.

**Notes**:
- PCM-only; compressed formats (MP3, OGG) not supported (use separate audio library).
- For game audio, WAV is the interchange format (tools produce WAV; engine streams via audio system).
- Endianness: little-endian (standard RIFF).
- Real-time audio playback via cd::audio (which consumes WavPcm buffers).
