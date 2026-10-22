#include "settings.hpp"

#include <mods/service.hpp>
#include <mods/svc/config.h>
#include <mods/svc/ui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>

namespace bloom {
namespace {

enum class Setting : size_t {
    Active,
    Intensity,
    Size,
    Diffusion,
    CoreWidth,
    CoreContribution,
    InnerWidth,
    InnerContribution,
    OuterWidth,
    OuterContribution,
    HaloWidth,
    HaloContribution,
    Softness,
    GainSource,
    ResponseGain,
    HighlightRolloff,
    AutoCompensation,
    HighlightLimit,
    HighlightColour,
    TintSource,
    Saturation,
    Tint,
    SceneStrength,
    SceneDarkening,
    PulseEnabled,
    PulseSource,
    PulsePeriod,
    PulseIrregular,
    PulseSize,
    PulseIntensity,
    PulseFade,
    ThresholdSource,
    Threshold,
    ThresholdNormalization,
    AreaSource,
    Blend,
    Resolution,
    Count
};

enum class Section { Bloom, Profile, Response, Colour, Pulse, BloomOverrides, Performance };

struct Group {
    Section section;
    const char* title;
};

constexpr std::array groups{Group{Section::Bloom, "Bloom options"},
    Group{Section::Profile, "Glow profile"}, Group{Section::Response, "Bloom response"},
    Group{Section::Colour, "Colour"}, Group{Section::Pulse, "Pulse options"},
    Group{Section::BloomOverrides, "Overrides"}, Group{Section::Performance, "Performance"}};

struct Option {
    const char* name;
    const char* label;
    UiControlKind kind;
    int64_t initial;
    int64_t minimum;
    int64_t maximum;
    const char* suffix;
    Section section;
    ConfigVarHandle handle = 0;
    double value = 0;
};

std::array options{
    Option{"active", "Enable Better Bloom", UI_CONTROL_TOGGLE, 1, 0, 1, nullptr, Section::Bloom},
    Option{"intensity", "Intensity", UI_CONTROL_NUMBER, 100, 0, 20000, "%", Section::Bloom},
    Option{"size", "Size", UI_CONTROL_NUMBER, 100, 0, 4000, "%", Section::Bloom},
    Option{"diffusion", "Base diffusion", UI_CONTROL_NUMBER, 0, 0, 4000, "%", Section::Profile},
    Option{"coreWidth", "Core width", UI_CONTROL_NUMBER, 100, 0, 5000, "%", Section::Profile},
    Option{"coreContribution", "Core contribution", UI_CONTROL_NUMBER, 100, 0, 2000, "%",
        Section::Profile},
    Option{
        "innerWidth", "Inner glow width", UI_CONTROL_NUMBER, 100, 0, 5000, "%", Section::Profile},
    Option{"innerContribution", "Inner glow contribution", UI_CONTROL_NUMBER, 100, 0, 2000, "%",
        Section::Profile},
    Option{
        "outerWidth", "Outer glow width", UI_CONTROL_NUMBER, 100, 0, 5000, "%", Section::Profile},
    Option{"outerContribution", "Outer glow contribution", UI_CONTROL_NUMBER, 100, 0, 2000, "%",
        Section::Profile},
    Option{"haloWidth", "Halo width", UI_CONTROL_NUMBER, 100, 0, 5000, "%", Section::Profile},
    Option{"haloContribution", "Halo contribution", UI_CONTROL_NUMBER, 100, 0, 2000, "%",
        Section::Profile},
    Option{"softness", "Soft threshold", UI_CONTROL_NUMBER, 50, 0, 667, "%", Section::Bloom},
    Option{
        "intensityMode", "Gain source", UI_CONTROL_DROPDOWN, 0, 0, 1, nullptr, Section::Response},
    Option{
        "responseGain", "Response gain", UI_CONTROL_NUMBER, 100, 0, 4000, "%", Section::Response},
    Option{"highlightRolloff", "Highlight rolloff", UI_CONTROL_NUMBER, 100, 0, 400, "%",
        Section::Response},
    Option{"autoCompensation", "Auto compensation", UI_CONTROL_TOGGLE, 1, 0, 1, nullptr,
        Section::Response},
    Option{
        "highlightLimit", "Highlight limit", UI_CONTROL_NUMBER, 0, 0, 4000, "%", Section::Response},
    Option{"highlightColour", "Highlight desaturation", UI_CONTROL_NUMBER, 0, 0, 100, "%",
        Section::Response},
    Option{"tintSource", "Tint source", UI_CONTROL_DROPDOWN, 0, 0, 1, nullptr, Section::Colour},
    Option{"saturation", "Saturation", UI_CONTROL_NUMBER, 100, 0, 300, "%", Section::Colour},
    Option{"tint", "Tint multiplier", UI_CONTROL_COLOR, 0, 0, 0, nullptr, Section::Colour},
    Option{"sceneStrength", "Colour layer strength", UI_CONTROL_NUMBER, 100, 0, 1000, "%",
        Section::Colour},
    Option{
        "sceneDarkening", "Scene darkening", UI_CONTROL_NUMBER, 100, 0, 100, "%", Section::Colour},
    Option{"pulseEnabled", "Enable pulsing", UI_CONTROL_TOGGLE, 1, 0, 1, nullptr, Section::Pulse},
    Option{
        "pulseSource", "Pulse activation", UI_CONTROL_DROPDOWN, 0, 0, 1, nullptr, Section::Pulse},
    Option{"pulsePeriod", "Average cycle duration", UI_CONTROL_NUMBER, defaultPulsePeriodMs, 100,
        60000, " ms", Section::Pulse},
    Option{
        "pulseIrregular", "Irregular timing", UI_CONTROL_TOGGLE, 1, 0, 1, nullptr, Section::Pulse},
    Option{"pulseSize", "Size variation", UI_CONTROL_NUMBER, 20, 0, 100, "%", Section::Pulse},
    Option{
        "pulseIntensity", "Intensity variation", UI_CONTROL_NUMBER, 0, 0, 100, "%", Section::Pulse},
    Option{"pulseFade", "Area transition", UI_CONTROL_NUMBER, 500, 0, 10000, " ms", Section::Pulse},
    Option{"thresholdMode", "Threshold source", UI_CONTROL_DROPDOWN, 0, 0, 2, nullptr,
        Section::BloomOverrides},
    Option{"threshold", "Threshold value", UI_CONTROL_NUMBER, 100, 0, 400, "%",
        Section::BloomOverrides},
    Option{"thresholdNormalization", "Threshold normalization", UI_CONTROL_NUMBER, 50, 0, 100, "%",
        Section::BloomOverrides},
    Option{"areaSource", "Bloom activation", UI_CONTROL_DROPDOWN, 0, 0, 1, nullptr,
        Section::BloomOverrides},
    Option{"blend", "Blend mode", UI_CONTROL_DROPDOWN, 0, 0, 2, nullptr, Section::BloomOverrides},
    Option{"resolution", "Processing resolution", UI_CONTROL_DROPDOWN, 1, 0, 2, nullptr,
        Section::Performance}};

static_assert(options.size() == static_cast<size_t>(Setting::Count));
struct Preset {
    int64_t id;
    const char* name;
    std::array<double, static_cast<size_t>(Setting::Count)> values;
    const char* tint = "";
};

enum class PresetKind { Faithful, FaithfulCold, Modern, ModernCold, Hybrid, Haze };

auto preset_values(PresetKind kind) {
    std::array<double, static_cast<size_t>(Setting::Count)> result{};
    for (size_t i = 0; i < options.size(); ++i) {
        result[i] = static_cast<double>(options[i].initial);
    }
    const auto set = [&](Setting setting, double value) {
        result[static_cast<size_t>(setting)] = value;
    };
    const bool faithful = kind == PresetKind::Faithful || kind == PresetKind::FaithfulCold ||
                          kind == PresetKind::Hybrid;
    if (!faithful) {
        set(Setting::Intensity, 129);
        set(Setting::Size, 50);
        set(Setting::Softness, 47);
        set(Setting::Diffusion, 29);
        set(Setting::ResponseGain, 40);
        set(Setting::SceneDarkening, 0);
        if (kind == PresetKind::ModernCold) {
            set(Setting::Intensity, 100);
            set(Setting::Softness, 50);
            set(Setting::ResponseGain, 50);
            set(Setting::TintSource, 1);
            set(Setting::SceneDarkening, 100);
            set(Setting::PulseEnabled, 0);
        }
        if (kind == PresetKind::Haze) {
            set(Setting::Intensity, 135);
            set(Setting::Size, 100);
            set(Setting::Softness, 70);
            set(Setting::Diffusion, 45);
            set(Setting::CoreContribution, 15);
            set(Setting::InnerWidth, 120);
            set(Setting::InnerContribution, 40);
            set(Setting::OuterWidth, 150);
            set(Setting::HaloWidth, 220);
            set(Setting::HaloContribution, 100);
            set(Setting::HighlightRolloff, 120);
            set(Setting::HighlightLimit, 130);
            set(Setting::Saturation, 85);
            set(Setting::PulseEnabled, 0);
        }
    } else {
        set(Setting::Intensity, 600);
        set(Setting::Softness, 0);
        set(Setting::Diffusion, 73);
        set(Setting::CoreWidth, 116);
        set(Setting::InnerWidth, 451);
        set(Setting::InnerContribution, 17);
        set(Setting::OuterContribution, 0);
        set(Setting::HaloContribution, 0);
        set(Setting::ResponseGain, 24);
        set(Setting::HighlightRolloff, 176);
        set(Setting::HighlightLimit, 95);
        set(Setting::HighlightColour, 100);
        if (kind == PresetKind::FaithfulCold) {
            set(Setting::TintSource, 1);
        }
        if (kind == PresetKind::Hybrid) {
            set(Setting::Intensity, 680);
            set(Setting::OuterWidth, 200);
            set(Setting::OuterContribution, 20);
            set(Setting::HaloContribution, 8);
        }
    }
    const float gain =
        static_cast<float>(result[static_cast<size_t>(Setting::ResponseGain)] / 100) *
        defaultResponseGain;
    const float rolloff =
        static_cast<float>(result[static_cast<size_t>(Setting::HighlightRolloff)] / 100);
    if (faithful && result[static_cast<size_t>(Setting::AutoCompensation)] != 0) {
        result[static_cast<size_t>(Setting::Intensity)] /= response_compensation(gain, rolloff);
    }
    return result;
}

const std::array presets{Preset{1, "Faithful", preset_values(PresetKind::Faithful)},
    Preset{7, "Faithful (Cold)", preset_values(PresetKind::FaithfulCold), "847770"},
    Preset{3, "Modern", preset_values(PresetKind::Modern)},
    Preset{6, "Modern (Cold)", preset_values(PresetKind::ModernCold), "fff4e8"},
    Preset{4, "Hybrid", preset_values(PresetKind::Hybrid)},
    Preset{5, "Haze", preset_values(PresetKind::Haze)}};
ConfigVarHandle lastPresetHandle = 0;
int64_t lastPreset = 0;
bool applyingPreset = false;
bool customTint = false;
Settings current;
std::array<float, 3> tint{1, 1, 1};

const Preset* find_preset(int64_t id) {
    if (id == 2) {
        id = 3;
    }
    for (const auto& preset : presets) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

int64_t saved_preset(int64_t id) {
    const auto* preset = find_preset(id);
    return preset != nullptr ? preset->id : 0;
}

std::array<float, 3> decode_tint(std::string_view text) {
    if (text.starts_with('#')) {
        text.remove_prefix(1);
    }
    uint32_t rgb = 0;
    if (text.size() == 6) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), rgb, 16);
        if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
            return {static_cast<float>((rgb >> 16) & 255u) / 255.0f,
                static_cast<float>((rgb >> 8) & 255u) / 255.0f,
                static_cast<float>(rgb & 255u) / 255.0f};
        }
    }
    return {1, 1, 1};
}

