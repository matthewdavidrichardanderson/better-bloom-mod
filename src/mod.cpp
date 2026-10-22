#include "renderer.hpp"
#include "settings.hpp"

#include <d/d_kankyo.h>
#include <helpers/gx_helper.h>
#include <m_Do/m_Do_graphic.h>
#include <mods/service.hpp>
#include <mods/svc/config.h>
#include <mods/svc/hook.hpp>
#include <mods/svc/log.h>
#include <mods/svc/resource.h>
#include <mods/svc/ui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <string_view>

DEFINE_MOD();
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE_VERSION(GfxService, svc_gfx, 2);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ResourceService, svc_resource);
IMPORT_SERVICE(UiService, svc_ui);
DEFINE_HOOK(&mDoGph_gInf_c::bloom_c::draw, BloomDraw);

namespace {

bloom::Renderer renderer;
GfxDrawTypeHandle drawType = 0;
GfxComputeTypeHandle computeType = 0;
uint64_t serial = 0;
std::atomic_bool renderFailed = false;
bool failureReported = false;
bloom::Pulse pulse;
std::chrono::steady_clock::time_point lastPulseSample;
bool pulseSampled = false;

class Resource {
  public:
    explicit Resource(const char* name) { svc_resource->load(mod_ctx, name, &m_buffer); }
    ~Resource() { svc_resource->free(mod_ctx, &m_buffer); }
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    std::string_view text() const {
        if (m_buffer.data == nullptr) {
            return {};
        }
        return {static_cast<const char*>(m_buffer.data), m_buffer.size};
    }

