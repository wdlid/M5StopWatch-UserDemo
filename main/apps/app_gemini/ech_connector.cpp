/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ech_connector.h"

#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/base64.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/sha1.h>
#include <psa/crypto.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace gemini {

namespace {

constexpr const char* _tag = "ECH-WS";

struct EndpointCache {
    std::mutex mutex;
    std::string host;
    std::string dohUrl1;
    std::string dohUrl2;
    std::string connectIp;
    std::string echConfigDomain;
    std::string staticEchConfigB64;
    bool dohEnabled = false;
    bool echEnabled = false;
    uint32_t resolvedAtMs = 0;
    ResolvedEndpoint endpoint;
    std::string preferredIp;
    std::string preferredSni;
};

EndpointCache& get_endpoint_cache()
{
    static EndpointCache cache;
    return cache;
}

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

constexpr uint16_t DNS_TYPE_A     = 1;
constexpr uint16_t DNS_TYPE_HTTPS = 65;
constexpr uint16_t DNS_CLASS_IN   = 1;

// Standard Cloudflare `cloudflare-ech.com` V18 (0xfe0d) EchConfigList fallback bytes (71 bytes)
constexpr uint8_t FALLBACK_CLOUDFLARE_ECH_CONFIG_LIST[] = {
    0x00, 0x45, 0xfe, 0x0d, 0x00, 0x41, 0x8a, 0x00, 0x20, 0x00, 0x20, 0xd4, 0x8b, 0x79, 0xb3, 0x96, 0xda, 0xc8,
    0xcd, 0x4c, 0xa7, 0xc4, 0x2a, 0x23, 0x68, 0x5a, 0x20, 0x11, 0x86, 0x3a, 0x5c, 0xbc, 0x28, 0x34, 0x4c, 0x92,
    0x77, 0x6a, 0xc3, 0x90, 0xf0, 0x53, 0x06, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00, 0x12, 0x63, 0x6c, 0x6f,
    0x75, 0x64, 0x66, 0x6c, 0x61, 0x72, 0x65, 0x2d, 0x65, 0x63, 0x68, 0x2e, 0x63, 0x6f, 0x6d, 0x00, 0x00,
};

constexpr const char* FALLBACK_CLOUDFLARE_IPS[] = {
    "172.64.229.205",
    "172.64.229.94",
    "104.16.132.229",
};

uint16_t read_u16_be(const uint8_t* p)
{
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

bool skip_dns_name(const uint8_t* buf, std::size_t len, std::size_t& offset)
{
    std::size_t jumps = 0;
    while (true) {
        if (offset >= len) {
            return false;
        }
        uint8_t b = buf[offset];
        if (b == 0) {
            offset += 1;
            return true;
        } else if ((b & 0xC0) == 0xC0) {
            if (offset + 2 > len) {
                return false;
            }
            offset += 2;
            return true;
        } else if ((b & 0xC0) != 0) {
            return false;
        } else {
            std::size_t label_len = b;
            if (offset + 1 + label_len > len) {
                return false;
            }
            offset += 1 + label_len;
        }
        if (++jumps > 128) {
            return false;
        }
    }
}

std::string base64url_encode_no_pad(const uint8_t* data, std::size_t len)
{
    std::size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, data, len);
    std::vector<unsigned char> buf(olen + 4, 0);
    if (mbedtls_base64_encode(buf.data(), buf.size(), &olen, data, len) != 0) {
        return {};
    }
    std::string out(reinterpret_cast<char*>(buf.data()), olen);
    for (char& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') {
        out.pop_back();
    }
    return out;
}

bool decode_base64_flexible(const std::string& input, std::vector<uint8_t>& out)
{
    std::string clean;
    clean.reserve(input.size() + 4);
    for (char c : input) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '-') clean.push_back('+');
        else if (c == '_') clean.push_back('/');
        else clean.push_back(c);
    }
    if (clean.empty()) return false;
    while (clean.size() % 4 != 0) {
        clean.push_back('=');
    }

    std::size_t olen = 0;
    mbedtls_base64_decode(nullptr, 0, &olen, reinterpret_cast<const unsigned char*>(clean.data()), clean.size());
    out.resize(olen + 4);
    if (mbedtls_base64_decode(out.data(), out.size(), &olen, reinterpret_cast<const unsigned char*>(clean.data()),
                              clean.size()) != 0) {
        out.clear();
        return false;
    }
    out.resize(olen);
    return !out.empty();
}

// Extract public_name from V18 (0xfe0d) EchConfigList bytes
std::string parse_ech_public_name(const std::vector<uint8_t>& echBytes)
{
    if (echBytes.size() < 16) {
        return "cloudflare-ech.com";
    }
    std::size_t pos = 2;  // skip list length u16
    if (pos + 4 > echBytes.size()) return "cloudflare-ech.com";
    uint16_t version = read_u16_be(&echBytes[pos]);
    uint16_t length  = read_u16_be(&echBytes[pos + 2]);
    pos += 4;
    if (version != 0xfe0d || pos + length > echBytes.size() || length < 10) {
        return "cloudflare-ech.com";
    }
    // Inside V18 EchConfigContents:
    // config_id (1) + kem_id (2) + public_key_len (2) + public_key + cipher_suites_len (2) + cipher_suites + max_name_len (1) + public_name_len (1) + public_name
    pos += 1 + 2;
    if (pos + 2 > echBytes.size()) return "cloudflare-ech.com";
    uint16_t pk_len = read_u16_be(&echBytes[pos]);
    pos += 2 + pk_len;
    if (pos + 2 > echBytes.size()) return "cloudflare-ech.com";
    uint16_t cs_len = read_u16_be(&echBytes[pos]);
    pos += 2 + cs_len;
    if (pos + 2 > echBytes.size()) return "cloudflare-ech.com";
    pos += 1;  // maximum_name_length
    uint8_t name_len = echBytes[pos++];
    if (name_len > 0 && pos + name_len <= echBytes.size()) {
        return std::string(reinterpret_cast<const char*>(&echBytes[pos]), name_len);
    }
    return "cloudflare-ech.com";
}

