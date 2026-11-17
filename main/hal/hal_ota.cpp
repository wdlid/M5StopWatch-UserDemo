/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/ota/ota.h"

bool Hal::startOta(std::function<void(std::string_view)> onLog)
{
    return ota::OtaManager::getInstance().start(onLog);
}

void Hal::stopOta()
{
    ota::OtaManager::getInstance().stop();
}

bool Hal::isOtaRunning()
{
    return ota::OtaManager::getInstance().isRunning();
}

Hal::OtaStatus Hal::getOtaStatus()
{
    auto st = ota::OtaManager::getInstance().getStatus();
    Hal::OtaStatus out;
    switch (st.state) {
    case ota::State::Idle:
        out.state = Hal::OtaState::Idle;
        break;
    case ota::State::Listening:
        out.state = Hal::OtaState::Ready;
        break;
    case ota::State::Flashing:
        out.state = Hal::OtaState::Flashing;
        break;
    case ota::State::Success:
        out.state = Hal::OtaState::Success;
        break;
    case ota::State::Failed:
        out.state = Hal::OtaState::Failed;
        break;
    }
    out.progress = st.progress;
    out.bytesReceived = st.bytesReceived;
    out.totalBytes = st.totalBytes;
    out.runningPartition = st.runningPartition;
    out.targetPartition = st.targetPartition;
    out.ipAddress = st.ipAddress;
    out.ssid = st.ssid;
    out.message = st.message;
    return out;
}
