struct Frame {
    tint: vec4f,
    mono: vec4f,
    threshold: f32,
    knee: f32,
    intensity: f32,
    mode: u32,
    enabled: u32,
    source_srgb: u32,
    target_srgb: u32,
    saturation: f32,
    gain: f32,
    response_gain: f32,
    reserved: u32,
    highlight_colour: f32,
    highlight_rolloff: f32,
    highlight_limit: f32,
    response_scale: f32,
    padding2: f32,
    brightness_metric: u32,
    pre_diffusion_gain: f32,
    source_limit: f32,
    filtering_space: u32,
    spread: vec2f,
    padding3: vec2u,
}

@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var bloom: texture_2d<f32>;
@group(0) @binding(2) var linear_sampler: sampler;
@group(0) @binding(3) var<uniform> frame: Frame;

struct Vertex {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vertex(@builtin(vertex_index) index: u32) -> Vertex {
    let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return Vertex(vec4f(uv * vec2f(2, -2) + vec2f(-1, 1), 0, 1), uv);
}

fn to_linear(value: vec3f) -> vec3f {
    return select(pow((value + 0.055) / 1.055, vec3f(2.4)), value / 12.92, value <= vec3f(0.04045));
}

fn to_srgb(value: vec3f) -> vec3f {
    return select(1.055 * pow(max(value, vec3f(0)), vec3f(1.0 / 2.4)) - 0.055,
        value * 12.92, value <= vec3f(0.0031308));
}

fn soft_clip(value: vec3f) -> vec3f {
    if frame.highlight_rolloff <= 0.0 {
        return min(value, vec3f(1));
    }
    if frame.highlight_rolloff == 1.0 {
        return value / (1.0 + value);
    }
    let ratio = min(value, 1.0 / max(value, vec3f(0.00000001)));
    return min(value, vec3f(1)) * pow(1.0 + pow(ratio, vec3f(1.0 / frame.highlight_rolloff)),
        vec3f(-frame.highlight_rolloff));
}

@fragment
fn fragment(input: Vertex) -> @location(0) vec4f {
    let sampled = textureSampleLevel(scene, linear_sampler, input.uv, 0.0);
    var encoded = sampled.rgb;
    if frame.source_srgb != 0u {
        encoded = to_srgb(sampled.rgb);
    }
    let graded = mix(encoded, vec3f(encoded.r) * frame.mono.rgb, frame.mono.a);
    var base = graded;
    var glow = vec3f(0);
    var colour = graded;
    if frame.enabled != 0u {
        var accumulated = textureSampleLevel(bloom, linear_sampler, input.uv, 0.0).rgb;
        if frame.filtering_space == 0u {
            accumulated = to_srgb(accumulated);
        }
        var amplified = max(accumulated * frame.gain * frame.response_gain, vec3f(0));
        if frame.pre_diffusion_gain > 0.0 {
            var remaining_gain = 1.0;
            if frame.pre_diffusion_gain < 1.0 {
                remaining_gain = pow(max(frame.gain * frame.response_gain, 0.0), 1.0 - frame.pre_diffusion_gain);
            }
            amplified = max(accumulated * remaining_gain, vec3f(0));
        }
        let peak = max(amplified.r, max(amplified.g, amplified.b));
        var response: vec3f;
        if frame.highlight_colour >= 1.0 {
            response = soft_clip(amplified);
        } else {
            response = amplified / (1.0 + peak);
            if frame.highlight_rolloff != 1.0 {
                let compressed_peak = soft_clip(vec3f(peak)).x;
                response = amplified * (compressed_peak / max(peak, 0.00000001));
            }
            if frame.highlight_colour > 0.0 {
                response = mix(response, soft_clip(amplified), frame.highlight_colour);
            }
        }
        response *= frame.response_scale;
        var intensity = frame.intensity;
        if frame.highlight_limit > 0.0 {
            let signal = response * intensity;
            if frame.highlight_colour >= 1.0 {
                response = min(signal, vec3f(frame.highlight_limit));
            } else {
                let signal_peak = max(signal.r, max(signal.g, signal.b));
                let preserved = signal * min(1.0, frame.highlight_limit / max(signal_peak, 0.00000001));
                response = preserved;
                if frame.highlight_colour > 0.0 {
                    response = mix(preserved, min(signal, vec3f(frame.highlight_limit)), frame.highlight_colour);
                }
            }
            intensity = 1.0;
        }
        let filtered = response * frame.tint.rgb;
        let luminance = dot(filtered, vec3f(0.2126, 0.7152, 0.0722));
        glow = max(mix(vec3f(luminance), filtered, frame.saturation), vec3f(0)) * intensity;
        let transmission = select(vec3f(1), max(vec3f(0), 1.0 - colour), frame.mode == 1u);
        base = graded * frame.tint.a;
        colour = base + glow * transmission;
    }
    var output = clamp(colour, vec3f(0), vec3f(1));
    if frame.target_srgb != 0u {
        output = to_linear(output);
    }
    return vec4f(output, sampled.a);
}
