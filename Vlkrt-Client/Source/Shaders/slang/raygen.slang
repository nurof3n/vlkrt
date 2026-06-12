// ================================================================== //
// raygen.slang — path-tracing raygen with stratified sampling
// ================================================================== //
import common;
import path_tracer;

[shader("raygeneration")]
void RayGenMain() {
    float3 finalColor = float3(0.0f);
    uint   numSamples = g_scene.pathSqrtSamplesPerPixel * g_scene.pathSqrtSamplesPerPixel;

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
            payload.rngState       = seed;
            payload.recursionDepth = 0u;
            payload.inside         = 0u;
            payload.bsdfPdf        = 1.0f;

            RayDesc rayDesc;
            rayDesc.Origin    = ray.origin;
            rayDesc.Direction = ray.direction;
            rayDesc.TMin      = 0.0001f;
            rayDesc.TMax      = 10000.0f;

            TraceRay(g_tlas, RAY_FLAG_NONE, 0xFF, 0, 2, 0, rayDesc, payload);
            payload.color = RadianceClamp(payload.color);

            const float gammaPow = 1.0f / 2.2f;
            finalColor += pow(ACES(payload.color.xyz), float3(gammaPow));
        }
    }

    g_outputImage[int2(DispatchRaysIndex().xy)] = float4(finalColor / float(numSamples), 1.0f);
}
