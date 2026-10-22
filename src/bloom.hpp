#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bloom {

constexpr float defaultResponseGain = 2.5f;
constexpr double defaultResponseScale = 0.6;
constexpr uint32_t maxLevels = 16;
constexpr uint32_t uniformAlignment = 256;

enum class ThresholdSource : uint32_t { Vanilla, Custom, Normalized };

inline float resolve_threshold(float vanilla, float reference, ThresholdSource source,
                               float normalization) {
    vanilla = std::clamp(vanilla, 0.0f, 1.0f);
    reference = std::clamp(reference, 0.0f, 1.0f);
    switch (source) {
    case ThresholdSource::Custom:
        return reference;
    case ThresholdSource::Normalized:
        return std::lerp(vanilla, reference, std::clamp(normalization, 0.0f, 1.0f));
    default:
        return vanilla;
    }
}

inline std::array<float, 4> resolve_tint(std::array<float, 4> authored,
                                         const std::array<float, 3>& multiplier, bool neutral,
                                         float darkening) {
    for (size_t i = 0; i < multiplier.size(); ++i) {
        authored[i] = (neutral ? 1.0f : authored[i]) * multiplier[i];
    }
    authored[3] = std::lerp(1.0f, authored[3], std::clamp(darkening, 0.0f, 1.0f));
    return authored;
}

inline float response_compensation(float gain, float rolloff) {
    if (!std::isfinite(gain) || !std::isfinite(rolloff)) {
        return 1;
    }
    constexpr double referenceInput = 0.5;
    constexpr double referenceResponse =
        referenceInput * defaultResponseGain / (1 + referenceInput * defaultResponseGain);
    const double input =
        referenceInput * std::max(double(gain), double(defaultResponseGain) * 0.01);
    const double shoulder = std::clamp(double(rolloff), 0.0, 4.0);
    double response = std::min(input, 1.0);
    if (shoulder == 1) {
        response = input / (1 + input);
    } else if (shoulder > 0) {
        const double ratio = std::min(input, 1 / input);
        response *= std::exp(-shoulder * std::log1p(std::pow(ratio, 1 / shoulder)));
    }
    return static_cast<float>(referenceResponse / response);
}

struct alignas(16) FrameUniforms {
    std::array<float, 4> tint{1, 1, 1, 1};
    std::array<float, 4> mono{};
    float threshold = 0.5f;
    float knee = 0.25f;
    float intensity = 1;
    uint32_t mode = 0;
    uint32_t enabled = 1;
    uint32_t sourceSrgb = 0;
    uint32_t targetSrgb = 0;
    float saturation = 1;
    float gain = 1;
    float responseGain = defaultResponseGain;
    uint32_t reserved = 0;
    float highlightColour = 0;
    float highlightRolloff = 1;
    float highlightLimit = 0;
    float responseScale = static_cast<float>(defaultResponseScale);
    float padding = 0;
    uint32_t brightnessMetric = 0;
    float preDiffusionGain = 0;
    float sourceLimit = 0;
    uint32_t filteringSpace = 0;
    std::array<float, 2> spread{1, 1};
    std::array<uint32_t, 2> padding3{};
};

struct alignas(16) StepUniforms {
    float weight = 0;
    uint32_t terminal = 0;
    std::array<uint32_t, 2> padding{};
};

struct LevelSize {
    uint32_t width = 0;
    uint32_t height = 0;
    float variance = 0;
    float weight = 0;
};

struct Plan {
    std::array<LevelSize, maxLevels> levels{};
    uint32_t count = 0;
};

struct Profile {
    std::array<float, 4> scales{0.15745041f, 0.79673580f, 2.35287885f, 9.83874529f};
    std::array<float, 4> weights{0.37376473f, 0.22746248f, 0.19247125f, 0.20630154f};
};