double value(Setting setting) {
    return options[static_cast<size_t>(setting)].value;
}
float percent(Setting setting) {
    return static_cast<float>(value(setting) / 100.0);
}

bool preset_option(Setting setting) {
    return setting != Setting::Active && setting != Setting::Resolution &&
           options[static_cast<size_t>(setting)].section != Section::BloomOverrides;
}

int64_t matching_preset() {
    for (size_t p = 0; p < presets.size(); ++p) {
        if (customTint != (presets[p].tint[0] != '\0') || tint != decode_tint(presets[p].tint)) {
            continue;
        }
        bool matches = true;
        for (size_t i = 0; i < options.size(); ++i) {
            if (preset_option(static_cast<Setting>(i)) && options[i].kind != UI_CONTROL_COLOR &&
                std::abs(options[i].value - presets[p].values[i]) > 0.000001)
            {
                matches = false;
                break;
            }
        }
        if (matches) {
            return static_cast<int64_t>(p + 1);
        }
    }
    return 0;
}

bool continuous(Setting setting) {
    return setting == Setting::Intensity || setting == Setting::ResponseGain;
}

void rebuild() {
    current.active = value(Setting::Active) != 0;
    current.forceBloom = value(Setting::AreaSource) != 0;
    current.thresholdSource = static_cast<ThresholdSource>(value(Setting::ThresholdSource));
    current.fixedGain = value(Setting::GainSource) != 0;
    current.threshold = percent(Setting::Threshold) * 0.25f;
    current.thresholdNormalization = percent(Setting::ThresholdNormalization);
    current.softness = percent(Setting::Softness) * 0.6f;
    current.responseGain = percent(Setting::ResponseGain) * defaultResponseGain;
    current.highlightColour = percent(Setting::HighlightColour);
    current.highlightRolloff = percent(Setting::HighlightRolloff);
    current.autoCompensation = value(Setting::AutoCompensation) != 0;
    current.responseCompensation =
        current.autoCompensation ?
            response_compensation(current.responseGain, current.highlightRolloff) :
            1;
    current.highlightLimit = percent(Setting::HighlightLimit);
    current.sceneStrength = percent(Setting::SceneStrength);
    current.sceneDarkening = percent(Setting::SceneDarkening);
    current.neutralTint = value(Setting::TintSource) != 0;
    current.intensity = percent(Setting::Intensity) * defaultBloomIntensity;
    current.radius = percent(Setting::Size) * defaultBloomRadius;
    current.profile = make_profile({percent(Setting::CoreWidth), percent(Setting::InnerWidth),
                                       percent(Setting::OuterWidth), percent(Setting::HaloWidth)},
        {percent(Setting::CoreContribution), percent(Setting::InnerContribution),
            percent(Setting::OuterContribution), percent(Setting::HaloContribution)},
        percent(Setting::Diffusion) * 1.7f);
    current.saturation = percent(Setting::Saturation);
    current.tint = tint;
    current.blend = static_cast<uint32_t>(value(Setting::Blend));
    current.divisor = 1u << static_cast<uint32_t>(value(Setting::Resolution));
    current.pulse.enabled = value(Setting::PulseEnabled) != 0;
    current.pulse.irregular = value(Setting::PulseIrregular) != 0;
    current.pulse.source = static_cast<PulseSource>(value(Setting::PulseSource));
    current.pulse.period = static_cast<double>(value(Setting::PulsePeriod)) / 1000;
    current.pulse.sizeAmount = percent(Setting::PulseSize);
    current.pulse.intensityAmount = percent(Setting::PulseIntensity);
    current.pulse.fade = static_cast<double>(value(Setting::PulseFade)) / 1000;
}

