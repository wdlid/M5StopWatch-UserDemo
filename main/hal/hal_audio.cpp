/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings/settings.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mooncake_log.h>
#include <driver/i2s_std.h>
#include <esp_dsp.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

static const std::string_view _tag = "HAL-Audio";

#define I2S_PORT         I2S_NUM_0
#define I2S_MCLK_PIN     (gpio_num_t)18
#define I2S_BCLK_PIN     (gpio_num_t)17
#define I2S_DADC_IN_PIN  (gpio_num_t)16
#define I2S_LRCK_PIN     (gpio_num_t)15
#define I2S_DDAC_OUT_PIN (gpio_num_t)21

static class AudioCodec {
public:
    static constexpr int sample_rate       = 44100;
    static constexpr int spectrum_fft_size = 512;
    static constexpr int spectrum_hop_size = 256;
    // PSRAM streaming ring buffer capacity in samples (~25 seconds at 44.1kHz = 1,102,500 samples)
    static constexpr std::size_t stream_ring_capacity = 1102500;

    void init(i2c_master_bus_handle_t i2c_bus)
    {
        _silence_buffer.resize(sample_rate * 0.1);
        _silence_buffer.assign(_silence_buffer.size(), 0);

        _stream_ring = static_cast<int16_t*>(
            heap_caps_malloc(stream_ring_capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (_stream_ring == nullptr) {
            static constexpr std::size_t fallback_capacity = 441000;  // ~10 seconds
            _stream_ring = static_cast<int16_t*>(
                heap_caps_malloc(fallback_capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (_stream_ring != nullptr) {
                _stream_capacity = fallback_capacity;
            } else {
                _stream_ring     = static_cast<int16_t*>(malloc(32768 * sizeof(int16_t)));
                _stream_capacity = _stream_ring ? 32768 : 0;
            }
        } else {
            _stream_capacity = stream_ring_capacity;
        }

        _spectrum_init();
        xTaskCreate([](void* obj) { static_cast<AudioCodec*>(obj)->_task_entry(); }, "audio_task", 6 * 1024, this, 6,
                    &_task_handle);

        _i2s_init();

        audio_codec_i2s_cfg_t i2s_cfg = {
            .rx_handle = _rx_handle,
            .tx_handle = _tx_handle,
        };
        _data_if = audio_codec_new_i2s_data(&i2s_cfg);

        audio_codec_i2c_cfg_t i2c_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus};
        _ctrl_if                      = audio_codec_new_i2c_ctrl(&i2c_cfg);

        _gpio_if = audio_codec_new_gpio();

        es8311_codec_cfg_t es8311_cfg = {
            .ctrl_if     = _ctrl_if,
            .gpio_if     = _gpio_if,
            .codec_mode  = ESP_CODEC_DEV_WORK_MODE_BOTH,
            .pa_pin      = GPIO_NUM_NC,
            .pa_reverted = false,
            .use_mclk    = true,
        };
        _codec_if = es8311_codec_new(&es8311_cfg);

        esp_codec_dev_cfg_t dev_cfg = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
            .codec_if = _codec_if,
            .data_if  = _data_if,
        };
        _codec_dev = esp_codec_dev_new(&dev_cfg);

        esp_codec_dev_set_in_gain(_codec_dev, 32.0);
        _current_in_gain = 32.0f;

        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel         = 1,
            .sample_rate     = sample_rate,
        };
        esp_codec_dev_open(_codec_dev, &fs);

        // Calibrated psychoacoustic volume curve:
        // Default linear mapping (-50dB to 0dB) sets default vol 80 to -10dB (too quiet).
        // Mapping: 0 -> -50dB, 40 -> -16dB, 60 -> -9dB, 80 -> -3dB, 100 -> 0dB (clean 100% full scale DAC output).
        static esp_codec_dev_vol_map_t vol_map[] = {
            {.vol = 0,   .db_value = -50.0f},
            {.vol = 20,  .db_value = -28.0f},
            {.vol = 40,  .db_value = -16.0f},
            {.vol = 60,  .db_value = -9.0f},
            {.vol = 80,  .db_value = -3.0f},
            {.vol = 100, .db_value = 0.0f},
        };
        esp_codec_dev_vol_curve_t vol_curve = {
            .vol_map = vol_map,
            .count   = sizeof(vol_map) / sizeof(vol_map[0]),
        };
        esp_codec_dev_set_vol_curve(_codec_dev, &vol_curve);
    }

    void updateSpectrum()
    {
        if (_spectrum_available == false) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(_play_mutex);
            if (_is_playing || _stream_count > 0 || _stream_prebuffering || _stream_active) {
                return;
            }
        }

        if (_read_spectrum_hop() == false) {
            return;
        }

        if (_spectrum_samples_ready < spectrum_fft_size) {
            return;
        }

        std::lock_guard<std::mutex> lock(_spectrum_frame_mutex);
        _process_spectrum_frame(_audio_spectrum_frame);
    }