bool is_google_direct_domain(const std::string& host)
{
    std::string h = host;
    for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!h.empty() && h.back() == '.') h.pop_back();
    auto ends_with = [](const std::string& s, const char* suf) {
        std::size_t slen = std::strlen(suf);
        return s.size() >= slen && s.compare(s.size() - slen, slen, suf) == 0;
    };
    return h == "googleapis.com" || ends_with(h, ".googleapis.com") || h == "google.com" ||
           ends_with(h, ".google.com") || h == "gstatic.com" || ends_with(h, ".gstatic.com");
}

struct HttpBuffer {
    std::vector<uint8_t> data;
};

esp_err_t doh_http_event_handler(esp_http_client_event_t* evt)
{
    auto* buf = static_cast<HttpBuffer*>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && buf != nullptr && evt->data != nullptr && evt->data_len > 0) {
        if (buf->data.size() + evt->data_len <= 4096) {
            const auto* p = static_cast<const uint8_t*>(evt->data);
            buf->data.insert(buf->data.end(), p, p + evt->data_len);
        }
    }
    return ESP_OK;
}

struct DohBatchQuery {
    std::string domain;
    uint16_t qtype = DNS_TYPE_A;
    DnsLookupResult* outResult = nullptr;
};

bool doh_query_batch(const std::string& dohUrl, std::vector<DohBatchQuery>& queries)
{
    if (dohUrl.empty() || queries.empty()) return false;
    if (!GetHAL().isWifiConnected() || GetHAL().isScreenSleeping()) {
        return false;
    }

    HttpBuffer resp_buf;
    esp_http_client_config_t config = {};
    config.url                      = dohUrl.c_str();
    config.method                   = HTTP_METHOD_GET;
    config.timeout_ms               = 2500;
    config.crt_bundle_attach        = esp_crt_bundle_attach;
    config.event_handler            = doh_http_event_handler;
    config.user_data                = &resp_buf;
    config.buffer_size              = 1024;
    config.buffer_size_tx           = 1024;
    config.keep_alive_enable        = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    esp_http_client_set_header(client, "Accept", "application/dns-message");
    bool any_ok = false;

    for (auto& q : queries) {
        if (!GetHAL().isWifiConnected() || GetHAL().isScreenSleeping()) {
            break;
        }
        std::vector<uint8_t> query_pkt;
        if (!buildDnsQuery(q.domain, q.qtype, query_pkt)) {
            continue;
        }

        std::string b64url   = base64url_encode_no_pad(query_pkt.data(), query_pkt.size());
        std::string full_url = dohUrl + (dohUrl.find('?') != std::string::npos ? "&dns=" : "?dns=") + b64url;

        resp_buf.data.clear();
        esp_http_client_set_url(client, full_url.c_str());
        esp_err_t err   = esp_http_client_perform(client);
        int status_code = esp_http_client_get_status_code(client);

        if (err == ESP_OK && status_code >= 200 && status_code < 300 && !resp_buf.data.empty()) {
            if (q.outResult && parseDnsResponse(resp_buf.data.data(), resp_buf.data.size(), *q.outResult)) {
                any_ok = true;
            }
        } else {
            break;
        }
    }

    esp_http_client_cleanup(client);
    return any_ok;
}

bool parse_ws_url(const std::string& url,
                  bool& isTls,
                  std::string& host,
                  uint16_t& port,
                  std::string& pathAndQuery)
{
    std::string trimmed = url;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();

    std::size_t scheme_end = trimmed.find("://");
    if (scheme_end == std::string::npos) {
        return false;
    }
    std::string scheme = trimmed.substr(0, scheme_end);
    for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (scheme == "wss" || scheme == "https") {
        isTls = true;
        port  = 443;
    } else if (scheme == "ws" || scheme == "http") {
        isTls = false;
        port  = 80;
    } else {
        return false;
    }

    std::size_t authority_start = scheme_end + 3;
    std::size_t path_start      = trimmed.find('/', authority_start);
    std::size_t query_start     = trimmed.find('?', authority_start);

    std::size_t host_port_end = std::string::npos;
    if (path_start != std::string::npos && query_start != std::string::npos) {
        host_port_end = std::min(path_start, query_start);
    } else if (path_start != std::string::npos) {
        host_port_end = path_start;
    } else if (query_start != std::string::npos) {
        host_port_end = query_start;
    }

    std::string authority = (host_port_end == std::string::npos)
                                ? trimmed.substr(authority_start)
                                : trimmed.substr(authority_start, host_port_end - authority_start);
    if (authority.empty()) {
        return false;
    }

    if (host_port_end == std::string::npos) {
        pathAndQuery = "/";
    } else if (trimmed[host_port_end] == '?') {
        pathAndQuery = "/" + trimmed.substr(host_port_end);
    } else {
        pathAndQuery = trimmed.substr(host_port_end);
    }

    std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        int parsed_port = std::atoi(authority.substr(colon + 1).c_str());
        if (parsed_port > 0 && parsed_port <= 65535) {
            port = static_cast<uint16_t>(parsed_port);
        }
    } else {
        host = authority;
    }

    return !host.empty();
}

}  // namespace

