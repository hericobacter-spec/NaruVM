#include <metal_stdlib>
using namespace metal;
kernel void runtime_sample(texture2d<float> image [[texture(0)]],
                           device float4 *out [[buffer(0)]],
                           uint i [[thread_position_in_grid]]) {
    constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::linear);
    float2 uv = float2(float(i % 8) + 0.5f, 0.5f) / float2(image.get_width(), image.get_height());
    out[i] = image.sample(s, uv, level(0));
}
kernel void runtime_dynamic(texture2d<float> image [[texture(0)]], sampler s [[sampler(0)]],
                            device float4 *out [[buffer(0)]], uint i [[thread_position_in_grid]]) {
    out[i] = image.sample(s, float2(0.5), level(0));
}
struct SampleArgs { texture2d<float> image [[id(0)]]; sampler s [[id(1)]]; };
kernel void runtime_indirect(constant SampleArgs &args [[buffer(0)]],
                             device float4 *out [[buffer(1)]], uint i [[thread_position_in_grid]]) {
    out[i] = args.image.sample(args.s, float2(0.5), level(0));
}
