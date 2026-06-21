// ================================================================== //
// raygen_temporal.slang — temporal-accumulation raygen (one sample/frame)
// ================================================================== //
import common;
import path_tracer;

// Push constants: hot scene parameters (16B)
[[vk::push_constant]] PushConstants pc;

[shader("raygeneration")]
void RayGenTemporalMain() {
    bool fsrEnabled = (g_scene.fsrEnabled > 0.5f);
    uint seed = HashSeed(DispatchRaysIndex().xy, g_scene.elapsedTicks);

    uint   numSamples = g_scene.pathSqrtSamplesPerPixel * g_scene.pathSqrtSamplesPerPixel;
    float2 offset;

    if (numSamples == 1u) {
        offset = float2(0.5f, 0.5f);
    } else {
        // Cycle inside the stratified SPP grid; without modulo, offsets drift out of tile.
        uint fiCycle = (g_scene.pathFrameCacheIndex - 1u) % numSamples;
        uint row = fiCycle % g_scene.pathSqrtSamplesPerPixel;
        uint col = fiCycle / g_scene.pathSqrtSamplesPerPixel;
        offset = float2((float(row) + 0.5f) / float(g_scene.pathSqrtSamplesPerPixel),
                        (float(col) + 0.5f) / float(g_scene.pathSqrtSamplesPerPixel));
    }

    if (g_scene.applyJitter != 0u)
        offset += (float2(Random(seed), Random(seed)) - 0.5f)
                  / float(g_scene.pathSqrtSamplesPerPixel);

    Ray ray = GenerateCameraRay(DispatchRaysIndex().xy, offset);

    RayPayload payload;
    payload.color          = float3(0.0f);
    payload.worldPosition  = float3(0.0f);
    payload.hitDistance    = 0.0f;
    payload.throughput     = float3(1.0f);
    payload.absorption     = float3(0.0f);
    payload.rngState       = seed;
    payload.recursionDepth = 0u;
    payload.inside         = 0u;
    payload.bsdfPdf        = 1.0f;
    payload.firstBounceHitDist = 0.0f;
    payload.diffuse        = float3(0.0f);
    payload.specular       = float3(0.0f);
    payload.diffuseHitDist = 0.0f;
    payload.specularHitDist = 0.0f;
    payload.emission       = float3(0.0f);
    payload.firstHitWorldPosPrev = float3(0.0f);
    payload.firstHitPackedNormalData = PackFirstHitNormalData(float3(0.0f, 1.0f, 0.0f), 0.5f, 0.0f);
    payload.firstHitPackedMaterialData = PackFirstHitMaterialData(float3(0.0f), 0.0f, 0.0f);

    RayDesc rayDesc;
    rayDesc.Origin    = ray.origin;
    rayDesc.Direction = ray.direction;
    rayDesc.TMin      = 0.0001f;
    rayDesc.TMax      = 10000.0f;

    TraceRay(g_tlas, RAY_FLAG_NONE, 0xFF, 0, 2, 0, rayDesc, payload);
    payload.color = RadianceClamp(payload.color);

    const float gammaPow  = 1.0f / 2.2f;
    float3      frameColor = pow(ACES(payload.color), float3(gammaPow));

    // Blend with accumulated history
    float lerpFactor = float(g_scene.pathFrameCacheIndex - 1u)
                     / float(g_scene.pathFrameCacheIndex);
    float4 history = g_accumImage[int2(DispatchRaysIndex().xy)];
    float3 result  = lerp(frameColor, history.xyz, lerpFactor);

    g_accumImage [int2(DispatchRaysIndex().xy)] = float4(result, 1.0f);
    g_outputImage[int2(DispatchRaysIndex().xy)] = float4(result, 1.0f);

    int2 pixelCoord = int2(DispatchRaysIndex().xy);
    if (payload.hitDistance > 0.0f) {
        float3 n;
        float roughness;
        float metallic;
        float3 albedo;
        float specularWeight;
        float materialID;
        UnpackFirstHitNormalData(payload.firstHitPackedNormalData, n, roughness, metallic);
        UnpackFirstHitMaterialData(payload.firstHitPackedMaterialData, albedo, specularWeight, materialID);
        float depth = ComputeViewDepth(payload.worldPosition);
        float2 motion = ComputeMotionVector(payload.worldPosition, payload.firstHitWorldPosPrev);

        float diffHitDist = (payload.diffuseHitDist > 0.0f) ? max(payload.diffuseHitDist, 0.001f) : 0.0f;
        float specHitDist = (payload.specularHitDist > 0.0f) ? max(payload.specularHitDist, 0.001f) : 0.0f;

        Ray rayCenter = GenerateCameraRay(pixelCoord, float2(0.5f, 0.5f));
        float3 V = -rayCenter.direction;
        float3 Rf0 = lerp(float3(0.04f), albedo, metallic);
        float3 diffFactor, specFactor;
        NRD_MaterialFactors(n, V, albedo, Rf0, roughness, diffFactor, specFactor);

        float3 demodulatedDiffuse = clamp(payload.diffuse / diffFactor, 0.0f, 20.0f);
        float3 demodulatedSpecular = clamp(payload.specular / specFactor, 0.0f, 250.0f);

        float4 clipPos = mul(g_scene.worldToClip, float4(payload.worldPosition, 1.0f));
        float ndcDepth = (clipPos.w > 0.0f) ? (clipPos.z / clipPos.w) : 1.0f;

        g_guideNormalRoughness[pixelCoord] = PackNormalAndRoughness(n, roughness, materialID);
        g_guideViewZ[pixelCoord] = float4(depth, albedo);  // RGBA16F
        g_guideMotionVectors[pixelCoord] = float4(motion, metallic, 1.0f);  // RGBA16F
        g_guideDiffRadianceHitDist[pixelCoord]
            = RELAX_FrontEnd_PackRadianceAndHitDist(demodulatedDiffuse, diffHitDist, false);
        g_guideSpecRadianceHitDist[pixelCoord]
            = RELAX_FrontEnd_PackRadianceAndHitDist(demodulatedSpecular, specHitDist, false);
        g_guideEmission[pixelCoord] = float4(payload.emission, 1.0f);  // RGBA16F
        if (fsrEnabled)
            g_guideDepth[pixelCoord] = ndcDepth;  // R32F: scalar
    } else {
        g_guideNormalRoughness[pixelCoord] = PackNormalAndRoughness(float3(0.0f, 1.0f, 0.0f), 1.0f, 0.0f);
        g_guideViewZ[pixelCoord] = float4(65504.0f, float3(0.0f));  // RGBA16F max sentinel
        g_guideMotionVectors[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);  // RGBA16F
        g_guideDiffRadianceHitDist[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_guideSpecRadianceHitDist[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_guideEmission[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        if (fsrEnabled)
            g_guideDepth[pixelCoord] = 1.0f;  // R32F: scalar
    }
}
