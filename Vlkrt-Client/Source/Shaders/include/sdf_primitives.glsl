#ifndef SDF_PRIMITIVES_GLSL
#define SDF_PRIMITIVES_GLSL

#include "common.glsl"

// ================================================================== //
// SDF boolean operators
// ================================================================== //
float opS(float d1, float d2) { return max(d1, -d2); }
float opU(float d1, float d2) { return min(d1, d2);  }
float opI(float d1, float d2) { return max(d1, d2);  }

vec3  opRep(vec3 p, vec3 c) { return mod(p, c) - 0.5 * c; }

float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}
float smax(float a, float b, float k) {
    float h = clamp(0.5 - 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) + k * h * (1.0 - h);
}

float opBlendU(float d1, float d2, float k) { return smin(d1, d2, k); }
float opBlendI(float d1, float d2, float k) { return smax(d1, d2, k); }

vec3 opTwist(vec3 p, float k) {
    float c = cos(k * p.y);
    float s = sin(k * p.y);
    return vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
}

// ================================================================== //
// SDF primitives (all centered at origin in local space)
// ================================================================== //
float sdPlane(vec3 p)                   { return p.y; }
float sdSphere(vec3 p, float r)         { return length(p) - r; }
float sdBox(vec3 p, vec3 b)             { vec3 d = abs(p) - b; return min(max(d.x, max(d.y, d.z)), 0.0) + length(max(d, 0.0)); }
float sdEllipsoid(vec3 p, vec3 r)       { return (length(p / r) - 1.0) * min(min(r.x, r.y), r.z); }
float udRoundBox(vec3 p, float b, float r) { return length(max(abs(p) - vec3(b), 0.0)) - r; }
float sdTorus(vec3 p, vec2 t)           { return length(vec2(length(p.xz) - t.x, p.y)) - t.y; }
float sdTorus82(vec3 p, vec2 t)         { vec2 q = vec2(length(p.xz) - t.x, p.y); return length(q) - t.y; }
float sdTorus88(vec3 p, vec2 t)         {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return pow(pow(abs(q.x), 8.0) + pow(abs(q.y), 8.0), 0.125) - t.y;
}
float sdHexPrism(vec3 p, vec2 h) {
    vec3 q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025 + q.y * 0.5, q.y) - h.x);
}
float sdCapsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}
float sdEquilateralTriangle(vec2 p) {
    const float k = 1.732050808;
    p.x = abs(p.x) - 1.0;
    p.y = p.y + 1.0 / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -2.0, 0.0);
    return -length(p) * sign(p.y);
}
float sdTriPrism(vec3 p, vec2 h) {
    vec3 q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025 + p.y * 0.5, -p.y) - h.x * 0.5);
}
float sdCylinder(vec3 p, vec2 h)        { return max(length(p.xz) - h.x, abs(p.y) - h.y); }
float sdCylinder6(vec3 p, vec2 h) {
    return max(dot(abs(p.xz), normalize(vec2(1.0, 1.732050808))) - h.x, abs(p.y) - h.y);
}
float sdCone(vec3 p, vec2 c) {
    float q = length(p.xy);
    return dot(c, vec2(q, p.z));
}
float sdConeSection(vec3 p, float h, float r1, float r2) {
    float d1 = -p.y - h;
    float q  = p.y - h;
    float si = 0.5 * (r1 - r2) / h;
    float d2 = max(sqrt(dot(p.xz, p.xz) * (1.0 - si * si)) + q * si - r2, q);
    return length(max(vec2(d1, d2), 0.0)) + min(max(d1, d2), 0.0);
}
float sdOctahedron(vec3 p, float s) {
    p = abs(p);
    float m = p.x + p.y + p.z - s;
    vec3 q;
    if (3.0 * p.x < m) q = p.xyz;
    else if (3.0 * p.y < m) q = p.yzx;
    else if (3.0 * p.z < m) q = p.zxy;
    else return m * 0.57735027;
    float k = clamp(0.5 * (q.z - q.y + s), 0.0, s);
    return length(vec3(q.x, q.y - s + k, q.z - k));
}
float sdPyramid(vec3 p, float h) {
    float m2 = h * h + 0.25;
    p.xz = abs(p.xz);
    p.xz = (p.z > p.x) ? p.zx : p.xz;
    p.xz -= 0.5;
    vec3 q = vec3(p.z, h * p.y - 0.5 * p.x, h * p.x + 0.5 * p.y);
    float s = max(-q.x, 0.0);
    float t = clamp((q.y - 0.5 * p.z) / (m2 + 0.25), 0.0, 1.0);
    float a = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
    float b = m2 * (q.x + 0.5 * t) * (q.x + 0.5 * t) + (q.y - m2 * t) * (q.y - m2 * t);
    float d = (min(q.y, -q.x * m2 - q.y * 0.5) > 0.0) ? 0.0 : min(a, b);
    return sqrt((d + q.z * q.z) / m2) * sign(max(q.z, -p.y));
}
float sdSolidAngle(vec3 p, vec2 c, float ra) {
    vec2 q = vec2(length(p.xz), p.y);
    float l = length(q) - ra;
    float m = length(q - c * clamp(dot(q, c), 0.0, ra));
    return max(l, m * sign(c.y * q.x - c.x * q.y));
}

// ================================================================== //
// SDF normal (tetrahedron finite-differences)
// ================================================================== //
// Forward declaration — uses GetDistanceFromSignedDistancePrimitive defined in procedural_library.glsl
// We include the normal calculation inline in procedural_library.glsl instead.

// ================================================================== //
// Ray march against a signed distance field
// Returns true if a hit is found, sets thit and hitNormal.
// The hitNormal is computed via finite differences on the SDF.
// ================================================================== //
// NOTE: The actual SDF dispatch (GetDistanceFromSignedDistancePrimitive) is
// defined in procedural_library.glsl which includes this file. The RaySignedDistancePrimitiveTest
// function is therefore also defined there, after the SDF dispatch function.

#endif // SDF_PRIMITIVES_GLSL