void parse_tint(const char* text, size_t length) {
    customTint = text != nullptr && length != 0;
    tint = decode_tint(text != nullptr ? std::string_view(text, length) : std::string_view{});
}

void on_changed(ModContext*, ConfigVarHandle, const ConfigVarValue* next, const ConfigVarValue*,
    void* userdata) {
    auto& option = *static_cast<Option*>(userdata);
    if (option.kind == UI_CONTROL_COLOR) {
        parse_tint(next->string_value, next->string_length);
    } else {
        const double number = next->type == CONFIG_VAR_FLOAT ? next->float_value :
                              next->type == CONFIG_VAR_BOOL  ? next->bool_value :
                                                               next->int_value;
        option.value = std::isfinite(number) ?
                           std::clamp(number, double(option.minimum), double(option.maximum)) :
                           option.initial;
    }
    rebuild();
    if (!applyingPreset && lastPresetHandle != 0) {
        const auto matching = matching_preset();
        if (matching != 0 && presets[matching - 1].id != lastPreset) {
            svc_config->set_int(mod_ctx, lastPresetHandle, presets[matching - 1].id);
        }
    }
}

void restore_defaults(ModContext*, void*) {
    applyingPreset = true;
    for (auto& option : options) {
        if (option.kind == UI_CONTROL_COLOR) {
            svc_config->set_string(mod_ctx, option.handle, "");
        } else if (option.kind == UI_CONTROL_TOGGLE) {
            svc_config->set_bool(mod_ctx, option.handle, option.initial != 0);
        } else if (&option == &options[static_cast<size_t>(Setting::Intensity)] ||
                   &option == &options[static_cast<size_t>(Setting::ResponseGain)])
        {
            svc_config->set_float(mod_ctx, option.handle, static_cast<double>(option.initial));
        } else {
            svc_config->set_int(mod_ctx, option.handle, option.initial);
        }
    }
    svc_config->set_int(mod_ctx, lastPresetHandle, 0);
    applyingPreset = false;
}

