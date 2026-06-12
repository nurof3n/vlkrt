#ifndef PATH_TRACING_GLSL
#define PATH_TRACING_GLSL

#include "disney_bsdf.glsl"

// ================================================================== //
// Payload declarations
// In closest-hit shaders the incoming payload is rayPayloadInEXT;
// in raygen shaders it is rayPayloadEXT.
// ================================================================== //
#ifdef VLKRT_CLOSESTHIT_STAGE
layout(location = 0) rayPayloadInEXT RayPayload      g_payload;
#else
layout(location = 0) rayPayloadEXT   RayPayload      g_payload;
#endif
layout(location = 1) rayPayloadEXT   ShadowRayPayload g_shadowPayload;

// ================================================================== //
// Light sampling helpers
// ================================================================== //

struct LightSample {
    vec3  L;        // normalized direction to light
    vec3  emission; // light emission * intensity (* area)
    float dist;     // distance to sample
    float pdf;      // area / cosine PDF
};

float PowerHeuristic(float nf, float fPdf, float ng, float gPdf) {
    float f = nf * fPdf;
    float g = ng * gPdf;
    return sq(f) / (sq(f) + sq(g));
}

void SampleSquareLight(GPULight light, vec3 hitPos, vec3 L, float dist, out LightSample ls) {
    ls.L        = L;
    ls.dist     = dist;
    ls.emission = light.intensity * light.emission * sq(light.size);
    ls.pdf      = sq(dist) / dot(L, vec3(0.0, 1.0, 0.0));
}

// ================================================================== //
// Ray-tracing wrappers (require caller to declare payloads)
// ================================================================== //

// These are called from raygen and closesthit shaders.
// The payload variables below are declared in each caller shader:
//   layout(location = 0) rayPayloadEXT/rayPayloadInEXT RayPayload g_payload;
//   layout(location = 1) rayPayloadEXT ShadowRayPayload g_shadowPayload;

void TraceRadianceRay(Ray ray) {
    // g_payload must already be set up by the caller (recursionDepth, throughput, etc.)
    traceRayEXT(
        g_tlas,
        gl_RayFlagsNoneEXT,
        0xFF,
        0,      // sbtOffset: radiance = 0
        2,      // sbtStride: 2 ray types per geometry
        0,      // missIndex: radiance miss = 0
        ray.origin,
        0.0001,
        ray.direction,
        10000.0,
        0       // payload location
    );
    // After return, g_payload.color holds the result.
    g_payload.color = radianceClamp(g_payload.color);
}

bool TraceShadowRay(Ray ray, float lightDist, uint currentDepth) {
    if (currentDepth >= g_scene.maxShadowRecursionDepth) return false;

    g_shadowPayload.hit = true;
    traceRayEXT(
        g_tlas,
        gl_RayFlagsCullBackFacingTrianglesEXT |
        gl_RayFlagsTerminateOnFirstHitEXT     |
        gl_RayFlagsOpaqueEXT                  |
        gl_RayFlagsSkipClosestHitShaderEXT,
        0xFF,
        1,      // sbtOffset: shadow = 1
        2,      // sbtStride
        1,      // missIndex: shadow miss = 1
        ray.origin,
        0.0001,
        ray.direction,
        lightDist * 0.999999,
        1       // shadow payload location
    );
    return g_shadowPayload.hit;
}

// ================================================================== //
// Direct lighting / NEE
// (Only valid in hit-stage shaders that can use gl_WorldRayDirectionEXT)
// ================================================================== //
#ifdef VLKRT_HIT_STAGE
bool NextEventEstimation(inout uint rngState, GPULight light, vec3 hitPos, vec3 normal,
                         uint recursionDepth, out LightSample ls) {
    if (light.type == LIGHT_TYPE_SQUARE) {
        vec2  eps     = vec2(random(rngState), random(rngState));
        vec3  samplePos = light.position + vec3(eps.x - 0.5, 0.0, eps.y - 0.5) * light.size;
        ls.L    = samplePos - hitPos;
        ls.dist = length(ls.L);
        ls.L   /= ls.dist;
        float angleL = dot(ls.L, vec3(0.0, 1.0, 0.0));
        if (angleL <= 0.0) return false;
        if (dot(normal, ls.L) <= 0.0) return false;
        ls.emission = light.intensity * light.emission * sq(light.size);
        ls.pdf      = sq(ls.dist) / angleL;
    } else { // directional
        ls.L        = -normalize(light.direction);
        if (dot(normal, ls.L) <= 0.0) return false;
        ls.dist     = 10000.0;
        ls.emission = light.intensity * light.emission;
        ls.pdf      = 1.0;
    }

    Ray shadowRay;
    shadowRay.origin    = hitPos;
    shadowRay.direction = ls.L;
    bool occluded = TraceShadowRay(shadowRay, ls.dist, recursionDepth);
    return !occluded;
}

