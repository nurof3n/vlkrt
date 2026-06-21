// ================================================================== //
// miss.slang — radiance miss and shadow miss shaders
// ================================================================== //
import common;

// Push constants: hot scene parameters (16B)
[[vk::push_constant]] PushConstants pc;

// Radiance miss: set background colour × path throughput
[shader("miss")]
void MissMain([[vk::location(0)]] inout RayPayload payload) {
    payload.worldPosition = float3(0.0f);
    payload.hitDistance   = 0.0f;
    payload.color         = ComputeBackground(WorldRayDirection(), payload.recursionDepth == 0u).xyz * payload.throughput;
    if (payload.recursionDepth == 1u) {
        payload.firstBounceHitDist = 1e6f;
    }
}

// Shadow miss: ray reached the light without hitting anything
[shader("miss")]
void ShadowMissMain([[vk::location(1)]] inout ShadowRayPayload payload) {
    payload.hit = false;
}
