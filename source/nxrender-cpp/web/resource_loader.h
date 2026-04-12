// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <deque>

namespace NXRender {

// ==================================================================
// Resource types
// ==================================================================

enum class ResourceType : uint8_t {
    Document, Stylesheet, Script, Image, Font, Media,
    XHR, Fetch, Beacon, WebSocket, EventSource,
    Manifest, ServiceWorker, SharedWorker, Worker,
    Preflight, Ping, CSPReport, Other
};

enum class ResourcePriority : uint8_t {
    VeryHigh, High, Medium, Low, VeryLow, Idle
};

enum class ResourceState : uint8_t {
    Pending, Queued, Connecting, Sending, Receiving, Complete, Failed, Cancelled
};

// ==================================================================
// HTTP Headers
// ==================================================================

class HTTPHeaders {
public:
    void set(const std::string& name, const std::string& value);
    void append(const std::string& name, const std::string& value);
    std::string get(const std::string& name) const;
    bool has(const std::string& name) const;
    void remove(const std::string& name);
    std::vector<std::pair<std::string, std::string>> entries() const;

    // Common headers
    std::string contentType() const { return get("Content-Type"); }
    std::string cacheControl() const { return get("Cache-Control"); }
    std::string etag() const { return get("ETag"); }
    int64_t contentLength() const;
    std::string lastModified() const { return get("Last-Modified"); }

private:
    std::vector<std::pair<std::string, std::string>> headers_;
};

// ==================================================================
// Request
// ==================================================================

struct ResourceRequest {
    std::string url;
    std::string method = "GET";
    HTTPHeaders headers;
    std::vector<uint8_t> body;
    ResourceType type = ResourceType::Other;
    ResourcePriority priority = ResourcePriority::Medium;

    // Fetch metadata
    std::string referrer;
    enum class ReferrerPolicy : uint8_t {
        NoReferrer, NoReferrerWhenDowngrade, Origin, OriginWhenCrossOrigin,
        SameOrigin, StrictOrigin, StrictOriginWhenCrossOrigin, UnsafeUrl
    } referrerPolicy = ReferrerPolicy::StrictOriginWhenCrossOrigin;

    enum class CredentialsMode : uint8_t { Omit, SameOrigin, Include } credentials = CredentialsMode::SameOrigin;
    enum class CacheMode : uint8_t { Default, NoStore, Reload, NoCache, ForceCache, OnlyIfCached } cache = CacheMode::Default;
    enum class RedirectMode : uint8_t { Follow, Error, Manual } redirect = RedirectMode::Follow;
    enum class Mode : uint8_t { SameOrigin, NoCors, Cors, Navigate } mode = Mode::Cors;

    std::string integrity; // SRI hash
    bool keepalive = false;
    int timeout = 0; // ms, 0 = no timeout
};

// ==================================================================
// Response
// ==================================================================

struct ResourceResponse {
    int statusCode = 0;
    std::string statusText;
    std::string url;       // Final URL after redirects
    HTTPHeaders headers;
    std::vector<uint8_t> body;

    bool ok() const { return statusCode >= 200 && statusCode < 300; }
    bool redirected = false;

    enum class Type : uint8_t { Basic, Cors, Default, Error, Opaque, OpaqueRedirect } type = Type::Default;

    // Timing
    double requestStart = 0;
    double responseStart = 0;
    double responseEnd = 0;
    double dnsLookup = 0;
    double tcpConnect = 0;
    double tlsHandshake = 0;

    // Body access
    std::string text() const;
    std::vector<uint8_t> arrayBuffer() const { return body; }

    // Cache info
    bool fromCache = false;
    bool fromServiceWorker = false;
};

// ==================================================================
// Resource Loader
// ==================================================================

class ResourceLoader {
public:
    static ResourceLoader& instance();

    using ResponseCallback = std::function<void(const ResourceResponse&)>;
    using ProgressCallback = std::function<void(size_t loaded, size_t total)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    struct LoadHandle {
        uint64_t id = 0;
        ResourceState state = ResourceState::Pending;
        void cancel();
    };

    // Start a load
    LoadHandle load(const ResourceRequest& request,
                     ResponseCallback onComplete,
                     ProgressCallback onProgress = nullptr,
                     ErrorCallback onError = nullptr);

    // Quick helpers
    LoadHandle fetch(const std::string& url, ResponseCallback onComplete);
    LoadHandle fetchJSON(const std::string& url,
                          std::function<void(const std::string& json)> onComplete);

