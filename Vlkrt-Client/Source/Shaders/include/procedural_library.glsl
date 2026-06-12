#ifndef PROCEDURAL_LIBRARY_GLSL
#define PROCEDURAL_LIBRARY_GLSL

#include "analytic_primitives.glsl"
#include "sdf_primitives.glsl"
#include "sdf_fractals.glsl"

// ================================================================== //
// SDF dispatch — returns signed distance for the requested SDF type
// ================================================================== //
float GetDistanceFromSignedDistancePrimitive(vec3 pos, uint primitiveType) {
    if (primitiveType == SDF_INTERSECTED_ROUND_CUBE) {
        // opS(opS(udRoundBox, sdSphere_outer), -sdSphere_inner)
        return opS(opS(udRoundBox(pos, 0.75, 0.2), sdSphere(pos, 1.20)), -sdSphere(pos, 1.32));
    }
    else if (primitiveType == SDF_SQUARE_TORUS) {
        return sdTorus82(pos, vec2(0.75, 0.15));
    }
    else if (primitiveType == SDF_COG) {
        // Cog: torus with cylindrical teeth cut in
        vec3 repPos = opRep(
            vec3(atan(pos.z, pos.x) / 6.2831, 1.0, 0.015 + 0.25 * length(pos)) + 1.0,
            vec3(0.05, 1.0, 0.075)
        );
        return opS(
            sdTorus82(pos, vec2(0.60, 0.3)),
            sdCylinder(repPos, vec2(0.02, 0.8))
        );
    }
    else if (primitiveType == SDF_CYLINDER) {
        // Repeated cylinders intersected with a box
        vec3 rp = opRep(pos + vec3(1.0), vec3(1.0, 2.0, 1.0));
        return opI(
            sdCylinder(rp, vec2(0.3, 2.0)),
            sdBox(pos + vec3(1.0), vec3(2.0))
        );
    }
    else { // SDF_SOLID_ANGLE
        return sdSolidAngle(pos + vec3(0.0, 1.0, 0.0), 0.2 * vec2(3.0, 4.0), 0.4);
    }
}

// ================================================================== //
// SDF normal via tetrahedron finite differences
// ================================================================== //
vec3 sdCalculateNormal(vec3 p, uint primitiveType) {
    const vec2 e = vec2(0.0001, -0.0001);
    return normalize(
        e.xyy * GetDistanceFromSignedDistancePrimitive(p + e.xyy, primitiveType) +
        e.yyx * GetDistanceFromSignedDistancePrimitive(p + e.yyx, primitiveType) +
        e.yxy * GetDistanceFromSignedDistancePrimitive(p + e.yxy, primitiveType) +
        e.xxx * GetDistanceFromSignedDistancePrimitive(p + e.xxx, primitiveType)
    );
}

// ================================================================== //
// SDF ray march
// ================================================================== //
bool RaySignedDistancePrimitiveTest(Ray localRay, uint primitiveType,
                                    out float thit, out vec3 hitNormal,
                                    float stepScale) {
    const float threshold = 0.0001;
    const int   maxSteps  = 512;

    float t = gl_RayTminEXT;
    for (int i = 0; i < maxSteps; i++) {
        vec3  p = localRay.origin + t * localRay.direction;
        float d = stepScale * GetDistanceFromSignedDistancePrimitive(p, primitiveType);
        if (d <= threshold) {
            if (!IsInRange(t, gl_RayTminEXT, gl_RayTmaxEXT)) return false;
            thit      = t;
            hitNormal = sdCalculateNormal(p, primitiveType);
            return true;
        }
        t += d;
        if (t > gl_RayTmaxEXT) break;
    }
    return false;
}

#endif // PROCEDURAL_LIBRARY_GLSL
