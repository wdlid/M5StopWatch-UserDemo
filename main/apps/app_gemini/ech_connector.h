/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

namespace gemini {

struct DohEchOptions {
    bool dohEnabled = true;
    std::string dohUrl1 = "https://edge.1molchuan.top/linuxdo";
    std::string dohUrl2 = "https://edge.1molchuan.top/plus";
    std::string connectIp = "";
};

struct DnsLookupResult {
    std::vector<std::string> ipv4Addrs;
    std::vector<std::string> ipv4Hints;

    void merge(const DnsLookupResult& other);
    std::string firstIpv4() const;
};

struct ResolvedEndpoint {
    std::vector<std::string> candidateIps;
};

// RFC 8484 DNS wireformat helpers
bool buildDnsQuery(const std::string& domain, uint16_t qtype, std::vector<uint8_t>& outPacket);
bool parseDnsResponse(const uint8_t* packet, std::size_t len, DnsLookupResult& outResult);
bool resolveHostAndEch(const std::string& targetHost, const DohEchOptions& opts, ResolvedEndpoint& outEndpoint);

// Custom WebSocket over TLS 1.3 / 1.2 client with DoH, preferred IP routing, and ECH / outer SNI support
class EchWebSocketClient {
public:
    using OnTextCallback   = std::function<void(const std::string&)>;
    using OnBinaryCallback = std::function<void(const uint8_t*, std::size_t)>;
    using OnErrorCallback  = std::function<void(const std::string&)>;

    EchWebSocketClient();
    ~EchWebSocketClient();

    bool connect(const std::string& wsUrl,
                 const std::string& bearerToken,
                 const std::string& apiKeyHeader,
                 const DohEchOptions& opts,
                 std::string& outError);

    void disconnect();
    void abort();
    bool isConnected() const
    {
        return _connected;
    }

    bool sendText(const std::string& text);
    bool sendBinary(const uint8_t* data, std::size_t len);
    bool sendPing();

    // Non-blocking poll for incoming WebSocket frames (call from background task)
    bool poll(int timeoutMs,
              const OnTextCallback& onText,
              const OnBinaryCallback& onBinary,
              const OnErrorCallback& onError);

    std::string connectedIp() const
    {
        return _connected_ip;
    }

private:
    bool openTcpSocket(const std::string& ip, uint16_t port, int timeoutMs);
    bool performTlsHandshake(const std::string& targetHost,
                             const std::string& sniHostname,
                             bool forceTls13,
                             std::string& outError);
    bool performWsUpgrade(const std::string& hostHeader,
                          const std::string& pathAndQuery,
                          const std::string& bearerToken,
                          const std::string& apiKeyHeader,
                          std::string& outError);

    int tlsReadExact(uint8_t* buf, std::size_t len, int timeoutMs);
    int tlsWriteAll(const uint8_t* buf, std::size_t len);
    bool sendWsFrame(uint8_t opcode, const uint8_t* payload, std::size_t len);

    int _sockfd     = -1;
    bool _connected = false;
    bool _is_tls    = true;
    std::atomic<bool> _abort{false};
    std::string _connected_ip;

    mbedtls_ssl_context _ssl;
    mbedtls_ssl_config _conf;
    mbedtls_entropy_context _entropy;
    mbedtls_ctr_drbg_context _ctr_drbg;
    bool _tls_initialized = false;

    std::vector<uint8_t> _rx_residual;
    std::vector<uint8_t> _frag_buffer;
    uint8_t _frag_opcode = 0;
};

}  // namespace gemini
