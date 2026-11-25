/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <hal/hal.h>
#include <random>

namespace audio {

static std::vector<int> c_major_scale = {60, 62, 64, 65, 67, 69, 71};  // C大调音阶（C D E F G A B）

static float get_effective_volume_scale(float volumeScale)
{
    return std::max(0.0f, volumeScale);
}

void play_tone(int frequency, float durationSec, float volumeScale)
{
    if (GetHAL().getSpeakerVolume() <= 0) {
        return;
    }
    if (GetHAL().isScreenSleeping()) {
        GetHAL().setSpeakerEnable(true);
    }

    const int sample_rate = GetHAL().getAudioSampleRate();
    const int samples     = static_cast<int>(sample_rate * durationSec);
    std::vector<int16_t> buffer(samples);

    const int fade_len        = 200;
    const float amplitude     = 32767.0f / 5;
    const float scaled_volume = get_effective_volume_scale(volumeScale);

    // Optimization: Use float (sinf) and incremental phase to avoid double precision math and division inside loop
    const float angle_step = 2.0f * (float)M_PI * frequency / sample_rate;
    float current_angle    = 0.0f;

    for (int i = 0; i < samples; ++i) {
        float amp = amplitude * scaled_volume;

        if (i >= samples - fade_len) {
            float fade_factor = static_cast<float>(samples - i) / fade_len;
            amp *= fade_factor;
        }

        // Use sinf instead of sin
        int16_t value = static_cast<int16_t>(amp * sinf(current_angle));
        buffer[i]     = value;

        current_angle += angle_step;
        // Keep angle within reasonable bounds to preserve precision (though for short clips it matters less)
        if (current_angle > 2.0f * (float)M_PI) {
            current_angle -= 2.0f * (float)M_PI;
        }
    }

    GetHAL().audioPlay(buffer);
}

void play_melody(const std::vector<int>& midiList, float durationSec, float volumeScale)
{
    if (GetHAL().getSpeakerVolume() <= 0) {
        return;
    }
    if (GetHAL().isScreenSleeping()) {
        GetHAL().setSpeakerEnable(true);
    }

    const int sample_rate      = GetHAL().getAudioSampleRate();
    const int samples_per_note = static_cast<int>(sample_rate * durationSec);
    const int fade_len         = 200;  // 每个音符结尾的淡出长度
    const float amplitude      = 32767.0f / 5;
    const float scaled_volume  = get_effective_volume_scale(volumeScale);

    std::vector<int16_t> buffer;  // 大 buffer 存放整首旋律
    buffer.reserve(midiList.size() * samples_per_note);

    for (int midiNote : midiList) {
        float angle_step = 0.0f;
        if (midiNote >= 0) {
            // Optimization: Calculate frequency and step outside the inner loop
            float freq = 440.0f * powf(2.0f, (midiNote - 69) / 12.0f);
            angle_step = 2.0f * (float)M_PI * freq / sample_rate;
        }

        float current_angle = 0.0f;

        for (int i = 0; i < samples_per_note; ++i) {
            float amp = amplitude * scaled_volume;

            // 应用淡出（仅每个音符的结尾）
            if (i >= samples_per_note - fade_len) {
                float fade_factor = static_cast<float>(samples_per_note - i) / fade_len;
                amp *= fade_factor;
            }

            int16_t sample = 0;
            if (midiNote >= 0) {
                sample = static_cast<int16_t>(amp * sinf(current_angle));

                current_angle += angle_step;
                if (current_angle > 2.0f * (float)M_PI) {
                    current_angle -= 2.0f * (float)M_PI;
                }
            }

            buffer.push_back(sample);
        }
    }

    GetHAL().audioPlay(buffer);
}

void play_tone_from_midi(int midi, float durationSec, float volumeScale)
{
    if (GetHAL().getSpeakerVolume() <= 0) {
        return;
    }

    // Optimization: Use float math
    float freq = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
    play_tone(static_cast<int>(freq), durationSec, volumeScale);
}

void play_random_tone(int semitoneShift, float durationSec, float volumeScale)
{
    if (GetHAL().getSpeakerVolume() <= 0) {
        return;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(c_major_scale.size()) - 1);

    int index = dist(gen);
    int midi  = c_major_scale[index] + semitoneShift;

    play_tone_from_midi(midi, durationSec, volumeScale);
}

void play_eye_care_chime(float volumeScale)
{
    if (GetHAL().getSpeakerVolume() <= 0) {
        return;
    }
    if (GetHAL().isScreenSleeping()) {
        GetHAL().setSpeakerEnable(true);
    }

    const int sample_rate     = GetHAL().getAudioSampleRate();
    const float scaled_volume = get_effective_volume_scale(volumeScale);
    // Brahms lullaby opening up an octave (E6/E6/G6) for a clear "ding ding ding".
    constexpr int kMidiNotes[]     = {88, 88, 91};
    constexpr float kOnsetSec[]    = {0.00f, 0.30f, 0.60f};
    constexpr float kDecayTauSec[] = {0.14f, 0.14f, 0.20f};
    constexpr float kNoteAmp[]     = {1.00f, 0.94f, 1.00f};
    constexpr float kTotalSec      = 1.35f;
    constexpr float kAttackSec     = 0.003f;
    // Keep headroom so bright partials + note overlap never clip.
    constexpr float kPeakAmplitude = 32767.0f / 4.8f;
    constexpr float kTwoPi         = 2.0f * static_cast<float>(M_PI);

    const int total_samples = static_cast<int>(sample_rate * kTotalSec);
    std::vector<float> mix(static_cast<size_t>(total_samples), 0.0f);

    for (size_t n = 0; n < sizeof(kMidiNotes) / sizeof(kMidiNotes[0]); ++n) {
        const float freq =
            440.0f * powf(2.0f, (static_cast<float>(kMidiNotes[n]) - 69.0f) / 12.0f);
        // Glockenspiel-ish inharmonic partials (brighter "ding", not dull "deng").
        const float w1           = kTwoPi * freq / sample_rate;
        const float w2           = kTwoPi * freq * 2.76f / sample_rate;
        const float w3           = kTwoPi * freq * 5.40f / sample_rate;
        const int onset          = static_cast<int>(kOnsetSec[n] * sample_rate);
        const int attack_samples = std::max(1, static_cast<int>(kAttackSec * sample_rate));
        const float inv_tau1     = 1.0f / (kDecayTauSec[n] * sample_rate);
        const float inv_tau2     = 1.0f / (kDecayTauSec[n] * 0.45f * sample_rate);
        const float inv_tau3     = 1.0f / (kDecayTauSec[n] * 0.22f * sample_rate);
        const float note_amp     = kPeakAmplitude * scaled_volume * kNoteAmp[n];

        float phase1 = 0.0f;
        float phase2 = 0.0f;
        float phase3 = 0.0f;

        for (int i = onset; i < total_samples; ++i) {
            const int age   = i - onset;
            const float a   = static_cast<float>(age);
            float env1      = expf(-a * inv_tau1);
            float env2      = expf(-a * inv_tau2);
            float env3      = expf(-a * inv_tau3);
            if (age < attack_samples) {
                const float t = a / static_cast<float>(attack_samples);
                const float attack = 0.5f * (1.0f - cosf(static_cast<float>(M_PI) * t));
                env1 *= attack;
                env2 *= attack;
                env3 *= attack;
            } else if (env1 < 0.0008f) {
                break;
            }

            mix[static_cast<size_t>(i)] +=
                note_amp * (0.62f * env1 * sinf(phase1) + 0.28f * env2 * sinf(phase2) +
                            0.10f * env3 * sinf(phase3));

            phase1 += w1;
            phase2 += w2;
            phase3 += w3;
            if (phase1 > kTwoPi) {
                phase1 -= kTwoPi;
            }
            if (phase2 > kTwoPi) {
                phase2 -= kTwoPi;
            }
            if (phase3 > kTwoPi) {
                phase3 -= kTwoPi;
            }
        }
    }

    // Soft peak normalize — crisp without hard clipping / 破音.
    float peak = 1.0f;
    for (float s : mix) {
        peak = std::max(peak, fabsf(s));
    }
    const float norm = (peak > 12000.0f) ? (12000.0f / peak) : 1.0f;

    std::vector<int16_t> buffer(static_cast<size_t>(total_samples));
    for (int i = 0; i < total_samples; ++i) {
        buffer[static_cast<size_t>(i)] =
            static_cast<int16_t>(std::clamp(mix[static_cast<size_t>(i)] * norm, -12000.0f, 12000.0f));
    }

    GetHAL().audioPlay(buffer);
}

}  // namespace audio