void DnsLookupResult::merge(const DnsLookupResult& other)
{
    for (const auto& ip : other.ipv4Addrs) {
        if (std::find(ipv4Addrs.begin(), ipv4Addrs.end(), ip) == ipv4Addrs.end()) {
            ipv4Addrs.push_back(ip);
        }
    }
    for (const auto& ip : other.ipv4Hints) {
        if (std::find(ipv4Hints.begin(), ipv4Hints.end(), ip) == ipv4Hints.end()) {
            ipv4Hints.push_back(ip);
        }
    }
    if (echConfigList.empty() && !other.echConfigList.empty()) {
        echConfigList = other.echConfigList;
    }
}

std::string DnsLookupResult::firstIpv4() const
{
    if (!ipv4Addrs.empty()) return ipv4Addrs.front();
    if (!ipv4Hints.empty()) return ipv4Hints.front();
    return {};
}

bool buildDnsQuery(const std::string& domain, uint16_t qtype, std::vector<uint8_t>& outPacket)
{
    std::string trimmed = domain;
    while (!trimmed.empty() && (trimmed.back() == '.' || trimmed.back() == ' ')) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) {
        return false;
    }

    outPacket = {
        0x12, 0x34,  // ID
        0x01, 0x00,  // Flags (RD=1)
        0x00, 0x01,  // QDCOUNT = 1
        0x00, 0x00,  // ANCOUNT = 0
        0x00, 0x00,  // NSCOUNT = 0
        0x00, 0x00,  // ARCOUNT = 0
    };

    std::size_t start = 0;
    while (start < trimmed.size()) {
        std::size_t dot = trimmed.find('.', start);
        std::string label = (dot == std::string::npos) ? trimmed.substr(start) : trimmed.substr(start, dot - start);
        if (label.empty() || label.size() > 63) {
            return false;
        }
        outPacket.push_back(static_cast<uint8_t>(label.size()));
        outPacket.insert(outPacket.end(), label.begin(), label.end());
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    outPacket.push_back(0x00);

    outPacket.push_back(static_cast<uint8_t>((qtype >> 8) & 0xFF));
    outPacket.push_back(static_cast<uint8_t>(qtype & 0xFF));
    outPacket.push_back(static_cast<uint8_t>((DNS_CLASS_IN >> 8) & 0xFF));
    outPacket.push_back(static_cast<uint8_t>(DNS_CLASS_IN & 0xFF));
    return true;
}

bool parseDnsResponse(const uint8_t* packet, std::size_t len, DnsLookupResult& outResult)
{
    if (packet == nullptr || len < 12) {
        return false;
    }

    uint16_t qdcount = read_u16_be(packet + 4);
    uint16_t ancount = read_u16_be(packet + 6);

    std::size_t offset = 12;
    for (uint16_t i = 0; i < qdcount; ++i) {
        if (!skip_dns_name(packet, len, offset) || offset + 4 > len) {
            return false;
        }
        offset += 4;
    }

    for (uint16_t i = 0; i < ancount; ++i) {
        if (!skip_dns_name(packet, len, offset) || offset + 10 > len) {
            return false;
        }

        uint16_t rtype    = read_u16_be(packet + offset);
        uint16_t rdlength = read_u16_be(packet + offset + 8);
        offset += 10;

        if (offset + rdlength > len) {
            return false;
        }

        const uint8_t* rdata = packet + offset;
        offset += rdlength;

        if (rtype == DNS_TYPE_A && rdlength == 4) {
            char ip_buf[20] = {};
            std::snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", rdata[0], rdata[1], rdata[2], rdata[3]);
            std::string ip_str(ip_buf);
            if (std::find(outResult.ipv4Addrs.begin(), outResult.ipv4Addrs.end(), ip_str) ==
                outResult.ipv4Addrs.end()) {
                outResult.ipv4Addrs.push_back(ip_str);
            }
        } else if (rtype == DNS_TYPE_HTTPS && rdlength >= 3) {
            std::size_t r_off = 2;  // skip SvcPriority
            if (!skip_dns_name(rdata, rdlength, r_off)) {
                continue;
            }
            while (r_off + 4 <= rdlength) {
                uint16_t param_key = read_u16_be(rdata + r_off);
                uint16_t param_len = read_u16_be(rdata + r_off + 2);
                r_off += 4;
                if (r_off + param_len > rdlength) {
                    break;
                }
                const uint8_t* val = rdata + r_off;
                r_off += param_len;

                if (param_key == 4) {
                    // ipv4hint
                    for (std::size_t k = 0; k + 4 <= param_len; k += 4) {
                        char ip_buf[20] = {};
                        std::snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", val[k], val[k + 1], val[k + 2],
                                      val[k + 3]);
                        std::string ip_str(ip_buf);
                        if (std::find(outResult.ipv4Hints.begin(), outResult.ipv4Hints.end(), ip_str) ==
                            outResult.ipv4Hints.end()) {
                            outResult.ipv4Hints.push_back(ip_str);
                        }
                    }
                } else if (param_key == 5) {
                    // ech (EchConfigList bytes)
                    if (param_len > 0 && outResult.echConfigList.empty()) {
                        outResult.echConfigList.assign(val, val + param_len);
                    }
                }
            }
        }
    }

    return true;
}