inline Profile make_profile(const std::array<float, 4>& widths,
                            const std::array<float, 4>& contributions, float diffusion = 0) {
    Profile profile;
    diffusion = std::isfinite(diffusion) ? std::max(diffusion, 0.0f) : 0;
    for (size_t i = 0; i < profile.weights.size(); ++i) {
        profile.scales[i] *= std::isfinite(widths[i]) ? std::max(widths[i], 0.0f) : 1;
        profile.scales[i] = std::hypot(profile.scales[i], diffusion);
        profile.weights[i] *=
            std::isfinite(contributions[i]) ? std::max(contributions[i], 0.0f) : 1;
    }
    return profile;
}

inline Plan make_plan(uint32_t width, uint32_t height, float radius, const Profile& profile = {},
                      uint32_t divisor = 2) {
    Plan plan;
    if (width == 0 || height == 0) {
        return plan;
    }

    radius = std::isfinite(radius) ? std::max(radius, 0.0f) : 0.01f;
    const float sigma = radius * static_cast<float>(height);
    float maximumScale = 0;
    for (size_t i = 0; i < profile.scales.size(); ++i) {
        if (profile.weights[i] > 0) {
            maximumScale = std::max(maximumScale, profile.scales[i]);
        }
    }
    const float maximumVariance = sigma * sigma * maximumScale * maximumScale;
    float downVariance = 1.5f;
    float upVariance = 0;
    float previousScale = 1;
    uint32_t levelWidth = width;
    uint32_t levelHeight = height;

    for (uint32_t i = 0; i < maxLevels; ++i) {
        const uint32_t reduction = i == 0 ? std::clamp(divisor, 1u, 4u) : 2u;
        levelWidth = (levelWidth + reduction - 1) / reduction;
        levelHeight = (levelHeight + reduction - 1) / reduction;
        const float scale = static_cast<float>(height) / static_cast<float>(levelHeight);
        if (i != 0) {
            downVariance += 1.5f * previousScale * previousScale;
            upVariance += 0.5f * scale * scale;
        }
        const float baseScale = static_cast<float>(height) /
                                static_cast<float>(i == 0 ? levelHeight : plan.levels[0].height);
        const float reconstructionVariance =
            i == 0 ? scale * scale / 6.0f : baseScale * baseScale / 6.0f;
        plan.levels[i] = {levelWidth, levelHeight,
                          downVariance + upVariance + reconstructionVariance, 0};
        plan.count = i + 1;
        previousScale = scale;
        if (plan.levels[i].variance >= maximumVariance || (levelWidth == 1 && levelHeight == 1)) {
            break;
        }
    }

    float totalWeight = 0;
    for (float weight : profile.weights) {
        totalWeight += std::max(weight, 0.0f);
    }
    for (size_t band = 0; band < profile.scales.size(); ++band) {
        const float weight =
            std::max(profile.weights[band], 0.0f) / std::max(totalWeight, 0.00001f);
        const float targetVariance = sigma * sigma * profile.scales[band] * profile.scales[band];
        uint32_t high = 0;
        while (high + 1 < plan.count && plan.levels[high].variance < targetVariance) {
            ++high;
        }
        if (high == 0 || targetVariance >= plan.levels[high].variance) {
            plan.levels[high].weight += weight;
        } else {
            const float lower = plan.levels[high - 1].variance;
            const float blend = std::clamp(
                (targetVariance - lower) / (plan.levels[high].variance - lower), 0.0f, 1.0f);
            plan.levels[high - 1].weight += weight * (1 - blend);
            plan.levels[high].weight += weight * blend;
        }
    }
    return plan;
}

using UniformBlock = std::array<std::byte, uniformAlignment*(maxLevels + 1)>;

inline UniformBlock pack_uniforms(FrameUniforms frame, const Plan& plan) {
    UniformBlock block{};
    std::memcpy(block.data(), &frame, sizeof(frame));
    for (uint32_t i = 0; i < plan.count; ++i) {
        StepUniforms step{plan.levels[i].weight, i + 1 == plan.count ? 1u : 0u};
        std::memcpy(block.data() + uniformAlignment * (i + 1), &step, sizeof(step));
    }
    return block;
}

static_assert(sizeof(FrameUniforms) == 128);
static_assert(sizeof(StepUniforms) == 16);

}
