// ================================================================== //
// disney_bsdf.slang — Disney BSDF evaluation and sampling
//
// Faithful port of DieXaR's DisneyBSDF.hlsli, refactored as a Slang
// struct that implements the IBxDF interface.
//
// Tangent-space convention (DieXaR):
//   y = surface normal   x = tangent T   z = bitangent B
// ================================================================== //

import common;
import bsdf;

// ------------------------------------------------------------------ //
// IBxDF — interface for all material models
// ------------------------------------------------------------------ //
interface IBxDF {
    /// Evaluate the BSDF for a fixed (wo, wi) pair and return the pdf.
    float3 Evaluate(float3 wo, float3 wi, float3 N, out float pdf);
    /// Importance-sample the BSDF, returning the sampled wi and pdf.
    float3 Sample(inout uint rng, float3 wo, float3 N, out float3 wi, out float pdf, out bool isDiffuseLobe);
}

// ------------------------------------------------------------------ //
// Helper: compute anisotropic alpha values from linear roughness
// ------------------------------------------------------------------ //
void ComputeAnisotropicAlphas(float roughness, float anisotropic,
                               out float ax, out float ay) {
    float aspect = sqrt(1.0f - 0.9f * anisotropic);
    // Pass linear roughness — DGTR2Anisotropic / SmithG1Anisotropic expect it
    ax = max(0.0001f, roughness / aspect);
    ay = max(0.0001f, roughness * aspect);
}

// ------------------------------------------------------------------ //
// Disney BSDF struct
// ------------------------------------------------------------------ //
struct DisneyBSDF : IBxDF {
    GPUPBRMaterial mat;
    float          eta;       ///< IOR (may be inverted for refractive media)
    bool           aniso;

    // Cached per-instance values computed once in the constructor
    float  ax, ay;            ///< anisotropic alpha
    float3 specColor;
    float3 sheenColor;

    /// Construct from a material, IOR, and anisotropic flag.
    __init(GPUPBRMaterial _mat, float _eta, bool _aniso) {
        mat  = _mat;
        eta  = _eta;
        aniso = _aniso;
        ComputeAnisotropicAlphas(mat.roughness, mat.anisotropic, ax, ay);
        _ComputeSpecularColor(specColor, sheenColor);
    }

    // ----------------------------------------------------------------
    // Private helpers
    // ----------------------------------------------------------------

    float _FresnelMix(float dotVH) {
        float mF = SchlickF(dotVH);
        float dF = FresnelDielectric(dotVH, eta);
        return lerp(dF, mF, mat.metallic);
    }

    void _ComputeSpecularColor(out float3 sc, out float3 shc) {
        float lum = GetLuminance(mat.albedo);
        float3 tint = lum > 0.0f ? mat.albedo / lum : float3(1.0f);
        float  R0   = R0FromIOR(eta);
        shc = lerp(float3(1.0f), tint, mat.sheenTint);
        sc  = lerp(float3(R0) * shc, mat.albedo, mat.metallic);
    }

    void _ComputePdfs(float fresnelMix,
                      out float pSpecRefl, out float pDiff,
                      out float pClearcoat, out float pSpecRefr) {
        float wDiff      = GetLuminance(mat.albedo) * (1.0f - mat.metallic) * (1.0f - mat.specularTransmission);
        float wSpecRefl  = GetLuminance(lerp(specColor, float3(1.0f), fresnelMix));
        float wSpecRefr  = GetLuminance(mat.albedo) * (1.0f - fresnelMix) * mat.specularTransmission * (1.0f - mat.metallic);
        float wClearcoat = mat.clearcoat * (1.0f - mat.metallic);
        float total      = wDiff + wSpecRefl + wClearcoat + wSpecRefr;
        pSpecRefl   = wSpecRefl   / total;
        pDiff       = wDiff       / total;
        pClearcoat  = wClearcoat  / total;
        pSpecRefr   = wSpecRefr   / total;
    }

    // ----------------------------------------------------------------
    // Lobe evaluation (tangent space)
    // ----------------------------------------------------------------