  private:
    ResourceBuffer m_buffer = RESOURCE_BUFFER_INIT;
};

std::array<float, 4> colour(GXColor value) {
    return {value.r / 255.0f, value.g / 255.0f, value.b / 255.0f, value.a / 255.0f};
}

void on_compute(ModContext*, const GfxComputeContext* context, const void* payload, size_t size,
                void*) {
    if (size != sizeof(bloom::Frame) || renderFailed.load(std::memory_order_relaxed)) {
        return;
    }
    bloom::Frame frame;
    std::memcpy(&frame, payload, sizeof(frame));
    try {
        if (!renderer.compute(*context, frame)) {
            renderFailed.store(true, std::memory_order_release);
        }
    } catch (...) {
        renderFailed.store(true, std::memory_order_release);
    }
}

void on_draw(ModContext*, const GfxDrawContext* context, const void* payload, size_t size, void*) {
    if (size != sizeof(bloom::Frame) || renderFailed.load(std::memory_order_relaxed)) {
        return;
    }
    bloom::Frame frame;
    std::memcpy(&frame, payload, sizeof(frame));
    try {
        if (!renderer.composite(*context, frame)) {
            renderFailed.store(true, std::memory_order_release);
        }
    } catch (...) {
        renderFailed.store(true, std::memory_order_release);
    }
}

HookAction on_bloom(ModContext*, void* args, void*, void*) {
    const auto& settings = bloom::settings();
    if (!settings.active || renderFailed.load(std::memory_order_acquire)) {
        pulse.reset();
        pulseSampled = false;
        return HOOK_CONTINUE;
    }
    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        pulseSampled ? std::chrono::duration<double>(now - lastPulseSample).count() : 0;
    lastPulseSample = now;
    pulseSampled = true;
    const bloom::PulseScale animation =
        pulse.advance(elapsed, dKy_darkworld_check() != 0, settings.pulse);
    auto* current = mods::arg<mDoGph_gInf_c::bloom_c*>(args, 0);
    const bool enabled = current->getEnable() || settings.forceBloom;
    if (!enabled && current->getMonoColor()->a == 0) {
        return HOOK_SKIP_ORIGINAL;
    }

    GfxResolveDesc request = GFX_RESOLVE_DESC_INIT;
    GfxResolvedTargets scene = GFX_RESOLVED_TARGETS_INIT;
    if (svc_gfx->resolve_pass(mod_ctx, &request, &scene) != MOD_OK || scene.color == nullptr ||
        scene.width == 0 || scene.height == 0) {
        return HOOK_CONTINUE;
    }

    bloom::FrameUniforms uniforms;
    uniforms.tint = bloom::resolve_tint(colour(*current->getBlendColor()), settings.tint,
                                        settings.neutralTint, settings.sceneDarkening);
    uniforms.mono = colour(*current->getMonoColor());
    uniforms.mono[3] = std::clamp(uniforms.mono[3] * settings.sceneStrength, 0.0f, 1.0f);
    uniforms.threshold =
        bloom::resolve_threshold(current->getPoint() / 255.0f, settings.threshold,
                                 settings.thresholdSource, settings.thresholdNormalization);
    uniforms.knee = settings.softness;
    const float density =
        settings.fixedGain ? (128.0f / 255.0f) : current->getBlureRatio() / 255.0f;
    uniforms.gain = 8.0f * density;
    uniforms.intensity = settings.intensity * settings.responseCompensation * animation.intensity;
    uniforms.saturation = settings.saturation;
    uniforms.responseGain = settings.responseGain;
    uniforms.highlightColour = settings.highlightColour;
    uniforms.highlightRolloff = settings.highlightRolloff;
    uniforms.highlightLimit = settings.highlightLimit;
    uniforms.brightnessMetric = settings.brightnessMetric;
    uniforms.preDiffusionGain = settings.preDiffusionGain;
    uniforms.sourceLimit = settings.sourceLimit;
    uniforms.filteringSpace = settings.filteringSpace;
    uniforms.spread = settings.spread;
    uniforms.mode = settings.blend == 0 ? current->mMode : settings.blend - 1;
    uniforms.enabled = enabled ? 1u : 0u;
    uniforms.sourceSrgb = bloom::is_srgb(scene.color_format) ? 1u : 0u;
    uniforms.targetSrgb = uniforms.sourceSrgb;
    const float radius = settings.radius * animation.size;
    const bloom::Plan plan =
        bloom::make_plan(scene.width, scene.height, radius, settings.profile, settings.divisor);
    const bloom::UniformBlock block = bloom::pack_uniforms(uniforms, plan);
    GfxRange range{};
    if (svc_gfx->push_uniform(mod_ctx, block.data(), bloom::uniformAlignment * (plan.count + 1),
                              &range) != MOD_OK) {
        return HOOK_CONTINUE;
    }

    bloom::Frame frame{scene.color, ++serial, range.offset, scene.width, scene.height, radius};
    frame.divisor = settings.divisor;
    frame.filter = enabled && uniforms.intensity > 0 && uniforms.gain > 0 ? 1u : 0u;
    frame.profile = settings.profile;
    if (svc_gfx->push_compute(mod_ctx, computeType, &frame, sizeof(frame)) != MOD_OK ||
        svc_gfx->push_draw(mod_ctx, drawType, &frame, sizeof(frame)) != MOD_OK) {
        return HOOK_CONTINUE;
    }
    return HOOK_SKIP_ORIGINAL;
}

ModResult initialize(ModError* error) {
    ModResult result = bloom::initialize_settings(error);
    if (result != MOD_OK) {
        return result;
    }
    GfxDeviceInfo device = GFX_DEVICE_INFO_INIT;
    GfxRenderTargetLayout layout = GFX_RENDER_TARGET_LAYOUT_INIT;
    if (svc_gfx->get_device_info(mod_ctx, &device) != MOD_OK || device.device == nullptr ||
        svc_gfx->get_scene_target_layout(mod_ctx, &layout) != MOD_OK) {
        return mods::set_error(error, MOD_UNAVAILABLE, "The graphics device is unavailable.");
    }
    Resource filter("filter.wgsl");
    Resource composite("composite.wgsl");
    if (filter.text().empty() || composite.text().empty()) {
        return mods::set_error(error, MOD_ERROR, "The bloom shaders could not be loaded.");
    }
    if (!renderer.initialize(device.device, filter.text(), composite.text(), layout)) {
        return mods::set_error(error, MOD_UNSUPPORTED, "The bloom pipelines could not be created.");
    }

    GfxComputeTypeDesc compute = GFX_COMPUTE_TYPE_DESC_INIT;
    compute.label = "Better Bloom";
    compute.callback = on_compute;
    result = svc_gfx->register_compute_type(mod_ctx, &compute, &computeType);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "Could not register the bloom filter.");
    }
    GfxDrawTypeDesc draw = GFX_DRAW_TYPE_DESC_INIT;
    draw.label = "Better Bloom composite";
    draw.draw = on_draw;
    result = svc_gfx->register_draw_type(mod_ctx, &draw, &drawType);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "Could not register the bloom composite.");
    }
    result = mods::hook::add_pre<BloomDraw>(on_bloom);
    if (result != MOD_OK) {
        return mods::set_error(error, result,
                               "The game's bloom draw function could not be hooked.");
    }
    return MOD_OK;
}

}

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    renderFailed.store(false, std::memory_order_relaxed);
    failureReported = false;
    pulse.reset();
    pulseSampled = false;
    serial = 0;
    try {
        const ModResult result = initialize(error);
        if (result != MOD_OK) {
            renderer.reset();
        }
        return result;
    } catch (const std::exception& exception) {
        renderer.reset();
        return mods::set_error(error, MOD_ERROR, exception.what());
    }
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (renderFailed.load(std::memory_order_acquire) && !failureReported) {
        svc_log->error(mod_ctx, "Bloom rendering failed. The game's bloom has been restored.");
        failureReported = true;
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    renderer.reset();
    drawType = computeType = 0;
    return MOD_OK;
}
}