bool resolveHostAndEch(const std::string& targetHost, const DohEchOptions& opts, ResolvedEndpoint& outEndpoint)
{
    outEndpoint = {};
    if (!GetHAL().isWifiConnected() || GetHAL().isScreenSleeping()) {
        return false;
    }
    std::string host = targetHost;
    const bool is_google = is_google_direct_domain(host);

    // Check in-memory endpoint cache (valid for 10 minutes while config matches)
    {
        auto& cache = get_endpoint_cache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        const uint32_t now = now_ms();
        if (cache.resolvedAtMs != 0 && (now - cache.resolvedAtMs) < 600000 &&
            cache.host == host && cache.dohEnabled == opts.dohEnabled &&
            cache.echEnabled == opts.echEnabled && cache.dohUrl1 == opts.dohUrl1 &&
            cache.dohUrl2 == opts.dohUrl2 && cache.connectIp == opts.connectIp &&
            cache.echConfigDomain == opts.echConfigDomain &&
            cache.staticEchConfigB64 == opts.staticEchConfigB64 &&
            !cache.endpoint.candidateIps.empty()) {
            outEndpoint = cache.endpoint;
            if (!cache.preferredIp.empty()) {
                auto it = std::find(outEndpoint.candidateIps.begin(), outEndpoint.candidateIps.end(), cache.preferredIp);
                if (it != outEndpoint.candidateIps.end() && it != outEndpoint.candidateIps.begin()) {
                    std::rotate(outEndpoint.candidateIps.begin(), it, it + 1);
                }
            }
            return true;
        }
    }

    // 1. Check explicit connectIp override
    if (!opts.connectIp.empty()) {
        outEndpoint.candidateIps.push_back(opts.connectIp);
    }

    // 2. Check static ECH config override
    if (opts.echEnabled && !opts.staticEchConfigB64.empty()) {
        std::vector<uint8_t> decoded;
        if (decode_base64_flexible(opts.staticEchConfigB64, decoded)) {
            outEndpoint.echConfigList = decoded;
            outEndpoint.publicName    = parse_ech_public_name(decoded);
        }
    }

    std::vector<std::string> doh_urls;
    if (!opts.dohUrl1.empty()) doh_urls.push_back(opts.dohUrl1);
    if (!opts.dohUrl2.empty()) doh_urls.push_back(opts.dohUrl2);
    if (doh_urls.empty()) {
        doh_urls.push_back("https://edge.1molchuan.top/linuxdo");
        doh_urls.push_back("https://edge.1molchuan.top/plus");
    }

    // Only perform DoH lookup if we don't already have a static connectIp override
    DnsLookupResult host_lookup;
    if (opts.dohEnabled && outEndpoint.candidateIps.empty()) {
        std::vector<DohBatchQuery> batch;
        batch.push_back({host, DNS_TYPE_A, &host_lookup});
        if (opts.echEnabled && outEndpoint.echConfigList.empty()) {
            batch.push_back({host, DNS_TYPE_HTTPS, &host_lookup});
        }

        for (const auto& url : doh_urls) {
            if (doh_query_batch(url, batch)) {
                break;
            }
        }

        if (!host_lookup.echConfigList.empty()) {
            outEndpoint.echConfigList = host_lookup.echConfigList;
            outEndpoint.publicName    = parse_ech_public_name(outEndpoint.echConfigList);
        }
    }

    // Query cloudflare-ech.com only if we still have no candidate IPs for a non-Google domain
    DnsLookupResult cf_lookup;
    std::string ech_domain = opts.echConfigDomain.empty() ? "cloudflare-ech.com" : opts.echConfigDomain;
    if (!is_google && opts.dohEnabled &&
        outEndpoint.candidateIps.empty() && host_lookup.ipv4Addrs.empty() && host_lookup.ipv4Hints.empty()) {
        std::vector<DohBatchQuery> cf_batch;
        cf_batch.push_back({ech_domain, DNS_TYPE_A, &cf_lookup});
        if (opts.echEnabled && outEndpoint.echConfigList.empty()) {
            cf_batch.push_back({ech_domain, DNS_TYPE_HTTPS, &cf_lookup});
        }
        for (const auto& url : doh_urls) {
            if (doh_query_batch(url, cf_batch)) {
                break;
            }
        }

        if (opts.echEnabled && outEndpoint.echConfigList.empty() && !cf_lookup.echConfigList.empty()) {
            outEndpoint.echConfigList   = cf_lookup.echConfigList;
            outEndpoint.publicName      = parse_ech_public_name(outEndpoint.echConfigList);
            outEndpoint.echFromFallback = true;
        }
    }

    // Built-in Cloudflare ECH fallback
    if (opts.echEnabled && outEndpoint.echConfigList.empty() && !is_google) {
        outEndpoint.echConfigList.assign(
            FALLBACK_CLOUDFLARE_ECH_CONFIG_LIST,
            FALLBACK_CLOUDFLARE_ECH_CONFIG_LIST + sizeof(FALLBACK_CLOUDFLARE_ECH_CONFIG_LIST));
        outEndpoint.publicName      = ech_domain;
        outEndpoint.echFromFallback = true;
    }

    // Populate candidate IPs
    if (outEndpoint.candidateIps.empty()) {
        for (const auto& ip : host_lookup.ipv4Addrs) outEndpoint.candidateIps.push_back(ip);
        for (const auto& ip : host_lookup.ipv4Hints) {
            if (std::find(outEndpoint.candidateIps.begin(), outEndpoint.candidateIps.end(), ip) ==
                outEndpoint.candidateIps.end()) {
                outEndpoint.candidateIps.push_back(ip);
            }
        }
        if (outEndpoint.candidateIps.empty() && !is_google) {
            for (const auto& ip : cf_lookup.ipv4Addrs) outEndpoint.candidateIps.push_back(ip);
            for (const auto& ip : cf_lookup.ipv4Hints) {
                if (std::find(outEndpoint.candidateIps.begin(), outEndpoint.candidateIps.end(), ip) ==
                    outEndpoint.candidateIps.end()) {
                    outEndpoint.candidateIps.push_back(ip);
                }
            }
        }
    }

    // OS DNS fallback
    if (outEndpoint.candidateIps.empty()) {
        struct addrinfo hints = {};
        hints.ai_family       = AF_INET;
        hints.ai_socktype     = SOCK_STREAM;
        struct addrinfo* res  = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res != nullptr) {
            for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
                auto* ipv4 = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);
                char ip_str[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, sizeof(ip_str)) != nullptr) {
                    outEndpoint.candidateIps.emplace_back(ip_str);
                }
            }
            freeaddrinfo(res);
        }
    }

    // Final Cloudflare Anycast IP fallback when ECH/Cloudflare mode is enabled
    if (outEndpoint.candidateIps.empty() && !is_google) {
        for (const char* ip : FALLBACK_CLOUDFLARE_IPS) {
            outEndpoint.candidateIps.emplace_back(ip);
        }
    }

    if (!outEndpoint.candidateIps.empty()) {
        auto& cache = get_endpoint_cache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.host               = host;
        cache.dohEnabled         = opts.dohEnabled;
        cache.echEnabled         = opts.echEnabled;
        cache.dohUrl1            = opts.dohUrl1;
        cache.dohUrl2            = opts.dohUrl2;
        cache.connectIp          = opts.connectIp;
        cache.echConfigDomain    = opts.echConfigDomain;
        cache.staticEchConfigB64 = opts.staticEchConfigB64;
        cache.resolvedAtMs       = now_ms();
        cache.endpoint           = outEndpoint;
    }

    return !outEndpoint.candidateIps.empty();
}

