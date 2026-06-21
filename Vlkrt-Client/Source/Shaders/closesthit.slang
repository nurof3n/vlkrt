// ================================================================== //
// closesthit.slang — triangle-mesh closest-hit shader
// ================================================================== //
import common;
import path_tracer;

[shader("closesthit")]
void ClosestHitMain([[vk::location(0)]] inout RayPayload payload,
                    BuiltInTriangleIntersectionAttributes attr) {
    float2 bary = attr.barycentrics;

    // Triangle indices
    uint3 tri;
    tri.x = g_idx[PrimitiveIndex() * 3u + 0u];
    tri.y = g_idx[PrimitiveIndex() * 3u + 1u];
    tri.z = g_idx[PrimitiveIndex() * 3u + 2u];

    // Per-primitive material
    uint           matIdx = g_matIdx[PrimitiveIndex()];
    GPUPBRMaterial mat    = g_materials[matIdx];

    // Interpolate normal
    float3 n0     = g_vtx[tri.x].normal;
    float3 n1     = g_vtx[tri.y].normal;
    float3 n2     = g_vtx[tri.z].normal;
    float3 normal = n0 + bary.x * (n1 - n0) + bary.y * (n2 - n0);
    // Transform to world space (rotation/scale submatrix of ObjectToWorld3x4)
    normal = normalize(mul(float3x3(ObjectToWorld3x4()), normal));

    float2 uv0 = g_vtx[tri.x].texCoord;
    float2 uv1 = g_vtx[tri.y].texCoord;
    float2 uv2 = g_vtx[tri.z].texCoord;
    float2 uv  = uv0 + bary.x * (uv1 - uv0) + bary.y * (uv2 - uv0);
    uv *= mat.tiling;

    if (mat.albedoTextureIndex >= 0) {
        mat.albedo = g_textures[NonUniformResourceIndex(mat.albedoTextureIndex)].SampleLevel(uv, 0.0f).rgb;
    }

    if (mat.metallicRoughnessTextureIndex >= 0) {
        float3 mr = g_textures[NonUniformResourceIndex(mat.metallicRoughnessTextureIndex)].SampleLevel(uv, 0.0f).rgb;
        mat.roughness = clamp(mat.roughness * mr.g, 0.001f, 1.0f);
        mat.metallic  = clamp(mat.metallic * mr.b, 0.0f, 1.0f);
    }

    if (mat.emissiveTextureIndex >= 0) {
        float3 em = g_textures[NonUniformResourceIndex(mat.emissiveTextureIndex)].SampleLevel(uv, 0.0f).rgb;
        mat.emission *= em;
    }

    if (mat.normalTextureIndex >= 0) {
        float3 p0 = g_vtx[tri.x].position;
        float3 p1 = g_vtx[tri.y].position;
        float3 p2 = g_vtx[tri.z].position;
        float2 duv1 = uv1 - uv0;
        float2 duv2 = uv2 - uv0;
        float3 dp1 = p1 - p0;
        float3 dp2 = p2 - p0;

        float denom = duv1.x * duv2.y - duv1.y * duv2.x;
        float3 tangentModel = float3(1.0f, 0.0f, 0.0f);
        float3 bitangentModel = float3(0.0f, 1.0f, 0.0f);
        if (abs(denom) > 1e-8f) {
            float inv = 1.0f / denom;
            tangentModel = normalize((dp1 * duv2.y - dp2 * duv1.y) * inv);
            bitangentModel = normalize((dp2 * duv1.x - dp1 * duv2.x) * inv);
        }

        float3 T = normalize(mul(float3x3(ObjectToWorld3x4()), tangentModel));
        float3 B = normalize(mul(float3x3(ObjectToWorld3x4()), bitangentModel));
        float3 N = normalize(normal);

        T = normalize(T - N * dot(T, N));
        B = normalize(cross(N, T));

        float3 mapN = g_textures[NonUniformResourceIndex(mat.normalTextureIndex)].SampleLevel(uv, 0.0f).xyz;
        mapN = normalize(mapN * 2.0f - 1.0f);
        normal = normalize(T * mapN.x + B * mapN.y + N * mapN.z);
    }

    float3 hitPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    // Reconstruct previous-frame hit position using barycentric interpolation only at depth 0.
    float3 prevHitPos = float3(0.0f);
    if (payload.recursionDepth == 0u) {
        float3 p0Prev = g_vtxPrev[tri.x].position;
        float3 p1Prev = g_vtxPrev[tri.y].position;
        float3 p2Prev = g_vtxPrev[tri.z].position;
        float3 prevHitPosModel = p0Prev + bary.x * (p1Prev - p0Prev) + bary.y * (p2Prev - p0Prev);
        prevHitPos = mul(float3x4(ObjectToWorld3x4()), float4(prevHitPosModel, 1.0f));
    }

    ClosestHitHelper(payload, mat, normal, hitPos, prevHitPos, true);
}
