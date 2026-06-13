// ================================================================== //
// miss.slang — radiance miss and shadow miss shaders
// ================================================================== //
import common;

// Radiance miss: set background colour × path throughput
[shader("miss")]
void MissMain([[vk::location(0)]] inout RayPayload payload) {
    payload.worldPosition = float4(0.0f);
    payload.color         = ComputeBackground(WorldRayDirection(), payload.recursionDepth == 0u) * payload.throughput;
    if (payload.recursionDepth == 1u) {
        payload.firstBounceHitDist = 1e6f;
    }
}

// Shadow miss: ray reached the light without hitting anything → not occluded
[shader("miss")]
void ShadowMissMain([[vk::location(1)]] inout ShadowRayPayload payload) {
    payload.hit = false;
}
