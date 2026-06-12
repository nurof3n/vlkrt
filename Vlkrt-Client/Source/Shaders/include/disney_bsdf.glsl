#ifndef DISNEY_BSDF_GLSL
#define DISNEY_BSDF_GLSL

#include "bsdf.glsl"

// ================================================================== //
// Disney BSDF — faithful port of DieXaR's DisneyBSDF.hlsli
// All evaluation and sampling is done in TANGENT SPACE where:
//   y = normal direction (hemisphere "up")
//   x = tangent T
//   z = bitangent B
// ================================================================== //

void ComputeAnisotropicAlphas(float roughness, float anisotropic, out float ax, out float ay) {
    float aspect = sqrt(1.0 - 0.9 * anisotropic);
    // Pass linear roughness so DGTR2Anisotropic / SmithG1Anisotropic square it
    // internally — matching the isotropic DGTR2 / SmithG1 convention.
    ax = max(0.0001, roughness / aspect);
    ay = max(0.0001, roughness * aspect);
}

float DisneyFresnelMix(float dotVH, float eta, float metallic) {
    float metallicFresnel   = FresnelReflectanceSchlickF(dotVH);
    float dielectricFresnel = FresnelDielectric(dotVH, eta);
    return mix(dielectricFresnel, metallicFresnel, metallic);
}

void ComputeSpecularColor(GPUPBRMaterial mat, float eta, out vec3 specularColor, out vec3 sheenColor) {
    float lum = GetLuminance(mat.albedo);
    vec3  tint = lum > 0.0 ? mat.albedo / lum : vec3(1.0);
    float R0   = R0FromIOR(eta);
    sheenColor   = mix(vec3(1.0), tint, mat.sheenTint);
    specularColor = mix(vec3(R0) * sheenColor, mat.albedo, mat.metallic);
}

// ------------------------------------------------------------------ //
// BSDF lobes (all in tangent space)
// ------------------------------------------------------------------ //

vec3 EvaluateClearcoat(GPUPBRMaterial mat, bool aniso,
                       vec3 V, vec3 L, vec3 H, out float pdf) {
    pdf = 0.0;
    if (L.y <= 0.0) return vec3(0.0);

    float dotVH = dot(V, H);
    float a = mix(0.1, 0.001, mat.clearcoatGloss);
    float D = DGTR1(H.y, a);
    float F = mix(0.04, 1.0, FresnelDielectric(dotVH, 0.6667));
    float G;
    if (aniso)
        G = SmithG1Anisotropic(L.x, L.z, L.y, 0.25, 0.25) *
            SmithG1Anisotropic(V.x, V.z, V.y, 0.25, 0.25);
    else
        G = SmithG1(L.y, 0.25) * SmithG1(V.y, 0.25);

    pdf = 0.25 * H.y * D / dotVH;
    return vec3(0.25) * mat.clearcoat * D * F * G / (4.0 * L.y * V.y);
}

vec3 EvaluateDiffuse(GPUPBRMaterial mat, vec3 sheenColor,
                     vec3 V, vec3 L, vec3 H, out float pdf) {
    pdf = 0.0;
    if (L.y <= 0.0) return vec3(0.0);

    float dotHL = dot(H, L);
    float fd90  = FD90(mat.roughness, dotHL);
    float d     = FD(fd90, V.y) * FD(fd90, L.y);
    float fss90 = FSS90(mat.roughness, dotHL);
    float Fss   = FD(fss90, V.y) * FD(fss90, L.y);
    float ss    = 1.25 * (Fss * (1.0 / (L.y + V.y) - 0.5) + 0.5);
    vec3  sh    = mat.sheen * FresnelReflectanceSchlickF(dotHL) * sheenColor;

    pdf = L.y * INV_PI;
    return (1.0 - mat.metallic) * (1.0 - mat.specularTransmission) *
           (INV_PI * mat.albedo * mix(d, ss, mat.subsurface) + sh);
}

