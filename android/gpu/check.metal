// Authored diagnostic shaders. These are not guest macOS shaders.
#include <metal_stdlib>
using namespace metal;

kernel void lab_scale(device const uint *input [[buffer(0)]],
                      device uint *output [[buffer(1)]],
                      uint index [[thread_position_in_grid]]) {
    output[index] = 3u * input[index] + 7u;
}

struct LabVertex {
    float4 position [[position]];
};
vertex LabVertex lab_vertex(device const float2 *positions [[buffer(0)]],
                            uint index [[vertex_id]]) {
    return {float4(positions[index], 0.0f, 1.0f)};
}
fragment float4 lab_fragment() {
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}

struct LabOrderedVertex {
    float4 position [[position]];
    float value [[flat]];
};
vertex LabOrderedVertex lab_order_vertex(uint vid [[vertex_id]],
                                         uint instance [[instance_id]]) {
    const float2 p[] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    return {float4(p[vid], 0, 1), float(instance + 1)};
}
// Indexed list: nonzero index-buffer offset and base vertex are exercised by the harness.
vertex LabOrderedVertex lab_order_list_vertex(uint vid [[vertex_id]],
                                              uint instance [[instance_id]]) {
    const float2 p[] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    return {float4(p[vid % 3], 0, 1), float(vid / 3 + instance + 1)};
}
fragment float4 lab_fetch(LabOrderedVertex in [[stage_in]], float4 dst [[color(0)]]) {
    return dst * 0.5f + float4(in.value, 1, 0.5f, 0);
}
fragment float4 lab_rog_fetch(LabOrderedVertex in [[stage_in]],
                             float4 dst [[color(0), raster_order_group(0)]]) {
    return dst * 0.5f + float4(in.value, 1, 0.5f, 0);
}
struct LabOrderedOutput {
    float4 color [[color(0), raster_order_group(0)]];
};
fragment LabOrderedOutput lab_rog_fetch_output(LabOrderedVertex in [[stage_in]],
                                               float4 dst [[color(0)]]) {
    return {dst * 0.5f + float4(in.value, 1, 0.5f, 0)};
}
fragment float4 lab_rog_buffer(device uint *value [[buffer(0), raster_order_group(0)]]) {
    return float4(float(value[0]));
}

// Authored ordinary storage-image access for primitive-serialization experiments.
fragment float4 lab_texture_serial(LabOrderedVertex in [[stage_in]],
                                    texture2d<uint, access::read_write> values [[texture(0)]]) {
    uint2 pixel = uint2(in.position.xy);
    uint next = values.read(pixel).x * 3u + uint(in.value);
    values.write(uint4(next, 0, 0, 0), pixel);
    return float4(0, 0, 0, 1);
}
fragment float4 lab_texture_serial_half(LabOrderedVertex in [[stage_in]],
                                         texture2d<half, access::read_write> values [[texture(0)]]) {
    uint2 pixel = uint2(in.position.xy);
    half next = values.read(pixel).x * half(0.5f) + half(in.value);
    values.write(half4(next, 0, 0, 0), pixel);
    return float4(0, 0, 0, 1);
}
fragment float4 lab_texture_native_rog(LabOrderedVertex in [[stage_in]],
                    texture2d<uint, access::read_write> values [[texture(0), raster_order_group(0)]]) {
    uint2 pixel = uint2(in.position.xy);
    uint next = values.read(pixel).x * 3u + uint(in.value);
    values.write(uint4(next, 0, 0, 0), pixel);
    return float4(0, 0, 0, 1);
}
fragment float4 lab_texture_native_rog_half(LabOrderedVertex in [[stage_in]],
                    texture2d<half, access::read_write> values [[texture(0), raster_order_group(0)]]) {
    uint2 pixel = uint2(in.position.xy);
    half next = values.read(pixel).x * half(0.5f) + half(in.value);
    values.write(half4(next, 0, 0, 0), pixel);
    return float4(0, 0, 0, 1);
}

// Authored dynamic private array: force TLS scratch for command-pool retention checks.
kernel void lab_tls(device const uint *input [[buffer(0)]],
                    device uint *output [[buffer(1)]],
                    uint index [[thread_position_in_grid]]) {
    uint values[256];
    uint x = input[index];
    for (uint j = 0; j < 256; ++j) values[j] = x + j * 17u;
    output[index] = (x * 3u + 7u) ^ values[index & 255u];
}
