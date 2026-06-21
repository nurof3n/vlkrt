// ================================================================== //
// raygen.slang — path-tracing raygen with stratified sampling
// ================================================================== //
import common;
import path_tracer;

// Push constants: hot scene parameters (16B)
[[vk::push_constant]] PushConstants pc;

[shader("raygeneration")]
void RayGenMain() {
    bool fsrEnabled = (g_scene.fsrEnabled > 0.5f);
    float3 finalColor = float3(0.0f);
    uint   numSamples = g_scene.pathSqrtSamplesPerPixel * g_scene.pathSqrtSamplesPerPixel;
    float3 guideNormal = float3(0.0f);
    float  guideRoughness = 0.0f;
    float  guideDepth = 0.0f;
    float  guideNdcDepth = 0.0f;
    float3 guideDiffuse = float3(0.0f);
    float3 guideSpecular = float3(0.0f);
    float2 guideMotion = float2(0.0f);
    float  guideDiffuseHitDist = 0.0f;
    float  guideDiffuseHitCount = 0.0f;
    float  guideSpecularHitDist = 1e9f;
    float  guideSpecularHitCount = 0.0f;
    float3 guideAlbedo = float3(0.0f);
    float  guideMaterialClass = 0.0f;
    float  guideHitCount = 0.0f;
    float3 guideEmission = float3(0.0f);
    float  guideMetallic = 0.0f;

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

            if (payload.hitDistance > 0.0f) {
                float3 n;
                float roughness;
                float metallic;
                float3 albedo;
                float specularWeight;
                float materialClass;
                UnpackFirstHitNormalData(payload.firstHitPackedNormalData, n, roughness, metallic);
                UnpackFirstHitMaterialData(payload.firstHitPackedMaterialData, albedo, specularWeight, materialClass);
                float depth = ComputeViewDepth(payload.worldPosition);

                guideNormal += n;
                guideRoughness += roughness;
                guideDepth += depth;
                guideAlbedo += albedo;
                guideDiffuse += payload.diffuse;
                guideSpecular += payload.specular;
                guideMotion += ComputeMotionVector(payload.worldPosition, payload.firstHitWorldPosPrev);
                guideMetallic += clamp(metallic, 0.0f, 1.0f);
                guideEmission += payload.emission;
                float4 clipPos = mul(g_scene.worldToClip, float4(payload.worldPosition, 1.0f));
                float ndcDepth = (clipPos.w > 0.0f) ? (clipPos.z / clipPos.w) : 1.0f;
                guideNdcDepth += ndcDepth;
                
                if (payload.diffuseHitDist > 0.0f) {
                    guideDiffuseHitDist += payload.diffuseHitDist;
                    guideDiffuseHitCount += 1.0f;
                }
                if (payload.specularHitDist > 0.0f) {
                    guideSpecularHitDist = min(guideSpecularHitDist, payload.specularHitDist);
                    guideSpecularHitCount += 1.0f;
                }

                guideMaterialClass += materialClass;
                guideHitCount += 1.0f;
            } else {
                guideNdcDepth += 1.0f;
            }

            const float gammaPow = 1.0f / 2.2f;
            finalColor += pow(ACES(payload.color), float3(gammaPow));
        }
    }

    int2 pixelCoord = int2(DispatchRaysIndex().xy);
    g_outputImage[pixelCoord] = float4(finalColor / float(numSamples), 1.0f);

    if (guideHitCount > 0.0f) {
        float invHitCount = 1.0f / guideHitCount;
        float3 avgNormal = normalize(guideNormal * invHitCount);
        float avgRoughness = guideRoughness * invHitCount;
        float avgDepth = guideDepth * invHitCount;
        
        float avgDiffuseHitDist = (guideDiffuseHitCount > 0.0f) ? max(guideDiffuseHitDist / guideDiffuseHitCount, 0.001f) : 0.0f;
        float avgSpecularHitDist = (guideSpecularHitCount > 0.0f) ? max(guideSpecularHitDist, 0.001f) : 0.0f;

        float2 avgMotion = guideMotion * invHitCount;
        float3 avgAlbedo = guideAlbedo * invHitCount;
        float avgMaterialClass = guideMaterialClass * invHitCount;
        float avgMetallic = guideMetallic * invHitCount;
        float3 avgEmission = guideEmission * invHitCount;

        float3 avgDiffuse = guideDiffuse * invHitCount;
        float3 avgSpecular = guideSpecular * invHitCount;

        Ray rayCenter = GenerateCameraRay(pixelCoord, float2(0.5f, 0.5f));
        float3 V = -rayCenter.direction;
        float3 Rf0 = lerp(float3(0.04f), avgAlbedo, avgMetallic);
        float3 diffFactor, specFactor;
        NRD_MaterialFactors(avgNormal, V, avgAlbedo, Rf0, avgRoughness, diffFactor, specFactor);

        float3 demodulatedDiffuse = clamp(avgDiffuse / diffFactor, 0.0f, 20.0f);
        float3 demodulatedSpecular = clamp(avgSpecular / specFactor, 0.0f, 250.0f);
        float materialID = clamp(floor(avgMaterialClass + 0.5f), 0.0f, 3.0f);

        g_guideNormalRoughness[pixelCoord] = PackNormalAndRoughness(avgNormal, avgRoughness, materialID);
        g_guideViewZ[pixelCoord] = float4(avgDepth, avgAlbedo);  // RGBA16F
        g_guideMotionVectors[pixelCoord] = float4(avgMotion, avgMetallic, 1.0f);  // RGBA16F
        g_guideDiffRadianceHitDist[pixelCoord]
            = RELAX_FrontEnd_PackRadianceAndHitDist(demodulatedDiffuse, avgDiffuseHitDist, false);
        g_guideSpecRadianceHitDist[pixelCoord]
            = RELAX_FrontEnd_PackRadianceAndHitDist(demodulatedSpecular, avgSpecularHitDist, false);
        g_guideEmission[pixelCoord] = float4(avgEmission, 1.0f);  // RGBA16F
        if (fsrEnabled)
            g_guideDepth[pixelCoord] = guideNdcDepth / float(numSamples);  // R32F: scalar
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