    void feedSpectrum(const int16_t* pcm, std::size_t count)
    {
        if (!_spectrum_available || count == 0 || pcm == nullptr) {
            return;
        }
        std::size_t n = std::min<std::size_t>(count, spectrum_fft_size);
        std::move(_spectrum_time_domain.begin() + n, _spectrum_time_domain.end(), _spectrum_time_domain.begin());
        const std::size_t src_offset = count >= spectrum_fft_size ? (count - n) : 0;
        for (std::size_t i = 0; i < n; ++i) {
            _spectrum_time_domain[spectrum_fft_size - n + i] =
                static_cast<float>(pcm[src_offset + i]) / 32768.0f;
        }
        _spectrum_samples_ready = spectrum_fft_size;
        std::lock_guard<std::mutex> lock(_spectrum_frame_mutex);
        _process_spectrum_frame(_audio_spectrum_frame);
    }

    Hal::AudioSpectrumFrame getSpectrum()
    {
        std::lock_guard<std::mutex> lock(_spectrum_frame_mutex);
        return _audio_spectrum_frame;
    }

    void setVolume(int volume)
    {
        std::lock_guard<std::mutex> lock(_ctrl_mutex);
        esp_codec_dev_set_out_vol(_codec_dev, volume);
    }

    int getVolume()
    {
        std::lock_guard<std::mutex> lock(_ctrl_mutex);
        int volume = 0;
        esp_codec_dev_get_out_vol(_codec_dev, &volume);
        return volume;
    }

    void setMicGain(float gain)
    {
        std::lock_guard<std::mutex> lock(_ctrl_mutex);
        if (std::fabs(_current_in_gain - gain) > 0.1f) {
            esp_codec_dev_set_in_gain(_codec_dev, gain);
            _current_in_gain = gain;
        }
    }

    void setOutputMute(bool mute)
    {
        std::lock_guard<std::mutex> lock(_ctrl_mutex);
        if (_out_muted.load() != mute) {
            _out_muted.store(mute);
            esp_codec_dev_set_out_mute(_codec_dev, mute);
        }
    }

    bool isPlaying()
    {
        std::lock_guard<std::mutex> lock(_play_mutex);
        return _is_playing || _stream_count > 0 || _stream_prebuffering;
    }

    float getCurrentRms() const
    {
        return _current_rms.load(std::memory_order_relaxed);
    }

    void play(std::vector<int16_t>& data, bool async)
    {
        if (_out_muted) {
            setOutputMute(false);
        }
        if (async) {
            {
                std::lock_guard<std::mutex> lock(_play_mutex);
                // Ignore UI click/tone sound effects while AI voice stream is active or buffered
                if (!data.empty() && (_stream_count > 0 || _stream_active || _stream_prebuffering)) {
                    return;
                }
                _audio_data = data;
                _is_playing = true;
            }
            xTaskNotifyGive(_task_handle);
        } else {
            {
                std::lock_guard<std::mutex> lock(_play_mutex);
                if (_is_playing || _stream_count > 0 || _stream_active || _stream_prebuffering) {
                    mclog::tagWarn(_tag, "audio is playing");
                    return;
                }
            }
            _write(data);
        }
    }

