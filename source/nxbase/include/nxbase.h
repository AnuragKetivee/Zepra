/**
 * @file nxbase.h
 * @brief NxBase - Core utilities for Zepra stack
 * 
 * Provides common utilities used by all Nx libraries:
 * - String utilities (UTF-8)
 * - Buffer management
 * - Memory allocation
 * - Error handling
 * - Logging
 */

#ifndef NXBASE_H
#define NXBASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Error Handling
// ============================================================================

typedef enum {
    NX_OK = 0,
    NX_ERROR = -1,
    NX_ERROR_NOMEM = -2,
    NX_ERROR_INVALID = -3,
    NX_ERROR_IO = -4,
    NX_ERROR_TIMEOUT = -5,
    NX_ERROR_PARSE = -6,
    NX_ERROR_NOT_FOUND = -7,
    NX_ERROR_OVERFLOW = -8,
    NX_ERROR_UNSUPPORTED = -9
} NxResult;

const char* nx_error_string(NxResult result);

// ============================================================================
// Buffer - Dynamic byte buffer
// ============================================================================

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} NxBuffer;

NxBuffer* nx_buffer_create(size_t initial_capacity);
void nx_buffer_destroy(NxBuffer* buf);
NxResult nx_buffer_reserve(NxBuffer* buf, size_t capacity);
NxResult nx_buffer_append(NxBuffer* buf, const void* data, size_t len);
NxResult nx_buffer_append_byte(NxBuffer* buf, uint8_t byte);
NxResult nx_buffer_append_str(NxBuffer* buf, const char* str);
void nx_buffer_clear(NxBuffer* buf);
char* nx_buffer_to_string(NxBuffer* buf);  // Returns null-terminated copy

// ============================================================================
// String Utilities (UTF-8 aware)
// ============================================================================

typedef struct {
    char* data;
    size_t len;
    size_t capacity;
} NxString;

NxString* nx_string_create(const char* str);
NxString* nx_string_create_len(const char* str, size_t len);
void nx_string_destroy(NxString* s);
NxResult nx_string_append(NxString* s, const char* str);
NxResult nx_string_append_len(NxString* s, const char* str, size_t len);
void nx_string_clear(NxString* s);
const char* nx_string_cstr(const NxString* s);
size_t nx_string_len(const NxString* s);

// UTF-8 utilities
size_t nx_utf8_len(const char* str);              // Count UTF-8 code points
size_t nx_utf8_char_len(uint8_t first_byte);      // Bytes in UTF-8 char
bool nx_utf8_valid(const char* str, size_t len);  // Validate UTF-8

// ============================================================================
// Memory Pool Allocator
// ============================================================================

typedef struct NxPool NxPool;

NxPool* nx_pool_create(size_t block_size);
void nx_pool_destroy(NxPool* pool);
void* nx_pool_alloc(NxPool* pool, size_t size);
void nx_pool_reset(NxPool* pool);  // Reset without freeing memory

// ============================================================================
// Logging
// ============================================================================

typedef enum {
    NX_LOG_TRACE = 0,
    NX_LOG_DEBUG = 1,
    NX_LOG_INFO = 2,
    NX_LOG_WARN = 3,
    NX_LOG_ERROR = 4,
    NX_LOG_FATAL = 5
} NxLogLevel;

typedef void (*NxLogCallback)(NxLogLevel level, const char* file, int line, const char* msg);

void nx_log_set_level(NxLogLevel level);
void nx_log_set_callback(NxLogCallback callback);
void nx_log(NxLogLevel level, const char* file, int line, const char* fmt, ...);

#define NX_TRACE(...) nx_log(NX_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define NX_DEBUG(...) nx_log(NX_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define NX_INFO(...)  nx_log(NX_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define NX_WARN(...)  nx_log(NX_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define NX_ERROR(...) nx_log(NX_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define NX_FATAL(...) nx_log(NX_LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // NXBASE_H
