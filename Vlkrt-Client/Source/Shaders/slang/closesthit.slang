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

    // Optional texture sample
    if (mat.textureIndex >= 0) {
        float2 uv0 = g_vtx[tri.x].texCoord;
        float2 uv1 = g_vtx[tri.y].texCoord;
        float2 uv2 = g_vtx[tri.z].texCoord;
        float2 uv  = uv0 + bary.x * (uv1 - uv0) + bary.y * (uv2 - uv0);
        uv *= mat.tiling;
        mat.albedo = g_textures[NonUniformResourceIndex(mat.textureIndex)].SampleLevel(uv, 0.0f).rgb;
    }

    float3 hitPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    ClosestHitHelper(payload, mat, normal, hitPos, true);
}