    void pushStreamPcm(const int16_t* samples, std::size_t count, int srcRate)
    {
        if (samples == nullptr || count == 0 || _stream_ring == nullptr || _stream_capacity == 0) {
            return;
        }
        const int spk_vol = GetHAL().getSpeakerVolume();
        if (spk_vol <= 0) {
            return;
        }
        if (GetHAL().isScreenSleeping()) {
            GetHAL().setSpeakerEnable(true);
        }

        if (_out_muted) {
            setOutputMute(false);
        }

        // Resample from srcRate (e.g. 24000 Hz) to hardware sample_rate (44100 Hz) with continuous phase across chunks
        const float step = static_cast<float>(std::max(8000, srcRate)) / static_cast<float>(sample_rate);

        float sum_sq            = 0.0f;
        std::size_t out_samples = 0;
        bool trigger_task       = false;
        {
            std::unique_lock<std::mutex> lock(_play_mutex);
            if (_stream_abort_req) {
                return;
            }

            const bool starting_fresh = (_stream_count == 0 && !_is_playing && !_stream_active && !_stream_has_prev);
            if (starting_fresh) {
                _resample_pos    = 0.0f;
                _prev_src_sample = samples[0];
                _stream_has_prev = true;
            }

            float pos         = _resample_pos;
            const float limit = static_cast<float>(count) - 1.0f;
            while (pos <= limit) {
                float s0   = 0.0f;
                float s1   = 0.0f;
                float frac = 0.0f;

                if (pos < 0.0f) {
                    s0   = static_cast<float>(_prev_src_sample);
                    s1   = static_cast<float>(samples[0]);
                    frac = pos + 1.0f;
                } else {
                    const std::size_t idx0 = static_cast<std::size_t>(pos);
                    const std::size_t idx1 = std::min<std::size_t>(idx0 + 1, count - 1);
                    frac                   = pos - static_cast<float>(idx0);
                    s0                     = static_cast<float>(samples[idx0]);
                    s1                     = static_cast<float>(samples[idx1]);
                }

                // Clean 1:1 original speech signal without digital clipping or harmonic waveshaping distortion
                // Proper volume & loudness are controlled via ES8311 DAC hardware volume curve
                float val = s0 * (1.0f - frac) + s1 * frac;

                // Apply 64-sample cosine fade-in at the very beginning of a new stream burst
                if (starting_fresh && out_samples < 64) {
                    const float fade =
                        0.5f * (1.0f - std::cos(static_cast<float>(M_PI) * static_cast<float>(out_samples) / 64.0f));
                    val *= fade;
                }

                int16_t sample_val = static_cast<int16_t>(std::clamp(val, -32768.0f, 32767.0f));
                sum_sq += static_cast<float>(sample_val) * static_cast<float>(sample_val);
                out_samples++;

                // Bounded buffer flow control: if ring buffer is full, wait for audio playback task to consume samples.
                // NEVER overwrite unplayed samples or advance read pointer, which causes fast-forward/skipping distortion!
                while (_stream_count >= _stream_capacity && !_stream_abort_req) {
                    _is_playing    = true;
                    _stream_active = true;
                    xTaskNotifyGive(_task_handle);
                    lock.unlock();
                    vTaskDelay(pdMS_TO_TICKS(12));
                    lock.lock();
                }
                if (_stream_abort_req) {
                    return;
                }

                _stream_ring[_stream_write_pos] = sample_val;
                _stream_write_pos               = (_stream_write_pos + 1) % _stream_capacity;
                _stream_count++;

                pos += step;
            }

            _resample_pos    = pos - static_cast<float>(count);
            _prev_src_sample = samples[count - 1];
            _stream_has_prev = true;

            // Pre-buffering jitter filter: accumulate ~120ms before starting playback to absorb initial network jitter
            constexpr std::size_t PREBUFFER_SAMPLES = static_cast<std::size_t>(sample_rate * 0.12f);
            if (!_stream_active) {
                if (!_stream_prebuffering) {
                    _stream_prebuffering    = true;
                    _stream_prebuffer_start = xTaskGetTickCount();
                }
                const uint32_t elapsed_ms =
                    (xTaskGetTickCount() - _stream_prebuffer_start) * portTICK_PERIOD_MS;
                if (_stream_count >= PREBUFFER_SAMPLES || elapsed_ms >= 150) {
                    _stream_prebuffering = false;
                    _stream_active       = true;
                    _is_playing          = true;
                    trigger_task         = true;
                }
            } else {
                _is_playing  = true;
                trigger_task = true;
            }
        }

        if (out_samples > 0) {
            const float rms = static_cast<float>(std::sqrt(sum_sq / static_cast<float>(out_samples)) / 32768.0f);
            _current_rms.store(std::clamp(rms * 3.2f, 0.0f, 1.0f), std::memory_order_relaxed);
        }

        if (trigger_task) {
            xTaskNotifyGive(_task_handle);
        }
    }

    void clearStream()
    {
        {
            std::lock_guard<std::mutex> lock(_play_mutex);
            _stream_read_pos     = 0;
            _stream_write_pos    = 0;
            _stream_count        = 0;
            _stream_prebuffering = false;
            _stream_active       = false;
            _stream_has_prev     = false;
            _resample_pos        = 0.0f;
            _prev_src_sample     = 0;
            _stream_abort_req    = true;
            _is_playing          = false;
            _audio_data.clear();
        }
        setOutputMute(true);
        _current_rms.store(0.0f, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(_spectrum_frame_mutex);
            _audio_spectrum_frame.bands.fill(0.0f);
            _audio_spectrum_frame.peakFrequencyHz = 0.0f;
        }
        xTaskNotifyGive(_task_handle);
    }

    std::size_t getStreamQueuedMs()
    {
        std::lock_guard<std::mutex> lock(_play_mutex);
        return (_stream_count * 1000ULL) / sample_rate;
    }