void restore_profile(ModContext*, void*) {
    const auto* preset = find_preset(lastPreset);
    for (auto& option : options) {
        if (option.section == Section::Profile) {
            const size_t index = static_cast<size_t>(&option - options.data());
            const double reference = preset != nullptr ? preset->values[index] : option.initial;
            svc_config->set_int(mod_ctx, option.handle, static_cast<int64_t>(reference));
        }
    }
}

bool control_disabled(ModContext*, void* userdata) {
    const auto setting = static_cast<Setting>(reinterpret_cast<uintptr_t>(userdata));
    switch (setting) {
    case Setting::Threshold:
        return current.thresholdSource == ThresholdSource::Vanilla;
    case Setting::ThresholdNormalization:
        return current.thresholdSource != ThresholdSource::Normalized;
    case Setting::PulsePeriod:
    case Setting::PulseIrregular:
    case Setting::PulseSize:
    case Setting::PulseIntensity:
    case Setting::PulseFade:
    case Setting::PulseSource:
        return !current.pulse.enabled;
    default:
        return false;
    }
}

void get_number(ModContext*, void* userdata, UiControlValue* output) {
    const auto setting = static_cast<Setting>(reinterpret_cast<uintptr_t>(userdata));
    output->int_value = static_cast<int64_t>(std::lround(value(setting)));
}