EchWebSocketClient::EchWebSocketClient()
{
    mbedtls_ssl_init(&_ssl);
    mbedtls_ssl_config_init(&_conf);
    mbedtls_entropy_init(&_entropy);
    mbedtls_ctr_drbg_init(&_ctr_drbg);
}

EchWebSocketClient::~EchWebSocketClient()
{
    disconnect();
}

void EchWebSocketClient::abort()
{
    _abort = true;
    int fd = _sockfd;
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
    }
}

void EchWebSocketClient::disconnect()
{
    _connected = false;
    int fd     = _sockfd;
    _sockfd    = -1;
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }

    if (_tls_initialized) {
        if (mbedtls_ssl_is_handshake_over(&_ssl) == 1) {
            mbedtls_ssl_close_notify(&_ssl);
        }
        mbedtls_ssl_free(&_ssl);
        mbedtls_ssl_config_free(&_conf);
        mbedtls_ctr_drbg_free(&_ctr_drbg);
        mbedtls_entropy_free(&_entropy);

        mbedtls_ssl_init(&_ssl);
        mbedtls_ssl_config_init(&_conf);
        mbedtls_entropy_init(&_entropy);
        mbedtls_ctr_drbg_init(&_ctr_drbg);
        _tls_initialized = false;
    }
    _rx_residual.clear();
    _frag_buffer.clear();
    _frag_opcode = 0;
}

bool EchWebSocketClient::openTcpSocket(const std::string& ip, uint16_t port, int timeoutMs)
{
    if (_abort.load()) {
        return false;
    }
    if (_sockfd >= 0) {
        close(_sockfd);
        _sockfd = -1;
    }

    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family         = AF_INET;
    dest_addr.sin_port           = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &dest_addr.sin_addr) <= 0) {
        return false;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        return false;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int res = ::connect(sock, reinterpret_cast<struct sockaddr*>(&dest_addr), sizeof(dest_addr));
    if (res < 0 && errno != EINPROGRESS) {
        close(sock);
        return false;
    }

    if (res != 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        struct timeval tv = {
            .tv_sec  = timeoutMs / 1000,
            .tv_usec = (timeoutMs % 1000) * 1000,
        };
        int sel = select(sock + 1, nullptr, &wfds, nullptr, &tv);
        if (sel <= 0) {
            close(sock);
            return false;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            close(sock);
            return false;
        }
    }

    // Restore blocking mode with socket read/write timeouts
    fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    struct timeval rw_tv = {.tv_sec = 4, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rw_tv, sizeof(rw_tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &rw_tv, sizeof(rw_tv));

    _sockfd = sock;
    return true;
}

static int socket_bio_send(void* ctx, const unsigned char* buf, std::size_t len)
{
    int fd = *static_cast<int*>(ctx);
    if (fd < 0) return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    ssize_t ret = ::send(fd, buf, len, 0);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return static_cast<int>(ret);
}

static int socket_bio_recv(void* ctx, unsigned char* buf, std::size_t len)
{
    int fd = *static_cast<int*>(ctx);
    if (fd < 0) return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    ssize_t ret = ::recv(fd, buf, len, 0);
    if (ret == 0) {
        return 0; // Clean EOF
    }
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        if (errno == ECONNRESET || errno == EPIPE) {
            return MBEDTLS_ERR_NET_CONN_RESET;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return static_cast<int>(ret);
}

bool EchWebSocketClient::performTlsHandshake(const std::string& targetHost,
                                             const std::string& sniHostname,
                                             bool preferTls13,
                                             std::string& outError)
{
    if (_abort.load()) {
        outError = "Handshake aborted";
        return false;
    }

#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
    psa_crypto_init();
#endif

    if (_tls_initialized) {
        mbedtls_ssl_free(&_ssl);
        mbedtls_ssl_config_free(&_conf);
        mbedtls_ctr_drbg_free(&_ctr_drbg);
        mbedtls_entropy_free(&_entropy);
        mbedtls_ssl_init(&_ssl);
        mbedtls_ssl_config_init(&_conf);
        mbedtls_entropy_init(&_entropy);
        mbedtls_ctr_drbg_init(&_ctr_drbg);
    }
    _tls_initialized = true;

    int ret = mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy, nullptr, 0);
    if (ret != 0) {
        outError = "DRBG seed failed";
        return false;
    }

    ret = mbedtls_ssl_config_defaults(&_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        outError = "SSL config defaults failed";
        return false;
    }

    mbedtls_ssl_conf_rng(&_conf, mbedtls_ctr_drbg_random, &_ctr_drbg);
    esp_crt_bundle_attach(&_conf);

    // When using outer SNI ("cloudflare-ech.com" or custom domain fronting) that differs from targetHost,
    // verify the CA signature chain via esp_crt_bundle_attach while allowing the edge cert SAN (OPTIONAL)
    if (!sniHostname.empty() && sniHostname != targetHost) {
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    } else {
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    }

#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
    if (preferTls13) {
        mbedtls_ssl_conf_min_tls_version(&_conf, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&_conf, MBEDTLS_SSL_VERSION_TLS1_3);
    }
#endif

    ret = mbedtls_ssl_setup(&_ssl, &_conf);
    if (ret != 0) {
        outError = "mbedtls_ssl_setup failed";
        return false;
    }

    const std::string& effective_sni = sniHostname.empty() ? targetHost : sniHostname;
    mbedtls_ssl_set_hostname(&_ssl, effective_sni.c_str());
    mbedtls_ssl_set_bio(&_ssl, &_sockfd, socket_bio_send, socket_bio_recv, nullptr);

    uint32_t start_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    while ((ret = mbedtls_ssl_handshake(&_ssl)) != 0) {
        if (_abort.load() || !GetHAL().isWifiConnected() || GetHAL().isScreenSleeping()) {
            outError = "TLS handshake aborted";
            return false;
        }
        if ((static_cast<uint32_t>(esp_timer_get_time() / 1000) - start_ms) > 5000) {
            outError = "TLS handshake timed out";
            return false;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char err_buf[96] = {};
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            outError = std::string("TLS handshake failed: ") + err_buf;
            return false;
        }
        vTaskDelay(1);
    }

    return true;
}

int EchWebSocketClient::tlsWriteAll(const uint8_t* buf, std::size_t len)
{
    std::size_t written = 0;
    while (written < len) {
        int ret = 0;
        if (_is_tls) {
            ret = mbedtls_ssl_write(&_ssl, buf + written, len - written);
            if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
                continue;
            }
        } else {
            ret = ::send(_sockfd, buf + written, len - written, 0);
        }
        if (ret <= 0) {
            return -1;
        }
        written += static_cast<std::size_t>(ret);
    }
    return static_cast<int>(written);
}

