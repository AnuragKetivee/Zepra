// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

/**
 * @file fetch_tests.cpp
 * @brief Integration tests for Fetch API with real HTTP requests
 */

#include <gtest/gtest.h>
#include "browser/fetch.hpp"
#include "browser/url.hpp"
#include "runtime/async/promise.hpp"

using namespace Zepra::Browser;
using namespace Zepra::Runtime;

// =============================================================================
// URL Integration with Fetch
// =============================================================================

TEST(FetchIntegrationTests, URLParsing) {
    URL url("https://jsonplaceholder.typicode.com/todos/1");
    
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.protocol(), "https:");
    EXPECT_EQ(url.hostname(), "jsonplaceholder.typicode.com");
    EXPECT_EQ(url.pathname(), "/todos/1");
}

// =============================================================================
// Headers Tests
// =============================================================================

TEST(FetchIntegrationTests, HeadersBasic) {
    Headers headers;
    
    headers.setHeader("Content-Type", "application/json");
    headers.setHeader("Authorization", "Bearer token123");
    
    EXPECT_EQ(headers.getHeader("content-type"), "application/json");
    EXPECT_EQ(headers.getHeader("authorization"), "Bearer token123");
    EXPECT_TRUE(headers.has("Content-Type"));
}

TEST(FetchIntegrationTests, HeadersAppend) {
    Headers headers;
    
    headers.append("Accept", "text/html");
    headers.append("Accept", "application/json");
    
    std::string accept = headers.getHeader("accept");
    EXPECT_NE(accept.find("text/html"), std::string::npos);
    EXPECT_NE(accept.find("application/json"), std::string::npos);
}

TEST(FetchIntegrationTests, HeadersRemove) {
    Headers headers;
    
    headers.setHeader("X-Custom", "value");
    EXPECT_TRUE(headers.has("x-custom"));
    
    headers.remove("X-Custom");
    EXPECT_FALSE(headers.has("x-custom"));
}

// =============================================================================
// Request Tests
// =============================================================================

TEST(FetchIntegrationTests, RequestBasic) {
    Request req("https://api.example.com/data", "POST");
    
    EXPECT_EQ(req.url(), "https://api.example.com/data");
    EXPECT_EQ(req.method(), "POST");
}

TEST(FetchIntegrationTests, RequestWithBody) {
    Request req("https://api.example.com/data", "POST");
    req.setBody("{\"key\": \"value\"}");
    
    EXPECT_EQ(req.body(), "{\"key\": \"value\"}");
}

TEST(FetchIntegrationTests, RequestWithHeaders) {
    Request req("https://api.example.com/data");
    req.setHeader("Accept", "application/json");
    
    EXPECT_EQ(req.headers()->getHeader("accept"), "application/json");
}

// =============================================================================
// Response Tests
// =============================================================================

TEST(FetchIntegrationTests, ResponseConstruction) {
    Response resp(200, "OK");
    
    EXPECT_EQ(resp.status(), 200);
    EXPECT_EQ(resp.statusText(), "OK");
    EXPECT_TRUE(resp.ok());
}

TEST(FetchIntegrationTests, ResponseNotOk) {
    Response resp(404, "Not Found");
    
    EXPECT_EQ(resp.status(), 404);
    EXPECT_FALSE(resp.ok());
}

TEST(FetchIntegrationTests, ResponseWithBody) {
    Response resp(200);
    resp.setBody("Hello World");
    
    EXPECT_EQ(resp.body(), "Hello World");
    EXPECT_FALSE(resp.bodyUsed());
}

TEST(FetchIntegrationTests, ResponseHeaders) {
    Response resp(200);
    resp.headers()->setHeader("Content-Type", "text/plain");
    
    EXPECT_EQ(resp.headers()->getHeader("content-type"), "text/plain");
}

// =============================================================================
// Live HTTP Tests (require network)
// =============================================================================

// Note: These tests require network access. They use jsonplaceholder.typicode.com
// which is a free fake API for testing.

TEST(FetchLiveTests, DISABLED_GetRequest) {
    // Disabled by default - enable when testing with network
    Promise* promise = FetchAPI::fetch("https://jsonplaceholder.typicode.com/todos/1");
    
    EXPECT_NE(promise, nullptr);
    // In a real async test, we'd wait for resolution
}

TEST(FetchLiveTests, DISABLED_PostRequest) {
    Request* req = new Request("https://jsonplaceholder.typicode.com/posts", "POST");
    req->setBody("{\"title\":\"test\",\"body\":\"content\",\"userId\":1}");
    req->setHeader("Content-Type", "application/json");
    
    Promise* promise = FetchAPI::fetch(req->url(), req);
    EXPECT_NE(promise, nullptr);
    
    delete req;
}

// =============================================================================
// Error Status Code Handling
// =============================================================================

TEST(FetchErrorTests, Status400BadRequest) {
    Response resp(400, "Bad Request");
    EXPECT_EQ(resp.status(), 400);
    EXPECT_FALSE(resp.ok());
    EXPECT_EQ(resp.statusText(), "Bad Request");
}