    bool recordStream16k(std::vector<int16_t>& out16k, uint16_t durationMs, float gain)
    {
        std::lock_guard<std::mutex> rx_lock(_rx_mutex);
        setMicGain(gain);

        const std::size_t src_samples = static_cast<std::size_t>((sample_rate * durationMs) / 1000);
        if (src_samples == 0) {
            out16k.clear();
            return false;
        }

        _record_raw_buf.resize(src_samples);
        esp_err_t ret = esp_codec_dev_read(_codec_dev, _record_raw_buf.data(), src_samples * sizeof(int16_t));
        if (ret != ESP_OK) {
            mclog::tagError(_tag, "recordStream16k failed: {}", ret);
            out16k.clear();
            return false;
        }

        // Feed live spectrum from recorded microphone samples
        feedSpectrum(_record_raw_buf.data(), _record_raw_buf.size());

        // Remove DC offset and downsample from 44100 Hz to 16000 Hz using boxcar anti-aliasing filter
        int64_t dc_sum = 0;
        for (std::size_t i = 0; i < src_samples; ++i) {
            dc_sum += _record_raw_buf[i];
        }
        const int32_t dc_offset = static_cast<int32_t>(dc_sum / static_cast<int64_t>(src_samples));

        const std::size_t dst_samples = static_cast<std::size_t>((16000ULL * durationMs) / 1000ULL);
        out16k.resize(dst_samples);

        double sum_sq = 0.0;
        const double step = static_cast<double>(src_samples) / static_cast<double>(dst_samples);
        for (std::size_t i = 0; i < dst_samples; ++i) {
            const double start_f = i * step;
            const std::size_t s0 = std::min<std::size_t>(static_cast<std::size_t>(start_f), src_samples - 1);
            const std::size_t s1 = std::min<std::size_t>(s0 + 1, src_samples - 1);
            const std::size_t s2 = std::min<std::size_t>(s0 + 2, src_samples - 1);

            // 3-tap anti-aliasing weighted average [0.25, 0.50, 0.25]
            const int32_t v0 = static_cast<int32_t>(_record_raw_buf[s0]) - dc_offset;
            const int32_t v1 = static_cast<int32_t>(_record_raw_buf[s1]) - dc_offset;
            const int32_t v2 = static_cast<int32_t>(_record_raw_buf[s2]) - dc_offset;
            const int32_t filtered = (v0 + (v1 * 2) + v2) / 4;

            const int16_t clamped = static_cast<int16_t>(std::clamp<int32_t>(filtered, -32768, 32767));
            out16k[i]             = clamped;
            sum_sq += static_cast<double>(clamped) * static_cast<double>(clamped);
        }

        const float rms = static_cast<float>(std::sqrt(sum_sq / static_cast<double>(dst_samples)) / 32768.0);
        _current_rms.store(std::clamp(rms * 4.5f, 0.0f, 1.0f), std::memory_order_relaxed);
        return true;
    }