vec3 EvaluateSpecularReflection(GPUPBRMaterial mat, float eta, vec3 specularColor,
                                vec3 V, vec3 L, vec3 H,
                                bool aniso, float ax, float ay, out float pdf) {
    pdf = 0.0;
    if (L.y <= 0.0) return vec3(0.0);

    float dotLH = dot(L, H);
    float dotVH = dot(V, H);
    float D = aniso ? DGTR2Anisotropic(H.x, H.z, H.y, ax, ay) : DGTR2(H.y, mat.roughness);
    vec3  F = mix(specularColor, vec3(1.0), DisneyFresnelMix(dotVH, eta, mat.metallic));
    float Gv = aniso ? SmithG1Anisotropic(V.x, V.z, V.y, ax, ay) : SmithG1(V.y, mat.roughness);
    float G  = Gv * (aniso ? SmithG1Anisotropic(L.x, L.z, L.y, ax, ay) : SmithG1(L.y, mat.roughness));

    pdf = 0.25 * Gv * max(0.0, dotVH) * D / (V.y * dotVH);
    return 0.25 * D * F * G / (L.y * V.y);
}

vec3 EvaluateSpecularTransmission(GPUPBRMaterial mat, float eta,
                                  vec3 V, vec3 L, vec3 H,
                                  bool aniso, float ax, float ay, out float pdf) {
    pdf = 0.0;
    if (L.y >= 0.0) return vec3(0.0);

    float dotLH = dot(L, H);
    float dotVH = dot(V, H);
    float tmp   = 1.0 / sq(abs(dotLH) + dotVH * eta);
    float D  = aniso ? DGTR2Anisotropic(H.x, H.z, H.y, ax, ay) : DGTR2(H.y, mat.roughness);
    float F  = FresnelDielectric(abs(dotVH), eta);
    float Gv = aniso ? SmithG1Anisotropic(V.x, V.z, V.y, ax, ay) : SmithG1(V.y, mat.roughness);
    // For transmitted rays L.y < 0 (lower hemisphere); pass abs(L.y) so the
    // isotropic SmithG1 (which uses dotWN directly) returns a positive G value,
    // matching SmithG1Anisotropic which squares dotWN internally.
    float G  = Gv * (aniso ? SmithG1Anisotropic(L.x, L.z, L.y, ax, ay) : SmithG1(abs(L.y), mat.roughness));

    pdf = Gv * max(0.0, dotVH) * D * tmp * abs(dotLH) / V.y;
    return (1.0 - mat.metallic) * mat.specularTransmission *
           sqrt(mat.albedo) * (1.0 - F) * D * G *
           abs(dotLH) * abs(dotVH) * sq(eta) * tmp / abs(L.y * V.y);
}

void ComputePdfs(GPUPBRMaterial mat, vec3 specularColor, float fresnelMix,
                 out float pSpecRefl, out float pDiff, out float pClearcoat, out float pSpecRefr) {
    float wDiff       = GetLuminance(mat.albedo) * (1.0 - mat.metallic) * (1.0 - mat.specularTransmission);
    float wSpecRefl   = GetLuminance(mix(specularColor, vec3(1.0), fresnelMix));
    float wSpecRefr   = GetLuminance(mat.albedo) * (1.0 - fresnelMix) * mat.specularTransmission * (1.0 - mat.metallic);
    float wClearcoat  = mat.clearcoat * (1.0 - mat.metallic);
    float total = wDiff + wSpecRefl + wClearcoat + wSpecRefr;
    pSpecRefl   = wSpecRefl   / total;
    pDiff       = wDiff       / total;
    pClearcoat  = wClearcoat  / total;
    pSpecRefr   = wSpecRefr   / total;
}

// ------------------------------------------------------------------ //
// Full Disney BSDF evaluation
// ------------------------------------------------------------------ //
vec3 EvaluateDisneyBSDF(GPUPBRMaterial mat, bool aniso,
                        float eta, vec3 V, vec3 L, vec3 N, out float pdf) {
    pdf = 0.0;

    vec3 T, B;
    ComputeLocalSpace(N, T, B);
    V = GetWorldToTangent(N, T, B, V);
    L = GetWorldToTangent(N, T, B, L);

    vec3 H = (L.y >= 0.0) ? normalize(L + V) : normalize(L + V * eta);
    if (H.y < 0.0) H = -H;

    float dotVH = dot(V, H);
    float ax = 0.0, ay = 0.0;
    if (aniso) ComputeAnisotropicAlphas(mat.roughness, mat.anisotropic, ax, ay);

    vec3 specColor, sheenColor;
    ComputeSpecularColor(mat, eta, specColor, sheenColor);

    float pDiff, pSpecRefl, pClearcoat, pSpecRefr;
    float fMix = DisneyFresnelMix(dotVH, eta, mat.metallic);
    ComputePdfs(mat, specColor, fMix, pSpecRefl, pDiff, pClearcoat, pSpecRefr);

    vec3  reflectance = vec3(0.0);
    float lobePdf;

    if (pDiff > 0.0 && L.y > 0.0) {
        reflectance += EvaluateDiffuse(mat, sheenColor, V, L, H, lobePdf);
        pdf += pDiff * lobePdf;
    }
    if (pSpecRefl > 0.0 && L.y > 0.0 && V.y > 0.0) {
        reflectance += EvaluateSpecularReflection(mat, eta, specColor, V, L, H, aniso, ax, ay, lobePdf);
        pdf += pSpecRefl * lobePdf;
    }
    if (pClearcoat > 0.0 && L.y > 0.0 && V.y > 0.0) {
        reflectance += EvaluateClearcoat(mat, aniso, V, L, H, lobePdf);
        pdf += pClearcoat * lobePdf;
    }
    if (pSpecRefr > 0.0 && L.y < 0.0) {
        reflectance += EvaluateSpecularTransmission(mat, eta, V, L, H, aniso, ax, ay, lobePdf);
        pdf += pSpecRefr * lobePdf;
    }

    return reflectance * abs(L.y);
}

