// Copyright (c) 2026 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include "resource_loader.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <cctype>
#include <chrono>

namespace NXRender {

// ==================================================================
// HTTPHeaders
// ==================================================================

void HTTPHeaders::set(const std::string& name, const std::string& value) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto& h : headers_) {
        if (h.first == lower) { h.second = value; return; }
    }
    headers_.push_back({lower, value});
}

void HTTPHeaders::append(const std::string& name, const std::string& value) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto& h : headers_) {
        if (h.first == lower) { h.second += ", " + value; return; }
    }
    headers_.push_back({lower, value});
}

std::string HTTPHeaders::get(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& h : headers_) {
        if (h.first == lower) return h.second;
    }
    return "";
}

bool HTTPHeaders::has(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& h : headers_) {
        if (h.first == lower) return true;
    }
    return false;
}

void HTTPHeaders::remove(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    headers_.erase(std::remove_if(headers_.begin(), headers_.end(),
        [&lower](const auto& h) { return h.first == lower; }), headers_.end());
}

std::vector<std::pair<std::string, std::string>> HTTPHeaders::entries() const {
    return headers_;
}

int64_t HTTPHeaders::contentLength() const {
    std::string cl = get("Content-Length");
    if (cl.empty()) return -1;
    return std::strtoll(cl.c_str(), nullptr, 10);
}

// ==================================================================
// ResourceResponse
// ==================================================================

std::string ResourceResponse::text() const {
    return std::string(body.begin(), body.end());
}

// ==================================================================
// ResourceLoader
// ==================================================================

ResourceLoader& ResourceLoader::instance() {
    static ResourceLoader inst;
    return inst;
}

ResourceLoader::LoadHandle ResourceLoader::load(const ResourceRequest& request,
                                                   ResponseCallback onComplete,
                                                   ProgressCallback onProgress,
                                                   ErrorCallback onError) {
    std::lock_guard<std::mutex> lock(mutex_);

    LoadHandle handle;
    handle.id = nextId_++;
    handle.state = ResourceState::Queued;

    PendingLoad pl;
    pl.id = handle.id;
    pl.request = request;
    pl.onComplete = onComplete;
    pl.onProgress = onProgress;
    pl.onError = onError;
    pl.state = ResourceState::Queued;

    // Insert by priority
    auto it = queue_.begin();
    while (it != queue_.end()) {
        if (static_cast<int>(request.priority) < static_cast<int>(it->request.priority)) {
            break;
        }
        ++it;
    }
    queue_.insert(it, pl);

    stats_.totalRequests++;
    processQueue();

    return handle;
}

ResourceLoader::LoadHandle ResourceLoader::fetch(const std::string& url,
                                                    ResponseCallback onComplete) {
    ResourceRequest req;
    req.url = url;
    req.method = "GET";
    return load(req, onComplete);
}

ResourceLoader::LoadHandle ResourceLoader::fetchJSON(const std::string& url,
                                                        std::function<void(const std::string&)> onComplete) {
    ResourceRequest req;
    req.url = url;
    req.method = "GET";
    req.headers.set("Accept", "application/json");
    return load(req, [onComplete](const ResourceResponse& resp) {
        if (onComplete) onComplete(resp.text());
    });
}

int ResourceLoader::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

int ResourceLoader::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(active_.size());
}

void ResourceLoader::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.cancelledRequests += queue_.size() + active_.size();
    queue_.clear();
    active_.clear();
}

void ResourceLoader::pauseAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = true;
}

void ResourceLoader::resumeAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = false;
    processQueue();
}

void ResourceLoader::preload(const std::string& url, ResourceType type) {
    ResourceRequest req;
    req.url = url;
    req.type = type;
    req.priority = ResourcePriority::High;
    load(req, [](const ResourceResponse&) {});
}

void ResourceLoader::prefetch(const std::string& url) {
    ResourceRequest req;
    req.url = url;
    req.priority = ResourcePriority::Idle;
    load(req, [](const ResourceResponse&) {});
}