    void record(std::vector<int16_t>& data, uint16_t durationMs, float gain)
    {
        std::lock_guard<std::mutex> rx_lock(_rx_mutex);
        setMicGain(gain);

        size_t sample_count = (size_t)(sample_rate * durationMs / 1000);
        size_t byte_size    = sample_count * sizeof(int16_t);

        data.resize(sample_count);

        esp_err_t ret = esp_codec_dev_read(_codec_dev, data.data(), byte_size);
        if (ret != ESP_OK) {
            mclog::tagError(_tag, "record failed: {}", ret);
            data.clear();
        }
    }

private:
    void _task_entry()
    {
        mclog::tagInfo(_tag, "start audio play task");
        std::vector<int16_t> current_data;
        constexpr std::size_t CHUNK_SAMPLES = 512;
        std::array<int16_t, CHUNK_SAMPLES> stream_chunk = {};
        bool was_streaming = false;
        int16_t last_stream_val = 0;
        uint32_t fft_decim      = 0;

        while (1) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            while (true) {
                bool has_oneshot      = false;
                std::size_t popped    = 0;
                bool abort_requested  = false;

                bool need_prebuffer_wait = false;
                {
                    std::lock_guard<std::mutex> lock(_play_mutex);
                    if (_stream_abort_req) {
                        _stream_abort_req = false;
                        abort_requested   = true;
                    }

                    if (!_audio_data.empty()) {
                        current_data = std::move(_audio_data);
                        _audio_data.clear();
                        has_oneshot = true;
                        _is_playing = true;
                    } else if (_stream_count > 0 && _stream_ring != nullptr) {
                        if (_stream_prebuffering) {
                            constexpr std::size_t PREBUFFER_SAMPLES = static_cast<std::size_t>(sample_rate * 0.12f);
                            const uint32_t elapsed_ms =
                                (xTaskGetTickCount() - _stream_prebuffer_start) * portTICK_PERIOD_MS;
                            if (_stream_count < PREBUFFER_SAMPLES && elapsed_ms < 150) {
                                need_prebuffer_wait = true;
                            } else {
                                _stream_prebuffering = false;
                                _stream_active       = true;
                            }
                        }

                        if (!need_prebuffer_wait) {
                            popped = std::min<std::size_t>(_stream_count, CHUNK_SAMPLES);
                            for (std::size_t i = 0; i < popped; ++i) {
                                stream_chunk[i]  = _stream_ring[_stream_read_pos];
                                _stream_read_pos = (_stream_read_pos + 1) % _stream_capacity;
                            }
                            _stream_count -= popped;
                            _is_playing = true;
                        }
                    } else {
                        _is_playing = false;
                    }
                }

                if (need_prebuffer_wait) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }

                if (abort_requested) {
                    // Drain hardware pipeline with silence without stopping the master I2S clock
                    // (Disabling TX in duplex mode halts shared BCLK/WS, causing ES8311 frame desync and squeal)
                    esp_codec_dev_write(_codec_dev, (void*)_silence_buffer.data(),
                                        std::min<std::size_t>(_silence_buffer.size(), 512) * sizeof(int16_t));
                    was_streaming = false;
                }

                if (has_oneshot) {
                    size_t offset        = 0;
                    size_t total_samples = current_data.size();
                    bool interrupted     = false;

                    while (offset < total_samples) {
                        {
                            std::lock_guard<std::mutex> lock(_play_mutex);
                            if (!_audio_data.empty() || _stream_abort_req) {
                                interrupted = true;
                                break;
                            }
                        }

                        size_t remain        = total_samples - offset;
                        size_t write_samples = (remain > CHUNK_SAMPLES) ? CHUNK_SAMPLES : remain;

                        esp_codec_dev_write(_codec_dev, (void*)&current_data[offset], write_samples * sizeof(int16_t));
                        offset += write_samples;
                    }

                    if (interrupted) {
                        continue;
                    }

                    esp_codec_dev_write(_codec_dev, (void*)_silence_buffer.data(),
                                        _silence_buffer.size() * sizeof(int16_t));
                    if (GetHAL().isScreenSleeping()) {
                        GetHAL().setSpeakerEnable(false);
                    }
                    continue;
                }

                if (popped > 0) {
                    if (_out_muted.load()) {
                        setOutputMute(false);
                    }
                    was_streaming   = true;
                    last_stream_val = stream_chunk[popped - 1];
                    // Update RMS level for UI animation during playback
                    float sum_sq = 0.0f;
                    for (std::size_t i = 0; i < popped; ++i) {
                        sum_sq += static_cast<float>(stream_chunk[i]) * static_cast<float>(stream_chunk[i]);
                    }
                    float rms = static_cast<float>(std::sqrt(sum_sq / static_cast<float>(popped)) / 32768.0f);
                    _current_rms.store(std::clamp(rms * 3.2f, 0.0f, 1.0f), std::memory_order_relaxed);

                    if ((++fft_decim & 3) == 0) {
                        feedSpectrum(stream_chunk.data(), popped);
                    }

                    esp_codec_dev_write(_codec_dev, (void*)stream_chunk.data(), popped * sizeof(int16_t));
                    continue;
                }

                if (was_streaming) {
                    // Hold stream open across network jitter gaps (up to ~360ms in 10ms steps) before flushing silence tail
                    bool received_more = false;
                    for (int wait_idx = 0; wait_idx < 36; ++wait_idx) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        std::lock_guard<std::mutex> lock(_play_mutex);
                        if (_stream_abort_req) {
                            break;
                        }
                        if (_stream_count >= static_cast<std::size_t>(sample_rate * 0.06f) || !_audio_data.empty() ||
                            (_stream_count > 0 && wait_idx > 10)) {
                            received_more = true;
                            break;
                        }
                    }

                    if (received_more) {
                        continue;
                    }

                    // Stream finished: flush silence tail to ensure clean DMA buffer without popping
                    esp_codec_dev_write(_codec_dev, (void*)_silence_buffer.data(),
                                        std::min<std::size_t>(_silence_buffer.size(), 512) * sizeof(int16_t));

                    _current_rms.store(0.0f, std::memory_order_relaxed);
                    {
                        std::lock_guard<std::mutex> lock(_spectrum_frame_mutex);
                        _audio_spectrum_frame.bands.fill(0.0f);
                        _audio_spectrum_frame.peakFrequencyHz = 0.0f;
                    }
                    {
                        std::lock_guard<std::mutex> lock(_play_mutex);
                        _stream_active       = false;
                        _stream_prebuffering = false;
                        _stream_has_prev     = false;
                        _resample_pos        = 0.0f;
                        _prev_src_sample     = 0;
                        _is_playing          = false;
                    }
                    was_streaming   = false;
                    last_stream_val = 0;
                    if (GetHAL().isScreenSleeping()) {
                        GetHAL().setSpeakerEnable(false);
                    }
                }