vec3 MIS(inout uint rngState, GPUPBRMaterial mat, float eta,
         vec3 hitPos, GPULight light, vec3 N, uint recursionDepth) {
    LightSample ls;
    if (!NextEventEstimation(rngState, light, hitPos, N, recursionDepth, ls))
        return vec3(0.0);

    float bsdfPdf;
    vec3 reflectance = EvaluateDisneyBSDF(mat, g_scene.anisotropicBSDF != 0u,
                                          eta, -gl_WorldRayDirectionEXT, ls.L, N, bsdfPdf);
    if (bsdfPdf <= 0.0) return vec3(0.0);

    float weight = (light.type == LIGHT_TYPE_DIRECTIONAL) ? 1.0
                 : PowerHeuristic(1.0, ls.pdf, 1.0, bsdfPdf);

    return reflectance * weight * ls.emission / ls.pdf;
}

// ================================================================== //
// Core path tracing step — called from both triangle and AABB hit shaders
// ================================================================== //
vec3 DoPathTracing(GPUPBRMaterial mat, vec3 N, vec3 hitPos, float hitDist) {
    // Correct-side normal (agrees with incoming direction)
    vec3 normalSide = dot(gl_WorldRayDirectionEXT, N) < 0.0 ? N : -N;

    // Checkerboard pattern for the floor (materialIndex == 0, Demo and PBR Showcase scenes)
    if (mat.materialIndex == 0u && (g_scene.sceneIndex == SCENE_DEMO || g_scene.sceneIndex == SCENE_PBR_SHOWCASE)) {
        float pattern = Checkerboard(hitPos);
        mat.roughness   = pattern * 0.25;
        mat.albedo      = pattern * 0.5 * mat.albedo + 0.5 * mat.albedo;
    }

    mat.roughness = max(0.001, mat.roughness);

    // IOR: flip when exiting
    float eta = (g_payload.inside != 0u) ? mat.eta : 1.0 / mat.eta;

    // Beer–Lambert: accumulate absorption only while inside
    vec3 absorption = (g_payload.inside != 0u) ? g_payload.absorption.xyz : vec3(0.0);

    // Apply absorption over travelled distance
    vec3 throughput = g_payload.throughput.xyz * exp(-absorption * hitDist);

    vec3 color = vec3(0.0);

    // ---- Direct lighting (NEE + MIS) ----
    if (g_scene.sceneIndex != SCENE_PBR_SHOWCASE) {
        if (g_scene.onlyOneLightSample != 0u) {
            uint idx = uint(random(g_payload.rngState) * float(g_scene.numLights));
            color += throughput * MIS(g_payload.rngState, mat, eta, hitPos,
                                      g_lights[idx], normalSide,
                                      g_payload.recursionDepth) * float(g_scene.numLights);
        } else {
            for (uint i = 0u; i < g_scene.numLights; i++)
                color += throughput * MIS(g_payload.rngState, mat, eta, hitPos,
                                          g_lights[i], normalSide,
                                          g_payload.recursionDepth);
        }
    }

    // ---- Russian roulette ----
    float rrPdf = 1.0;
    if (g_payload.recursionDepth >= g_scene.russianRouletteDepth) {
        rrPdf = min(max(throughput.r, max(throughput.g, throughput.b)) + 0.001, 0.95);
        if (random(g_payload.rngState) > rrPdf)
            return color;
    }

    // ---- Indirect: sample BSDF ----
    vec3  L;
    float samplePdf, bsdfPdf;
    vec3  reflectance;

    bool aniso = g_scene.anisotropicBSDF != 0u;

    if (g_scene.importanceSamplingType == IS_UNIFORM) {
        vec3 T, B;
        ComputeLocalSpace(N, T, B);
        L = UniformSampleSphere(random(g_payload.rngState), random(g_payload.rngState), samplePdf);
        L = normalize(GetTangentToWorld(N, T, B, L));
        reflectance = EvaluateDisneyBSDF(mat, aniso, eta, -gl_WorldRayDirectionEXT, L, normalSide, bsdfPdf);
        bsdfPdf = samplePdf;
    } else if (g_scene.importanceSamplingType == IS_COSINE) {
        vec3 T, B;
        ComputeLocalSpace(N, T, B);
        L = CosineSampleHemisphere(random(g_payload.rngState), random(g_payload.rngState), samplePdf);
        L = normalize(GetTangentToWorld(N, T, B, L));
        reflectance = EvaluateDisneyBSDF(mat, aniso, eta, -gl_WorldRayDirectionEXT, L, normalSide, bsdfPdf);
        bsdfPdf = samplePdf;
    } else {
        reflectance = SampleDisneyBSDF(g_payload.rngState, mat, eta, aniso,
                                       -gl_WorldRayDirectionEXT, normalSide, L, bsdfPdf);
    }

    if (bsdfPdf > 0.0) {
        bool refraction = dot(normalSide, L) < 0.0;
        vec3 newAbsorption = absorption;
        if (refraction)
            newAbsorption = -log(mat.extinction) / mat.atDistance;

        throughput *= reflectance / bsdfPdf;
        throughput /= rrPdf;

        // Save and update payload for recursive ray
        uint  savedDepth    = g_payload.recursionDepth;
        uint  savedInside   = g_payload.inside;
        vec4  savedColor    = g_payload.color;
        float savedBsdfPdf  = g_payload.bsdfPdf;

        g_payload.throughput    = vec4(throughput, 1.0);
        g_payload.absorption    = vec4(newAbsorption, 1.0);
        g_payload.bsdfPdf       = bsdfPdf;
        g_payload.recursionDepth++;
        if (refraction) g_payload.inside ^= 1u;

        Ray bounceRay;
        bounceRay.origin    = hitPos;
        bounceRay.direction = L;

        if (g_payload.recursionDepth < g_scene.maxRecursionDepth) {
            traceRayEXT(g_tlas, gl_RayFlagsNoneEXT, 0xFF,
                        0, 2, 0, bounceRay.origin, 0.0001, bounceRay.direction, 10000.0, 0);
            g_payload.color = radianceClamp(g_payload.color);
            color += g_payload.color.xyz;
        }

        // Restore caller's state fields that survive recursion
        g_payload.recursionDepth = savedDepth;
        g_payload.inside         = savedInside;
        g_payload.bsdfPdf        = savedBsdfPdf;
        g_payload.color          = savedColor;
    }

    return color;
}

