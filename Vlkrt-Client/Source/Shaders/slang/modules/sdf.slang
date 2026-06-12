// ================================================================== //
// sdf.slang — SDF primitives, fractals, normal estimation, ray marcher
// ================================================================== //

import common;

// ------------------------------------------------------------------ //
// Boolean operators
// ------------------------------------------------------------------ //
float opS(float d1, float d2) { return max(d1, -d2); }
float opU(float d1, float d2) { return min(d1,  d2); }
float opI(float d1, float d2) { return max(d1,  d2); }

float3 opRep(float3 p, float3 c) { return fmod(p, c) - 0.5f * c; }

float smin(float a, float b, float k) {
    float h = clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return lerp(b, a, h) - k * h * (1.0f - h);
}
float smax(float a, float b, float k) {
    float h = clamp(0.5f - 0.5f * (b - a) / k, 0.0f, 1.0f);
    return lerp(b, a, h) + k * h * (1.0f - h);
}
float opBlendU(float d1, float d2, float k) { return smin(d1, d2, k); }
float opBlendI(float d1, float d2, float k) { return smax(d1, d2, k); }

float3 opTwist(float3 p, float k) {
    float c = cos(k * p.y);
    float s = sin(k * p.y);
    return float3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
}

// ------------------------------------------------------------------ //
// SDF primitives
// ------------------------------------------------------------------ //
float sdPlane(float3 p)                    { return p.y; }
float sdSphere(float3 p, float r)          { return length(p) - r; }

float sdBox(float3 p, float3 b) {
    float3 d = abs(p) - b;
    return min(max(d.x, max(d.y, d.z)), 0.0f) + length(max(d, 0.0f));
}

float sdEllipsoid(float3 p, float3 r) {
    return (length(p / r) - 1.0f) * min(min(r.x, r.y), r.z);
}

float udRoundBox(float3 p, float b, float r) {
    return length(max(abs(p) - float3(b, b, b), 0.0f)) - r;
}

float sdTorus(float3 p, float2 t) {
    return length(float2(length(p.xz) - t.x, p.y)) - t.y;
}

float sdTorus82(float3 p, float2 t) {
    float2 q = float2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float sdTorus88(float3 p, float2 t) {
    float2 q = float2(length(p.xz) - t.x, p.y);
    return pow(pow(abs(q.x), 8.0f) + pow(abs(q.y), 8.0f), 0.125f) - t.y;
}

float sdHexPrism(float3 p, float2 h) {
    float3 q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025f + q.y * 0.5f, q.y) - h.x);
}

float sdCapsule(float3 p, float3 a, float3 b, float r) {
    float3 pa = p - a, ba = b - a;
    float  h  = clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
    return length(pa - ba * h) - r;
}

float sdEquilateralTriangle(float2 p) {
    const float k = 1.732050808f;
    p.x = abs(p.x) - 1.0f;
    p.y = p.y + 1.0f / k;
    if (p.x + k * p.y > 0.0f) p = float2(p.x - k * p.y, -k * p.x - p.y) * 0.5f;
    p.x -= clamp(p.x, -2.0f, 0.0f);
    return -length(p) * sign(p.y);
}

float sdTriPrism(float3 p, float2 h) {
    float3 q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025f + p.y * 0.5f, -p.y) - h.x * 0.5f);
}

float sdCylinder(float3 p, float2 h) {
    return max(length(p.xz) - h.x, abs(p.y) - h.y);
}

float sdCylinder6(float3 p, float2 h) {
    return max(dot(abs(p.xz), normalize(float2(1.0f, 1.732050808f))) - h.x, abs(p.y) - h.y);
}

float sdCone(float3 p, float2 c) {
    return dot(c, float2(length(p.xy), p.z));
}

float sdConeSection(float3 p, float h, float r1, float r2) {
    float d1 = -p.y - h;
    float q  = p.y - h;
    float si = 0.5f * (r1 - r2) / h;
    float d2 = max(sqrt(dot(p.xz, p.xz) * (1.0f - si * si)) + q * si - r2, q);
    return length(max(float2(d1, d2), 0.0f)) + min(max(d1, d2), 0.0f);
}

float sdOctahedron(float3 p, float s) {
    p = abs(p);
    float m = p.x + p.y + p.z - s;
    float3 q;
    if (3.0f * p.x < m) q = p.xyz;
    else if (3.0f * p.y < m) q = p.yzx;
    else if (3.0f * p.z < m) q = p.zxy;
    else return m * 0.57735027f;
    float k = clamp(0.5f * (q.z - q.y + s), 0.0f, s);
    return length(float3(q.x, q.y - s + k, q.z - k));
}