                break;
            }
        }
    }

    void _write(const std::vector<int16_t>& data)
    {
        esp_codec_dev_write(_codec_dev, (void*)data.data(), data.size() * sizeof(int16_t));
        esp_codec_dev_write(_codec_dev, (void*)_silence_buffer.data(), _silence_buffer.size() * sizeof(int16_t));
    }

    void _i2s_init()
    {
        mclog::tagInfo(_tag, "i2s init");

        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
        i2s_std_config_t std_cfg   = {
            .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg =
                {
                    .mclk = I2S_MCLK_PIN,
                    .bclk = I2S_BCLK_PIN,
                    .ws   = I2S_LRCK_PIN,
                    .dout = I2S_DDAC_OUT_PIN,
                    .din  = I2S_DADC_IN_PIN,
                },
        };

        ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &_tx_handle, &_rx_handle));
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(_tx_handle, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(_rx_handle, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(_tx_handle));
        ESP_ERROR_CHECK(i2s_channel_enable(_rx_handle));
    }

    void _spectrum_init()
    {
        esp_err_t ret = dsps_fft2r_init_fc32(nullptr, spectrum_fft_size);
        if (ret != ESP_OK) {
            mclog::tagError(_tag, "fft init failed: {}", ret);
            return;
        }

        dsps_wind_hann_f32(_spectrum_window.data(), spectrum_fft_size);

        constexpr int max_bin = spectrum_fft_size / 2;
        const float nyquist   = static_cast<float>(sample_rate) * 0.5f;
        const float min_hz    = static_cast<float>(sample_rate) / static_cast<float>(spectrum_fft_size);
        const float log_min   = std::log10(min_hz);
        const float log_max   = std::log10(nyquist);

        _band_bin_edges[0] = 1;
        for (std::size_t i = 1; i < Hal::AudioSpectrumFrame::bandCount; ++i) {
            float t            = static_cast<float>(i) / static_cast<float>(Hal::AudioSpectrumFrame::bandCount);
            float edge_hz      = std::pow(10.0f, log_min + (log_max - log_min) * t);
            int edge_bin       = static_cast<int>(std::lround(edge_hz * spectrum_fft_size / sample_rate));
            int min_edge       = _band_bin_edges[i - 1] + 1;
            int max_edge       = max_bin - static_cast<int>(Hal::AudioSpectrumFrame::bandCount - i);
            _band_bin_edges[i] = std::clamp(edge_bin, min_edge, max_edge);
        }
        _band_bin_edges[Hal::AudioSpectrumFrame::bandCount] = max_bin;
        _spectrum_available                                 = true;
    }

    bool _read_spectrum_hop()
    {
        std::unique_lock<std::mutex> lock(_rx_mutex, std::try_to_lock);
        if (lock.owns_lock() == false) {
            return false;
        }

        esp_err_t ret = esp_codec_dev_read(_codec_dev, _spectrum_pcm_hop.data(), sizeof(int16_t) * spectrum_hop_size);
        if (ret != ESP_OK) {
            return false;
        }

        std::move(_spectrum_time_domain.begin() + spectrum_hop_size, _spectrum_time_domain.end(),
                  _spectrum_time_domain.begin());

        for (int i = 0; i < spectrum_hop_size; ++i) {
            _spectrum_time_domain[spectrum_fft_size - spectrum_hop_size + i] =
                static_cast<float>(_spectrum_pcm_hop[i]) / 32768.0f;
        }

        _spectrum_samples_ready = std::min<std::size_t>(_spectrum_samples_ready + spectrum_hop_size, spectrum_fft_size);
        return true;
    }

    void _process_spectrum_frame(Hal::AudioSpectrumFrame& frame)
    {
        float mean = 0.0f;
        for (float sample : _spectrum_time_domain) {
            mean += sample;
        }
        mean /= static_cast<float>(spectrum_fft_size);

        for (int i = 0; i < spectrum_fft_size; ++i) {
            float sample                    = (_spectrum_time_domain[i] - mean) * _spectrum_window[i];
            _spectrum_fft_buffer[i * 2]     = sample;
            _spectrum_fft_buffer[i * 2 + 1] = 0.0f;
        }

        if (dsps_fft2r_fc32(_spectrum_fft_buffer.data(), spectrum_fft_size) != ESP_OK) {
            return;
        }
        if (dsps_bit_rev_fc32(_spectrum_fft_buffer.data(), spectrum_fft_size) != ESP_OK) {
            return;
        }

        float peak_bin_magnitude = 0.0f;
        int peak_bin_index       = 0;
        float top1               = 0.0f;
        float top2               = 0.0f;
        float top3               = 0.0f;

        for (std::size_t band = 0; band < Hal::AudioSpectrumFrame::bandCount; ++band) {
            int start_bin = _band_bin_edges[band];
            int end_bin   = _band_bin_edges[band + 1];
            float energy  = 0.0f;
            float peak    = 0.0f;
            int count     = 0;

            for (int bin = start_bin; bin < end_bin; ++bin) {
                float re  = _spectrum_fft_buffer[bin * 2];
                float im  = _spectrum_fft_buffer[bin * 2 + 1];
                float mag = std::sqrt(re * re + im * im) * (2.0f / static_cast<float>(spectrum_fft_size));
                if (mag > peak_bin_magnitude) {
                    peak_bin_magnitude = mag;
                    peak_bin_index     = bin;
                }
                peak = std::max(peak, mag);
                energy += mag * mag;
                ++count;
            }

            float rms = count > 0 ? std::sqrt(energy / static_cast<float>(count)) : 0.0f;
            float raw = rms * 0.48f + peak * 0.52f;
            float low_emphasis =
                1.12f - 0.22f * (static_cast<float>(band) / static_cast<float>(Hal::AudioSpectrumFrame::bandCount - 1));
            raw *= low_emphasis;

            float floor_alpha = raw < _spectrum_noise_floor[band] ? 0.45f : 0.004f;
            _spectrum_noise_floor[band] += (raw - _spectrum_noise_floor[band]) * floor_alpha;
            raw                       = std::max(raw - (_spectrum_noise_floor[band] * 2.20f + 0.0018f), 0.0f);
            _spectrum_raw_bands[band] = raw;

            if (raw >= top1) {
                top3 = top2;
                top2 = top1;
                top1 = raw;
            } else if (raw >= top2) {
                top3 = top2;
                top2 = raw;
            } else if (raw > top3) {
                top3 = raw;
            }
        }

        float frame_reference = std::max(top1 * 0.80f + top2 * 0.14f + top3 * 0.06f, 0.0015f);
        float norm_alpha      = frame_reference > _spectrum_normalization_level ? 0.44f : 0.16f;
        _spectrum_normalization_level += (frame_reference - _spectrum_normalization_level) * norm_alpha;
        _spectrum_normalization_level = std::clamp(_spectrum_normalization_level, 0.0015f, 1.0f);

        if (peak_bin_magnitude > 0.0f) {
            float refined_bin = static_cast<float>(peak_bin_index);
            if (peak_bin_index > 1 && peak_bin_index < (spectrum_fft_size / 2 - 1)) {
                float left_re      = _spectrum_fft_buffer[(peak_bin_index - 1) * 2];
                float left_im      = _spectrum_fft_buffer[(peak_bin_index - 1) * 2 + 1];
                float right_re     = _spectrum_fft_buffer[(peak_bin_index + 1) * 2];
                float right_im     = _spectrum_fft_buffer[(peak_bin_index + 1) * 2 + 1];
                float center_power = peak_bin_magnitude * peak_bin_magnitude;
                float left_power   = left_re * left_re + left_im * left_im;
                float right_power  = right_re * right_re + right_im * right_im;
                float denom        = left_power - 2.0f * center_power + right_power;

                if (std::fabs(denom) > 1e-9f) {
                    float offset = 0.5f * (left_power - right_power) / denom;
                    refined_bin += std::clamp(offset, -0.5f, 0.5f);
                }
            }
            frame.peakFrequencyHz =
                refined_bin * static_cast<float>(sample_rate) / static_cast<float>(spectrum_fft_size);
        } else {
            frame.peakFrequencyHz = 0.0f;
        }

        for (std::size_t band = 0; band < Hal::AudioSpectrumFrame::bandCount; ++band) {
            float ratio      = _spectrum_raw_bands[band] / _spectrum_normalization_level;
            float normalized = std::clamp(std::pow(ratio, 0.55f), 0.0f, 1.0f);
            if (normalized < 0.035f) {
                normalized = 0.0f;
            }

            float smooth_alpha = normalized > _spectrum_smoothed_bands[band] ? 0.82f : 0.40f;
            _spectrum_smoothed_bands[band] += (normalized - _spectrum_smoothed_bands[band]) * smooth_alpha;
            frame.bands[band] = std::clamp(_spectrum_smoothed_bands[band], 0.0f, 1.0f);
        }
    }

    i2s_chan_handle_t _tx_handle          = NULL;
    i2s_chan_handle_t _rx_handle          = NULL;
    esp_codec_dev_handle_t _codec_dev     = NULL;
    const audio_codec_data_if_t* _data_if = NULL;
    const audio_codec_ctrl_if_t* _ctrl_if = NULL;
    const audio_codec_gpio_if_t* _gpio_if = NULL;
    const audio_codec_if_t* _codec_if     = NULL;

    TaskHandle_t _task_handle;
    std::mutex _play_mutex;
    std::mutex _rx_mutex;
    std::mutex _ctrl_mutex;
    std::vector<int16_t> _audio_data;
    std::vector<int16_t> _silence_buffer;
    std::vector<int16_t> _record_raw_buf;
    int16_t* _stream_ring                                                          = nullptr;
    std::size_t _stream_capacity                                                   = 0;
    std::size_t _stream_read_pos                                                   = 0;
    std::size_t _stream_write_pos                                                  = 0;
    std::size_t _stream_count                                                      = 0;
    bool _stream_abort_req                                                         = false;
    bool _stream_active                                                            = false;
    bool _stream_prebuffering                                                      = false;
    bool _stream_has_prev                                                          = false;
    float _resample_pos                                                            = 0.0f;
    int16_t _prev_src_sample                                                       = 0;
    TickType_t _stream_prebuffer_start                                             = 0;
    float _current_in_gain                                                         = 32.0f;
    std::atomic<bool> _out_muted{false};
    std::atomic<float> _current_rms{0.0f};
    std::array<int16_t, spectrum_hop_size> _spectrum_pcm_hop                       = {};
    std::array<float, spectrum_fft_size> _spectrum_time_domain                     = {};
    std::array<float, spectrum_fft_size> _spectrum_window                          = {};
    std::array<float, spectrum_fft_size * 2> _spectrum_fft_buffer                  = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_raw_bands      = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_smoothed_bands = {};
    std::array<float, Hal::AudioSpectrumFrame::bandCount> _spectrum_noise_floor    = {};
    std::array<int, Hal::AudioSpectrumFrame::bandCount + 1> _band_bin_edges        = {};
    std::size_t _spectrum_samples_ready                                            = 0;
    float _spectrum_normalization_level                                            = 0.03f;
    bool _spectrum_available                                                       = false;
    bool _is_playing                                                               = false;
    std::mutex _spectrum_frame_mutex;
    Hal::AudioSpectrumFrame _audio_spectrum_frame                                  = {};
} _audio_codec;

