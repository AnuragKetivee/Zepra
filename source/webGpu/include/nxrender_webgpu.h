// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * @file nxrender_webgpu.h
 * @brief C ABI header for NXGFX WebGPU functions
 * 
 * WebGPU C++ code includes this header to call NXGFX via FFI.
 * This is the ONLY way WebGPU should talk to the GPU.
 */

#ifndef NXRENDER_WEBGPU_H
#define NXRENDER_WEBGPU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============= Handle Types =============

typedef uint64_t NxGpuBufferHandle;
typedef uint64_t NxGpuTextureHandle;
typedef uint64_t NxGpuShaderHandle;
typedef uint64_t NxGpuPipelineHandle;
typedef uint64_t NxGpuCommandHandle;

// ============= Buffer Usage Flags =============

#define NX_GPU_BUFFER_USAGE_MAP_READ   0x0001
#define NX_GPU_BUFFER_USAGE_MAP_WRITE  0x0002
#define NX_GPU_BUFFER_USAGE_COPY_SRC   0x0004
#define NX_GPU_BUFFER_USAGE_COPY_DST   0x0008
#define NX_GPU_BUFFER_USAGE_INDEX      0x0010
#define NX_GPU_BUFFER_USAGE_VERTEX     0x0020
#define NX_GPU_BUFFER_USAGE_UNIFORM    0x0040
#define NX_GPU_BUFFER_USAGE_STORAGE    0x0080

// ============= Texture Formats =============

typedef enum {
    NX_GPU_TEXTURE_FORMAT_RGBA8_UNORM = 0,
    NX_GPU_TEXTURE_FORMAT_RGBA8_UNORM_SRGB = 1,
    NX_GPU_TEXTURE_FORMAT_BGRA8_UNORM = 2,
    NX_GPU_TEXTURE_FORMAT_BGRA8_UNORM_SRGB = 3,
    NX_GPU_TEXTURE_FORMAT_DEPTH24_PLUS = 4,
    NX_GPU_TEXTURE_FORMAT_DEPTH24_PLUS_STENCIL8 = 5,
    NX_GPU_TEXTURE_FORMAT_DEPTH32_FLOAT = 6,
} NxGpuTextureFormat;

// ============= Shader Stages =============

typedef enum {
    NX_GPU_SHADER_STAGE_VERTEX = 0,
    NX_GPU_SHADER_STAGE_FRAGMENT = 1,
    NX_GPU_SHADER_STAGE_COMPUTE = 2,
} NxGpuShaderStage;

// ============= Descriptors =============

typedef struct {
    uint64_t size;
    uint32_t usage;
    bool mapped_at_creation;
} NxGpuBufferDesc;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mip_levels;
    uint32_t sample_count;
    NxGpuTextureFormat format;
    uint32_t usage;
} NxGpuTextureDesc;

// ============= Buffer Functions =============

/**
 * Create a GPU buffer
 * @param desc Buffer descriptor
 * @return Buffer handle (0 on failure)
 */
NxGpuBufferHandle nx_webgpu_buffer_create(const NxGpuBufferDesc* desc);

/**
 * Destroy a GPU buffer
 */
void nx_webgpu_buffer_destroy(NxGpuBufferHandle handle);

/**
 * Get buffer size
 */
uint64_t nx_webgpu_buffer_size(NxGpuBufferHandle handle);

/**
 * Write data to buffer
 * @return true on success
 */
bool nx_webgpu_buffer_write(NxGpuBufferHandle handle, uint64_t offset, 
                             const uint8_t* data, uint64_t size);

/**
 * Map buffer for CPU access
 * @return Pointer to mapped memory (NULL on failure)
 */
uint8_t* nx_webgpu_buffer_map(NxGpuBufferHandle handle);

/**
 * Unmap buffer
 */
void nx_webgpu_buffer_unmap(NxGpuBufferHandle handle);

// ============= Texture Functions =============

/**
 * Create a GPU texture
 */
NxGpuTextureHandle nx_webgpu_texture_create(const NxGpuTextureDesc* desc);

/**
 * Destroy a GPU texture
 */
void nx_webgpu_texture_destroy(NxGpuTextureHandle handle);

/**
 * Get texture dimensions
 */
uint32_t nx_webgpu_texture_width(NxGpuTextureHandle handle);
uint32_t nx_webgpu_texture_height(NxGpuTextureHandle handle);

// ============= Shader Functions =============

/**
 * Create a shader module from WGSL code
 */
NxGpuShaderHandle nx_webgpu_shader_create(NxGpuShaderStage stage, const char* code);

/**
 * Destroy a shader module
 */
void nx_webgpu_shader_destroy(NxGpuShaderHandle handle);

// ============= Command Encoder Functions =============

/**
 * Begin a command encoder
 */
NxGpuCommandHandle nx_webgpu_command_begin(void);

/**
 * Finish command encoder and get command buffer
 */
NxGpuCommandHandle nx_webgpu_command_finish(NxGpuCommandHandle encoder);

/**
 * Submit command buffer to GPU queue
 */
void nx_webgpu_queue_submit(NxGpuCommandHandle command);

// ============= Render Commands =============

/**
 * Clear color attachment
 */
void nx_webgpu_cmd_clear_color(NxGpuCommandHandle encoder, 
                                float r, float g, float b, float a);

/**
 * Set viewport
 */
void nx_webgpu_cmd_set_viewport(NxGpuCommandHandle encoder,
                                 float x, float y, float width, float height);

/**
 * Draw vertices
 */
void nx_webgpu_cmd_draw(NxGpuCommandHandle encoder,
                         uint32_t vertex_count, uint32_t instance_count,
                         uint32_t first_vertex, uint32_t first_instance);

/**
 * Draw indexed
 */
void nx_webgpu_cmd_draw_indexed(NxGpuCommandHandle encoder,
                                 uint32_t index_count, uint32_t instance_count,
                                 uint32_t first_index, int32_t base_vertex,
                                 uint32_t first_instance);

/**
 * Dispatch compute workgroups
 */
void nx_webgpu_cmd_dispatch(NxGpuCommandHandle encoder,
                             uint32_t x, uint32_t y, uint32_t z);

#ifdef __cplusplus
}
#endif

#endif // NXRENDER_WEBGPU_H
