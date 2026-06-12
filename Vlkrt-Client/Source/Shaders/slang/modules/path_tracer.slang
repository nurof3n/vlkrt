// ================================================================== //
// path_tracer.slang — light sampling, MIS, DoPathTracing, ClosestHitHelper
// ================================================================== //

import common;
import bsdf;
import disney_bsdf;

// ------------------------------------------------------------------ //
// Light sampling structures
// ------------------------------------------------------------------ //
struct LightSample {
    float3 L;         ///< normalized direction to the light
    float3 emission;  ///< light emission × intensity [× area]
    float  dist;      ///< distance to the sampled point
    float  pdf;       ///< sampling PDF
};

float PowerHeuristic(float nf, float fPdf, float ng, float gPdf) {
    float f = nf * fPdf;
    float g = ng * gPdf;
    return sq(f) / (sq(f) + sq(g));
}

void MakeLightSampleSquare(GPULight light, float3 hitPos, float3 L, float dist,
                            out LightSample ls) {
    ls.L        = L;
    ls.dist     = dist;
    ls.emission = light.intensity * light.emission * sq(light.size);
    ls.pdf      = sq(dist) / dot(L, float3(0.0f, 1.0f, 0.0f));
}

// ------------------------------------------------------------------ //
// Shadow ray helper
// ------------------------------------------------------------------ //
bool TraceShadowRay(uint depth, float3 origin, float3 dir, float lightDist) {
    if (depth >= g_scene.maxShadowRecursionDepth) return false;

    ShadowRayPayload shadowPayload;
    shadowPayload.hit = true;

    RayDesc shadowRay;
    shadowRay.Origin    = origin;
    shadowRay.Direction = dir;
    shadowRay.TMin      = 0.0001f;
    shadowRay.TMax      = lightDist * 0.999999f;

    TraceRay(g_tlas,
             RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
             RAY_FLAG_FORCE_OPAQUE |
             RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             0xFF, 1, 2, 1, shadowRay, shadowPayload);
    return shadowPayload.hit;
}

// ------------------------------------------------------------------ //
// Next-Event Estimation (NEE)
// ------------------------------------------------------------------ //
bool NextEventEstimation(inout uint rngState, uint depth,
                          GPULight light, float3 hitPos, float3 N,
                          out LightSample ls) {
    ls = (LightSample)0;
    if (light.type == LIGHT_TYPE_SQUARE) {
        float2 eps      = float2(Random(rngState), Random(rngState));
        float3 samplePos = light.position +
                           float3(eps.x - 0.5f, 0.0f, eps.y - 0.5f) * light.size;
        ls.L    = samplePos - hitPos;
        ls.dist = length(ls.L);
        ls.L   /= ls.dist;
        float angleL = dot(ls.L, float3(0.0f, 1.0f, 0.0f));
        if (angleL <= 0.0f || dot(N, ls.L) <= 0.0f) return false;
        ls.emission = light.intensity * light.emission * sq(light.size);
        ls.pdf      = sq(ls.dist) / angleL;
    } else { // directional
        ls.L = -normalize(light.direction);
        if (dot(N, ls.L) <= 0.0f) return false;
        ls.dist     = 10000.0f;
        ls.emission = light.intensity * light.emission;
        ls.pdf      = 1.0f;
    }
    bool occluded = TraceShadowRay(depth, hitPos, ls.L, ls.dist);
    return !occluded;
}

// ------------------------------------------------------------------ //
// MIS: BSDF × light sample weight
// ------------------------------------------------------------------ //
float3 MIS_DirectLight(inout uint rngState, uint depth,
                        GPUPBRMaterial mat, float eta, bool aniso,
                        float3 hitPos, GPULight light, float3 N, float3 wo) {
    LightSample ls;
    if (!NextEventEstimation(rngState, depth, light, hitPos, N, ls))
        return float3(0.0f);

    DisneyBSDF bsdf = DisneyBSDF(mat, eta, aniso);
    float      bsdfPdf;
    float3     reflectance = bsdf.Evaluate(wo, ls.L, N, bsdfPdf);
    if (bsdfPdf <= 0.0f) return float3(0.0f);

    float weight = (light.type == LIGHT_TYPE_DIRECTIONAL) ? 1.0f
                 : PowerHeuristic(1.0f, ls.pdf, 1.0f, bsdfPdf);
    return reflectance * weight * ls.emission / ls.pdf;
}

