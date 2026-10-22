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
}

struct Step {
    weight: f32,
    terminal: u32,
    padding0: u32,
    padding1: u32,
}

@group(0) @binding(0) var high: texture_2d<f32>;
@group(0) @binding(1) var low: texture_2d<f32>;
@group(0) @binding(2) var linear_sampler: sampler;
@group(0) @binding(3) var output: texture_storage_2d<rgba16float, write>;
@group(0) @binding(4) var<uniform> frame: Frame;
@group(0) @binding(5) var<uniform> step: Step;

var<workgroup> highlights: array<vec3f, 361>;

fn to_linear(value: vec3f) -> vec3f {
    return select(pow((value + 0.055) / 1.055, vec3f(2.4)), value / 12.92, value <= vec3f(0.04045));
}

fn to_srgb(value: vec3f) -> vec3f {
    return select(1.055 * pow(max(value, vec3f(0)), vec3f(1.0 / 2.4)) - 0.055,
        value * 12.92, value <= vec3f(0.0031308));
}

fn extract(uv: vec2f) -> vec3f {
    let sampled = textureSampleLevel(high, linear_sampler, uv, 0.0).rgb;
    var encoded = sampled;
    if frame.source_srgb != 0u {
        encoded = to_srgb(sampled);
    }
    let brightness = dot(encoded, vec3f(0.25, 0.25, 0.5));
    let knee = max(frame.knee, 0.00001);
    let shoulder = clamp(brightness - frame.threshold + knee, 0.0, 2.0 * knee);
    let contribution = min(brightness,
        max(brightness - frame.threshold, shoulder * shoulder / (4.0 * knee)));
    let extracted = encoded * max(contribution, 0.0);
    return to_linear(extracted);
}

fn sample_high(uv: vec2f, prefilter: bool) -> vec3f {
    if prefilter {
        return extract(uv);
    }
    return textureSampleLevel(high, linear_sampler, uv, 0.0).rgb;
}

fn reduce(uv: vec2f, prefilter: bool) -> vec3f {
    let texel = 1.0 / vec2f(textureDimensions(high));
    var result = sample_high(uv, prefilter) * 0.125;
    result += (sample_high(uv + texel * vec2f(-2, -2), prefilter)
        + sample_high(uv + texel * vec2f(2, -2), prefilter)
        + sample_high(uv + texel * vec2f(-2, 2), prefilter)
        + sample_high(uv + texel * vec2f(2, 2), prefilter)) * 0.03125;
    result += (sample_high(uv + texel * vec2f(0, -2), prefilter)
        + sample_high(uv + texel * vec2f(-2, 0), prefilter)
        + sample_high(uv + texel * vec2f(2, 0), prefilter)
        + sample_high(uv + texel * vec2f(0, 2), prefilter)) * 0.0625;
    result += (sample_high(uv + texel * vec2f(-1, -1), prefilter)
        + sample_high(uv + texel * vec2f(1, -1), prefilter)
        + sample_high(uv + texel * vec2f(-1, 1), prefilter)
        + sample_high(uv + texel * vec2f(1, 1), prefilter)) * 0.125;
    return result;
}

@compute @workgroup_size(8, 8)
fn prefilter(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(output);
    if any(id.xy >= size) {
        return;
    }
    let uv = (vec2f(id.xy) + 0.5) / vec2f(size);
    var colour = vec3f(0);
    if frame.enabled != 0u {
        colour = reduce(uv, true);
    }
    textureStore(output, id.xy, vec4f(colour, 1));
}

@compute @workgroup_size(8, 8)
fn downsample(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(output);
    if any(id.xy >= size) {
        return;
    }
    let uv = (vec2f(id.xy) + 0.5) / vec2f(size);
    textureStore(output, id.xy, vec4f(reduce(uv, false), 1));
}

@compute @workgroup_size(8, 8)
fn prefilter_cached(@builtin(global_invocation_id) id: vec3u,
    @builtin(workgroup_id) group: vec3u,
    @builtin(local_invocation_id) local: vec3u,
    @builtin(local_invocation_index) index: u32) {
    let size = textureDimensions(output);
    let source_size = textureDimensions(high);
    let stride = source_size / size;
    let tile_size = vec2u(7) * stride + vec2u(5);
    let origin = (vec2f(group.xy * 8u) + 0.5) / vec2f(size);
    for (var i = index; i < tile_size.x * tile_size.y; i += 64u) {
        let offset = vec2f(vec2u(i % tile_size.x, i / tile_size.x)) - 2.0;
        var colour = vec3f(0);
        if frame.enabled != 0u {
            colour = extract(origin + offset / vec2f(source_size));
        }
        highlights[i] = colour;
    }
    workgroupBarrier();
    if any(id.xy >= size) {
        return;
    }
    let centre = local.xy * stride + vec2u(2);
    let p = centre.y * tile_size.x + centre.x;
    let row = tile_size.x;
    var colour = highlights[p] * 0.125;
    colour += (highlights[p - 2u * row - 2u] + highlights[p - 2u * row + 2u]
        + highlights[p + 2u * row - 2u] + highlights[p + 2u * row + 2u]) * 0.03125;
    colour += (highlights[p - 2u * row] + highlights[p - 2u]
        + highlights[p + 2u] + highlights[p + 2u * row]) * 0.0625;
    colour += (highlights[p - row - 1u] + highlights[p - row + 1u]
        + highlights[p + row - 1u] + highlights[p + row + 1u]) * 0.125;
    textureStore(output, id.xy, vec4f(colour, 1));
}

fn reconstruct(uv: vec2f) -> vec3f {
    let size = vec2f(textureDimensions(low));
    let position = uv * size - 0.5;
    let fraction = fract(position);
    let lower_weight = 0.75 - 0.5 * fraction;
    let upper_weight = 1.0 - lower_weight;
    let lower = (floor(position) - 0.5 + (0.5 - 0.25 * fraction) / lower_weight) / size;
    let upper = (floor(position) + 1.5 + 0.25 * fraction / upper_weight) / size;
    return textureSampleLevel(low, linear_sampler, lower, 0.0).rgb
            * lower_weight.x * lower_weight.y
        + textureSampleLevel(low, linear_sampler, vec2f(upper.x, lower.y), 0.0).rgb
            * upper_weight.x * lower_weight.y
        + textureSampleLevel(low, linear_sampler, vec2f(lower.x, upper.y), 0.0).rgb
            * lower_weight.x * upper_weight.y
        + textureSampleLevel(low, linear_sampler, upper, 0.0).rgb
            * upper_weight.x * upper_weight.y;
}

@compute @workgroup_size(8, 8)
fn upsample(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(output);
    if any(id.xy >= size) {
        return;
    }
    let uv = (vec2f(id.xy) + 0.5) / vec2f(size);
    var colour = textureSampleLevel(high, linear_sampler, uv, 0.0).rgb * step.weight;
    if step.terminal == 0u {
        colour += reconstruct(uv);
    }
    textureStore(output, id.xy, vec4f(colour, 1));
}
