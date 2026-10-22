#include "renderer.hpp"

#include <algorithm>
#include <array>

namespace bloom {
namespace {

Shader create_shader(WGPUDevice device, const char* label, std::string_view source) {
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = {source.data(), source.size()};
    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &wgsl.chain;
    descriptor.label = {label, WGPU_STRLEN};
    return Shader(wgpuDeviceCreateShaderModule(device, &descriptor));
}

WGPUBindGroupEntry texture_entry(uint32_t binding, WGPUTextureView view) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.textureView = view;
    return entry;
}

WGPUBindGroupEntry uniform_entry(uint32_t binding, WGPUBuffer buffer, uint64_t offset,
                                 uint64_t size) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.buffer = buffer;
    entry.offset = offset;
    entry.size = size;
    return entry;
}

}

bool is_srgb(WGPUTextureFormat format) {
    return format == WGPUTextureFormat_RGBA8UnormSrgb || format == WGPUTextureFormat_BGRA8UnormSrgb;
}

bool Renderer::initialize(WGPUDevice device, std::string_view filter, std::string_view composite,
                          const GfxRenderTargetLayout& layout) {
    reset();
    m_device = device;

    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler.label = {"Bloom filter", WGPU_STRLEN};
    sampler.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler.magFilter = WGPUFilterMode_Linear;
    sampler.minFilter = WGPUFilterMode_Linear;
    sampler.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    m_sampler.reset(wgpuDeviceCreateSampler(device, &sampler));

    std::array<WGPUBindGroupLayoutEntry, 6> entries;
    entries.fill(WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT);
    for (uint32_t i = 0; i < entries.size(); ++i) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Compute;
    }
    for (uint32_t i : {0u, 1u}) {
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[3].storageTexture.format = WGPUTextureFormat_RGBA16Float;
    entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
    entries[4].buffer.type = WGPUBufferBindingType_Uniform;
    entries[4].buffer.hasDynamicOffset = true;
    entries[4].buffer.minBindingSize = sizeof(FrameUniforms);
    entries[5].buffer.type = WGPUBufferBindingType_Uniform;
    entries[5].buffer.hasDynamicOffset = true;
    entries[5].buffer.minBindingSize = sizeof(StepUniforms);

    WGPUBindGroupLayoutDescriptor bindings = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bindings.label = {"Bloom filter bindings", WGPU_STRLEN};
    bindings.entryCount = entries.size();
    bindings.entries = entries.data();
    m_filterLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &bindings));
    WGPUBindGroupLayout filterLayout = m_filterLayout.get();
    WGPUPipelineLayoutDescriptor pipelineLayout = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayout.bindGroupLayoutCount = 1;
    pipelineLayout.bindGroupLayouts = &filterLayout;
    m_filterPipelineLayout.reset(wgpuDeviceCreatePipelineLayout(device, &pipelineLayout));

    Shader module = create_shader(device, "Bloom filters", filter);
    constexpr std::array entryPoints{"prefilter", "downsample", "upsample", "prefilter_cached"};
    for (size_t i = 0; i < entryPoints.size(); ++i) {
        WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        descriptor.label = {entryPoints[i], WGPU_STRLEN};
        descriptor.layout = m_filterPipelineLayout.get();
        descriptor.compute.module = module.get();
        descriptor.compute.entryPoint = {entryPoints[i], WGPU_STRLEN};
        m_filters[i].reset(wgpuDeviceCreateComputePipeline(device, &descriptor));
    }

    m_compositeShader = create_shader(device, "Bloom composite", composite);
    return m_sampler && m_filterLayout && m_filterPipelineLayout &&
           std::all_of(m_filters.begin(), m_filters.end(),
                       [](const auto& value) { return static_cast<bool>(value); }) &&
           m_compositeShader && ensure_composite(layout);
}

bool Renderer::ensure_composite(const GfxRenderTargetLayout& layout) {
    if (m_composite && m_layout.key == layout.key) {
        return true;
    }
    if (layout.color_attachment_count == 0) {
        return false;
    }

    WGPUColorTargetState targets[GFX_MAX_COLOR_ATTACHMENTS];
    const uint32_t targetCount = gfx_init_color_target_states(
        &layout, targets, nullptr,
        WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue);
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = m_compositeShader.get();
    fragment.entryPoint = {"fragment", WGPU_STRLEN};
    fragment.targetCount = targetCount;
    fragment.targets = targets;
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = layout.depth_stencil_format;
    depth.depthWriteEnabled = WGPUOptionalBool_False;
    depth.depthCompare = WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = {"Bloom composite", WGPU_STRLEN};
    descriptor.vertex.module = m_compositeShader.get();
    descriptor.vertex.entryPoint = {"vertex", WGPU_STRLEN};
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.multisample.count = layout.sample_count;
    descriptor.fragment = &fragment;
    descriptor.depthStencil = depth.format == WGPUTextureFormat_Undefined ? nullptr : &depth;
    RenderPipeline pipeline(wgpuDeviceCreateRenderPipeline(m_device, &descriptor));
    if (!pipeline) {
        return false;
    }
    BindLayout bindings(wgpuRenderPipelineGetBindGroupLayout(pipeline.get(), 0));
    if (!bindings) {
        return false;
    }
    m_composite = std::move(pipeline);
    m_compositeLayout = std::move(bindings);
    m_layout = layout;
    return true;
}

