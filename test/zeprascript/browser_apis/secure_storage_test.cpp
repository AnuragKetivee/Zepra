// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <memory>

#include "browser/secure_storage.hpp"

namespace Zepra {
namespace browser {
namespace testing {

class SecureStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage_ = std::make_unique<SecureStorage>("test_origin_domain.com");
    }

    void TearDown() override {
        storage_->ClearAll();
        storage_.reset();
    }

    std::unique_ptr<SecureStorage> storage_;
};

TEST_F(SecureStorageTest, InitializeWithoutErrors) {
    EXPECT_TRUE(storage_->IsReady());
}

TEST_F(SecureStorageTest, EncryptDecryptString) {
    std::string plaintext = "SensitiveAccessToken123!";
    storage_->SetItem("auth_token", plaintext);
    
    std::string decrypted = storage_->GetItem("auth_token");
    EXPECT_EQ(plaintext, decrypted);
}

TEST_F(SecureStorageTest, VerifyGCMAuthenticationTag) {
    std::string plaintext = "DataThatShouldNotBeTamperedWith";
    storage_->SetItem("important_data", plaintext);
    
    storage_->SimulateStorageCorruption("important_data");
    
    EXPECT_THROW(storage_->GetItem("important_data"), SecureStorageDecryptionError);
}

TEST_F(SecureStorageTest, ClearAllRemovesData) {
    storage_->SetItem("key1", "val1");
    storage_->SetItem("key2", "val2");
    
    storage_->ClearAll();
    
    EXPECT_EQ(storage_->GetItem("key1"), "");
    EXPECT_EQ(storage_->GetItem("key2"), "");
}

TEST_F(SecureStorageTest, RemoveSpecificItem) {
    storage_->SetItem("key1", "val1");
    storage_->SetItem("key2", "val2");
    
    storage_->RemoveItem("key1");
    
    EXPECT_EQ(storage_->GetItem("key1"), "");
    EXPECT_EQ(storage_->GetItem("key2"), "val2");
}

TEST_F(SecureStorageTest, NoPlaintextLeakInStorage) {
    std::string plaintext = "SuperSecretData123";
    storage_->SetItem("secret_key", plaintext);
    
    std::string raw_storage_blob = storage_->GetRawEncryptedBlob("secret_key");
    EXPECT_EQ(raw_storage_blob.find(plaintext), std::string::npos);
}

TEST_F(SecureStorageTest, DifferentOriginsHaveDifferentKeys) {
    SecureStorage other_storage("other_origin.com");
    storage_->SetItem("shared_key_name", "secret");
    
    EXPECT_EQ(other_storage.GetItem("shared_key_name"), "");
}

TEST_F(SecureStorageTest, EmptyValueHandling) {
    storage_->SetItem("empty", "");
    EXPECT_EQ(storage_->GetItem("empty"), "");
}

TEST_F(SecureStorageTest, LongDataEncryption) {
    std::string long_data(1024 * 1024, 'A'); // 1MB data
    storage_->SetItem("large_blob", long_data);
    
    EXPECT_EQ(storage_->GetItem("large_blob"), long_data);
}

TEST_F(SecureStorageTest, OverwriteExistingKey) {
    storage_->SetItem("key", "first_value");
    storage_->SetItem("key", "second_value");
    
    EXPECT_EQ(storage_->GetItem("key"), "second_value");
}

TEST_F(SecureStorageTest, AccessNonExistentKey) {
    EXPECT_EQ(storage_->GetItem("never_set"), "");
}

TEST_F(SecureStorageTest, SpecialCharactersInKeyAndValue) {
    storage_->SetItem("key!@#$%^&*()", "value \n\t\r \0 hidden");
    EXPECT_EQ(storage_->GetItem("key!@#$%^&*()"), "value \n\t\r \0 hidden");
}

} // namespace testing
} // namespace browser
} // namespace Zepra
