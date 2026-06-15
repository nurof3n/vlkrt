// ================================================================== //
// closesthit_aabb.slang — procedural (AABB) closest-hit shader
// ================================================================== //
import common;
import path_tracer;

[[vk::shader_record]] ConstantBuffer<ShaderRecordData> g_shaderRecord;

[shader("closesthit")]
void AabbClosestHitMain([[vk::location(0)]] inout RayPayload payload,
                        AabbHitAttr attr) {
    uint instanceIdx = g_shaderRecord.GetInstanceIndex();
    GPUPBRMaterial mat    = g_aabbMaterials[instanceIdx];
    float3         hitPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    ClosestHitHelper(payload, mat, attr.normal, hitPos, attr.prevWorldPos, false);
}