    // Queue management
    void setMaxConcurrent(int max) { maxConcurrent_ = max; }
    int pendingCount() const;
    int activeCount() const;
    void cancelAll();
    void pauseAll();
    void resumeAll();

    // Preloading
    void preload(const std::string& url, ResourceType type);
    void prefetch(const std::string& url);
    void preconnect(const std::string& origin);
    void dnsPrefetch(const std::string& hostname);

    // Priority override
    void setPriority(uint64_t loadId, ResourcePriority priority);

    // Stats
    struct Stats {
        size_t totalRequests = 0;
        size_t completedRequests = 0;
        size_t failedRequests = 0;
        size_t cancelledRequests = 0;
        size_t bytesReceived = 0;
        size_t bytesSent = 0;
        double totalTime = 0;
    };
    Stats stats() const;

private:
    ResourceLoader() = default;
    mutable std::mutex mutex_;
    int maxConcurrent_ = 6;
    uint64_t nextId_ = 1;
    bool paused_ = false;

    struct PendingLoad {
        uint64_t id;
        ResourceRequest request;
        ResponseCallback onComplete;
        ProgressCallback onProgress;
        ErrorCallback onError;
        ResourceState state;
    };
    std::deque<PendingLoad> queue_;
    std::vector<PendingLoad> active_;
    Stats stats_;

    void processQueue();
};

// ==================================================================
// HTTP Cache
// ==================================================================

struct CacheEntry {
    std::string url;
    ResourceResponse response;
    double timestamp = 0;
    double maxAge = 0;     // seconds
    bool mustRevalidate = false;
    std::string etag;
    std::string lastModified;

    bool isExpired(double now) const;
    bool isStale(double now) const;
};

class HTTPCache {
public:
    static HTTPCache& instance();

    // Lookup
    const CacheEntry* get(const std::string& url) const;
    void put(const std::string& url, const ResourceResponse& response);
    void remove(const std::string& url);
    void clear();

    // Cache control
    void setMaxSize(size_t bytes) { maxSize_ = bytes; }
    size_t currentSize() const;
    void evict();

    // Match request against cache
    enum class CacheMatch { HIT, MISS, STALE, REVALIDATE };
    CacheMatch match(const ResourceRequest& request) const;

private:
    HTTPCache() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, CacheEntry> entries_;
    size_t maxSize_ = 100 * 1024 * 1024; // 100MB
};

// ==================================================================
// CORS — Cross-Origin Resource Sharing
// ==================================================================

class CORSChecker {
public:
    struct CORSResult {
        bool allowed = false;
        std::string error;
    };

    static CORSResult check(const ResourceRequest& request,
                              const ResourceResponse& response);

    static bool isSimpleRequest(const ResourceRequest& request);
    static bool needsPreflight(const ResourceRequest& request);

    static ResourceRequest createPreflight(const ResourceRequest& original);
    static CORSResult checkPreflight(const ResourceResponse& preflightResponse,
                                       const ResourceRequest& original);

private:
    static std::vector<std::string> parseHeaderList(const std::string& header);
    static bool isSimpleHeader(const std::string& name, const std::string& value);
    static bool originMatches(const std::string& origin, const std::string& pattern);
};

// ==================================================================
// Content Security Policy
// ==================================================================

class CSPPolicy {
public:
    void parse(const std::string& policyHeader);

    bool allowsScript(const std::string& url) const;
    bool allowsStyle(const std::string& url) const;
    bool allowsImage(const std::string& url) const;
    bool allowsFont(const std::string& url) const;
    bool allowsMedia(const std::string& url) const;
    bool allowsConnect(const std::string& url) const;
    bool allowsFrame(const std::string& url) const;
    bool allowsWorker(const std::string& url) const;
    bool allowsInlineScript() const;
    bool allowsInlineStyle() const;
    bool allowsEval() const;

    struct Directive {
        std::string name;
        std::vector<std::string> sources;
    };
    const std::vector<Directive>& directives() const { return directives_; }

private:
    std::vector<Directive> directives_;
    bool matchesSource(const std::string& url,
                        const std::vector<std::string>& sources) const;
};

// ==================================================================
// Subresource Integrity
// ==================================================================

class SRIChecker {
public:
    static bool verify(const std::vector<uint8_t>& data, const std::string& integrity);
    static std::string computeHash(const std::vector<uint8_t>& data,
                                     const std::string& algorithm);
};

} // namespace NXRender