int EchWebSocketClient::tlsReadExact(uint8_t* buf, std::size_t len, int timeoutMs)
{
    std::size_t got = 0;
    while (got < len && !_rx_residual.empty()) {
        buf[got++] = _rx_residual.front();
        _rx_residual.erase(_rx_residual.begin());
    }
    if (got == len) {
        return static_cast<int>(got);
    }

    while (got < len) {
        // Once at least 1 byte of the target read is read, wait up to 4000ms for the remaining bytes
        // so we never return a partial WebSocket header when timeoutMs == 0
        int effective_timeout = (got > 0 && timeoutMs < 1000) ? 4000 : timeoutMs;
        if (_is_tls && mbedtls_ssl_get_bytes_avail(&_ssl) == 0 && effective_timeout >= 0) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(_sockfd, &rfds);
            struct timeval tv = {
                .tv_sec  = effective_timeout / 1000,
                .tv_usec = (effective_timeout % 1000) * 1000,
            };
            int sel = select(_sockfd + 1, &rfds, nullptr, nullptr, &tv);
            if (sel == 0) {
                return got > 0 ? static_cast<int>(got) : 0;
            }
            if (sel < 0) {
                return -1;
            }
        } else if (!_is_tls && effective_timeout >= 0) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(_sockfd, &rfds);
            struct timeval tv = {
                .tv_sec  = effective_timeout / 1000,
                .tv_usec = (effective_timeout % 1000) * 1000,
            };
            int sel = select(_sockfd + 1, &rfds, nullptr, nullptr, &tv);
            if (sel == 0) {
                return got > 0 ? static_cast<int>(got) : 0;
            }
            if (sel < 0) {
                return -1;
            }
        }

        int ret = 0;
        if (_is_tls) {
            ret = mbedtls_ssl_read(&_ssl, buf + got, len - got);
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                continue;
            }
#endif
        } else {
            ret = ::recv(_sockfd, buf + got, len - got, 0);
        }

        if (ret <= 0) {
            return -1;
        }
        got += static_cast<std::size_t>(ret);
    }
    return static_cast<int>(got);
}

