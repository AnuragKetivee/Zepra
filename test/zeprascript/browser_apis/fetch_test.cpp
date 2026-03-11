// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <string>
#include <memory>

#include "browser/fetch.hpp"
#include "runtime/execution_context.h"

namespace Zepra {
namespace browser {
namespace testing {

class FetchAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_ = std::make_unique<runtime::ExecutionContext>();
        fetch_api_ = std::make_unique<FetchAPI>(ctx_.get());
    }

    void TearDown() override {
        fetch_api_.reset();
        ctx_.reset();
    }

    std::unique_ptr<runtime::ExecutionContext> ctx_;
    std::unique_ptr<FetchAPI> fetch_api_;
};

TEST_F(FetchAPITest, CreateRequestObject) {
    auto request = fetch_api_->CreateRequest("https://api.ketivee.com/data", "GET");
    EXPECT_NE(request, nullptr);
    EXPECT_EQ(request->GetMethod(), "GET");
    EXPECT_EQ(request->GetURL(), "https://api.ketivee.com/data");
}

TEST_F(FetchAPITest, BasicGetResponse) {
    auto request = fetch_api_->CreateRequest("https://api.ketivee.com/test", "GET");
    auto promise = fetch_api_->Fetch(request);
    
    promise->ResolveWithMockResponse(200, "{\"success\": true}");
    
    EXPECT_TRUE(promise->IsResolved());
    auto response = promise->GetResponse();
    EXPECT_EQ(response->GetStatus(), 200);
}

TEST_F(FetchAPITest, RequestTimeoutHandling) {
    auto request = fetch_api_->CreateRequest("https://api.ketivee.com/slow", "GET");
    request->SetTimeout(100); 
    auto promise = fetch_api_->Fetch(request);
    
    promise->SimulateTimeout();
    
    EXPECT_TRUE(promise->IsRejected());
    EXPECT_EQ(promise->GetError()->GetMessage(), "NetworkError: Request timed out");
}

TEST_F(FetchAPITest, OriginIsolationCORS) {
    ctx_->SetOrigin("https://evil.com");
    auto request = fetch_api_->CreateRequest("https://api.ketivee.com/secure", "GET");
    auto promise = fetch_api_->Fetch(request);
    
    promise->SimulateCORSFailure();
    
    EXPECT_TRUE(promise->IsRejected());
    EXPECT_TRUE(promise->GetError()->GetMessage().find("CORS") != std::string::npos);
}

TEST_F(FetchAPITest, PostRequestWithBody) {
    auto request = fetch_api_->CreateRequest("https://api.example.com", "POST");
    request->SetBody("payload=data");
    request->SetHeader("Content-Type", "application/x-www-form-urlencoded");
    
    auto promise = fetch_api_->Fetch(request);
    EXPECT_EQ(request->GetBody(), "payload=data");
    EXPECT_EQ(request->GetHeader("Content-Type"), "application/x-www-form-urlencoded");
}

TEST_F(FetchAPITest, ResponseParsingJSON) {
    auto response = fetch_api_->CreateMockResponse(200, "{\"id\": 123}", "application/json");
    auto json_promise = response->Json();
    
    EXPECT_TRUE(json_promise->IsResolved());
    EXPECT_TRUE(json_promise->GetValue().IsObject());
}

TEST_F(FetchAPITest, ResponseParsingText) {
    auto response = fetch_api_->CreateMockResponse(200, "Hello World", "text/plain");
    auto text_promise = response->Text();
    
    EXPECT_TRUE(text_promise->IsResolved());
    EXPECT_EQ(text_promise->GetValue().AsString(), "Hello World");
}

TEST_F(FetchAPITest, RedirectFollowLoopPrevention) {
    auto request = fetch_api_->CreateRequest("https://redirect.com", "GET");
    auto promise = fetch_api_->Fetch(request);
    
    promise->SimulateTooManyRedirects(25); 
    
    EXPECT_TRUE(promise->IsRejected());
}

TEST_F(FetchAPITest, AbortControllerInterruptsFetch) {
    auto abort_controller = fetch_api_->CreateAbortController();
    auto request = fetch_api_->CreateRequest("https://api.com/stream", "GET");
    request->SetSignal(abort_controller->GetSignal());
    
    auto promise = fetch_api_->Fetch(request);
    abort_controller->Abort();
    
    EXPECT_TRUE(promise->IsRejected());
    EXPECT_EQ(promise->GetError()->GetName(), "AbortError");
}

TEST_F(FetchAPITest, InvalidURLRejection) {
    auto request = fetch_api_->CreateRequest("not_a_valid_url", "GET");
    auto promise = fetch_api_->Fetch(request);
    
    EXPECT_TRUE(promise->IsRejected());
    EXPECT_EQ(promise->GetError()->GetName(), "TypeError");
}

TEST_F(FetchAPITest, ResponseHeadersAccess) {
    auto response = fetch_api_->CreateMockResponse(200, "OK", "text/plain");
    response->AddHeader("X-Custom-Header", "Value");
    
    EXPECT_EQ(response->GetHeader("X-Custom-Header"), "Value");
    EXPECT_EQ(response->GetHeader("Content-Type"), "text/plain");
}

TEST_F(FetchAPITest, RequestKeepAlive) {
    auto request = fetch_api_->CreateRequest("https://analytics.com", "POST");
    request->SetKeepAlive(true);
    
    EXPECT_TRUE(request->IsKeepAlive());
}

} // namespace testing
} // namespace browser
} // namespace Zepra
