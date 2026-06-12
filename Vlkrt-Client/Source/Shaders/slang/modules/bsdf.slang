// ================================================================== //
// bsdf.slang — sampling primitives, Fresnel, microfacet D and G terms
// ================================================================== //

import common;

// ------------------------------------------------------------------ //
// Hemisphere / sphere sampling
// ------------------------------------------------------------------ //

float3 UniformSampleHemisphere(float e0, float e1, out float pdf) {
    float sinTheta = sqrt(1.0f - e0 * e0);
    float phi = TWO_PI * e1;
    pdf = 0.5f * INV_PI;
    return float3(sinTheta * cos(phi), e0, sinTheta * sin(phi));
}

float3 UniformSampleSphere(float e0, float e1, out float pdf) {
    float cosTheta = 1.0f - 2.0f * e0;
    float sinTheta = sqrt(1.0f - sq(cosTheta));
    float phi = TWO_PI * e1;
    pdf = 0.25f * INV_PI;
    return float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

float3 CosineSampleHemisphere(float e0, float e1, out float pdf) {
    float sinTheta = sqrt(e0);
    float cosTheta = sqrt(1.0f - e0);
    float phi = TWO_PI * e1;
    pdf = INV_PI * cosTheta;
    return float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
}

/// VNDF sampling for isotropic GGX (roughness = linear alpha)
float3 SampleVNDF(float e0, float e1, float roughness, float3 V) {
    float3 sV = normalize(float3(roughness * V.x, V.y, roughness * V.z));

    float3 T, B;
    ComputeLocalSpace(sV, T, B);

    float r   = sqrt(e0);
    float phi = e1 * TWO_PI;
    float p1  = r * cos(phi);
    float p2  = r * sin(phi);
    float s   = 0.5f * (1.0f + sV.y);
    p2 = (1.0f - s) * sqrt(1.0f - sq(p1)) + s * p2;

    float3 n = p1 * T + p2 * B + sqrt(max(0.0f, 1.0f - sq(p1) - sq(p2))) * sV;
    return normalize(float3(roughness * n.x, max(0.0f, n.y), roughness * n.z));
}

/// VNDF sampling for anisotropic GGX (ax, ay = linear roughness)
/// https://hal.science/hal-01509746/document
float3 SampleVNDFAnisotropic(float e0, float e1, float ax, float ay, float3 V) {
    float3 sV = normalize(float3(ax * V.x, V.y, ay * V.z));

    float3 T, B;
    ComputeLocalSpace(sV, T, B);

    float a   = 1.0f / (1.0f + sV.y);
    float r   = sqrt(e0);
    float phi = e1 < a ? e1 / a * PI : PI + (e1 - a) / (1.0f - a) * PI;
    float p1  = r * cos(phi);
    float p2  = r * sin(phi) * (e1 < a ? 1.0f : sV.y);

    float3 n = p1 * T + p2 * B + sqrt(max(0.0f, 1.0f - sq(p1) - sq(p2))) * sV;
    return normalize(float3(ax * n.x, max(0.0f, n.y), ay * n.z));
}

// ------------------------------------------------------------------ //
// Fresnel terms
// ------------------------------------------------------------------ //

float R0FromIOR(float ior) { return sq((1.0f - ior) / (1.0f + ior)); }

float FresnelDielectric(float cosThetaI, float eta) {
    float sinTSq = sq(eta) * (1.0f - sq(cosThetaI));
    if (sinTSq > 1.0f) return 1.0f;
    float cosThetaT = sqrt(max(0.0f, 1.0f - sinTSq));
    float Rs = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);
    float Rp = (cosThetaT - eta * cosThetaI) / (cosThetaT + eta * cosThetaI);
    return 0.5f * (sq(Rs) + sq(Rp));
}

float SchlickF(float cosI) { return pow(clamp(1.0f - cosI, 0.0f, 1.0f), 5.0f); }

float3 FresnelSchlick(float3 I, float3 N, float3 f0) {
    return lerp(float3(1.0f), float3(SchlickF(clamp(dot(-I, N), 0.0f, 1.0f))), f0);
}

float FD90(float roughness, float dotWH)     { return 0.5f + 2.0f * roughness * sq(dotWH); }
float FD(float fd90, float dotWN)            { return lerp(1.0f, fd90, SchlickF(dotWN)); }
float FSS90(float roughness, float dotHL)    { return roughness * sq(dotHL); }

// ------------------------------------------------------------------ //
// Microfacet distribution (D term)
// ------------------------------------------------------------------ //

/// GTR1 — used for clear coat
float DGTR1(float dotNH, float a) {
    if (abs(a) >= 1.0f) return INV_PI;
    float a2 = sq(a);
    return INV_PI * (a2 - 1.0f) / (log(a2) * (1.0f + (a2 - 1.0f) * sq(dotNH)));
}

float3 SampleDGTR1(float e0, float e1, float roughness) {
    float a2  = sq(roughness);
    float phi = e0 * TWO_PI;
    float ct  = sqrt((1.0f - pow(a2, 1.0f - e1)) / (1.0f - a2));
    float st  = clamp(sqrt(1.0f - sq(ct)), 0.0f, 1.0f);
    return float3(st * cos(phi), ct, st * sin(phi));
}

/// GTR2 isotropic — alpha = linear roughness
float DGTR2(float dotNH, float a) {
    float a2 = sq(a);
    float t  = 1.0f + (a2 - 1.0f) * sq(dotNH);
    return INV_PI * a2 / sq(t);
}

float3 SampleDGTR2(float e0, float e1, float roughness) {
    float a2  = sq(roughness);
    float phi = e0 * TWO_PI;
    float ct  = sqrt((1.0f - e1) / (1.0f + (a2 - 1.0f) * e1));
    float st  = clamp(sqrt(1.0f - sq(ct)), 0.0f, 1.0f);
    return float3(st * cos(phi), ct, st * sin(phi));
}

/// GTR2 anisotropic — ax, ay = linear roughness values (NOT pre-squared)
float DGTR2Anisotropic(float dotHX, float dotHY, float dotHN, float ax, float ay) {
    return INV_PI / (ax * ay * sq(sq(dotHX / ax) + sq(dotHY / ay) + sq(dotHN)));
}

// ------------------------------------------------------------------ //
// Geometry / masking (G term) — Smith G1
// ------------------------------------------------------------------ //

/// Isotropic Smith G1 — alpha = linear roughness
float SmithG1(float dotWN, float a) {
    if (dotWN == 0.0f) return 0.0f;
    float a2   = sq(a);
    float dot2 = sq(dotWN);
    return (2.0f * dotWN) / (dotWN + sqrt(a2 + dot2 - a2 * dot2));
}

/// Anisotropic Smith G1 — ax, ay = linear roughness values
/// inv_a2 = (ax² · dotWX² + ay² · dotWY²) / dotWN²  — equivalent to isotropic when ax=ay
float SmithG1Anisotropic(float dotWX, float dotWY, float dotWN, float ax, float ay) {
    float ax2    = sq(ax);
    float ay2    = sq(ay);
    float inv_a2 = (sq(dotWX) * ax2 + sq(dotWY) * ay2) / sq(dotWN);
    float lambda = -0.5f + 0.5f * sqrt(1.0f + inv_a2);
    return 1.0f / (1.0f + lambda);
}