bool EchWebSocketClient::performWsUpgrade(const std::string& hostHeader,
                                          const std::string& pathAndQuery,
                                          const std::string& bearerToken,
                                          const std::string& apiKeyHeader,
                                          std::string& outError)
{
    uint8_t rand_key[16];
    esp_fill_random(rand_key, sizeof(rand_key));
    unsigned char ws_key_b64[32] = {};
    std::size_t olen             = 0;
    mbedtls_base64_encode(ws_key_b64, sizeof(ws_key_b64), &olen, rand_key, sizeof(rand_key));

    std::string req = "GET " + pathAndQuery + " HTTP/1.1\r\n";
    req += "Host: " + hostHeader + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "Sec-WebSocket-Key: " + std::string(reinterpret_cast<char*>(ws_key_b64), olen) + "\r\n";
    req += "User-Agent: M5StopWatch-GeminiLive/1.0\r\n";
    if (!bearerToken.empty()) {
        req += "Authorization: Bearer " + bearerToken + "\r\n";
    }
    if (!apiKeyHeader.empty()) {
        req += "x-goog-api-key: " + apiKeyHeader + "\r\n";
    }
    req += "\r\n";

    if (tlsWriteAll(reinterpret_cast<const uint8_t*>(req.data()), req.size()) < 0) {
        outError = "Failed to send WebSocket upgrade request";
        return false;
    }

    std::string header_buf;
    header_buf.reserve(1024);
    uint8_t chunk[256];
    std::size_t header_end_pos = std::string::npos;

    while (header_buf.size() < 4096) {
        int n = 0;
        if (_is_tls) {
            n = mbedtls_ssl_read(&_ssl, chunk, sizeof(chunk));
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                continue;
            }
#endif
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
        } else {
            n = ::recv(_sockfd, chunk, sizeof(chunk), 0);
        }
        if (n <= 0) {
            outError = "WebSocket upgrade response read failed";
            return false;
        }
        header_buf.append(reinterpret_cast<char*>(chunk), static_cast<std::size_t>(n));
        header_end_pos = header_buf.find("\r\n\r\n");
        if (header_end_pos != std::string::npos) {
            break;
        }
    }

    if (header_end_pos == std::string::npos) {
        outError = "WebSocket upgrade headers incomplete";
        return false;
    }

    std::string headers_only = header_buf.substr(0, header_end_pos);
    // Preserve any bytes after \r\n\r\n in _rx_residual
    std::size_t body_start = header_end_pos + 4;
    if (body_start < header_buf.size()) {
        _rx_residual.assign(header_buf.begin() + body_start, header_buf.end());
    }

    if (headers_only.find(" 101 ") == std::string::npos) {
        std::size_t first_line_end = headers_only.find("\r\n");
        std::string status_line =
            (first_line_end != std::string::npos) ? headers_only.substr(0, first_line_end) : headers_only;
        outError = "WS upgrade rejected: " + status_line;
        return false;
    }

    return true;
}

bool EchWebSocketClient::connect(const std::string& wsUrl,
                                 const std::string& bearerToken,
                                 const std::string& apiKeyHeader,
                                 const DohEchOptions& opts,
                                 std::string& outError)
{
    _abort = false;
    disconnect();

    if (_abort.load()) {
        outError = "Aborted";
        return false;
    }

    std::string host;
    uint16_t port = 443;
    std::string path_and_query;
    if (!parse_ws_url(wsUrl, _is_tls, host, port, path_and_query)) {
        outError = "Invalid WebSocket URL";
        return false;
    }

    ResolvedEndpoint resolved;
    if (!resolveHostAndEch(host, opts, resolved)) {
        outError = "DNS / DoH resolution failed for " + host;
        return false;
    }

    if (_abort.load()) {
        outError = "Aborted";
        return false;
    }

    const bool is_google = is_google_direct_domain(host);
    // Determine SNI order:
    // When ECH / outer SNI protection is enabled for a Cloudflare Worker domain (e.g. *.workers.dev),
    // try the public ECH outer name ("cloudflare-ech.com") first so domestic DPI never sees *.workers.dev in plaintext SNI;
    // if the edge requires direct SNI (e.g. custom domain), automatically fall back to `host` and remember the working SNI.
    std::vector<std::string> sni_candidates;
    if (_is_tls && opts.echEnabled && !is_google && !resolved.publicName.empty() && resolved.publicName != host) {
        sni_candidates.push_back(resolved.publicName);
        sni_candidates.push_back(host);
        {
            auto& cache = get_endpoint_cache();
            std::lock_guard<std::mutex> lock(cache.mutex);
            if (!cache.preferredSni.empty() && cache.host == host) {
                auto it = std::find(sni_candidates.begin(), sni_candidates.end(), cache.preferredSni);
                if (it != sni_candidates.end() && it != sni_candidates.begin()) {
                    std::rotate(sni_candidates.begin(), it, it + 1);
                }
            }
        }
    } else {
        sni_candidates.push_back(host);
    }

    for (const auto& ip : resolved.candidateIps) {
        for (const auto& sni_name : sni_candidates) {
            if (_abort.load()) {
                outError = "Aborted";
                disconnect();
                return false;
            }

            mclog::tagInfo(_tag, "Connecting TCP {}:{} (Host={}, SNI={})...", ip, port, host, sni_name);
            if (!openTcpSocket(ip, port, 2500)) {
                outError = "TCP connect timeout to " + ip;
                break;  // IP unreachable; skip remaining SNI candidates on this IP
            }

            if (_is_tls) {
                if (!performTlsHandshake(host, sni_name, true, outError)) {
                    mclog::tagWarn(_tag, "TLS handshake failed with {} (SNI={}): {}", ip, sni_name, outError);
                    disconnect();
                    continue;
                }
            }

            if (!performWsUpgrade(host, path_and_query, bearerToken, apiKeyHeader, outError)) {
                mclog::tagWarn(_tag, "WS upgrade failed with {} (SNI={}): {}", ip, sni_name, outError);
                disconnect();
                // If 401 or 403, check if this was with outer SNI (domain fronting)
                if (outError.find("401") != std::string::npos || outError.find("403") != std::string::npos) {
                    if (sni_name == host) {
                        return false;
                    }
                    // Outer SNI (domain fronting) was rejected by CDN/gateway; continue and try direct host SNI!
                }
                continue;
            }

            {
                auto& cache = get_endpoint_cache();
                std::lock_guard<std::mutex> lock(cache.mutex);
                cache.preferredIp  = ip;
                cache.preferredSni = sni_name;
            }

            _connected    = true;
            _connected_ip = ip;
            mclog::tagInfo(_tag, "WebSocket connected via {} (SNI={})", ip, sni_name);
            return true;
        }
    }

    // All candidate IPs failed; invalidate endpoint cache so next retry refreshes DNS
    {
        auto& cache = get_endpoint_cache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.resolvedAtMs = 0;
    }

    return false;
}

