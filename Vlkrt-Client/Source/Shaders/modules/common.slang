// ================================================================== //
// common.slang — shared types, descriptors, constants, and utilities
// ================================================================== //


// ------------------------------------------------------------------ //
// Constants
// ------------------------------------------------------------------ //
static const float PI     = 3.14159265359f;
static const float TWO_PI = 6.28318530718f;
static const float INV_PI = 0.31830988618f;
static const float INFINITY_F = 1.0f / 0.0f;

// Scene indices (must match SceneFactory::SCENE_* in C++)
static const uint SCENE_CORNELL_BOX  = 1u;
static const uint SCENE_DEMO         = 2u;
static const uint SCENE_PBR_SHOWCASE = 3u;

// Light types
static const uint LIGHT_TYPE_SQUARE      = 0u;
static const uint LIGHT_TYPE_DIRECTIONAL = 1u;

// Analytic primitive types
static const uint ANALYTIC_AABB    = 0u;
static const uint ANALYTIC_SPHERES = 1u;

// SDF primitive types
static const uint SDF_INTERSECTED_ROUND_CUBE = 0u;
static const uint SDF_SQUARE_TORUS           = 1u;
static const uint SDF_COG                    = 2u;
static const uint SDF_CYLINDER               = 3u;
static const uint SDF_SOLID_ANGLE            = 4u;

// Importance sampling modes
static const uint IS_UNIFORM = 0u;
static const uint IS_COSINE  = 1u;
static const uint IS_BSDF    = 2u;

// Fractal iterations
static const int N_FRACTAL_ITERATIONS = 4;

// ------------------------------------------------------------------ //
// Data structures (must match C++ layout byte-for-byte)
// ------------------------------------------------------------------ //

struct Ray {
    float3 origin;
    float3 direction;
};

/// Main path-tracing payload (location 0).
/// Total size: 180 bytes (keep maxPipelineRayPayloadSize in sync in Renderer.cpp).
///
/// Fields are grouped:
///   [0..63]  Multi-bounce core fields — carried at every recursion depth.
///   [64..111] NRD first-hit guide fields — only written at recursionDepth==0,
///             only read in raygen shaders. Future: could move to a separate payload
///             type for secondary bounces to reduce GPU continuation stack.
struct RayPayload {
    // ---- Core fields (every bounce) ----
    float4 color;          ///< accumulated radiance
    float4 worldPosition;  ///< hit world-pos; w=hit distance, w=0 on miss
    float4 throughput;     ///< path throughput
    float4 absorption;     ///< accumulated Beer-Lambert absorption
    uint   rngState;       ///< PCG RNG state
    uint   recursionDepth;
    uint   inside;         ///< non-zero when inside a refractive medium
    float  bsdfPdf;        ///< PDF of the last BSDF sample (MIS)
    float  firstBounceHitDist;

    // ---- NRD separate guide channels (depth-0 only) ----
    float3 diffuse;
    float3 specular;
    float  diffuseHitDist;
    float  specularHitDist;
    float3 emission;
    float  firstHitMetallic;

    // ---- NRD first-hit G-buffer data (depth-0 only) ----
    float4 firstHitNormalRoughness; ///< xyz = first-hit normal, w = first-hit roughness
    float4 firstHitMaterialParams;  ///< xyz = albedo, w = specularWeight
    float4 firstHitWorldPosPrev;    ///< xyz = first-hit previous-frame world pos, w = materialClass
};

/// Shadow ray payload (location 1). 4 bytes.
struct ShadowRayPayload {
    bool hit;
};

/// GPU vertex — 48 bytes, std430-compatible

struct GPUVertex {
    float3 position;  // offset  0
    float  _pad0;     // offset 12
    float3 normal;    // offset 16
    float  _pad1;     // offset 28
    float2 texCoord;  // offset 32
    float2 _pad2;     // offset 40
};

/// GPU light — 48 bytes, std430-compatible
struct GPULight {
    float3 position;   // offset  0
    float  intensity;  // offset 12
    float3 emission;   // offset 16
    float  size;       // offset 28
    float3 direction;  // offset 32
    uint   type;       // offset 44
};