    float3 _EvalClearcoat(float3 V, float3 L, float3 H, out float pdf) {
        pdf = 0.0f;
        if (L.y <= 0.0f) return float3(0.0f);
        float dotVH = dot(V, H);
        float a     = lerp(0.1f, 0.001f, mat.clearcoatGloss);
        float D     = DGTR1(H.y, a);
        float F     = lerp(0.04f, 1.0f, FresnelDielectric(dotVH, 0.6667f));
        float G     = aniso
            ? SmithG1Anisotropic(L.x, L.z, L.y, 0.25f, 0.25f) *
              SmithG1Anisotropic(V.x, V.z, V.y, 0.25f, 0.25f)
            : SmithG1(L.y, 0.25f) * SmithG1(V.y, 0.25f);
        pdf = 0.25f * H.y * D / dotVH;
        return float3(0.25f) * mat.clearcoat * D * F * G / (4.0f * L.y * V.y);
    }

    float3 _EvalDiffuse(float3 V, float3 L, float3 H, out float pdf) {
        pdf = 0.0f;
        if (L.y <= 0.0f) return float3(0.0f);
        float dotHL = dot(H, L);
        float fd90  = FD90(mat.roughness, dotHL);
        float d     = FD(fd90, V.y) * FD(fd90, L.y);
        float fss90 = FSS90(mat.roughness, dotHL);
        float Fss   = FD(fss90, V.y) * FD(fss90, L.y);
        float ss    = 1.25f * (Fss * (1.0f / (L.y + V.y) - 0.5f) + 0.5f);
        float3 sh   = mat.sheen * SchlickF(dotHL) * sheenColor;
        pdf = L.y * INV_PI;
        return (1.0f - mat.metallic) * (1.0f - mat.specularTransmission) *
               (INV_PI * mat.albedo * lerp(d, ss, mat.subsurface) + sh);
    }

    float3 _EvalSpecRefl(float3 V, float3 L, float3 H, out float pdf) {
        pdf = 0.0f;
        if (L.y <= 0.0f) return float3(0.0f);
        float dotLH = dot(L, H);
        float dotVH = dot(V, H);
        float D  = aniso ? DGTR2Anisotropic(H.x, H.z, H.y, ax, ay)
                         : DGTR2(H.y, mat.roughness);
        float3 F = lerp(specColor, float3(1.0f), _FresnelMix(dotVH));
        float Gv = aniso ? SmithG1Anisotropic(V.x, V.z, V.y, ax, ay)
                         : SmithG1(V.y, mat.roughness);
        float G  = Gv * (aniso ? SmithG1Anisotropic(L.x, L.z, L.y, ax, ay)
                               : SmithG1(L.y, mat.roughness));
        pdf = 0.25f * Gv * max(0.0f, dotVH) * D / (V.y * dotVH);
        return 0.25f * D * F * G / (L.y * V.y);
    }

    float3 _EvalSpecRefr(float3 V, float3 L, float3 H, out float pdf) {
        pdf = 0.0f;
        if (L.y >= 0.0f) return float3(0.0f);
        float dotLH = dot(L, H);
        float dotVH = dot(V, H);
        float tmp   = 1.0f / sq(abs(dotLH) + dotVH * eta);
        float D     = aniso ? DGTR2Anisotropic(H.x, H.z, H.y, ax, ay)
                            : DGTR2(H.y, mat.roughness);
        float F     = FresnelDielectric(abs(dotVH), eta);
        float Gv    = aniso ? SmithG1Anisotropic(V.x, V.z, V.y, ax, ay)
                            : SmithG1(V.y, mat.roughness);
        // Use abs(L.y) for the transmitted direction — L.y < 0 in lower hemisphere;
        // SmithG1Anisotropic is immune (uses sq), but SmithG1 would return negative.
        float G = Gv * (aniso ? SmithG1Anisotropic(L.x, L.z, L.y, ax, ay)
                              : SmithG1(abs(L.y), mat.roughness));
        pdf = Gv * max(0.0f, dotVH) * D * tmp * abs(dotLH) / V.y;
        return (1.0f - mat.metallic) * mat.specularTransmission *
               sqrt(mat.albedo) * (1.0f - F) * D * G *
               abs(dotLH) * abs(dotVH) * sq(eta) * tmp / abs(L.y * V.y);
    }

    // ----------------------------------------------------------------
    // IBxDF implementation
    // ----------------------------------------------------------------

