// ================================================================== //
// intersect_sdf.slang — SDF ray-marching intersection shader
// ================================================================== //
import common;
import sdf;

[[vk::shader_record]] ConstantBuffer<ShaderRecordData> g_shaderRecord;

[shader("intersection")]
void SdfIntersectionMain() {
    uint instanceIdx = g_shaderRecord.GetInstanceIndex();
    uint primType = g_shaderRecord.GetPrimitiveType();
    AABBTransform t = g_aabbTransforms[instanceIdx];
    AABBTransform tPrev = g_aabbTransformsPrev[instanceIdx];

    // Transform ray to primitive local space
    Ray localRay;
    localRay.origin = mul(t.bottomLevelASToLocalSpace, float4(ObjectRayOrigin(), 1.0f)).xyz;

    // Sphere tracing assumes unit-length direction in the SDF's metric space.
    // ObjectRayDirection transformed to local space is generally not unit-length
    // (especially under non-uniform transforms), which causes stepped/ring artifacts.
    float3 localDirRaw = mul(float3x3(t.bottomLevelASToLocalSpace), ObjectRayDirection());
    float  localDirLen = max(length(localDirRaw), 1e-8f);
    localRay.direction = localDirRaw / localDirLen;

    float stepScale = g_aabbMaterials[instanceIdx].stepScale;

    float  thit;
    float3 localNormal;
    if (!RaySDFTest(localRay, primType, stepScale,
                    thit, localNormal))
        return;

    // Convert local-distance parameter back to object-ray parameter expected by ReportHit.
    float thitObj = thit / localDirLen;

    if (!IsInRange(thitObj, RayTMin(), RayTCurrent()) || IsCulled(localRay, localNormal))
        return;

    float3 objectHitPos = ObjectRayOrigin() + thitObj * ObjectRayDirection();
    float3 localHitPos = mul(t.bottomLevelASToLocalSpace, float4(objectHitPos, 1.0f)).xyz;
    float3 prevObjectHitPos = mul(tPrev.localSpaceToBottomLevelAS, float4(localHitPos, 1.0f)).xyz;

    // Transform normal: local → BLAS → world
    float3 blasNormal  = mul(localNormal, float3x3(t.bottomLevelASToLocalSpace));
    float3 worldNormal = normalize(mul(float3x3(ObjectToWorld3x4()), blasNormal));

    float3 prevWorldPos = mul(float3x4(ObjectToWorld3x4()), float4(prevObjectHitPos, 1.0f));

    AabbHitAttr attr;
    attr.normal = worldNormal;
    attr.prevWorldPos = prevWorldPos;
    ReportHit(thitObj, 0u, attr);
}
