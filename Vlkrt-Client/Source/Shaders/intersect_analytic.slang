// ================================================================== //
// intersect_analytic.slang — analytic primitive intersection shader
// ================================================================== //
import common;
import analytic;

[[vk::shader_record]] ConstantBuffer<ShaderRecordData> g_shaderRecord;

[shader("intersection")]
void AnalyticIntersectionMain() {
    uint instanceIdx = g_shaderRecord.GetInstanceIndex();
    uint primType = g_shaderRecord.GetPrimitiveType();
    AABBTransform t = g_aabbTransforms[instanceIdx];
    AABBTransform tPrev = g_aabbTransformsPrev[instanceIdx];

    // Transform ray to primitive local space
    Ray localRay;
    localRay.origin    = mul(t.bottomLevelASToLocalSpace, float4(ObjectRayOrigin(),    1.0f)).xyz;
    localRay.direction = mul(float3x3(t.bottomLevelASToLocalSpace), ObjectRayDirection());

    float  thit;
    float3 localNormal;
    if (!RayAnalyticTest(g_scene.sceneIndex, localRay, primType,
                         thit, localNormal))
        return;

    if (!IsAValidHit(localRay, thit, localNormal))
        return;

    float3 objectHitPos = ObjectRayOrigin() + thit * ObjectRayDirection();
    float3 localHitPos = mul(t.bottomLevelASToLocalSpace, float4(objectHitPos, 1.0f)).xyz;
    float3 prevObjectHitPos = mul(tPrev.localSpaceToBottomLevelAS, float4(localHitPos, 1.0f)).xyz;

    // Transform normal: local → BLAS → world
    // mul(v, M) = M^T * v  →  transpose of bottomLevelASToLocalSpace applied to localNormal
    float3 blasNormal  = mul(localNormal, float3x3(t.bottomLevelASToLocalSpace));
    float3 worldNormal = normalize(mul(float3x3(ObjectToWorld3x4()), blasNormal));

    float3 prevWorldPos = mul(float3x4(ObjectToWorld3x4()), float4(prevObjectHitPos, 1.0f));

    AabbHitAttr attr;
    attr.normal = worldNormal;
    attr.prevWorldPos = prevWorldPos;
    ReportHit(thit, 0u, attr);
}