void ResourceLoader::preconnect(const std::string& /*origin*/) {
    // TCP connection warmup — platform-specific
}

void ResourceLoader::dnsPrefetch(const std::string& /*hostname*/) {
    // DNS resolution warmup — platform-specific
}

void ResourceLoader::setPriority(uint64_t loadId, ResourcePriority priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pl : queue_) {
        if (pl.id == loadId) {
            pl.request.priority = priority;
            break;
        }
    }
}

ResourceLoader::Stats ResourceLoader::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void ResourceLoader::processQueue() {
    if (paused_) return;

    while (!queue_.empty() && static_cast<int>(active_.size()) < maxConcurrent_) {
        auto pl = queue_.front();
        queue_.pop_front();
        pl.state = ResourceState::Connecting;
        active_.push_back(pl);
        // Actual network I/O delegated to platform layer
    }
}

void ResourceLoader::LoadHandle::cancel() {
    auto& loader = ResourceLoader::instance();
    std::lock_guard<std::mutex> lock(loader.mutex_);
    // Remove from queue
    loader.queue_.erase(
        std::remove_if(loader.queue_.begin(), loader.queue_.end(),
                        [this](const PendingLoad& pl) { return pl.id == id; }),
        loader.queue_.end());
    // Remove from active
    loader.active_.erase(
        std::remove_if(loader.active_.begin(), loader.active_.end(),
                        [this](const PendingLoad& pl) { return pl.id == id; }),
        loader.active_.end());
    state = ResourceState::Cancelled;
    loader.stats_.cancelledRequests++;
}

// ==================================================================
// HTTPCache
// ==================================================================

HTTPCache& HTTPCache::instance() {
    static HTTPCache inst;
    return inst;
}

const CacheEntry* HTTPCache::get(const std::string& url) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(url);
    return (it != entries_.end()) ? &it->second : nullptr;
}

void HTTPCache::put(const std::string& url, const ResourceResponse& response) {
    std::lock_guard<std::mutex> lock(mutex_);

    CacheEntry entry;
    entry.url = url;
    entry.response = response;

    auto now = std::chrono::steady_clock::now();
    entry.timestamp = std::chrono::duration<double>(now.time_since_epoch()).count();

    // Parse cache-control
    std::string cc = response.headers.cacheControl();
    if (!cc.empty()) {
        auto maxAgePos = cc.find("max-age=");
        if (maxAgePos != std::string::npos) {
            entry.maxAge = std::strtof(cc.c_str() + maxAgePos + 8, nullptr);
        }
        entry.mustRevalidate = (cc.find("must-revalidate") != std::string::npos);
    }

    entry.etag = response.headers.etag();
    entry.lastModified = response.headers.lastModified();

    entries_[url] = entry;

    // Evict if needed
    while (currentSize() > maxSize_) evict();
}

void HTTPCache::remove(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(url);
}

void HTTPCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

size_t HTTPCache::currentSize() const {
    size_t total = 0;
    for (const auto& [_, e] : entries_) {
        total += e.response.body.size();
    }
    return total;
}

void HTTPCache::evict() {
    // Evict oldest entry
    double oldest = std::numeric_limits<double>::max();
    std::string oldestUrl;
    for (const auto& [url, e] : entries_) {
        if (e.timestamp < oldest) {
            oldest = e.timestamp;
            oldestUrl = url;
        }
    }
    if (!oldestUrl.empty()) entries_.erase(oldestUrl);
}

HTTPCache::CacheMatch HTTPCache::match(const ResourceRequest& request) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(request.url);
    if (it == entries_.end()) return CacheMatch::MISS;

    auto now = std::chrono::steady_clock::now();
    double nowSec = std::chrono::duration<double>(now.time_since_epoch()).count();

    if (it->second.isExpired(nowSec)) {
        if (!it->second.etag.empty() || !it->second.lastModified.empty()) {
            return CacheMatch::REVALIDATE;
        }
        return CacheMatch::STALE;
    }
    return CacheMatch::HIT;
}

