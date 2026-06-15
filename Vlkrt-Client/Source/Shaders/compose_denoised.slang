import common;

Ray GenerateCameraRayCompute(uint2 index, float2 offset, float2 dimensions) {
    float2 xy  = float2(index) + offset;
    float2 scr = xy / dimensions * 2.0f - 1.0f;
    scr.y = -scr.y;

    float4 world = mul(g_scene.projectionToWorld, float4(scr, 0.0f, 1.0f));
    world.xyz /= world.w;

    Ray ray;
    ray.origin    = g_scene.cameraPosition.xyz;
    ray.direction = normalize(world.xyz - ray.origin);
    return ray;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComposeDenoisedMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;
    uint width = 0;
    uint height = 0;
    g_outputImage.GetDimensions(width, height);
    if (pixelCoord.x >= width || pixelCoord.y >= height)
        return;

    uint debugMode = g_scene.nrdDebugViewMode;
    float viewZ = g_guideViewZ[pixelCoord].x;
    // RGBA16F can't represent 1e6 miss-sentinel exactly; treat near-max half values as miss.
    if (viewZ >= 65000.0f)
        return;

    float3 albedo = g_guideViewZ[pixelCoord].yzw;
    float4 packed = g_guideNormalRoughness[pixelCoord];
    float3 normal;
    float roughness;
    NRD_DecodeNormalRoughness101010(packed.xyz, normal, roughness);
    float metallic = g_guideMotionVectors[pixelCoord].z;

    Ray rayCenter = GenerateCameraRayCompute(pixelCoord, float2(0.5f, 0.5f), float2(width, height));
    float3 V = -rayCenter.direction;
    float3 Rf0 = lerp(float3(0.04f), albedo, metallic);
    float3 diffFactor, specFactor;
    NRD_MaterialFactors(normal, V, albedo, Rf0, roughness, diffFactor, specFactor);

    float3 nrdDiff = RELAX_BackEnd_UnpackRadiance(g_outDiffRadianceHitDist[pixelCoord]).xyz * diffFactor;
    float3 nrdSpec = RELAX_BackEnd_UnpackRadiance(g_outSpecRadianceHitDist[pixelCoord]).xyz * specFactor;
    float3 emission = g_guideEmission[pixelCoord].xyz;
    float3 denoised = max(nrdDiff + nrdSpec + emission, 0.0f);

    // Fallback to raw guides if NRD outputs are currently unavailable.
    float nrdSum = dot(denoised, float3(1.0f, 1.0f, 1.0f));
    if (nrdSum < 1e-6f) {
        float3 inDiff = RELAX_BackEnd_UnpackRadiance(g_guideDiffRadianceHitDist[pixelCoord]).xyz * diffFactor;
        float3 inSpec = RELAX_BackEnd_UnpackRadiance(g_guideSpecRadianceHitDist[pixelCoord]).xyz * specFactor;
        denoised = max(inDiff + inSpec + emission, 0.0f);
    }

    float3 rawDiff = RELAX_BackEnd_UnpackRadiance(g_guideDiffRadianceHitDist[pixelCoord]).xyz * diffFactor;
    float3 rawSpec = RELAX_BackEnd_UnpackRadiance(g_guideSpecRadianceHitDist[pixelCoord]).xyz * specFactor;
    float3 rawNoisy = max(rawDiff + rawSpec + emission, 0.0f);

    const float gammaPow = 1.0f / 2.2f;
    float3 denoisedDisplay = pow(ACES(denoised), float3(gammaPow));
    float3 rawDisplay      = pow(ACES(rawNoisy), float3(gammaPow));

    // Accumulate luma delta statistics for CPU-side metrics.
    float rawLuma       = GetLuminance(rawDisplay);
    float denoisedLuma  = GetLuminance(denoisedDisplay);
    float lumaDiff      = abs(rawLuma - denoisedLuma);
    float lumaDiffSq    = lumaDiff * lumaDiff;
    uint sumSqIncrement = (uint) (saturate(lumaDiffSq) * 256.0f + 0.5f);
    uint sumAbsIncrement = (uint) (saturate(lumaDiff) * 256.0f + 0.5f);
    InterlockedAdd(g_qualityMetrics[0], sumSqIncrement);
    InterlockedAdd(g_qualityMetrics[1], sumAbsIncrement);
    InterlockedAdd(g_qualityMetrics[2], 1u);

    if (debugMode == 0u) // FinalImage
    {
        g_outputImage[pixelCoord] = float4(denoisedDisplay, 1.0f);
    }
    else if (debugMode == 1u) // NormalRoughness
    {
        g_outputImage[pixelCoord] = float4(normal * 0.5f + 0.5f, 1.0f);
    }
    else if (debugMode == 2u) // ViewZ
    {
        float debugDepth = EncodeDebugViewDepth(viewZ);
        g_outputImage[pixelCoord] = float4(debugDepth, debugDepth, debugDepth, 1.0f);
    }
    else if (debugMode == 3u) // MotionVectors
    {
        float2 motion = g_guideMotionVectors[pixelCoord].xy;
        float2 debugMotion = EncodeDebugMotionVector(motion);
        g_outputImage[pixelCoord] = float4(debugMotion, 0.0f, 1.0f);
    }
    else if (debugMode == 4u) // DiffRadianceHitDist
    {
        float3 diff = RELAX_BackEnd_UnpackRadiance(g_guideDiffRadianceHitDist[pixelCoord]).xyz * diffFactor;
        g_outputImage[pixelCoord] = float4(diff, 1.0f);
    }
    else if (debugMode == 5u) // SpecRadianceHitDist
    {
        float3 spec = RELAX_BackEnd_UnpackRadiance(g_guideSpecRadianceHitDist[pixelCoord]).xyz * specFactor;
        g_outputImage[pixelCoord] = float4(spec, 1.0f);
    }
    else if (debugMode == 6u) // RawNoDenoise
    {
        g_outputImage[pixelCoord] = float4(rawDisplay, 1.0f);
    }
    else if (debugMode == 7u) // SplitDenoisedVsRaw
    {
        bool leftSide = pixelCoord.x < (width / 2u);
        float3 color  = leftSide ? denoisedDisplay : rawDisplay;

        // 1px separator for easier visual comparison
        if (abs(int(pixelCoord.x) - int(width / 2u)) <= 1) color = float3(1.0f, 1.0f, 1.0f);

        g_outputImage[pixelCoord] = float4(color, 1.0f);
    }
}
