import common;

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

    if (debugMode == 0u) // FinalImage
    {
        float viewZ = g_guideViewZ[pixelCoord].x;
        if (viewZ > 99999.0f)
            return;

        float3 albedo = g_guideViewZ[pixelCoord].yzw;
        float3 nrdDiff = NRD_YCoCgToLinear(g_outDiffRadianceHitDist[pixelCoord].xyz) * albedo;
        float3 nrdSpec = NRD_YCoCgToLinear(g_outSpecRadianceHitDist[pixelCoord].xyz);
        float3 denoised = max(nrdDiff + nrdSpec, 0.0f);

        // DIAGNOSTIC: if NRD output is zero, fall back to the raw guide input so we can
        // tell whether the black screen is from the compose shader or NRD itself.
        float nrdSum = dot(denoised, float3(1.0f, 1.0f, 1.0f));
        if (nrdSum < 1e-6f) {
            float3 inDiff = NRD_YCoCgToLinear(g_guideDiffRadianceHitDist[pixelCoord].xyz) * albedo;
            float3 inSpec = NRD_YCoCgToLinear(g_guideSpecRadianceHitDist[pixelCoord].xyz);
            denoised = max(inDiff + inSpec, 0.0f);
        }

        const float gammaPow = 1.0f / 2.2f;
        g_outputImage[pixelCoord] = float4(pow(ACES(denoised), float3(gammaPow)), 1.0f);
    }
    else if (debugMode == 1u) // NormalRoughness
    {
        float4 packed = g_guideNormalRoughness[pixelCoord];
        float3 normal;
        float roughness;
        NRD_DecodeNormalRoughness101010(packed.xyz, normal, roughness);
        g_outputImage[pixelCoord] = float4(normal * 0.5f + 0.5f, 1.0f);
    }
    else if (debugMode == 2u) // ViewZ
    {
        float depth = g_guideViewZ[pixelCoord].x;
        float debugDepth = EncodeDebugViewDepth(depth);
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
        float3 albedo = g_guideViewZ[pixelCoord].yzw;
        float3 diff = NRD_YCoCgToLinear(g_guideDiffRadianceHitDist[pixelCoord].xyz) * albedo;
        g_outputImage[pixelCoord] = float4(diff, 1.0f);
    }
    else if (debugMode == 5u) // SpecRadianceHitDist
    {
        float3 spec = NRD_YCoCgToLinear(g_guideSpecRadianceHitDist[pixelCoord].xyz);
        g_outputImage[pixelCoord] = float4(spec, 1.0f);
    }
}