void set_number(ModContext*, void* userdata, const UiControlValue* input) {
    const auto setting = static_cast<Setting>(reinterpret_cast<uintptr_t>(userdata));
    const auto& option = options[static_cast<size_t>(setting)];
    svc_config->set_float(mod_ctx, option.handle,
        static_cast<double>(std::clamp(input->int_value, option.minimum, option.maximum)));
}

bool number_modified(ModContext*, void* userdata) {
    const auto setting = static_cast<Setting>(reinterpret_cast<uintptr_t>(userdata));
    const auto& option = options[static_cast<size_t>(setting)];
    return std::abs(option.value - option.initial) > 0.000001;
}

UiControlDesc describe_option(Setting setting) {
    static constexpr const char* sources[]{"Vanilla", "Fixed"};
    static constexpr const char* tints[]{"Vanilla", "Neutral"};
    static constexpr const char* thresholds[]{"Vanilla", "Custom", "Normalized"};
    static constexpr const char* activations[]{"Vanilla", "Everywhere"};
    static constexpr const char* blendModes[]{"Vanilla", "Additive", "Screen"};
    static constexpr const char* resolutions[]{"Full", "Half", "Quarter"};
    auto& option = options[static_cast<size_t>(setting)];
    UiControlDesc descriptor = UI_CONTROL_DESC_INIT;
    descriptor.kind = option.kind;
    descriptor.label = option.label;
    descriptor.binding = UI_BINDING_CONFIG_VAR;
    descriptor.config_var = option.handle;
    if (continuous(setting)) {
        descriptor.binding = UI_BINDING_CALLBACKS;
        descriptor.get = get_number;
        descriptor.set = set_number;
        descriptor.is_modified = number_modified;
    }
    descriptor.min = option.minimum;
    descriptor.max = option.maximum;
    descriptor.step = setting == Setting::PulsePeriod || setting == Setting::PulseFade ? 100 : 1;
    descriptor.suffix = option.suffix;
    descriptor.is_disabled = control_disabled;
    descriptor.user_data = reinterpret_cast<void*>(static_cast<uintptr_t>(setting));
    switch (setting) {
    case Setting::ThresholdSource:
        descriptor.options = thresholds;
        descriptor.option_count = std::size(thresholds);
        break;
    case Setting::TintSource:
        descriptor.options = tints;
        descriptor.option_count = std::size(tints);
        break;
    case Setting::GainSource:
        descriptor.options = sources;
        descriptor.option_count = std::size(sources);
        break;
    case Setting::AreaSource:
    case Setting::PulseSource:
        descriptor.options = activations;
        descriptor.option_count = std::size(activations);
        break;
    case Setting::Blend:
        descriptor.options = blendModes;
        descriptor.option_count = std::size(blendModes);
        break;
    case Setting::Resolution:
        descriptor.options = resolutions;
        descriptor.option_count = std::size(resolutions);
        break;
    default:
        break;
    }
    return descriptor;
}

void get_preset(ModContext*, void*, UiControlValue* output) {
    output->int_value = matching_preset();
}

void set_preset(ModContext*, void*, const UiControlValue* input) {
    if (input->int_value < 1 || input->int_value > static_cast<int64_t>(presets.size())) {
        return;
    }
    const auto& preset = presets[input->int_value - 1];
    applyingPreset = true;
    for (size_t i = 0; i < options.size(); ++i) {
        if (!preset_option(static_cast<Setting>(i))) {
            continue;
        }
        const auto& option = options[i];
        const double next = preset.values[i];
        if (option.kind == UI_CONTROL_COLOR) {
            svc_config->set_string(mod_ctx, option.handle, preset.tint);
        } else if (option.kind == UI_CONTROL_TOGGLE) {
            svc_config->set_bool(mod_ctx, option.handle, next != 0);
        } else if (continuous(static_cast<Setting>(i))) {
            svc_config->set_float(mod_ctx, option.handle, next);
        } else {
            svc_config->set_int(mod_ctx, option.handle, static_cast<int64_t>(next));
        }
    }
    svc_config->set_int(mod_ctx, lastPresetHandle, preset.id);
    applyingPreset = false;
}

