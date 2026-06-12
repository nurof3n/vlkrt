#ifndef BSDF_GLSL
#define BSDF_GLSL

#include "common.glsl"

// ================================================================== //
// Hemisphere / sphere sampling
// (y is the "up" / normal direction in tangent space)
// ================================================================== //

vec3 UniformSampleHemisphere(float eps0, float eps1, out float pdf) {
    float sinTheta = sqrt(1.0 - eps0 * eps0);
    float phi = TWO_PI * eps1;
    pdf = 0.5 * INV_PI;
    return vec3(sinTheta * cos(phi), eps0, sinTheta * sin(phi));
}

vec3 UniformSampleSphere(float eps0, float eps1, out float pdf) {
    float cosTheta = 1.0 - 2.0 * eps0;
    float sinTheta = sqrt(1.0 - sq(cosTheta));
    float phi = TWO_PI * eps1;
    pdf = 0.25 * INV_PI;
    return vec3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

// Cosine-weighted hemisphere sampling
vec3 CosineSampleHemisphere(float eps0, float eps1, out float pdf) {
    float sinTheta = sqrt(eps0);
    float cosTheta = sqrt(1.0 - eps0);
    float phi = TWO_PI * eps1;
    pdf = INV_PI * cosTheta;
    return vec3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

// Visible Normal Distribution Function (VNDF) sampling for isotropic GGX
vec3 SampleVNDF(float eps0, float eps1, float roughness, vec3 V) {
    vec3 stretchedV = normalize(vec3(roughness * V.x, V.y, roughness * V.z));

    vec3 T, B;
    ComputeLocalSpace(stretchedV, T, B);

    float r   = sqrt(eps0);
    float phi = eps1 * TWO_PI;
    float p1  = r * cos(phi);
    float p2  = r * sin(phi);
    float s   = 0.5 * (1.0 + stretchedV.y);
    p2 = (1.0 - s) * sqrt(1.0 - sq(p1)) + s * p2;

    vec3 normal = p1 * T + p2 * B + sqrt(max(0.0, 1.0 - sq(p1) - sq(p2))) * stretchedV;
    return normalize(vec3(roughness * normal.x, max(0.0, normal.y), roughness * normal.z));
}

// VNDF sampling for anisotropic GGX
// https://hal.science/hal-01509746/document
vec3 SampleVNDFAnisotropic(float eps0, float eps1, float ax, float ay, vec3 V) {
    vec3 stretchedV = normalize(vec3(ax * V.x, V.y, ay * V.z));

    vec3 T, B;
    ComputeLocalSpace(stretchedV, T, B);

    float a   = 1.0 / (1.0 + stretchedV.y);
    float r   = sqrt(eps0);
    float phi = eps1 < a ? eps1 / a * PI : PI + (eps1 - a) / (1.0 - a) * PI;
    float p1  = r * cos(phi);
    float p2  = r * sin(phi) * (eps1 < a ? 1.0 : stretchedV.y);

    vec3 normal = p1 * T + p2 * B + sqrt(max(0.0, 1.0 - sq(p1) - sq(p2))) * stretchedV;
    return normalize(vec3(ax * normal.x, max(0.0, normal.y), ay * normal.z));
}

// ================================================================== //
// Fresnel terms
// ================================================================== //

float R0FromIOR(float ior) {
    return sq((1.0 - ior) / (1.0 + ior));
}

float FresnelDielectric(float cosThetaI, float eta) {
    float sinThetaTSq = sq(eta) * (1.0 - sq(cosThetaI));
    if (sinThetaTSq > 1.0) return 1.0;
    float cosThetaT = sqrt(max(0.0, 1.0 - sinThetaTSq));
    float Rs = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);
    float Rp = (cosThetaT - eta * cosThetaI) / (cosThetaT + eta * cosThetaI);
    return 0.5 * (sq(Rs) + sq(Rp));
}

// Schlick approximation (scalar)
float FresnelReflectanceSchlickF(float cosi) {
    return pow(clamp(1.0 - cosi, 0.0, 1.0), 5.0);
}

// Schlick approximation (vec3 f0, incident direction I and normal N)
vec3 FresnelReflectanceSchlick(vec3 I, vec3 N, vec3 f0) {
    return mix(vec3(1.0), vec3(FresnelReflectanceSchlickF(clamp(dot(-I, N), 0.0, 1.0))), f0);
}

float FresnelReflectanceSchlickS(vec3 I, vec3 N, float f0) {
    return mix(1.0, FresnelReflectanceSchlickF(clamp(dot(-I, N), 0.0, 1.0)), f0);
}

// Schlick with dot product already computed
vec3 FresnelReflectanceSchlickDot(float dotNL, vec3 f0) {
    return mix(vec3(1.0), vec3(FresnelReflectanceSchlickF(dotNL)), f0);
}

float FresnelReflectanceSchlickDotS(float dotNL, float f0) {
    return mix(1.0, FresnelReflectanceSchlickF(dotNL), f0);
}

float FD90(float roughness, float dotWH) {
    return 0.5 + 2.0 * roughness * sq(dotWH);
}

float FD(float fd90, float dotWN) {
    return mix(1.0, fd90, FresnelReflectanceSchlickF(dotWN));
}

float FSS90(float roughness, float dotHL) {
    return roughness * sq(dotHL);
}

// ================================================================== //
// Microfacet distribution (D term)
// ================================================================== //

// GTR1 — used for clear coat
float DGTR1(float dotNH, float a) {
    if (abs(a) >= 1.0) return INV_PI;
    float a2 = sq(a);
    return INV_PI * (a2 - 1.0) / (log(a2) * (1.0 + (a2 - 1.0) * sq(dotNH)));
}

vec3 SampleDGTR1(float eps0, float eps1, float roughness) {
    float a2  = sq(roughness);
    float phi = eps0 * TWO_PI;
    float cosTheta = sqrt((1.0 - pow(a2, 1.0 - eps1)) / (1.0 - a2));
    float sinTheta = clamp(sqrt(1.0 - sq(cosTheta)), 0.0, 1.0);
    return vec3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

// GTR2 (isotropic)
float DGTR2(float dotNH, float a) {
    float a2 = sq(a);
    float t  = 1.0 + (a2 - 1.0) * sq(dotNH);
    return INV_PI * a2 / sq(t);
}

vec3 SampleDGTR2(float eps0, float eps1, float roughness) {
    float a2  = sq(roughness);
    float phi = eps0 * TWO_PI;
    float cosTheta = sqrt((1.0 - eps1) / (1.0 + (a2 - 1.0) * eps1));
    float sinTheta = clamp(sqrt(1.0 - sq(cosTheta)), 0.0, 1.0);
    return vec3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

// GTR2 (anisotropic) — ax/ay are linear roughness values, matching DGTR2 convention.
float DGTR2Anisotropic(float dotHX, float dotHY, float dotHN, float ax, float ay) {
    return INV_PI / (ax * ay * sq(sq(dotHX / ax) + sq(dotHY / ay) + sq(dotHN)));
}

vec3 SampleDGTR2Anisotropic(float eps0, float eps1, float ax, float ay) {
    float phi    = eps0 * TWO_PI;
    float sinPhi = ay * sin(phi);
    float cosPhi = ax * cos(phi);
    float tanTheta = sqrt(eps1 / (1.0 - eps1));
    return vec3(tanTheta * cosPhi, 1.0, tanTheta * sinPhi);
}

// ================================================================== //
// Geometry / masking (G term) — Smith G1
// ================================================================== //

float SmithG1(float dotWN, float a) {
    if (dotWN == 0.0) return 0.0;
    float a2   = sq(a);
    float dot2 = sq(dotWN);
    return (2.0 * dotWN) / (dotWN + sqrt(a2 + dot2 - a2 * dot2));
}

// ax/ay are linear roughness values, squared internally.
float SmithG1Anisotropic(float dotWX, float dotWY, float dotWN, float ax, float ay) {
    float ax2 = sq(ax), ay2 = sq(ay);
    float inv_a2 = (sq(dotWX) * ax2 + sq(dotWY) * ay2) / sq(dotWN);
    float lambda = -0.5 + 0.5 * sqrt(1.0 + inv_a2);
    return 1.0 / (1.0 + lambda);
}

#endif // BSDF_GLSL