/// GPU PBR material — 112 bytes, std430-compatible
struct GPUPBRMaterial {
    float3 albedo;               // offset   0
    int    textureIndex;         // offset  12  (-1 = no texture)
    float3 emission;             // offset  16
    float  tiling;               // offset  28
    float3 extinction;           // offset  32
    uint   materialIndex;        // offset  44
    float  stepScale;            // offset  48
    float  sheen;                // offset  52
    float  sheenTint;            // offset  56
    float  clearcoat;            // offset  60
    float  clearcoatGloss;       // offset  64
    float  roughness;            // offset  68
    float  subsurface;           // offset  72
    float  anisotropic;          // offset  76
    float  metallic;             // offset  80
    float  specularTint;         // offset  84
    float  specularTransmission; // offset  88
    float  eta;                  // offset  92
    float  atDistance;           // offset  96
    int    lightIndex;           // offset 100  (>=0 if mesh is emitter)
    float  _pad1;                // offset 104
    float  _pad2;                // offset 108
};

/// AABB transform pair — 128 bytes, std430-compatible
struct AABBTransform {
    float4x4 localSpaceToBottomLevelAS; ///< local → BLAS
    float4x4 bottomLevelASToLocalSpace; ///< BLAS → local
};

/// Scene uniform buffer — matches C++ GPUSceneData
struct SceneUBO {
    float4x4 projectionToWorld;
    float4x4 worldToClip;
    float4x4 worldToClipPrev;
    float4   cameraPosition;
    float4   backgroundColor;
    uint     numLights;
    float    elapsedTime;
    uint     elapsedTicks;
    uint     raytracingType;
    uint     importanceSamplingType;
    uint     maxRecursionDepth;
    uint     maxShadowRecursionDepth;
    uint     pathSqrtSamplesPerPixel;
    uint     pathFrameCacheIndex;
    uint     applyJitter;
    uint     onlyOneLightSample;
    uint     russianRouletteDepth;
    uint     anisotropicBSDF;
    uint     sceneIndex;
    float    cameraForwardX;
    float    cameraForwardY;
    float    cameraForwardZ;
    uint     nrdDebugViewMode;
    float    _pad2, _pad3;

    float3 GetCameraForward() {
        return float3(cameraForwardX, cameraForwardY, cameraForwardZ);
    }
};

// ------------------------------------------------------------------ //
// Descriptor set bindings (set 0)
// ------------------------------------------------------------------ //
[[vk::binding(0,  0)]] RaytracingAccelerationStructure        g_tlas;
[[vk::binding(1,  0)]] [[vk::image_format("rgba8")]]
                        RWTexture2D<float4>                    g_outputImage;