bool EchWebSocketClient::sendWsFrame(uint8_t opcode, const uint8_t* payload, std::size_t len)
{
    if (!_connected || _sockfd < 0) {
        return false;
    }

    uint8_t header[14] = {};
    std::size_t hlen   = 0;
    header[hlen++]     = 0x80 | (opcode & 0x0F);  // FIN = 1

    if (len <= 125) {
        header[hlen++] = 0x80 | static_cast<uint8_t>(len);
    } else if (len <= 65535) {
        header[hlen++] = 0x80 | 126;
        header[hlen++] = static_cast<uint8_t>((len >> 8) & 0xFF);
        header[hlen++] = static_cast<uint8_t>(len & 0xFF);
    } else {
        header[hlen++] = 0x80 | 127;
        header[hlen++] = 0;
        header[hlen++] = 0;
        header[hlen++] = 0;
        header[hlen++] = 0;
        header[hlen++] = static_cast<uint8_t>((len >> 24) & 0xFF);
        header[hlen++] = static_cast<uint8_t>((len >> 16) & 0xFF);
        header[hlen++] = static_cast<uint8_t>((len >> 8) & 0xFF);
        header[hlen++] = static_cast<uint8_t>(len & 0xFF);
    }

    uint8_t mask[4];
    esp_fill_random(mask, sizeof(mask));
    std::memcpy(&header[hlen], mask, 4);
    hlen += 4;

    std::vector<uint8_t> frame(hlen + len);
    std::memcpy(frame.data(), header, hlen);
    for (std::size_t i = 0; i < len; ++i) {
        frame[hlen + i] = payload[i] ^ mask[i & 3];
    }

    if (tlsWriteAll(frame.data(), frame.size()) < 0) {
        _connected = false;
        return false;
    }
    return true;
}

bool EchWebSocketClient::sendText(const std::string& text)
{
    return sendWsFrame(0x01, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool EchWebSocketClient::sendBinary(const uint8_t* data, std::size_t len)
{
    return sendWsFrame(0x02, data, len);
}

bool EchWebSocketClient::sendPing()
{
    return sendWsFrame(0x09, nullptr, 0);
}

bool EchWebSocketClient::poll(int timeoutMs,
                              const OnTextCallback& onText,
                              const OnBinaryCallback& onBinary,
                              const OnErrorCallback& onError)
{
    if (!_connected || _sockfd < 0) {
        return false;
    }

    uint8_t hdr2[2] = {};
    int r = tlsReadExact(hdr2, 2, timeoutMs);
    if (r == 0) {
        return true;  // Timeout, no frame ready
    }
    if (r < 2) {
        _connected = false;
        if (onError) onError("Connection closed by peer");
        return false;
    }

    const bool fin       = (hdr2[0] & 0x80) != 0;
    const uint8_t opcode = hdr2[0] & 0x0F;
    const bool masked    = (hdr2[1] & 0x80) != 0;
    uint64_t payload_len = hdr2[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2] = {};
        if (tlsReadExact(ext, 2, 4000) != 2) {
            _connected = false;
            return false;
        }
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8] = {};
        if (tlsReadExact(ext, 8, 4000) != 8) {
            _connected = false;
            return false;
        }
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    if (payload_len > 256 * 1024) {
        _connected = false;
        if (onError) onError("WebSocket frame too large");
        return false;
    }

    uint8_t mask_key[4] = {};
    if (masked) {
        if (tlsReadExact(mask_key, 4, 4000) != 4) {
            _connected = false;
            return false;
        }
    }

    std::vector<uint8_t> payload(static_cast<std::size_t>(payload_len));
    if (payload_len > 0) {
        if (tlsReadExact(payload.data(), payload.size(), 6000) != static_cast<int>(payload.size())) {
            _connected = false;
            if (onError) onError("WebSocket payload read truncated");
            return false;
        }
        if (masked) {
            for (std::size_t i = 0; i < payload.size(); ++i) {
                payload[i] ^= mask_key[i & 3];
            }
        }
    }

    // Handle control frames (Close = 0x08, Ping = 0x09, Pong = 0x0A)
    if (opcode == 0x08) {
        sendWsFrame(0x08, nullptr, 0);
        _connected = false;
        if (onError) onError("Server closed WebSocket");
        return false;
    } else if (opcode == 0x09) {
        sendWsFrame(0x0A, payload.data(), payload.size());
        return true;
    } else if (opcode == 0x0A) {
        return true;
    }

    // Handle data frames & fragmentation
    if (opcode == 0x01 || opcode == 0x02) {
        if (fin) {
            if (opcode == 0x01 && onText) {
                onText(std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));
            } else if (opcode == 0x02 && onBinary) {
                onBinary(payload.data(), payload.size());
            }
        } else {
            _frag_opcode = opcode;
            _frag_buffer = std::move(payload);
        }
    } else if (opcode == 0x00) {
        if (_frag_buffer.size() + payload.size() <= 256 * 1024) {
            _frag_buffer.insert(_frag_buffer.end(), payload.begin(), payload.end());
        }
        if (fin) {
            if (_frag_opcode == 0x01 && onText) {
                onText(std::string(reinterpret_cast<const char*>(_frag_buffer.data()), _frag_buffer.size()));
            } else if (_frag_opcode == 0x02 && onBinary) {
                onBinary(_frag_buffer.data(), _frag_buffer.size());
            }
            _frag_buffer.clear();
            _frag_opcode = 0;
        }
    }

    return true;
}

}  // namespace gemini
