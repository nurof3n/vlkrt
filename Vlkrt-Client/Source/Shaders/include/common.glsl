#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_ray_tracing          : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

// ================================================================== //
// Constants
// ================================================================== //
#define PI      3.14159265359
#define TWO_PI  6.28318530718
#define INV_PI  0.31830988618
#define INFINITY (1.0 / 0.0)

// Scene indices — must match SceneFactory::SCENE_* constants in SceneFactory.h
#define SCENE_CORNELL_BOX  1u
#define SCENE_DEMO         2u
#define SCENE_PBR_SHOWCASE 3u

// Light types
#define LIGHT_TYPE_SQUARE      0u
#define LIGHT_TYPE_DIRECTIONAL 1u

// Analytic primitive types
#define ANALYTIC_AABB    0u
#define ANALYTIC_SPHERES 1u

// SDF primitive types
#define SDF_INTERSECTED_ROUND_CUBE 0u
#define SDF_SQUARE_TORUS           1u
#define SDF_COG                    2u
#define SDF_CYLINDER               3u
#define SDF_SOLID_ANGLE            4u

// Raytracing modes
#define RT_PATH_TRACING          1u
#define RT_PATH_TRACING_TEMPORAL 2u

// Importance sampling types
#define IS_UNIFORM 0u
#define IS_COSINE  1u
#define IS_BSDF    2u

// Fractal iterations
#define N_FRACTAL_ITERATIONS 4

// ================================================================== //
// Shared data structures
// ================================================================== //

struct Ray {
    vec3 origin;
    vec3 direction;
};

// Main path tracing payload (location 0). 64 bytes.
struct RayPayload {
    vec4  color;          // accumulated color
    vec4  worldPosition;  // hit world pos; w=1 for hit, 0 for miss
    vec4  throughput;     // path throughput
    vec4  absorption;     // accumulated Beer–Lambert absorption
    uint  rngState;       // PCG RNG state
    uint  recursionDepth;
    uint  inside;         // non-zero when ray is inside a refractive medium
    float bsdfPdf;        // pdf of the last BSDF sample (for MIS)
};

// Shadow ray payload (location 1). 4 bytes.
struct ShadowRayPayload {
    bool hit;
};

// ------------------------------------------------------------------ //
// GPU buffer element types (must match C++ counterparts byte-for-byte)
// ------------------------------------------------------------------ //

// 48 bytes, std430-compatible
struct GPUVertex {
    vec3  position;   // 0
    float _pad0;      // 12
    vec3  normal;     // 16
    float _pad1;      // 28
    vec2  texCoord;   // 32
    vec2  _pad2;      // 40
};

// 48 bytes, std430-compatible
struct GPULight {
    vec3  position;   // 0
    float intensity;  // 12
    vec3  emission;   // 16
    float size;       // 28
    vec3  direction;  // 32
    uint  type;       // 44
};

// 112 bytes, std430-compatible
struct GPUPBRMaterial {
    vec3  albedo;               // 0
    int   textureIndex;         // 12  (-1 = no texture)
    vec3  emission;             // 16
    float tiling;               // 28
    vec3  extinction;           // 32
    uint  materialIndex;        // 44  (0 = floor/ground)
    float stepScale;            // 48
    float sheen;                // 52
    float sheenTint;            // 56
    float clearcoat;            // 60
    float clearcoatGloss;       // 64
    float roughness;            // 68
    float subsurface;           // 72
    float anisotropic;          // 76
    float metallic;             // 80
    float specularTint;         // 84
    float specularTransmission; // 88
    float eta;                  // 92
    float atDistance;           // 96
    int   lightIndex;           // 100  (>=0 if this mesh is a light)
    float _pad1;                // 104
    float _pad2;                // 108
};

// 128 bytes, std430-compatible
struct AABBTransform {
    mat4 localSpaceToBottomLevelAS; // local → BLAS space
    mat4 bottomLevelASToLocalSpace; // BLAS space → local
};

// ================================================================== //
// Descriptor set bindings (set 0)
// ================================================================== //