[[vk::binding(2,  0)]] StructuredBuffer<GPUVertex>            g_vtx;
[[vk::binding(3,  0)]] StructuredBuffer<uint>                 g_idx;
[[vk::binding(4,  0)]] StructuredBuffer<GPUPBRMaterial>       g_materials;
[[vk::binding(5,  0)]] StructuredBuffer<uint>                 g_matIdx;
[[vk::binding(6,  0)]] StructuredBuffer<GPULight>             g_lights;
[[vk::binding(7,  0)]] Sampler2D                              g_textures[16];
[[vk::binding(8,  0)]] StructuredBuffer<AABBTransform>        g_aabbTransforms;
[[vk::binding(9,  0)]] StructuredBuffer<GPUPBRMaterial>       g_aabbMaterials;
[[vk::binding(10, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_accumImage;
[[vk::binding(11, 0)]] ConstantBuffer<SceneUBO>               g_scene;

// NRD guide buffers (all RGBA32F for simplicity)
[[vk::binding(12, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_guideNormalRoughness;
[[vk::binding(13, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_guideViewZ;
[[vk::binding(14, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_guideMotionVectors;
[[vk::binding(15, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_guideDiffRadianceHitDist;
[[vk::binding(16, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_guideSpecRadianceHitDist;
[[vk::binding(17, 0)]] StructuredBuffer<GPUVertex>            g_vtxPrev;
[[vk::binding(18, 0)]] StructuredBuffer<AABBTransform>        g_aabbTransformsPrev;

[[vk::binding(19, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_outDiffRadianceHitDist;
[[vk::binding(20, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_outSpecRadianceHitDist;
[[vk::binding(21, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_guideEmission;
[[vk::binding(22, 0)]] [[vk::image_format("rgba32f")]]
                        RWTexture2D<float4>                    g_guideDepth;

// ------------------------------------------------------------------ //
// Math helpers
// ------------------------------------------------------------------ //
float  sq(float x)  { return x * x; }
float2 sq(float2 v) { return v * v; }

float GetLuminance(float3 c) { return dot(c, float3(0.2126f, 0.7152f, 0.0722f)); }

bool IsInRange(float v, float lo, float hi) { return v >= lo && v <= hi; }

/// ACES filmic tone-mapper
float3 ACES(float3 x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

float4 RadianceClamp(float4 color) {
    float lum = GetLuminance(color.xyz);
    if (lum > 250.0f) color.xyz *= 250.0f / lum;
    return color;
}

/// Transform world position to view space (using camera position)
float3 WorldToViewSpace(float3 worldPos) {
    return worldPos - g_scene.cameraPosition.xyz;
}

/// Compute linear view-space depth (coordinate along forward look axis)
float ComputeViewDepth(float3 worldPos) {
    return dot(worldPos - g_scene.cameraPosition.xyz, g_scene.GetCameraForward());
}

float EncodeDebugViewDepth(float rawDepth) {
    const float kDepthRange = 1024.0f;
    const float kInvLogRange = 1.0f / log2(1.0f + kDepthRange);
    return saturate(log2(1.0f + max(rawDepth, 0.0f)) * kInvLogRange);
}

float2 EncodeDebugMotionVector(float2 motion) {
    // Encode motion vectors for display: map from NDC space to [0,1]
    // motion typically in range [-1, 1] in NDC space
    // 0.5 offset centers the display: gray = no motion, bright = +motion, dark = -motion
    return saturate(motion * 0.5f + 0.5f);
}

float2 ComputeMotionVector(float3 worldPos, float3 prevWorldPos) {
    float4 currClip = mul(g_scene.worldToClip, float4(worldPos, 1.0f));
    float4 prevClip = mul(g_scene.worldToClipPrev, float4(prevWorldPos, 1.0f));

    if (abs(currClip.w) < 1e-6f || abs(prevClip.w) < 1e-6f)
        return float2(0.0f, 0.0f);

    float2 currNdc = currClip.xy / currClip.w;
    float2 prevNdc = prevClip.xy / prevClip.w;

    // NRD motion convention: current UV + mv = previous UV.
    // NDC X increases rightward, UV X increases rightward. (Same direction)
    // NDC Y increases upward, UV Y increases downward. (Opposite directions)
    return float2(
        (prevNdc.x - currNdc.x) * 0.5f,
        (currNdc.y - prevNdc.y) * 0.5f
    );
}

// ------------------------------------------------------------------ //
// PCG random number generator
// ------------------------------------------------------------------ //

/// PCG3D hash — strong spatial decorrelation for ray seeds
/// "Hash Functions for GPU Rendering", Jarzynski & Olano (2020)
uint3 pcg3d(uint3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

uint HashSeed(uint2 p, uint seed) {
    return pcg3d(uint3(p.x, p.y, seed)).x;
}

uint RandPCG(inout uint state) {
    uint s = state;
    state = state * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

float Random(inout uint state) {
    return float(RandPCG(state)) / float(0xFFFFFFFFu);
}

// ------------------------------------------------------------------ //
// Tangent-space helpers  (y = N, x = T, z = B — DieXaR convention)
// ------------------------------------------------------------------ //
void ComputeLocalSpace(float3 N, out float3 T, out float3 B) {
    T = (abs(N.y) < 0.999f) ? normalize(cross(float3(0, 1, 0), N))
                             : normalize(cross(float3(1, 0, 0), N));
    B = cross(N, T);
}

float3 TangentToWorld(float3 N, float3 T, float3 B, float3 v) {
    return v.x * T + v.y * N + v.z * B;
}

float3 WorldToTangent(float3 N, float3 T, float3 B, float3 v) {
    return float3(dot(v, T), dot(v, N), dot(v, B));
}

// ------------------------------------------------------------------ //
// Camera ray generation
// ------------------------------------------------------------------ //
Ray GenerateCameraRay(uint2 index, float2 offset) {
    float2 xy  = float2(index) + offset;
    float2 scr = xy / float2(DispatchRaysDimensions().xy) * 2.0f - 1.0f;
    scr.y = -scr.y;

    float4 world = mul(g_scene.projectionToWorld, float4(scr, 0.0f, 1.0f));
    world.xyz /= world.w;

    Ray ray;
    ray.origin    = g_scene.cameraPosition.xyz;
    ray.direction = normalize(world.xyz - ray.origin);
    return ray;
}

/// Hit attribute for AABB / procedural shaders (written by IS, read by CH)
struct AabbHitAttr {
    float3 normal;  ///< world-space surface normal
    float3 prevWorldPos; ///< previous-frame world-space hit position
};

/// Per-SBT-record payload for AABB hit groups
struct ShaderRecordData {
    uint instanceIndex;
    uint primitiveType;
};

// ------------------------------------------------------------------ //
// Intersection-stage validity helpers
// (callable from intersection / any-hit / closest-hit / miss stages)
// ------------------------------------------------------------------ //
bool IsCulled(Ray ray, float3 hitSurfaceNormal) {
    float d         = dot(ray.direction, hitSurfaceNormal);
    bool  cullBack  = ((RayFlags() & RAY_FLAG_CULL_BACK_FACING_TRIANGLES)  != 0u) && (d > 0.0f);
    bool  cullFront = ((RayFlags() & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) != 0u) && (d < 0.0f);
    return cullBack || cullFront;
}

bool IsAValidHit(Ray ray, float thit, float3 hitSurfaceNormal) {
    return IsInRange(thit, RayTMin(), RayTCurrent()) && !IsCulled(ray, hitSurfaceNormal);
}

// ------------------------------------------------------------------ //
// Background / sky
// ------------------------------------------------------------------ //
float Checkerboard(float3 hitPos) {
    return fmod(abs(floor((hitPos.x + 1.0f) / 2.0f) + floor(hitPos.z / 2.0f)), 2.0f);
}

float4 ComputeBackground(float3 rayDir, bool isPrimary) {
    if (g_scene.sceneIndex == SCENE_PBR_SHOWCASE) {
        float3 rd     = rayDir;
        float3 sundir = -normalize(g_lights[0].direction);
        float  yd     = min(rd.y, 0.0f);
        rd.y = max(rd.y, 0.0f);
        float  I      = sqrt(g_lights[0].intensity);
        float3 col    = float3(0.0f);
        col += I * float3(0.4f, 0.4f - exp(-rd.y * 20.0f) * 0.15f, 0.0f) * exp(-rd.y * 9.0f);
        col += I * float3(0.3f, 0.5f, 0.6f) * (1.0f - exp(-rd.y * 8.0f)) * exp(-rd.y * 0.9f);
        col  = I * lerp(col * 1.2f, float3(0.34f, 0.44f, 0.4f), 1.0f - exp(yd * 100.0f));
        if (isPrimary) {
            col += I * pow(clamp(dot(rd, sundir), 0.0f, 1.0f), 150.0f) * 0.15f;
            col += g_lights[0].intensity * float3(1.0f, 0.8f, 0.55f)
                 * pow(max(dot(rd, sundir), 0.0f), 15.0f) * 0.6f;
        }
        return float4(pow(col, float3(2.2f)), 1.0f);
    }
    return g_scene.backgroundColor;
}

// ------------------------------------------------------------------ //
// NRD helper functions
// ------------------------------------------------------------------ //

bool NRD_IsInvalid(float3 v) {
    return isnan(v.x) || isnan(v.y) || isnan(v.z) || isinf(v.x) || isinf(v.y) || isinf(v.z);
}

bool NRD_IsInvalid(float v) {
    return isnan(v) || isinf(v);
}

float3 NRD_LinearToYCoCg(float3 color) {
    float Y = dot(color, float3(0.25f, 0.5f, 0.25f));
    float Co = dot(color, float3(0.5f, 0.0f, -0.5f));
    float Cg = dot(color, float3(-0.25f, 0.5f, -0.25f));
    return float3(Y, Co, Cg);
}

float3 NRD_YCoCgToLinear(float3 color) {
    float t = color.x - color.z;
    float3 r;
    r.y = color.x + color.z;
    r.x = t + color.y;
    r.z = t - color.y;
    return max(r, 0.0f);
}

float NRD_GetSpecMagicCurve(float roughness, float power) {
    float f = 1.0f - exp2(-200.0f * roughness * roughness);
    f *= pow(saturate(roughness), power);
    return f;
}

float REBLUR_GetHitDistanceNormalization(float viewZ, float3 hitDistParams, float roughness) {
    float smc = NRD_GetSpecMagicCurve(roughness, 0.5f);
    return (hitDistParams.x + abs(viewZ) * hitDistParams.y) * lerp(hitDistParams.z, 1.0f, smc);
}

float REBLUR_FrontEnd_GetNormHitDist(float hitDist, float viewZ, float3 hitDistParams, float roughness) {
    float f = REBLUR_GetHitDistanceNormalization(viewZ, hitDistParams, roughness);
    return saturate(hitDist / f);
}

float4 REBLUR_FrontEnd_PackRadianceAndNormHitDist(float3 radiance, float normHitDist) {
    radiance = NRD_IsInvalid(radiance) ? float3(0.0f) : clamp(radiance, 0.0f, 65504.0f);
    normHitDist = NRD_IsInvalid(normHitDist) ? 0.0f : saturate(normHitDist);
    return float4(NRD_LinearToYCoCg(radiance), normHitDist);
}

float3 NRD_EncodeNormalRoughness101010(float3 n, float roughness) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float3 r;
    r.y = n.y * 0.5f + 0.5f;
    r.x = n.x * 0.5f + r.y;
    r.y -= n.x * 0.5f;
    roughness = max(roughness, 1.5f / 512.0f);
    float s = n.z < 0.0f ? -roughness : roughness;
    r.z = s * 0.5f + 0.5f;
    return r;
}

float4 PackNormalAndRoughness(float3 N, float roughness, float materialID) {
    float4 p;
    p.xyz = NRD_EncodeNormalRoughness101010(N, roughness);
    p.w = saturate(materialID / 3.0f);
    return p;
}

void NRD_DecodeNormalRoughness101010(float3 r, out float3 n, out float roughness) {
    float s = (r.z - 0.5f) * 2.0f;
    roughness = abs(s);
    float zSign = s >= 0.0f ? 1.0f : -1.0f;
    n.x = r.x - r.y;
    n.y = r.x + r.y - 1.0f;
    n.z = zSign * max(0.0f, 1.0f - abs(n.x) - abs(n.y));
    n = normalize(n);
}

float3 _NRD_EnvironmentTerm_Rtg(float3 Rf0, float NoV, float roughness) {
    float m = saturate(roughness * roughness);

    float4 X = float4(1.0f, NoV, NoV * NoV, NoV * NoV * NoV);
    float4 Y = float4(1.0f, m, m * m, m * m * m);

    float2x2 M1 = float2x2(0.99044f, -1.28514f, 1.29678f, -0.755907f);
    float3x3 M2 = float3x3(1.0f, 2.92338f, 59.4188f, 20.3225f, -27.0302f, 222.592f, 121.563f, 626.13f, 316.627f);
    float2x2 M3 = float2x2(0.0365463f, 3.32707f, 9.0632f, -9.04756f);
    float3x3 M4 = float3x3(1.0f, 3.59685f, -1.36772f, 9.04401f, -16.3174f, 9.22949f, 5.56589f, 19.7886f, -20.2123f);

    float2 m1_x = mul(M1, X.xy);
    float3 m2_x = mul(M2, X.xyw);
    float2 m3_x = mul(M3, X.xy);
    float3 m4_x = mul(M4, X.xzw);

    float bias = dot(m1_x, Y.xy) / max(dot(m2_x, Y.xyw), 1e-6f);
    float scale = dot(m3_x, Y.xy) / max(dot(m4_x, Y.xyw), 1e-6f);

    return saturate(Rf0 * scale + bias);
}

void NRD_MaterialFactors(float3 N, float3 V, float3 albedo, float3 Rf0, float roughness, out float3 diffFactor, out float3 specFactor) {
    float NoV = abs(dot(N, V));
    float3 Fenv = _NRD_EnvironmentTerm_Rtg(Rf0, NoV, roughness);

    diffFactor = (1.0f - Fenv) * albedo;
    diffFactor = lerp(float3(0.01f), float3(1.0f), diffFactor);

    specFactor = Fenv;
    specFactor *= lerp(float3(0.01f), float3(1.0f), roughness);
    specFactor = lerp(float3(0.01f), float3(1.0f), specFactor);
}