// ================================================================== //
// ClosestHitHelper — shared logic for triangle and AABB hits
// Only valid in closest-hit shaders (uses gl_HitTEXT).
// ================================================================== //
void ClosestHitHelper(GPUPBRMaterial mat, vec3 normal, vec3 hitPos, bool isTriangle) {
    g_payload.worldPosition = vec4(hitPos, 1.0);

    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);

    if (g_scene.raytracingType > 0u) {
        // Path tracing

        bool isLight = isTriangle && (mat.lightIndex >= 0);
        if (isLight) {
            int li = mat.lightIndex;
            // Only visible when ray hits from below (light emits downward)
            if (dot(vec3(0.0, 1.0, 0.0), gl_WorldRayDirectionEXT) > 0.0) {
                color.xyz = g_lights[li].emission * g_lights[li].intensity;
                if (g_payload.recursionDepth > 1u) {
                    LightSample ls;
                    SampleSquareLight(g_lights[li], hitPos, gl_WorldRayDirectionEXT, gl_HitTEXT, ls);
                    color.xyz *= g_payload.throughput.xyz *
                                 PowerHeuristic(g_payload.bsdfPdf, 1.0, ls.pdf, 1.0);
                }
            } else {
                color.xyz = vec3(0.0);
            }
        } else {
            color.xyz = mat.emission + DoPathTracing(mat, normal, hitPos, gl_HitTEXT);
        }
    }

    // Visibility distance falloff (atmospheric fog)
    float t = gl_HitTEXT;
    color = mix(color, ComputeBackground() * g_payload.throughput,
                1.0 - exp(-0.000002 * t * t * t));

    g_payload.color = color;
}
#endif // VLKRT_HIT_STAGE

#endif // PATH_TRACING_GLSL
