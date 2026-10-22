#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace bloom {

constexpr double originalPulsePeriod = 65536.0 / (1499.5 * 30.0);
constexpr int64_t defaultPulsePeriodMs = 1457;

enum class PulseSource {
    Vanilla,
    Everywhere,
};

struct PulseSettings {
    bool enabled = true;
    bool irregular = true;
    PulseSource source = PulseSource::Vanilla;
    double period = defaultPulsePeriodMs / 1000.0;
    float sizeAmount = 0.2f;
    float intensityAmount = 0;
    double fade = 0.5;
};

struct PulseScale {
    float size = 1;
    float intensity = 1;
};

class Pulse {
  public:
    Pulse() { reset(); }

    PulseScale advance(double elapsed, bool environment, const PulseSettings& settings) {
        if (!std::isfinite(elapsed) || elapsed < 0) {
            elapsed = 0;
        }
        const bool allowed =
            settings.enabled && (settings.source == PulseSource::Everywhere || environment);
        if (!settings.enabled) {
            m_envelope = 0;
        } else if (!std::isfinite(settings.fade) || settings.fade <= 0) {
            m_envelope = allowed ? 1 : 0;
        } else {
            const double direction = allowed ? 1 : -1;
            m_envelope = std::clamp(m_envelope + direction * elapsed / settings.fade, 0.0, 1.0);
        }
        if (!allowed && m_envelope == 0) {
            return {};
        }
        const double period =
            std::isfinite(settings.period) ? std::max(settings.period, 0.1) : originalPulsePeriod;
        advance_phase(elapsed * 30.0 * originalPulsePeriod / period, settings.irregular);
        const double wave = std::sin(2 * std::numbers::pi * m_phase) * m_envelope;
        return {static_cast<float>(1 + std::clamp(settings.sizeAmount, 0.0f, 1.0f) * wave),
                static_cast<float>(1 + std::clamp(settings.intensityAmount, 0.0f, 1.0f) * wave)};
    }

    void reset() {
        m_random = 0x91e10da5;
        m_phase = 0;
        m_tickFraction = 0;
        m_step = next_step();
        m_envelope = 0;
    }

  private:
    double next_step() {
        m_random ^= m_random << 13;
        m_random ^= m_random >> 17;
        m_random ^= m_random << 5;
        return static_cast<double>(500 + m_random % 2000) / 65536.0;
    }

    void advance_phase(double ticks, bool irregular) {
        constexpr double meanStep = 1499.5 / 65536.0;
        while (ticks > 0) {
            const double consumed = std::min(ticks, 1 - m_tickFraction);
            m_phase = std::fmod(m_phase + consumed * (irregular ? m_step : meanStep), 1.0);
            m_tickFraction += consumed;
            ticks -= consumed;
            if (m_tickFraction >= 1 - 1e-12) {
                m_tickFraction = 0;
                m_step = next_step();
            }
        }
    }

    uint32_t m_random = 0;
    double m_phase = 0;
    double m_tickFraction = 0;
    double m_step = 0;
    double m_envelope = 0;
};

}