    float3 Evaluate(float3 wo, float3 wi, float3 N, out float pdf) {
        pdf = 0.0f;
        float3 T, B;
        ComputeLocalSpace(N, T, B);
        float3 V = WorldToTangent(N, T, B, wo);
        float3 L = WorldToTangent(N, T, B, wi);

        float3 H = L.y >= 0.0f ? normalize(L + V) : normalize(L + V * eta);
        if (H.y < 0.0f) H = -H;

        float dotVH = dot(V, H);
        float fMix  = _FresnelMix(dotVH);

        float pDiff, pSpecRefl, pClearcoat, pSpecRefr;
        _ComputePdfs(fMix, pSpecRefl, pDiff, pClearcoat, pSpecRefr);

        float3 result = float3(0.0f);
        float  lobePdf;

        if (pDiff > 0.0f && L.y > 0.0f) {
            result += _EvalDiffuse(V, L, H, lobePdf);
            pdf += pDiff * lobePdf;
        }
        if (pSpecRefl > 0.0f && L.y > 0.0f && V.y > 0.0f) {
            result += _EvalSpecRefl(V, L, H, lobePdf);
            pdf += pSpecRefl * lobePdf;
        }
        if (pClearcoat > 0.0f && L.y > 0.0f && V.y > 0.0f) {
            result += _EvalClearcoat(V, L, H, lobePdf);
            pdf += pClearcoat * lobePdf;
        }
        if (pSpecRefr > 0.0f && L.y < 0.0f) {
            result += _EvalSpecRefr(V, L, H, lobePdf);
            pdf += pSpecRefr * lobePdf;
        }
        return result * abs(L.y);
    }

    float3 Sample(inout uint rng, float3 wo, float3 N, out float3 wi, out float pdf, out bool isDiffuseLobe) {
        pdf = 0.0f;
        wi  = float3(0.0f);
        isDiffuseLobe = false;

        float3 T, B;
        ComputeLocalSpace(N, T, B);
        float3 V = WorldToTangent(N, T, B, wo);

        float fMix = _FresnelMix(V.y);
        float pDiff, pSpecRefl, pClearcoat, pSpecRefr;
        _ComputePdfs(fMix, pSpecRefl, pDiff, pClearcoat, pSpecRefr);

        // Draw lobe choice first — the first PCG output from nearby seeds
        // can carry low-order correlation; using it for the scalar lobe
        // selector (which doesn't drive spatial structure) and deferring
        // the 2D VNDF pair to outputs 2+3 eliminates ring artefacts on
        // specular surfaces.
        float choice = Random(rng);
        float e0     = Random(rng);
        float e1     = Random(rng);

        float3 H;
        float3 result = float3(0.0f);

        float cdfDiff  = pDiff;
        float cdfSpec  = cdfDiff + pSpecRefl;
        float cdfClear = cdfSpec + pClearcoat;

        float3 L;
        float  lobePdf;

        if (choice < cdfDiff) {
            // Cosine-weighted diffuse
            L      = CosineSampleHemisphere(e0, e1, pdf); // pdf = sampling pdf
            H      = normalize(V + L);
            result = _EvalDiffuse(V, L, H, lobePdf);
            pdf   *= pDiff;
            isDiffuseLobe = true;
        }
        else if (choice < cdfSpec) {
            // Specular reflection — VNDF
            H = aniso ? SampleVNDFAnisotropic(e0, e1, ax, ay, V)
                      : SampleVNDF(e0, e1, mat.roughness, V);
            if (H.y < 0.0f) H = -H;
            L      = normalize(reflect(-V, H));
            result = _EvalSpecRefl(V, L, H, lobePdf);
            pdf    = lobePdf * pSpecRefl; // was: 0 * pSpecRefl
        }
        else if (choice < cdfClear) {
            // Clearcoat — GTR1
            float a = lerp(0.1f, 0.001f, mat.clearcoatGloss);
            H = SampleDGTR1(e0, e1, a);
            if (H.y < 0.0f) H = -H;
            L      = normalize(reflect(-V, H));
            result = _EvalClearcoat(V, L, H, lobePdf);
            pdf    = lobePdf * pClearcoat; // was: 0 * pClearcoat
        }
        else {
            // Specular transmission — VNDF + refraction
            H = aniso ? SampleVNDFAnisotropic(e0, e1, ax, ay, V)
                      : SampleVNDF(e0, e1, mat.roughness, V);
            if (H.y < 0.0f) H = -H;
            L = refract(-V, H, eta);
            if (dot(L, L) == 0.0f) { pdf = 0.0f; return float3(0.0f); }
            L      = normalize(L);
            result = _EvalSpecRefr(V, L, H, lobePdf);
            pdf    = lobePdf * pSpecRefr; // was: 0 * pSpecRefr
        }

        wi = TangentToWorld(N, T, B, L);
        return result * abs(dot(N, wi));
    }
}