layout(binding = 0,  set = 0)                           uniform accelerationStructureEXT g_tlas;
layout(binding = 1,  set = 0, rgba8)                    uniform image2D                  g_outputImage;
layout(binding = 2,  set = 0, std430) readonly buffer   VertexBuffer    { GPUVertex      g_vtx[];          };
layout(binding = 3,  set = 0, std430) readonly buffer   IndexBuffer     { uint           g_idx[];           };
layout(binding = 4,  set = 0, std430) readonly buffer   MaterialBuffer  { GPUPBRMaterial g_materials[];     };
layout(binding = 5,  set = 0, std430) readonly buffer   MatIdxBuffer    { uint           g_matIdx[];        };
layout(binding = 6,  set = 0, std430) readonly buffer   LightBuffer     { GPULight       g_lights[];        };
layout(binding = 7,  set = 0)                           uniform sampler2D                g_textures[16];
layout(binding = 8,  set = 0, std430) readonly buffer   AABBTransformBuf{ AABBTransform  g_aabbTransforms[];};
layout(binding = 9,  set = 0, std430) readonly buffer   AABBMatBuf      { GPUPBRMaterial g_aabbMaterials[]; };
layout(binding = 10, set = 0, rgba32f)                  uniform image2D                  g_accumImage;
layout(binding = 11, set = 0) uniform SceneUBO {
    mat4  projectionToWorld;
    vec4  cameraPosition;
    vec4  backgroundColor;
    uint  numLights;
    float elapsedTime;
    uint  elapsedTicks;
    uint  raytracingType;
    uint  importanceSamplingType;
    uint  maxRecursionDepth;
    uint  maxShadowRecursionDepth;
    uint  pathSqrtSamplesPerPixel;
    uint  pathFrameCacheIndex;
    uint  applyJitter;
    uint  onlyOneLightSample;
    uint  russianRouletteDepth;
    uint  anisotropicBSDF;
    uint  sceneIndex;
    float _pad1;
    float _pad2;
    float _pad3;
    float _pad4;
    float _pad5;
    float _pad6;
} g_scene;

// ================================================================== //
// Utility: math helpers
// ================================================================== //

float sq(float x) { return x * x; }
float length_toPow2(vec2 p) { return dot(p, p); }
float length_toPow2(vec3 p) { return dot(p, p); }

bool IsInRange(float val, float minVal, float maxVal) {
    return (val >= minVal && val <= maxVal);
}

// ACES tone-mapping operator
vec3 ACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

float GetLuminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec4 radianceClamp(vec4 color) {
    float lum = GetLuminance(color.xyz);
    if (lum > 5.0)
        color.xyz *= 5.0 / lum;
    return color;
}

// ================================================================== //
// PCG hash / RNG
// ================================================================== //