// ------------------------------------------------------------------ //
// Full Disney BSDF sampling
// ------------------------------------------------------------------ //
vec3 SampleDisneyBSDF(inout uint rng_state, GPUPBRMaterial mat, float eta,
                      bool aniso, vec3 V, vec3 N, out vec3 L, out float pdf) {
    pdf = 0.0;

    float ax = 0.0, ay = 0.0;
    ComputeAnisotropicAlphas(mat.roughness, mat.anisotropic, ax, ay);

    vec3 T, B;
    ComputeLocalSpace(N, T, B);
    V = GetWorldToTangent(N, T, B, V);

    vec3 specColor, sheenColor;
    ComputeSpecularColor(mat, eta, specColor, sheenColor);

    float pDiff, pSpecRefl, pClearcoat, pSpecRefr;
    float fMix = DisneyFresnelMix(V.y, eta, mat.metallic);
    ComputePdfs(mat, specColor, fMix, pSpecRefl, pDiff, pClearcoat, pSpecRefr);

    float eps0   = random(rng_state);
    float eps1   = random(rng_state);
    float choice = random(rng_state);

    vec3  H;
    vec3  reflectance = vec3(0.0);

    float cdfDiff     = pDiff;
    float cdfSpec     = cdfDiff + pSpecRefl;
    float cdfClear    = cdfSpec + pClearcoat;

    if (choice < cdfDiff) {
        // Cosine-weighted diffuse
        L = CosineSampleHemisphere(eps0, eps1, pdf);
        H = normalize(V + L);
        reflectance = EvaluateDiffuse(mat, sheenColor, V, L, H, pdf);
        pdf *= pDiff;
    } else if (choice < cdfSpec) {
        // Specular reflection via VNDF
        H = aniso ? SampleVNDFAnisotropic(eps0, eps1, ax, ay, V)
                  : SampleVNDF(eps0, eps1, mat.roughness, V);
        if (H.y < 0.0) H = -H;
        L = normalize(reflect(-V, H));
        reflectance = EvaluateSpecularReflection(mat, eta, specColor, V, L, H, aniso, ax, ay, pdf);
        pdf *= pSpecRefl;
    } else if (choice < cdfClear) {
        // Clearcoat via GTR1
        float a = mix(0.1, 0.001, mat.clearcoatGloss);
        H = SampleDGTR1(eps0, eps1, a);
        if (H.y < 0.0) H = -H;
        L = normalize(reflect(-V, H));
        reflectance = EvaluateClearcoat(mat, aniso, V, L, H, pdf);
        pdf *= pClearcoat;
    } else {
        // Specular transmission
        H = aniso ? SampleVNDFAnisotropic(eps0, eps1, ax, ay, V)
                  : SampleVNDF(eps0, eps1, mat.roughness, V);
        if (H.y < 0.0) H = -H;
        L = refract(-V, H, eta);
        if (dot(L, L) == 0.0) { pdf = 0.0; L = vec3(0.0); return vec3(0.0); }
        L = normalize(L);
        reflectance = EvaluateSpecularTransmission(mat, eta, V, L, H, aniso, ax, ay, pdf);
        pdf *= pSpecRefr;
    }

    // Back to world space
    L = GetTangentToWorld(N, T, B, L);
    return reflectance * abs(dot(N, L));
}

#endif // DISNEY_BSDF_GLSL