bool CacheEntry::isExpired(double now) const {
    if (maxAge > 0) return (now - timestamp) > maxAge;
    return false;
}

bool CacheEntry::isStale(double now) const {
    return isExpired(now);
}

// ==================================================================
// CORSChecker
// ==================================================================

CORSChecker::CORSResult CORSChecker::check(const ResourceRequest& request,
                                               const ResourceResponse& response) {
    CORSResult result;

    std::string origin = request.headers.get("Origin");
    if (origin.empty()) { result.allowed = true; return result; }

    std::string allowOrigin = response.headers.get("Access-Control-Allow-Origin");
    if (allowOrigin.empty()) {
        result.error = "No Access-Control-Allow-Origin header";
        return result;
    }

    if (allowOrigin == "*" && request.credentials != ResourceRequest::CredentialsMode::Include) {
        result.allowed = true;
        return result;
    }

    if (originMatches(origin, allowOrigin)) {
        result.allowed = true;

        // Check credentials
        if (request.credentials == ResourceRequest::CredentialsMode::Include) {
            std::string allowCreds = response.headers.get("Access-Control-Allow-Credentials");
            if (allowCreds != "true") {
                result.allowed = false;
                result.error = "Credentials not allowed";
            }
        }
    } else {
        result.error = "Origin " + origin + " not allowed by " + allowOrigin;
    }

    return result;
}

bool CORSChecker::isSimpleRequest(const ResourceRequest& request) {
    if (request.method != "GET" && request.method != "HEAD" && request.method != "POST")
        return false;

    auto headers = request.headers.entries();
    for (const auto& [name, value] : headers) {
        if (!isSimpleHeader(name, value)) return false;
    }
    return true;
}

bool CORSChecker::needsPreflight(const ResourceRequest& request) {
    return !isSimpleRequest(request);
}

ResourceRequest CORSChecker::createPreflight(const ResourceRequest& original) {
    ResourceRequest preflight;
    preflight.url = original.url;
    preflight.method = "OPTIONS";
    preflight.headers.set("Access-Control-Request-Method", original.method);

    std::string headerList;
    for (const auto& [name, value] : original.headers.entries()) {
        if (!isSimpleHeader(name, value)) {
            if (!headerList.empty()) headerList += ", ";
            headerList += name;
        }
    }
    if (!headerList.empty()) {
        preflight.headers.set("Access-Control-Request-Headers", headerList);
    }

    return preflight;
}

CORSChecker::CORSResult CORSChecker::checkPreflight(const ResourceResponse& preflightResponse,
                                                        const ResourceRequest& original) {
    CORSResult result;

    std::string allowMethods = preflightResponse.headers.get("Access-Control-Allow-Methods");
    if (allowMethods.find(original.method) == std::string::npos) {
        result.error = "Method " + original.method + " not allowed";
        return result;
    }

    result.allowed = true;
    return result;
}

std::vector<std::string> CORSChecker::parseHeaderList(const std::string& header) {
    std::vector<std::string> result;
    std::istringstream ss(header);
    std::string token;
    while (std::getline(ss, token, ',')) {
        while (!token.empty() && std::isspace(token.front())) token.erase(token.begin());
        while (!token.empty() && std::isspace(token.back())) token.pop_back();
        if (!token.empty()) result.push_back(token);
    }
    return result;
}

bool CORSChecker::isSimpleHeader(const std::string& name, const std::string& value) {
    if (name == "accept" || name == "accept-language" || name == "content-language") return true;
    if (name == "content-type") {
        return value.find("application/x-www-form-urlencoded") != std::string::npos ||
               value.find("multipart/form-data") != std::string::npos ||
               value.find("text/plain") != std::string::npos;
    }
    return false;
}

bool CORSChecker::originMatches(const std::string& origin, const std::string& pattern) {
    if (pattern == "*") return true;
    return origin == pattern;
}

// ==================================================================
// CSPPolicy
// ==================================================================