// PCG3D hash — strong spatial decorrelation for ray-tracing seeds
// "Hash Functions for GPU Rendering", Jarzynski & Olano (2020)
uvec3 pcg3d(uvec3 v) {
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

uint hashSeed(uvec2 p, uint seed) {
    return pcg3d(uvec3(p.x, p.y, seed)).x;
}

// PCG https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint rand_pcg(inout uint rng_state) {
    uint state = rng_state;
    rng_state = rng_state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(inout uint rng_state) {
    return float(rand_pcg(rng_state)) / float(0xFFFFFFFFu);
}

// ================================================================== //
// Ray tracing helpers
// ================================================================== //

// The three helpers below use stage-specific built-ins
// (gl_HitTEXT, gl_IncomingRayFlagsEXT, gl_RayTmin/maxEXT) that are
// only valid in closest-hit / any-hit / intersection stages.
// Define VLKRT_HIT_STAGE in those shaders before including this file.
#ifdef VLKRT_HIT_STAGE

// World-space hit position (valid in closesthit shaders only — needs gl_HitTEXT)
#ifdef VLKRT_CLOSESTHIT_STAGE
vec3 HitWorldPosition() {
    return gl_WorldRayOriginEXT + gl_HitTEXT * gl_WorldRayDirectionEXT;
}
#endif // VLKRT_CLOSESTHIT_STAGE

// Interpolate vertex attribute at hit using barycentric coordinates
vec3 HitAttribute(vec3 v0, vec3 v1, vec2 v2, vec2 bary) {
    // overload for vec3 attrib
    return v0 + bary.x * (v1 - v0) + bary.y * (vec3(v2, 0.0) - v0);
}

vec3 HitAttributeV3(vec3 a[3], vec2 bary) {
    return a[0] + bary.x * (a[1] - a[0]) + bary.y * (a[2] - a[0]);
}

vec2 HitAttributeV2(vec2 a[3], vec2 bary) {
    return a[0] + bary.x * (a[1] - a[0]) + bary.y * (a[2] - a[0]);
}

// Culling test for procedural primitives
bool IsCulled(Ray ray, vec3 hitSurfaceNormal) {
    float d = dot(ray.direction, hitSurfaceNormal);
    bool cullBack  = ((gl_IncomingRayFlagsEXT & gl_RayFlagsCullBackFacingTrianglesEXT)  != 0u) && (d > 0.0);
    bool cullFront = ((gl_IncomingRayFlagsEXT & gl_RayFlagsCullFrontFacingTrianglesEXT) != 0u) && (d < 0.0);
    return cullBack || cullFront;
}

// Valid hit for procedural intersection shaders (uses gl_RayTmaxEXT as upper bound)
bool IsAValidHit(Ray ray, float thit, vec3 hitSurfaceNormal) {
    return IsInRange(thit, gl_RayTminEXT, gl_RayTmaxEXT) && !IsCulled(ray, hitSurfaceNormal);
}

#endif // VLKRT_HIT_STAGE

// Generate camera ray from screen pixel index and sub-pixel offset [0,1)^2
Ray GenerateCameraRay(uvec2 index, vec2 offset) {
    vec2 xy = vec2(index) + offset;
    vec2 screenPos = xy / vec2(gl_LaunchSizeEXT.xy) * 2.0 - 1.0;
    screenPos.y = -screenPos.y;

    vec4 world = g_scene.projectionToWorld * vec4(screenPos, 0.0, 1.0);
    world.xyz /= world.w;

    Ray ray;
    ray.origin    = g_scene.cameraPosition.xyz;
    ray.direction = normalize(world.xyz - ray.origin);
    return ray;
}

// Background / sky
float Checkerboard(vec3 hitPos) {
    return mod(abs(floor((hitPos.x + 1.0) / 2.0) + floor(hitPos.z / 2.0)), 2.0);
}

// ComputeBackground uses gl_WorldRayDirectionEXT (valid in rint, rchit, rahit, rmiss — not rgen)
#ifdef VLKRT_HIT_STAGE
vec4 ComputeBackground() {
    if (g_scene.sceneIndex == SCENE_PBR_SHOWCASE) {
        // Simple sky-like gradient
        vec3 rd = gl_WorldRayDirectionEXT;
        vec3 sundir = -normalize(g_lights[0].direction);
        float yd = min(rd.y, 0.0);
        rd.y = max(rd.y, 0.0);
        float I = sqrt(g_lights[0].intensity);
        vec3 col = vec3(0.0);
        col += I * vec3(0.4, 0.4 - exp(-rd.y * 20.0) * 0.15, 0.0) * exp(-rd.y * 9.0);
        col += I * vec3(0.3, 0.5, 0.6) * (1.0 - exp(-rd.y * 8.0)) * exp(-rd.y * 0.9);
        col = I * mix(col * 1.2, vec3(0.34, 0.44, 0.4), 1.0 - exp(yd * 100.0));
        col += I * pow(clamp(dot(rd, sundir), 0.0, 1.0), 150.0) * 0.15;
        col += g_lights[0].intensity * vec3(1.0, 0.8, 0.55) * pow(max(dot(rd, sundir), 0.0), 15.0) * 0.6;
        return vec4(pow(col, vec3(2.2)), 1.0);
    }
    return g_scene.backgroundColor;
}

#else // !VLKRT_HIT_STAGE
// Stub for raygen stage (never actually called from raygen, but must be compilable)
vec4 ComputeBackground() { return g_scene.backgroundColor; }
#endif // VLKRT_HIT_STAGE (ComputeBackground)

// ================================================================== //
// Tangent-space helpers
// (DieXaR convention: tangent-space y = world N, x = T, z = B)
// ================================================================== //

void ComputeLocalSpace(vec3 N, out vec3 T, out vec3 B) {
    if (abs(N.y) < 0.999)
        T = normalize(cross(vec3(0, 1, 0), N));
    else
        T = normalize(cross(vec3(1, 0, 0), N));
    B = cross(N, T);
}

// tangent-space → world-space:  V.x*T + V.y*N + V.z*B
vec3 GetTangentToWorld(vec3 N, vec3 T, vec3 B, vec3 V) {
    return V.x * T + V.y * N + V.z * B;
}

// world-space → tangent-space:  (dot(V,T), dot(V,N), dot(V,B))
vec3 GetWorldToTangent(vec3 N, vec3 T, vec3 B, vec3 V) {
    return vec3(dot(V, T), dot(V, N), dot(V, B));
}

#endif // COMMON_GLSL