// ------------------------------------------------------------------ //
// Core path-tracing step  (called from hit shaders)
// Accepts rayDir = WorldRayDirection() and hitDist = RayTCurrent()
// so the helper can be in a module without calling those built-ins directly.
// ------------------------------------------------------------------ //
float3 DoPathTracing(inout RayPayload payload,
                     GPUPBRMaterial   mat,
                     float3           N,
                     float3           hitPos,
                     float            hitDist,
                     float3           rayDir) {
    float3 normalSide = dot(rayDir, N) < 0.0f ? N : -N;

    // Checkerboard floor (materialIndex == 0 in Demo / PBR Showcase)
    if (mat.materialIndex == 0u &&
        (g_scene.sceneIndex == SCENE_DEMO || g_scene.sceneIndex == SCENE_PBR_SHOWCASE)) {
        float pattern = Checkerboard(hitPos);
        mat.roughness = pattern * 0.25f;
        mat.albedo    = pattern * 0.5f * mat.albedo + 0.5f * mat.albedo;
    }
    mat.roughness = max(0.001f, mat.roughness);

    float  eta        = (payload.inside != 0u) ? mat.eta : 1.0f / mat.eta;
    float3 absorption = (payload.inside != 0u) ? payload.absorption.xyz : float3(0.0f);
    float3 throughput = payload.throughput.xyz * exp(-absorption * hitDist);
    float3 wo         = -rayDir;
    bool   aniso      = g_scene.anisotropicBSDF != 0u;

    float3 color = float3(0.0f);

    // ---- Direct lighting (NEE + MIS) ----
    if (g_scene.sceneIndex != SCENE_PBR_SHOWCASE) {
        if (g_scene.onlyOneLightSample != 0u) {
            uint idx = uint(Random(payload.rngState) * float(g_scene.numLights));
            color += throughput *
                     MIS_DirectLight(payload.rngState, payload.recursionDepth,
                                     mat, eta, aniso, hitPos, g_lights[idx], normalSide, wo) *
                     float(g_scene.numLights);
        } else {
            for (uint i = 0u; i < g_scene.numLights; i++)
                color += throughput *
                         MIS_DirectLight(payload.rngState, payload.recursionDepth,
                                         mat, eta, aniso, hitPos, g_lights[i], normalSide, wo);
        }
    }

    // ---- Russian roulette ----
    float rrPdf = 1.0f;
    if (payload.recursionDepth >= g_scene.russianRouletteDepth) {
        rrPdf = min(max(throughput.r, max(throughput.g, throughput.b)) + 0.001f, 0.95f);
        if (Random(payload.rngState) > rrPdf)
            return color;
    }

    // ---- Indirect: sample BSDF ----
    float3 L;
    float  bsdfPdf;
    float3 reflectance;

    if (g_scene.importanceSamplingType == IS_UNIFORM) {
        float3 T, B;
        ComputeLocalSpace(N, T, B);
        float samplePdf;
        float3 Ls   = UniformSampleSphere(Random(payload.rngState), Random(payload.rngState), samplePdf);
        L           = normalize(TangentToWorld(N, T, B, Ls));
        DisneyBSDF bsdf = DisneyBSDF(mat, eta, aniso);
        bsdfPdf     = samplePdf;
        reflectance = bsdf.Evaluate(wo, L, normalSide, bsdfPdf);
        bsdfPdf     = samplePdf;
    } else if (g_scene.importanceSamplingType == IS_COSINE) {
        float3 T, B;
        ComputeLocalSpace(N, T, B);
        float samplePdf;
        float3 Ls   = CosineSampleHemisphere(Random(payload.rngState), Random(payload.rngState), samplePdf);
        L           = normalize(TangentToWorld(N, T, B, Ls));
        DisneyBSDF bsdf = DisneyBSDF(mat, eta, aniso);
        bsdfPdf     = samplePdf;
        reflectance = bsdf.Evaluate(wo, L, normalSide, bsdfPdf);
        bsdfPdf     = samplePdf;
    } else { // IS_BSDF
        DisneyBSDF bsdf = DisneyBSDF(mat, eta, aniso);
        reflectance = bsdf.Sample(payload.rngState, wo, normalSide, L, bsdfPdf);
    }

    // ---- Recursive bounce ----
    if (bsdfPdf > 0.0f && payload.recursionDepth + 1u < g_scene.maxRecursionDepth) {
        bool   refraction    = dot(normalSide, L) < 0.0f;
        float3 newAbsorption = absorption;
        if (refraction)
            newAbsorption = -log(mat.extinction) / mat.atDistance;

        float3 newThroughput = throughput * reflectance / bsdfPdf / rrPdf;

        // Build child payload (copy + update mutated fields)
        RayPayload childPayload    = payload;
        childPayload.throughput    = float4(newThroughput, 1.0f);
        childPayload.absorption    = float4(newAbsorption, 1.0f);
        childPayload.bsdfPdf       = bsdfPdf;
        childPayload.recursionDepth++;
        childPayload.inside        = refraction ? (payload.inside ^ 1u) : payload.inside;
        childPayload.color         = float4(0.0f);
        childPayload.worldPosition = float4(0.0f);

        RayDesc bounceRay;
        bounceRay.Origin    = hitPos;
        bounceRay.Direction = L;
        bounceRay.TMin      = 0.0001f;
        bounceRay.TMax      = 10000.0f;

        TraceRay(g_tlas, RAY_FLAG_NONE, 0xFF, 0, 2, 0, bounceRay, childPayload);
        childPayload.color = RadianceClamp(childPayload.color);
        color += childPayload.color.xyz;

        // Propagate RNG advancement back to the parent
        payload.rngState = childPayload.rngState;
    }

    return color;
}