TEST(FetchErrorTests, Status403Forbidden) {
    Response resp(403, "Forbidden");
    EXPECT_EQ(resp.status(), 403);
    EXPECT_FALSE(resp.ok());
}

TEST(FetchErrorTests, Status404NotFound) {
    Response resp(404, "Not Found");
    EXPECT_FALSE(resp.ok());
}

TEST(FetchErrorTests, Status500InternalServerError) {
    Response resp(500, "Internal Server Error");
    EXPECT_EQ(resp.status(), 500);
    EXPECT_FALSE(resp.ok());
}

TEST(FetchErrorTests, Status502BadGateway) {
    Response resp(502, "Bad Gateway");
    EXPECT_FALSE(resp.ok());
}

TEST(FetchErrorTests, Status204NoContent) {
    Response resp(204, "No Content");
    EXPECT_TRUE(resp.ok());
    EXPECT_EQ(resp.status(), 204);
}

TEST(FetchErrorTests, Status301Redirect) {
    Response resp(301, "Moved Permanently");
    EXPECT_FALSE(resp.ok());  // 3xx is not "ok" per Fetch spec (only 200-299)
}

// =============================================================================
// Body Consumption Tracking
// =============================================================================

TEST(FetchBodyTests, BodyNotUsedInitially) {
    Response resp(200);
    resp.setBody("test data");
    EXPECT_FALSE(resp.bodyUsed());
}

TEST(FetchBodyTests, EmptyBody) {
    Response resp(200);
    EXPECT_EQ(resp.body(), "");
}

TEST(FetchBodyTests, LargeBody) {
    Response resp(200);
    std::string largeBody(10000, 'A');
    resp.setBody(largeBody);
    EXPECT_EQ(resp.body().size(), 10000u);
}

// =============================================================================
// Request Method Validation
// =============================================================================

TEST(FetchRequestTests, GetMethod) {
    Request req("https://example.com", "GET");
    EXPECT_EQ(req.method(), "GET");
}

TEST(FetchRequestTests, PutMethod) {
    Request req("https://example.com/resource", "PUT");
    EXPECT_EQ(req.method(), "PUT");
}

TEST(FetchRequestTests, DeleteMethod) {
    Request req("https://example.com/resource/1", "DELETE");
    EXPECT_EQ(req.method(), "DELETE");
}

TEST(FetchRequestTests, PatchMethod) {
    Request req("https://example.com/resource/1", "PATCH");
    EXPECT_EQ(req.method(), "PATCH");
}

// =============================================================================
// Credential / Cookie Isolation Across Origins
// =============================================================================

TEST(FetchCredentialTests, HeadersDoNotLeakAcrossOrigins) {
    // Simulate two requests to different origins.
    Request req1("https://site-a.com/api");
    req1.setHeader("Cookie", "session=abc123");
    req1.setHeader("Authorization", "Bearer token_site_a");

    Request req2("https://site-b.com/api");
    // req2 must NOT inherit req1's credentials.
    EXPECT_EQ(req2.headers()->getHeader("cookie"), "");
    EXPECT_EQ(req2.headers()->getHeader("authorization"), "");
}

TEST(FetchCredentialTests, SensitiveHeadersArePerRequest) {
    Headers h1;
    h1.setHeader("Authorization", "Bearer secret");

    Headers h2;
    // Independent header objects — no shared state.
    EXPECT_EQ(h2.getHeader("authorization"), "");
    EXPECT_EQ(h1.getHeader("authorization"), "Bearer secret");
}

// =============================================================================
// CORS Preflight Validation
// =============================================================================

TEST(FetchCORSTests, SimpleRequestNoCustomHeaders) {
    Request req("https://api.example.com/data");
    // GET with no custom headers = simple request, no preflight needed.
    EXPECT_EQ(req.method(), "GET");
    EXPECT_FALSE(req.headers()->has("X-Custom"));
}

TEST(FetchCORSTests, CustomHeaderTriggersPreflight) {
    Request req("https://api.example.com/data");
    req.setHeader("X-Custom-Auth", "token");
    // Custom header present — would need preflight in real CORS flow.
    EXPECT_TRUE(req.headers()->has("X-Custom-Auth"));
}

TEST(FetchCORSTests, ContentTypeJsonTriggersPreflight) {
    Request req("https://api.example.com/data", "POST");
    req.setHeader("Content-Type", "application/json");
    // application/json is not a "simple" content type — needs preflight.
    EXPECT_EQ(req.headers()->getHeader("content-type"), "application/json");
}

// =============================================================================
// URL Edge Cases
// =============================================================================

TEST(FetchURLTests, URLWithQueryString) {
    URL url("https://example.com/search?q=zepra&lang=en");
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.hostname(), "example.com");
    EXPECT_EQ(url.pathname(), "/search");
}

TEST(FetchURLTests, URLWithPort) {
    URL url("https://localhost:8080/api");
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.hostname(), "localhost");
}

TEST(FetchURLTests, URLWithFragment) {
    URL url("https://example.com/page#section");
    EXPECT_TRUE(url.isValid());
}