float sdPyramid(float3 p, float h) {
    float m2 = h * h + 0.25f;
    p.xz = abs(p.xz);
    p.xz = (p.z > p.x) ? p.zx : p.xz;
    p.xz -= 0.5f;
    float3 q = float3(p.z, h * p.y - 0.5f * p.x, h * p.x + 0.5f * p.y);
    float  s = max(-q.x, 0.0f);
    float  t = clamp((q.y - 0.5f * p.z) / (m2 + 0.25f), 0.0f, 1.0f);
    float  a = m2 * sq(q.x + s) + sq(q.y);
    float  b = m2 * sq(q.x + 0.5f * t) + sq(q.y - m2 * t);
    float  d = (min(q.y, -q.x * m2 - q.y * 0.5f) > 0.0f) ? 0.0f : min(a, b);
    return sqrt((d + sq(q.z)) / m2) * sign(max(q.z, -p.y));
}

float sdSolidAngle(float3 p, float2 c, float ra) {
    float2 q = float2(length(p.xz), p.y);
    float  l = length(q) - ra;
    float  m = length(q - c * clamp(dot(q, c), 0.0f, ra));
    return max(l, m * sign(c.y * q.x - c.x * q.y));
}

// ------------------------------------------------------------------ //
// Fractal pyramid SDF
// ------------------------------------------------------------------ //
float sdFractalPyramid(float3 p, float s) {
    float scale = 1.0f;
    for (int i = 0; i < N_FRACTAL_ITERATIONS; i++) {
        if (p.x + p.y < 0.0f) { float tmp = p.x; p.x = -p.y; p.y = -tmp; }
        if (p.x + p.z < 0.0f) { float tmp = p.x; p.x = -p.z; p.z = -tmp; }
        if (p.y + p.z < 0.0f) { float tmp = p.y; p.y = -p.z; p.z = -tmp; }
        p     = p * 2.0f - s * (2.0f - 1.0f);
        scale *= 2.0f;
    }
    return (length(p) - 2.0f) / scale;
}

// ------------------------------------------------------------------ //
// SDF dispatch
// ------------------------------------------------------------------ //
float SDFDispatch(float3 pos, uint primitiveType) {
    if (primitiveType == SDF_INTERSECTED_ROUND_CUBE)
        return opS(opS(udRoundBox(pos, 0.75f, 0.2f), sdSphere(pos, 1.20f)), -sdSphere(pos, 1.32f));
    if (primitiveType == SDF_SQUARE_TORUS)
        return sdTorus82(pos, float2(0.75f, 0.15f));
    if (primitiveType == SDF_COG) {
        float3 repPos = opRep(
            float3(atan2(pos.z, pos.x) / 6.2831f, 1.0f, 0.015f + 0.25f * length(pos)) + 1.0f,
            float3(0.05f, 1.0f, 0.075f)
        );
        return opS(sdTorus82(pos, float2(0.60f, 0.3f)),
                   sdCylinder(repPos, float2(0.02f, 0.8f)));
    }
    if (primitiveType == SDF_CYLINDER) {
        float3 rp = opRep(pos + float3(1.0f), float3(1.0f, 2.0f, 1.0f));
        return opI(sdCylinder(rp, float2(0.3f, 2.0f)), sdBox(pos + float3(1.0f), float3(2.0f)));
    }
    // SDF_SOLID_ANGLE
    return sdSolidAngle(pos + float3(0.0f, 1.0f, 0.0f), 0.2f * float2(3.0f, 4.0f), 0.4f);
}

// ------------------------------------------------------------------ //
// Normal estimation — tetrahedron FD
// ------------------------------------------------------------------ //
float3 SDFNormal(float3 p, uint primitiveType) {
    const float2 e = float2(0.0001f, -0.0001f);
    return normalize(
        e.xyy * SDFDispatch(p + e.xyy, primitiveType) +
        e.yyx * SDFDispatch(p + e.yyx, primitiveType) +
        e.yxy * SDFDispatch(p + e.yxy, primitiveType) +
        e.xxx * SDFDispatch(p + e.xxx, primitiveType)
    );
}

// ------------------------------------------------------------------ //
// Ray marcher
// ------------------------------------------------------------------ //
bool RaySDFTest(Ray localRay, uint primitiveType, float stepScale,
                out float thit, out float3 hitNormal) {
    thit = 0.0f; hitNormal = float3(0.0f, 1.0f, 0.0f);
    const float threshold = 0.0001f;
    const int   maxSteps  = 512;

    float tMin = RayTMin();
    float tMax = RayTCurrent();
    float t    = tMin;

    for (int i = 0; i < maxSteps; i++) {
        float3 p = localRay.origin + t * localRay.direction;
        float  d = stepScale * SDFDispatch(p, primitiveType);
        if (d <= threshold) {
            if (!IsInRange(t, tMin, tMax)) return false;
            thit      = t;
            hitNormal = SDFNormal(p, primitiveType);
            return true;
        }
        t += d;
        if (t > tMax) break;
    }
    return false;
}