void Hal::audio_init()
{
    mclog::tagInfo(_tag, "init");

    _audio_codec.init(i2c_bus_get_internal_bus_handle(_i2c_bus));

    ioe_speaker_enable(true);

    // Load volume from settings
    setSpeakerVolume(getSpeakerVolume(true), false);
}

void Hal::setSpeakerVolume(int volume, bool saveToSettings)
{
    _spk_volume = volume;
    _spk_volume = uitk::clamp(_spk_volume, 0, 100);

    mclog::tagInfo(_tag, "set speaker volume to {}", _spk_volume);
    _audio_codec.setVolume(_spk_volume);

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("spk_vol", _spk_volume);
        mclog::tagInfo(_tag, "volume saved to settings: {}", _spk_volume);
    }
}

int Hal::getSpeakerVolume(bool loadFromSettings)
{
    _spk_volume = _audio_codec.getVolume();

    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _spk_volume = settings.GetInt("spk_vol", 80);
        _spk_volume = uitk::clamp(_spk_volume, 0, 100);
        mclog::tagInfo(_tag, "volume loaded from settings: {}", _spk_volume);
    }

    return _spk_volume;
}

void Hal::audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain)
{
    _audio_codec.record(data, durationMs, gain);
}

void Hal::audioPlay(std::vector<int16_t>& data, bool async)
{
    _audio_codec.play(data, async);
}