// ------------------------------------------------------------------ //
// ClosestHitHelper — shared logic for triangle and AABB hit shaders
// hitDist   = RayTCurrent()
// rayDir    = WorldRayDirection()
// isTriangle — true for triangle mesh hits
// ------------------------------------------------------------------ //
void ClosestHitHelper(inout RayPayload payload,
                      GPUPBRMaterial   mat,
                      float3           normal,
                      float3           hitPos,
                      bool             isTriangle) {
    payload.worldPosition = float4(hitPos, 1.0f);

    float  hitDist = RayTCurrent();
    float3 rayDir  = WorldRayDirection();
    float4 color   = float4(0.0f, 0.0f, 0.0f, 1.0f);

    if (g_scene.raytracingType > 0u) {
        bool isLight = isTriangle && (mat.lightIndex >= 0);
        if (isLight) {
            int li = mat.lightIndex;
            if (dot(float3(0.0f, 1.0f, 0.0f), rayDir) > 0.0f) {
                color.xyz = g_lights[li].emission * float(g_lights[li].intensity);
                if (payload.recursionDepth > 1u) {
                    LightSample ls;
                    MakeLightSampleSquare(g_lights[li], hitPos, rayDir, hitDist, ls);
                    color.xyz *= payload.throughput.xyz *
                                 PowerHeuristic(payload.bsdfPdf, 1.0f, ls.pdf, 1.0f);
                }
            } else {
                color.xyz = float3(0.0f);
            }
        } else {
            color.xyz = mat.emission + DoPathTracing(payload, mat, normal, hitPos, hitDist, rayDir);
        }
    }

    // Atmospheric fog
    float t = hitDist;
    color = lerp(color,
                 ComputeBackground(rayDir) * payload.throughput,
                 1.0f - exp(-0.000002f * t * t * t));

    payload.color = color;
}