UiControlDesc describe_presets() {
    static const char* names[]{"Custom", presets[0].name, presets[1].name, presets[2].name,
        presets[3].name, presets[4].name, presets[5].name};
    static constexpr bool selectable[]{false, true, true, true, true, true, true};
    UiControlDesc descriptor = UI_CONTROL_DESC_INIT;
    descriptor.kind = UI_CONTROL_DROPDOWN;
    descriptor.label = "Preset";
    descriptor.binding = UI_BINDING_CALLBACKS;
    descriptor.config_var = lastPresetHandle;
    descriptor.get = get_preset;
    descriptor.set = set_preset;
    descriptor.options = names;
    descriptor.option_count = std::size(names);
    descriptor.min = 0;
    descriptor.max = static_cast<int64_t>(presets.size());
    descriptor.option_enabled = selectable;
    return descriptor;
}

ModResult add_option(UiElementHandle pane, Setting setting, ModError* error) {
    const auto descriptor = describe_option(setting);
    const ModResult result = svc_ui->pane_add_control(mod_ctx, pane, &descriptor, nullptr);
    return result == MOD_OK ? MOD_OK :
                              mods::set_error(error, result, "Could not create bloom controls.");
}

ModResult build_section(UiElementHandle pane, Section section, const char* title, ModError* error) {
    ModResult result = svc_ui->pane_add_section(mod_ctx, pane, title);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "Could not create settings section.");
    }
    for (size_t i = 0; i < options.size(); ++i) {
        if (options[i].section == section) {
            result = add_option(pane, static_cast<Setting>(i), error);
            if (result != MOD_OK) {
                return result;
            }
        }
    }
    if (section == Section::Profile) {
        UiControlDesc reset = UI_CONTROL_DESC_INIT;
        reset.kind = UI_CONTROL_BUTTON;
        reset.label = "Restore glow profile defaults";
        reset.on_pressed = restore_profile;
        return svc_ui->pane_add_control(mod_ctx, pane, &reset, nullptr);
    }
    return MOD_OK;
}

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError* error) {
    ModResult presetResult = svc_ui->pane_add_section(mod_ctx, panel, "Presets");
    if (presetResult == MOD_OK) {
        const auto descriptor = describe_presets();
        presetResult = svc_ui->pane_add_control(mod_ctx, panel, &descriptor, nullptr);
    }
    if (presetResult != MOD_OK) {
        return mods::set_error(error, presetResult, "Could not create bloom presets.");
    }
    for (const auto& group : groups) {
        const ModResult result = build_section(panel, group.section, group.title, error);
        if (result != MOD_OK) {
            return result;
        }
    }
    UiControlDesc reset = UI_CONTROL_DESC_INIT;
    reset.kind = UI_CONTROL_BUTTON;
    reset.label = "Restore all defaults";
    reset.on_pressed = restore_defaults;
    return svc_ui->pane_add_control(mod_ctx, panel, &reset, nullptr);
}

}  // namespace

const Settings& settings() {
    return current;
}

