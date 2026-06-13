// ================================================================== //
// raygen.slang — path-tracing raygen with stratified sampling
// ================================================================== //
import common;
import path_tracer;

[shader("raygeneration")]
void RayGenMain() {
    float3 finalColor = float3(0.0f);
    uint   numSamples = g_scene.pathSqrtSamplesPerPixel * g_scene.pathSqrtSamplesPerPixel;
    float3 guideNormal = float3(0.0f);
    float  guideRoughness = 0.0f;
    float  guideDepth = 0.0f;
    float3 guideDiffuse = float3(0.0f);
    float3 guideSpecular = float3(0.0f);
    float2 guideMotion = float2(0.0f);
    float  guideHitDist = 0.0f;
    float  guideHitCount = 0.0f;
    float3 guideAlbedo = float3(0.0f);
    float  guideMaterialClass = 0.0f;

    for (uint i = 0u; i < g_scene.pathSqrtSamplesPerPixel; i++) {
        for (uint j = 0u; j < g_scene.pathSqrtSamplesPerPixel; j++) {
            uint seed = HashSeed(DispatchRaysIndex().xy,
                                 g_scene.elapsedTicks + 67u * i * g_scene.pathSqrtSamplesPerPixel + j);

            float2 offset = (numSamples == 1u) ? float2(0.5f, 0.5f) :
                float2((float(i) + 0.5f) / float(g_scene.pathSqrtSamplesPerPixel),
                       (float(j) + 0.5f) / float(g_scene.pathSqrtSamplesPerPixel));

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

            RayDesc rayDesc;
            rayDesc.Origin    = ray.origin;
            rayDesc.Direction = ray.direction;
            rayDesc.TMin      = 0.0001f;
            rayDesc.TMax      = 10000.0f;

            TraceRay(g_tlas, RAY_FLAG_NONE, 0xFF, 0, 2, 0, rayDesc, payload);
            payload.color = RadianceClamp(payload.color);

            if (payload.worldPosition.w > 0.0f) {
                float3 n = payload.firstHitNormalRoughness.xyz;
                if (dot(n, n) > 1e-6f) {
                    n = normalize(n);
                } else {
                    n = float3(0.0f, 1.0f, 0.0f);
                }

                float roughness = clamp(payload.firstHitNormalRoughness.w, 0.0f, 1.0f);
                float depth = ComputeViewDepth(payload.worldPosition.xyz);
                float hitDist = max(payload.firstBounceHitDist, 0.001f);

                float3 albedo = payload.firstHitMaterialParams.xyz;
                float specularWeight = saturate(payload.firstHitMaterialParams.w);
                float diffuseWeight = saturate(1.0f - specularWeight);
                float materialClass = payload.firstHitWorldPosPrev.w;

                guideNormal += n;
                guideRoughness += roughness;
                guideDepth += depth;
                guideAlbedo += albedo;
                guideDiffuse += payload.color.xyz * diffuseWeight;
                guideSpecular += payload.color.xyz * specularWeight;
                guideMotion += ComputeMotionVector(payload.worldPosition.xyz, payload.firstHitWorldPosPrev.xyz);
                guideHitDist += hitDist;
                guideMaterialClass += materialClass;
                guideHitCount += 1.0f;
            }

            const float gammaPow = 1.0f / 2.2f;
            finalColor += pow(ACES(payload.color.xyz), float3(gammaPow));
        }
    }

    int2 pixelCoord = int2(DispatchRaysIndex().xy);
    g_outputImage[pixelCoord] = float4(finalColor / float(numSamples), 1.0f);

    if (guideHitCount > 0.0f) {
        float invHitCount = 1.0f / guideHitCount;
        float3 avgNormal = normalize(guideNormal * invHitCount);
        float avgRoughness = guideRoughness * invHitCount;
        float avgDepth = guideDepth * invHitCount;
        float avgHitDist = guideHitDist * invHitCount;
        float2 avgMotion = guideMotion * invHitCount;
        float3 avgAlbedo = guideAlbedo * invHitCount;
        float avgMaterialClass = guideMaterialClass * invHitCount;

        float normHitDistDiffuse = REBLUR_FrontEnd_GetNormHitDist(avgHitDist, avgDepth, float3(3.0f, 0.1f, 20.0f), 1.0f);
        float normHitDistSpecular = REBLUR_FrontEnd_GetNormHitDist(avgHitDist, avgDepth, float3(3.0f, 0.1f, 20.0f), avgRoughness);

        float3 avgDiffuse = guideDiffuse * invHitCount;
        float3 avgSpecular = guideSpecular * invHitCount;
        float3 demodulatedDiffuse = avgDiffuse / max(avgAlbedo, 0.01f);
        float materialID = clamp(floor(avgMaterialClass + 0.5f), 0.0f, 3.0f);

        g_guideNormalRoughness[pixelCoord] = PackNormalAndRoughness(avgNormal, avgRoughness, materialID);
        g_guideViewZ[pixelCoord] = float4(avgDepth, avgAlbedo);
        g_guideMotionVectors[pixelCoord] = float4(avgMotion, 0.0f, 1.0f);
        g_guideDiffRadianceHitDist[pixelCoord] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(demodulatedDiffuse, normHitDistDiffuse);
        g_guideSpecRadianceHitDist[pixelCoord] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(avgSpecular, normHitDistSpecular);
    } else {
        g_guideNormalRoughness[pixelCoord] = PackNormalAndRoughness(float3(0.0f, 1.0f, 0.0f), 1.0f, 0.0f);
        g_guideViewZ[pixelCoord] = float4(1e6f, float3(0.0f));
        g_guideMotionVectors[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_guideDiffRadianceHitDist[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_guideSpecRadianceHitDist[pixelCoord] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

}

