#pragma once

#include "bloom.hpp"
#include "pulse.hpp"

#include <mods/api.h>

namespace bloom {

constexpr float defaultBloomIntensity = 1;
constexpr float defaultBloomRadius = 0.005f;

struct Settings {
    bool active = true;
    bool forceBloom = false;
    ThresholdSource thresholdSource = ThresholdSource::Vanilla;
    bool fixedGain = false;
    bool neutralTint = false;
    bool autoCompensation = true;
    float threshold = 0.25f;
    float thresholdNormalization = 0.5f;
    float softness = 0.3f;
    float sceneStrength = 1;
    float sceneDarkening = 1;
    float intensity = defaultBloomIntensity;
    float radius = defaultBloomRadius;
    float saturation = 1;
    float responseGain = defaultResponseGain;
    float responseCompensation = 1;
    float highlightColour = 0;
    float highlightRolloff = 1;
    float highlightLimit = 0;
    std::array<float, 3> tint{1, 1, 1};
    uint32_t blend = 0;
    uint32_t divisor = 2;
    Profile profile;
    PulseSettings pulse;
};

ModResult initialize_settings(ModError* error);
const Settings& settings();

}
