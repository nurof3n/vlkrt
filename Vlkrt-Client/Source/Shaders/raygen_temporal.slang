// ================================================================== //
// raygen_temporal.slang — temporal-accumulation raygen (one sample/frame)
// ================================================================== //
import common;
import path_tracer;

[shader("raygeneration")]
void RayGenTemporalMain() {
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
    payload.color          = float4(0.0f);
    payload.worldPosition  = float4(0.0f);
    payload.throughput     = float4(1.0f);
    payload.absorption     = float4(0.0f);
    payload.firstHitNormalRoughness = float4(0.0f, 1.0f, 0.0f, 0.5f);
    payload.firstHitMaterialParams = float4(0.0f, 0.0f, 0.0f, 0.0f);
    payload.firstHitWorldPosPrev = float4(0.0f, 0.0f, 0.0f, 1.0f);
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
    payload.firstHitMetallic = 0.0f;

    RayDesc rayDesc;
    rayDesc.Origin    = ray.origin;
    rayDesc.Direction = ray.direction;
    rayDesc.TMin      = 0.0001f;
    rayDesc.TMax      = 10000.0f;

    TraceRay(g_tlas, RAY_FLAG_NONE, 0xFF, 0, 2, 0, rayDesc, payload);
    payload.color = RadianceClamp(payload.color);

    const float gammaPow  = 1.0f / 2.2f;
    float3      frameColor = pow(ACES(payload.color.xyz), float3(gammaPow));

    // Blend with accumulated history
    float lerpFactor = float(g_scene.pathFrameCacheIndex - 1u)
                     / float(g_scene.pathFrameCacheIndex);
    float4 history = g_accumImage[int2(DispatchRaysIndex().xy)];
    float3 result  = lerp(frameColor, history.xyz, lerpFactor);

    g_accumImage [int2(DispatchRaysIndex().xy)] = float4(result, 1.0f);
    g_outputImage[int2(DispatchRaysIndex().xy)] = float4(result, 1.0f);

    int2 pixelCoord = int2(DispatchRaysIndex().xy);
    if (payload.worldPosition.w > 0.0f) {
        float3 n = payload.firstHitNormalRoughness.xyz;
        if (dot(n, n) > 1e-6f) {
            n = normalize(n);
        } else {
            n = float3(0.0f, 1.0f, 0.0f);
        }

        float roughness = clamp(payload.firstHitNormalRoughness.w, 0.0f, 1.0f);
        float depth = ComputeViewDepth(payload.worldPosition.xyz);
        float2 motion = ComputeMotionVector(payload.worldPosition.xyz, payload.firstHitWorldPosPrev.xyz);

        float3 albedo = payload.firstHitMaterialParams.xyz;
        float materialID = clamp(floor(payload.firstHitWorldPosPrev.w + 0.5f), 0.0f, 3.0f);
        float metallic = clamp(payload.firstHitMetallic, 0.0f, 1.0f);

        float diffHitDist = (payload.diffuseHitDist > 0.0f) ? max(payload.diffuseHitDist, 0.001f) : 0.0f;
        float specHitDist = (payload.specularHitDist > 0.0f) ? max(payload.specularHitDist, 0.001f) : 0.0f;

        float normHitDistDiffuse = REBLUR_FrontEnd_GetNormHitDist(diffHitDist, depth, float3(3.0f, 0.1f, 20.0f), 1.0f);
        float normHitDistSpecular = REBLUR_FrontEnd_GetNormHitDist(specHitDist, depth, float3(3.0f, 0.1f, 20.0f), roughness);

        Ray rayCenter = GenerateCameraRay(pixelCoord, float2(0.5f, 0.5f));
        float3 V = -rayCenter.direction;
        float3 Rf0 = lerp(float3(0.04f), albedo, metallic);
        float3 diffFactor, specFactor;
        NRD_MaterialFactors(n, V, albedo, Rf0, roughness, diffFactor, specFactor);

        float3 demodulatedDiffuse = clamp(payload.diffuse / diffFactor, 0.0f, 20.0f);
        float3 demodulatedSpecular = clamp(payload.specular / specFactor, 0.0f, 250.0f);

        float4 clipPos = mul(g_scene.worldToClip, float4(payload.worldPosition.xyz, 1.0f));
        float ndcDepth = (clipPos.w > 0.0f) ? (clipPos.z / clipPos.w) : 1.0f;

        g_guideNormalRoughness[pixelCoord] = PackNormalAndRoughness(n, roughness, materialID);
        g_guideViewZ[pixelCoord] = float4(depth, albedo);
        g_guideMotionVectors[pixelCoord] = float4(motion, metallic, 1.0f);
        g_guideDiffRadianceHitDist[pixelCoord] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(demodulatedDiffuse, normHitDistDiffuse);
        g_guideSpecRadianceHitDist[pixelCoord] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(demodulatedSpecular, normHitDistSpecular);
        g_guideEmission[pixelCoord] = float4(payload.emission, 1.0f);
        g_guideDepth[pixelCoord] = float4(ndcDepth, 0.0f, 0.0f, 1.0f);
    } else {
        g_guideNormalRoughness[pixelCoord] = PackNormalAndRoughness(float3(0.0f, 1.0f, 0.0f), 1.0f, 0.0f);
        g_guideViewZ[pixelCoord] = float4(1e6f, float3(0.0f));
        g_guideMotionVectors[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_guideDiffRadianceHitDist[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_guideSpecRadianceHitDist[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_guideEmission[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_guideDepth[pixelCoord] = float4(1.0f, 0.0f, 0.0f, 1.0f);
    }
}
