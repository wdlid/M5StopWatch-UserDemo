/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ech_connector.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace gemini {

enum class SessionState {
    Disconnected = 0,
    Connecting,
    Ready,
    Listening,
    Thinking,
    Speaking,
    Error,
};

struct GeminiConfig {
    std::string wsUrl      = "wss://xiaozhi-gemini-live-gateway.testad.workers.dev/ws?mode=binary";
    std::string apiKey     = "";
    std::string wsToken    = "";
    std::string model      = "models/gemini-3.8-live";
    std::string voice      = "Leda";
    std::string thinking   = "LOW";
    bool binaryAudioMode   = true;
    std::string systemPrompt =
        "你是M5StopWatch智能手表上的全双工语音助手小智。用户默认所在位置为：北京市丰台区（东八区北京时间 UTC+8）。"
        "请用亲切自然、简练生动的中文口语与用户交流，不要使用Markdown或特殊符号。"
        "当需要调用任何工具（如查询天气 get_weather、调节手表音量 set_volume、调节屏幕亮度 set_brightness、"
        "震动提醒 trigger_vibration、设置闹钟 set_alarm、查询当前时间与电量 get_watch_status、"
        "结束对话 stop_conversation 等）时，必须直接立即发起 toolCall 函数调用，绝不要先说过渡废话。";
    DohEchOptions dohEch;

    static GeminiConfig loadFromNvs();
    void saveToNvs() const;
};

class GeminiLiveClient {
public:
    GeminiLiveClient();
    ~GeminiLiveClient();

    void start(const GeminiConfig& config);
    void stop();

    void setPttTalking(bool talking);
    bool isPttTalking() const
    {
        return _ptt_active.load(std::memory_order_relaxed);
    }
    void togglePttTalking();

    SessionState getState() const
    {
        return _state.load(std::memory_order_relaxed);
    }

    std::string getStatusText() const;
    std::string getUserTranscript() const;
    std::string getModelTranscript() const;
    std::string getConnectedIp() const;

private:
    void taskLoop();
    void handleServerJson(const std::string& jsonText);
    void handleServerBinaryPcm(const uint8_t* data, std::size_t len);
    std::string buildSetupJson() const;
    std::string executeToolCalls(const std::string& rawJsonText);

    void setStatusText(const std::string& text);
    void appendUserTranscript(const std::string& text, bool replace = false);
    void appendModelTranscript(const std::string& text, bool replace = false);

    GeminiConfig _config;
    mutable std::mutex _text_mutex;
    std::string _status_text      = "Idle";
    std::string _user_transcript  = "";
    std::string _model_transcript = "";
    std::string _connected_ip     = "";

    std::atomic<SessionState> _state{SessionState::Disconnected};
    std::atomic<bool> _running{false};
    std::atomic<bool> _ptt_active{false};
    std::atomic<bool> _ptt_just_started{false};
    std::atomic<bool> _ptt_just_ended{false};
    std::atomic<bool> _setup_complete{false};
    std::atomic<bool> _server_turn_finished{true};
    std::atomic<bool> _ignore_current_turn_audio{false};
    TickType_t _ignore_audio_start_tick    = 0;
    uint8_t _in_progress_continuation_count = 0;

    TaskHandle_t _task_handle = nullptr;
    EchWebSocketClient _ws;
};

}  // namespace gemini