Renderer::Image Renderer::create_image(uint32_t width, uint32_t height) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = {"Bloom pyramid", WGPU_STRLEN};
    descriptor.size = {width, height, 1};
    descriptor.format = WGPUTextureFormat_RGBA16Float;
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
    Image image;
    image.texture.reset(wgpuDeviceCreateTexture(m_device, &descriptor));
    if (image.texture) {
        image.view.reset(wgpuTextureCreateView(image.texture.get(), nullptr));
    }
    return image;
}

bool Renderer::ensure_targets(Pyramid& pyramid, const Frame& frame, const Plan& plan) {
    if (pyramid.width == frame.width && pyramid.height == frame.height &&
        pyramid.divisor == frame.divisor && pyramid.levels.size() >= plan.count) {
        return true;
    }
    std::vector<Level> levels;
    levels.reserve(maxLevels);
    uint32_t width = frame.width;
    uint32_t height = frame.height;
    for (uint32_t i = 0; i < maxLevels; ++i) {
        const uint32_t reduction = i == 0 ? std::clamp(frame.divisor, 1u, 4u) : 2u;
        width = (width + reduction - 1) / reduction;
        height = (height + reduction - 1) / reduction;
        Level level{create_image(width, height), create_image(width, height)};
        if (!level.down.view || !level.up.view) {
            return false;
        }
        levels.push_back(std::move(level));
        if (width == 1 && height == 1) {
            break;
        }
    }
    pyramid.groups.clear();
    pyramid.uniformBuffer = nullptr;
    pyramid.levels = std::move(levels);
    pyramid.width = frame.width;
    pyramid.height = frame.height;
    pyramid.divisor = frame.divisor;
    return true;
}

BindGroup Renderer::filter_group(WGPUTextureView high, WGPUTextureView low, WGPUTextureView output,
                                 WGPUBuffer uniforms) {
    std::array entries{texture_entry(0, high),
                       texture_entry(1, low),
                       WGPUBindGroupEntry WGPU_BIND_GROUP_ENTRY_INIT,
                       texture_entry(3, output),
                       uniform_entry(4, uniforms, 0, sizeof(FrameUniforms)),
                       uniform_entry(5, uniforms, 0, sizeof(StepUniforms))};
    entries[2].binding = 2;
    entries[2].sampler = m_sampler.get();
    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.layout = m_filterLayout.get();
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();
    return BindGroup(wgpuDeviceCreateBindGroup(m_device, &descriptor));
}

bool Renderer::ensure_groups(Pyramid& pyramid, WGPUBuffer uniforms, uint32_t count) {
    if (pyramid.uniformBuffer == uniforms && pyramid.groupCount == count &&
        !pyramid.groups.empty()) {
        return true;
    }
    std::vector<BindGroup> groups;
    groups.reserve(count * 2 - 1);
    for (uint32_t i = 1; i < count; ++i) {
        WGPUTextureView input = pyramid.levels[i - 1].down.view.get();
        groups.push_back(filter_group(input, input, pyramid.levels[i].down.view.get(), uniforms));
    }
    for (uint32_t i = 0; i < count; ++i) {
        WGPUTextureView low = i + 1 == count ? pyramid.levels[i].down.view.get()
                                             : pyramid.levels[i + 1].up.view.get();
        groups.push_back(filter_group(pyramid.levels[i].down.view.get(), low,
                                      pyramid.levels[i].up.view.get(), uniforms));
    }
    if (std::any_of(groups.begin(), groups.end(), [](const auto& group) { return !group; })) {
        return false;
    }
    pyramid.groups = std::move(groups);
    pyramid.uniformBuffer = uniforms;
    pyramid.groupCount = count;
    return true;
}

