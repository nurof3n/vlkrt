// ================================================================== //
// intersect_analytic.slang — analytic primitive intersection shader
// ================================================================== //
import common;
import analytic;

[[vk::shader_record]] ConstantBuffer<ShaderRecordData> g_shaderRecord;

[shader("intersection")]
void AnalyticIntersectionMain() {
    AABBTransform t = g_aabbTransforms[g_shaderRecord.instanceIndex];

    // Transform ray to primitive local space
    Ray localRay;
    localRay.origin    = mul(t.bottomLevelASToLocalSpace, float4(ObjectRayOrigin(),    1.0f)).xyz;
    localRay.direction = mul(float3x3(t.bottomLevelASToLocalSpace), ObjectRayDirection());

    float  thit;
    float3 localNormal;
    if (!RayAnalyticTest(g_scene.sceneIndex, localRay, g_shaderRecord.primitiveType,
                         thit, localNormal))
        return;

    if (!IsAValidHit(localRay, thit, localNormal))
        return;

    // Transform normal: local → BLAS → world
    // mul(v, M) = M^T * v  →  transpose of bottomLevelASToLocalSpace applied to localNormal
    float3 blasNormal  = mul(localNormal, float3x3(t.bottomLevelASToLocalSpace));
    float3 worldNormal = normalize(mul(float3x3(ObjectToWorld3x4()), blasNormal));

    AabbHitAttr attr;
    attr.normal = worldNormal;
    ReportHit(thit, 0u, attr);
}