void CSPPolicy::parse(const std::string& policyHeader) {
    directives_.clear();
    std::istringstream ss(policyHeader);
    std::string directive;
    while (std::getline(ss, directive, ';')) {
        while (!directive.empty() && std::isspace(directive.front()))
            directive.erase(directive.begin());

        if (directive.empty()) continue;

        Directive d;
        std::istringstream ds(directive);
        ds >> d.name;
        std::string source;
        while (ds >> source) d.sources.push_back(source);
        directives_.push_back(d);
    }
}

bool CSPPolicy::matchesSource(const std::string& url,
                                 const std::vector<std::string>& sources) const {
    for (const auto& src : sources) {
        if (src == "'self'") {
            // Would check same-origin
            return true;
        }
        if (src == "'none'") return false;
        if (src == "*") return true;
        if (src == "'unsafe-inline'") return true;
        if (src == "'unsafe-eval'") return true;

        // Check if URL matches source expression
        if (url.find(src) == 0) return true;
    }
    return false;
}

static std::vector<std::string> findDirectiveSources(
    const std::vector<CSPPolicy::Directive>& directives,
    const std::string& primary, const std::string& fallback) {

    for (const auto& d : directives) {
        if (d.name == primary) return d.sources;
    }
    for (const auto& d : directives) {
        if (d.name == fallback) return d.sources;
    }
    return {};
}

bool CSPPolicy::allowsScript(const std::string& url) const {
    auto sources = findDirectiveSources(directives_, "script-src", "default-src");
    return sources.empty() || matchesSource(url, sources);
}

bool CSPPolicy::allowsStyle(const std::string& url) const {
    auto sources = findDirectiveSources(directives_, "style-src", "default-src");
    return sources.empty() || matchesSource(url, sources);
}

bool CSPPolicy::allowsImage(const std::string& url) const {
    auto sources = findDirectiveSources(directives_, "img-src", "default-src");
    return sources.empty() || matchesSource(url, sources);
}

bool CSPPolicy::allowsFont(const std::string& url) const {
    auto sources = findDirectiveSources(directives_, "font-src", "default-src");
    return sources.empty() || matchesSource(url, sources);
}

bool CSPPolicy::allowsMedia(const std::string& url) const {
    auto sources = findDirectiveSources(directives_, "media-src", "default-src");
    return sources.empty() || matchesSource(url, sources);
}

bool CSPPolicy::allowsConnect(const std::string& url) const {
    auto sources = findDirectiveSources(directives_, "connect-src", "default-src");
    return sources.empty() || matchesSource(url, sources);
}

bool CSPPolicy::allowsFrame(const std::string& url) const {
    auto sources = findDirectiveSources(directives_, "frame-src", "default-src");
    return sources.empty() || matchesSource(url, sources);
}

bool CSPPolicy::allowsWorker(const std::string& url) const {
    auto sources = findDirectiveSources(directives_, "worker-src", "script-src");
    return sources.empty() || matchesSource(url, sources);
}

bool CSPPolicy::allowsInlineScript() const {
    auto sources = findDirectiveSources(directives_, "script-src", "default-src");
    for (const auto& s : sources) {
        if (s == "'unsafe-inline'") return true;
    }
    return sources.empty();
}

bool CSPPolicy::allowsInlineStyle() const {
    auto sources = findDirectiveSources(directives_, "style-src", "default-src");
    for (const auto& s : sources) {
        if (s == "'unsafe-inline'") return true;
    }
    return sources.empty();
}

bool CSPPolicy::allowsEval() const {
    auto sources = findDirectiveSources(directives_, "script-src", "default-src");
    for (const auto& s : sources) {
        if (s == "'unsafe-eval'") return true;
    }
    return sources.empty();
}

// ==================================================================
// SRIChecker
// ==================================================================

bool SRIChecker::verify(const std::vector<uint8_t>& /*data*/, const std::string& /*integrity*/) {
    // Would compute SHA-256/384/512 hash and compare
    // Real implementation requires crypto library
    return true;
}

std::string SRIChecker::computeHash(const std::vector<uint8_t>& /*data*/,
                                       const std::string& /*algorithm*/) {
    return "";
}

} // namespace NXRender
