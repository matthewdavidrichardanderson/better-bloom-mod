#pragma once

#include "bloom.hpp"

#include <mods/svc/gfx.h>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace bloom {

template <class T, void (*Release)(T)> class GpuHandle {
  public:
    GpuHandle() = default;
    explicit GpuHandle(T value) : m_value(value) {}
    ~GpuHandle() { reset(); }
    GpuHandle(const GpuHandle&) = delete;
    GpuHandle& operator=(const GpuHandle&) = delete;
    GpuHandle(GpuHandle&& other) noexcept : m_value(std::exchange(other.m_value, nullptr)) {}
    GpuHandle& operator=(GpuHandle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.m_value, nullptr));
        }
        return *this;
    }
    void reset(T value = nullptr) {
        if (m_value != nullptr) {
            Release(m_value);
        }
        m_value = value;
    }
    T get() const { return m_value; }
    explicit operator bool() const { return m_value != nullptr; }

  private:
    T m_value = nullptr;
};

using Texture = GpuHandle<WGPUTexture, wgpuTextureRelease>;
using TextureView = GpuHandle<WGPUTextureView, wgpuTextureViewRelease>;
using Shader = GpuHandle<WGPUShaderModule, wgpuShaderModuleRelease>;
using ComputePipeline = GpuHandle<WGPUComputePipeline, wgpuComputePipelineRelease>;
using RenderPipeline = GpuHandle<WGPURenderPipeline, wgpuRenderPipelineRelease>;
using BindLayout = GpuHandle<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease>;
using BindGroup = GpuHandle<WGPUBindGroup, wgpuBindGroupRelease>;
using PipelineLayout = GpuHandle<WGPUPipelineLayout, wgpuPipelineLayoutRelease>;
using Sampler = GpuHandle<WGPUSampler, wgpuSamplerRelease>;

struct Frame {
    WGPUTextureView source = nullptr;
    uint64_t serial = 0;
    uint32_t uniformOffset = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float radius = 0;
    uint32_t divisor = 2;
    uint32_t filter = 1;
    Profile profile;
};

bool is_srgb(WGPUTextureFormat format);

class Renderer {
  public:
    bool initialize(WGPUDevice device, std::string_view filter, std::string_view composite,
                    const GfxRenderTargetLayout& layout);
    bool compute(const GfxComputeContext& context, const Frame& frame,
                 const WGPUPassTimestampWrites* timestamps = nullptr);
    bool composite(const GfxDrawContext& context, const Frame& frame);
    void reset();

  private:
    struct Image {
        Texture texture;
        TextureView view;
    };

    struct Level {
        Image down;
        Image up;
    };

    struct Pyramid {
        std::vector<Level> levels;
        std::vector<BindGroup> groups;
        WGPUBuffer uniformBuffer = nullptr;
        uint32_t groupCount = 0;
        uint32_t divisor = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    Image create_image(uint32_t width, uint32_t height);
    bool ensure_targets(Pyramid& pyramid, const Frame& frame, const Plan& plan);
    bool ensure_composite(const GfxRenderTargetLayout& layout);
    BindGroup filter_group(WGPUTextureView high, WGPUTextureView low, WGPUTextureView output,
                           WGPUBuffer uniforms);
    bool ensure_groups(Pyramid& pyramid, WGPUBuffer uniforms, uint32_t count);
    bool encode_filter(WGPUComputePassEncoder pass, const GfxComputeContext& context,
                       const Frame& frame, const Plan& plan, Pyramid& pyramid);

    WGPUDevice m_device = nullptr;
    Sampler m_sampler;
    BindLayout m_filterLayout;
    PipelineLayout m_filterPipelineLayout;
    std::array<ComputePipeline, 4> m_filters;
    Shader m_compositeShader;
    RenderPipeline m_composite;
    BindLayout m_compositeLayout;
    GfxRenderTargetLayout m_layout = GFX_RENDER_TARGET_LAYOUT_INIT;
    Pyramid m_glow;
    uint64_t m_computedSerial = 0;
};

static_assert(sizeof(Frame) <= GFX_INLINE_DRAW_PAYLOAD_SIZE);

}