bool Renderer::compute(const GfxComputeContext& context, const Frame& frame,
                       const WGPUPassTimestampWrites* timestamps) {
    m_computedSerial = 0;
    if (frame.source == nullptr) {
        return false;
    }
    const Plan plan =
        make_plan(frame.width, frame.height, frame.radius, frame.profile, frame.divisor);
    if (plan.count == 0) {
        return false;
    }
    if (frame.filter != 0 && (!ensure_targets(m_glow, frame, plan) ||
                              !ensure_groups(m_glow, context.uniform_buffer, plan.count))) {
        return false;
    }
    WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    descriptor.label = {"Bloom pyramid", WGPU_STRLEN};
    descriptor.timestampWrites = timestamps;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(context.encoder, &descriptor);
    if (pass == nullptr) {
        return false;
    }
    const bool success = frame.filter == 0 || encode_filter(pass, context, frame, plan, m_glow);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    m_computedSerial = success ? frame.serial : 0;
    return success;
}

bool Renderer::encode_filter(WGPUComputePassEncoder pass, const GfxComputeContext& context,
                             const Frame& frame, const Plan& plan, Pyramid& pyramid) {
    BindGroup prefilter = filter_group(frame.source, frame.source,
                                       pyramid.levels[0].down.view.get(), context.uniform_buffer);
    if (!prefilter) {
        return false;
    }
    const auto& first = plan.levels[0];
    const bool cached = (frame.width == first.width || frame.width == first.width * 2) &&
                        (frame.height == first.height || frame.height == first.height * 2);
    wgpuComputePassEncoderSetPipeline(pass, m_filters[cached ? 3 : 0].get());
    for (uint32_t i = 0; i < plan.count; ++i) {
        if (i == 1) {
            wgpuComputePassEncoderSetPipeline(pass, m_filters[1].get());
        }
        const std::array offsets{frame.uniformOffset,
                                 frame.uniformOffset + uniformAlignment * (i + 1)};
        wgpuComputePassEncoderSetBindGroup(pass, 0,
                                           i == 0 ? prefilter.get() : pyramid.groups[i - 1].get(),
                                           offsets.size(), offsets.data());
        wgpuComputePassEncoderDispatchWorkgroups(pass, (plan.levels[i].width + 7) / 8,
                                                 (plan.levels[i].height + 7) / 8, 1);
    }
    wgpuComputePassEncoderSetPipeline(pass, m_filters[2].get());
    for (uint32_t i = plan.count; i-- > 0;) {
        const std::array offsets{frame.uniformOffset,
                                 frame.uniformOffset + uniformAlignment * (i + 1)};
        wgpuComputePassEncoderSetBindGroup(pass, 0, pyramid.groups[plan.count - 1 + i].get(),
                                           offsets.size(), offsets.data());
        wgpuComputePassEncoderDispatchWorkgroups(pass, (plan.levels[i].width + 7) / 8,
                                                 (plan.levels[i].height + 7) / 8, 1);
    }
    return true;
}

bool Renderer::composite(const GfxDrawContext& context, const Frame& frame) {
    if (frame.serial != m_computedSerial || !ensure_composite(context.layout)) {
        return false;
    }
    WGPUTextureView glow = frame.filter != 0 ? m_glow.levels[0].up.view.get() : frame.source;
    std::array entries{
        texture_entry(0, frame.source), texture_entry(1, glow),
        WGPUBindGroupEntry WGPU_BIND_GROUP_ENTRY_INIT,
        uniform_entry(3, context.uniform_buffer, frame.uniformOffset, sizeof(FrameUniforms))};
    entries[2].binding = 2;
    entries[2].sampler = m_sampler.get();
    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.layout = m_compositeLayout.get();
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();
    BindGroup group(wgpuDeviceCreateBindGroup(m_device, &descriptor));
    if (!group) {
        return false;
    }
    const auto& target = context.layout.color_attachments[GFX_SCENE_COLOR_ATTACHMENT_INDEX];
    wgpuRenderPassEncoderSetViewport(context.pass, 0, 0, static_cast<float>(target.width),
                                     static_cast<float>(target.height), 0, 1);
    wgpuRenderPassEncoderSetScissorRect(context.pass, 0, 0, target.width, target.height);
    wgpuRenderPassEncoderSetPipeline(context.pass, m_composite.get());
    wgpuRenderPassEncoderSetBindGroup(context.pass, 0, group.get(), 0, nullptr);
    wgpuRenderPassEncoderDraw(context.pass, 3, 1, 0, 0);
    return true;
}

void Renderer::reset() {
    m_computedSerial = 0;
    m_glow = {};

    m_composite.reset();
    m_compositeLayout.reset();
    m_compositeShader.reset();
    for (auto& pipeline : m_filters) {
        pipeline.reset();
    }
    m_filterPipelineLayout.reset();
    m_filterLayout.reset();
    m_sampler.reset();
    m_device = nullptr;
    m_layout = GFX_RENDER_TARGET_LAYOUT_INIT;
}

}