bool Hal::isAudioPlaying()
{
    return _audio_codec.isPlaying();
}

int Hal::getAudioSampleRate()
{
    return _audio_codec.sample_rate;
}

void Hal::updateAudioSpectrum()
{
    _audio_codec.updateSpectrum();
}

Hal::AudioSpectrumFrame Hal::getAudioSpectrum() const
{
    return const_cast<AudioCodec&>(_audio_codec).getSpectrum();
}

void Hal::setSpeakerEnable(bool enable)
{
    ioe_speaker_enable(enable);
}

bool Hal::audioRecordStream16k(std::vector<int16_t>& out16k, uint16_t durationMs, float gain)
{
    return _audio_codec.recordStream16k(out16k, durationMs, gain);
}

void Hal::audioPushStreamPcm(const int16_t* samples, std::size_t count, int srcSampleRate)
{
    _audio_codec.pushStreamPcm(samples, count, srcSampleRate);
}

void Hal::audioClearStream()
{
    _audio_codec.clearStream();
}

std::size_t Hal::audioGetStreamQueuedMs()
{
    return _audio_codec.getStreamQueuedMs();
}

float Hal::audioGetCurrentRms()
{
    return _audio_codec.getCurrentRms();
}

namespace {

extern const uint8_t _boot_sfx_start[] asm("_binary_boot_sfx_bin_start");
extern const uint8_t _boot_sfx_end[] asm("_binary_boot_sfx_bin_end");

}  // namespace

void Hal::playBootSfx()
{
    mclog::tagInfo(_tag, "play boot sfx");

    const std::size_t byte_count = _boot_sfx_end - _boot_sfx_start;
    if (byte_count == 0 || (byte_count % sizeof(int16_t)) != 0) {
        mclog::tagError(_tag, "boot sfx binary has invalid size: {}", byte_count);
        return;
    }

    const auto* samples     = reinterpret_cast<const int16_t*>(_boot_sfx_start);
    const std::size_t count = byte_count / sizeof(int16_t);
    std::vector<int16_t> pcm(samples, samples + count);

    audioPlay(pcm, true);
}