ModResult initialize_settings(ModError* error) {
    current = {};
    tint = {1, 1, 1};
    customTint = false;
    applyingPreset = false;
    lastPresetHandle = 0;
    lastPreset = 0;
    for (size_t i = 0; i < options.size(); ++i) {
        auto& option = options[i];
        option.handle = 0;
        option.value = static_cast<double>(option.initial);
        ConfigVarDesc descriptor = CONFIG_VAR_DESC_INIT;
        descriptor.name = option.name;
        descriptor.default_int = option.initial;
        descriptor.default_float = static_cast<double>(option.initial);
        descriptor.default_bool = option.initial != 0;
        descriptor.default_string = "";
        descriptor.type = option.kind == UI_CONTROL_TOGGLE    ? CONFIG_VAR_BOOL :
                          option.kind == UI_CONTROL_COLOR     ? CONFIG_VAR_STRING :
                          continuous(static_cast<Setting>(i)) ? CONFIG_VAR_FLOAT :
                                                                CONFIG_VAR_INT;
        ModResult result = svc_config->register_var(mod_ctx, &descriptor, &option.handle);
        if (result != MOD_OK) {
            return mods::set_error(error, result, "Could not register bloom settings.");
        }
        if (descriptor.type == CONFIG_VAR_BOOL) {
            bool state = option.initial != 0;
            svc_config->get_bool(mod_ctx, option.handle, &state);
            option.value = state;
        } else if (descriptor.type == CONFIG_VAR_STRING) {
            std::array<char, 16> text{};
            size_t length = 0;
            if (svc_config->get_string(mod_ctx, option.handle, text.data(), text.size(), &length) ==
                MOD_OK)
            {
                parse_tint(text.data(), length);
            }
        } else {
            if (descriptor.type == CONFIG_VAR_FLOAT) {
                svc_config->get_float(mod_ctx, option.handle, &option.value);
            } else {
                int64_t stored = option.initial;
                svc_config->get_int(mod_ctx, option.handle, &stored);
                option.value = static_cast<double>(stored);
            }
            option.value =
                std::isfinite(option.value) ?
                    std::clamp(option.value, double(option.minimum), double(option.maximum)) :
                    option.initial;
        }
        result = svc_config->subscribe(mod_ctx, option.handle, on_changed, &option, nullptr);
        if (result != MOD_OK) {
            return mods::set_error(error, result, "Could not subscribe to bloom settings.");
        }
    }
    ConfigVarDesc version = CONFIG_VAR_DESC_INIT;
    version.name = "settingsVersion";
    version.type = CONFIG_VAR_INT;
    ConfigVarHandle versionHandle = 0;
    int64_t savedVersion = 0;
    if (svc_config->register_var(mod_ctx, &version, &versionHandle) != MOD_OK ||
        svc_config->get_int(mod_ctx, versionHandle, &savedVersion) != MOD_OK)
    {
        return mods::set_error(error, MOD_ERROR, "Could not read bloom settings version.");
    }
    if (savedVersion < 1) {
        if (value(Setting::PulsePeriod) == 4000 &&
            svc_config->set_int(mod_ctx, options[static_cast<size_t>(Setting::PulsePeriod)].handle,
                defaultPulsePeriodMs) != MOD_OK)
        {
            return mods::set_error(error, MOD_ERROR, "Could not update pulse timing.");
        }
        if (tint == std::array<float, 3>{1, 1, 1} &&
            svc_config->set_string(
                mod_ctx, options[static_cast<size_t>(Setting::Tint)].handle, "") != MOD_OK)
        {
            return mods::set_error(error, MOD_ERROR, "Could not update the default tint.");
        }
    }
    if (savedVersion < 2) {
        const bool custom = std::any_of(options.begin(), options.end(), [](const auto& option) {
            return option.section == Section::Profile && option.value != option.initial;
        });
        if (custom) {
            constexpr Profile previous{{0.35f, 1, 2.5f, 6}, {0.45f, 0.30f, 0.17f, 0.08f}};
            constexpr Profile reference;
            for (size_t i = 0; i < previous.scales.size(); ++i) {
                const size_t first = static_cast<size_t>(Setting::CoreWidth) + i * 2;
                const std::array ratios{previous.scales[i] / reference.scales[i],
                    previous.weights[i] / reference.weights[i]};
                for (size_t j = 0; j < ratios.size(); ++j) {
                    auto& option = options[first + j];
                    const auto converted =
                        static_cast<int64_t>(std::lround(option.value * ratios[j]));
                    if (svc_config->set_int(mod_ctx, option.handle,
                            std::clamp(converted, option.minimum, option.maximum)) != MOD_OK)
                    {
                        return mods::set_error(
                            error, MOD_ERROR, "Could not migrate the bloom profile.");
                    }
                }
            }
        }
    }
    if (savedVersion > 0 && savedVersion < 3) {
        for (Setting setting : {Setting::Intensity, Setting::Size}) {
            auto& option = options[static_cast<size_t>(setting)];
            const double converted =
                std::clamp(option.value * 2, double(option.minimum), double(option.maximum));
            const ModResult result =
                continuous(setting) ?
                    svc_config->set_float(mod_ctx, option.handle, converted) :
                    svc_config->set_int(mod_ctx, option.handle, static_cast<int64_t>(converted));
            if (result != MOD_OK) {
                return mods::set_error(error, MOD_ERROR, "Could not update bloom scaling.");
            }
        }
    }
    if (savedVersion > 0 && savedVersion < 4) {
        auto& option = options[static_cast<size_t>(Setting::Softness)];
        const int64_t converted = (static_cast<int64_t>(option.value) * 5 + 1) / 3;
        if (svc_config->set_int(mod_ctx, option.handle,
                std::clamp(converted, option.minimum, option.maximum)) != MOD_OK)
        {
            return mods::set_error(error, MOD_ERROR, "Could not update soft threshold scaling.");
        }
    }
    if (savedVersion > 0 && savedVersion < 5) {
        auto& option = options[static_cast<size_t>(Setting::Threshold)];
        if (svc_config->set_int(mod_ctx, option.handle,
                std::clamp(static_cast<int64_t>(option.value) * 4, option.minimum,
                    option.maximum)) != MOD_OK)
        {
            return mods::set_error(error, MOD_ERROR, "Could not update threshold scaling.");
        }
    }
    if (savedVersion > 0 && savedVersion < 7) {
        auto& intensity = options[static_cast<size_t>(Setting::Intensity)];
        if (svc_config->set_float(
                mod_ctx, intensity.handle, std::floor((intensity.value + 1) / 2)) != MOD_OK)
        {
            return mods::set_error(error, MOD_ERROR, "Could not update intensity scaling.");
        }
    }
    if (savedVersion < 8) {
        const auto read_retired = [&](const char* name, int64_t initial, int64_t maximum,
                                      double& output) {
            ConfigVarDesc descriptor = CONFIG_VAR_DESC_INIT;
            descriptor.name = name;
            descriptor.type = CONFIG_VAR_INT;
            descriptor.default_int = initial;
            ConfigVarHandle handle = 0;
            ModResult result = svc_config->register_var(mod_ctx, &descriptor, &handle);
            if (result != MOD_OK) {
                return result;
            }
            int64_t stored = initial;
            result = svc_config->get_int(mod_ctx, handle, &stored);
            const ModResult removed = svc_config->unregister_var(mod_ctx, handle);
            output = static_cast<double>(std::clamp(stored, int64_t{0}, maximum)) / 100.0;
            return result != MOD_OK ? result : removed;
        };
        double emphasis = 1;
        double customGain = 1;
        if (read_retired("surfaceEmphasis", 100, 400, emphasis) != MOD_OK ||
            read_retired("customIntensity", 100, 1000, customGain) != MOD_OK)
        {
            return mods::set_error(error, MOD_ERROR, "Could not read previous bloom response.");
        }
        const double slope = 1 + 1.5 * emphasis;
        const double scale = (1 + 0.5 * emphasis) / slope;
        const double gain = slope * (value(Setting::GainSource) != 0 ? customGain : 1);
        const auto& intensity = options[static_cast<size_t>(Setting::Intensity)];
        const auto& responseGain = options[static_cast<size_t>(Setting::ResponseGain)];
        if (svc_config->set_float(mod_ctx, responseGain.handle, gain / defaultResponseGain * 100) !=
                MOD_OK ||
            svc_config->set_float(mod_ctx, intensity.handle,
                intensity.value * scale / defaultResponseScale) != MOD_OK)
        {
            return mods::set_error(error, MOD_ERROR, "Could not migrate bloom response controls.");
        }
    }
    rebuild();
    if (savedVersion < 9 && current.autoCompensation) {
        const auto& intensity = options[static_cast<size_t>(Setting::Intensity)];
        if (svc_config->set_float(mod_ctx, intensity.handle,
                intensity.value / current.responseCompensation) != MOD_OK)
        {
            return mods::set_error(error, MOD_ERROR,
                "Could not preserve bloom brightness with automatic compensation.");
        }
    }
    if (savedVersion < 9 && svc_config->set_int(mod_ctx, versionHandle, 9) != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "Could not save bloom settings version.");
    }
    rebuild();
    ConfigVarDesc reference = CONFIG_VAR_DESC_INIT;
    reference.name = "lastPreset";
    reference.type = CONFIG_VAR_INT;
    if (svc_config->register_var(mod_ctx, &reference, &lastPresetHandle) != MOD_OK ||
        svc_config->get_int(mod_ctx, lastPresetHandle, &lastPreset) != MOD_OK)
    {
        return mods::set_error(error, MOD_ERROR, "Could not read the last bloom preset.");
    }
    const int64_t normalizedPreset = saved_preset(lastPreset);
    if (lastPreset != normalizedPreset &&
        svc_config->set_int(mod_ctx, lastPresetHandle, normalizedPreset) != MOD_OK)
    {
        return mods::set_error(error, MOD_ERROR, "Could not update the saved bloom preset.");
    }
    lastPreset = normalizedPreset;
    const auto changed = [](ModContext*, ConfigVarHandle, const ConfigVarValue* next,
                             const ConfigVarValue*,
                             void*) { lastPreset = saved_preset(next->int_value); };
    if (svc_config->subscribe(mod_ctx, lastPresetHandle, changed, nullptr, nullptr) != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "Could not subscribe to the last bloom preset.");
    }
    const auto matching = matching_preset();
    if (matching != 0 && presets[matching - 1].id != lastPreset &&
        svc_config->set_int(mod_ctx, lastPresetHandle, presets[matching - 1].id) != MOD_OK)
    {
        return mods::set_error(error, MOD_ERROR, "Could not identify the saved bloom preset.");
    }
    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_panel;
    return svc_ui->register_mods_panel(mod_ctx, &panel);
}

}  // namespace bloom
