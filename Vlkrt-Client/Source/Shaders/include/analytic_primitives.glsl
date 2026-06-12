#ifndef ANALYTIC_PRIMITIVES_GLSL
#define ANALYTIC_PRIMITIVES_GLSL

#include "common.glsl"

// ================================================================== //
// Quadratic solver helper
// ================================================================== //
bool SolveQuadraticEqn(float a, float b, float c, out float t0, out float t1) {
    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return false;
    float sqrtDisc = sqrt(disc);
    t0 = (-b - sqrtDisc) / (2.0 * a);
    t1 = (-b + sqrtDisc) / (2.0 * a);
    return true;
}

// ================================================================== //
// Sphere intersection
// ================================================================== //
vec3 CalculateNormalForARaySphereHit(Ray ray, float thit, vec3 center) {
    return normalize(ray.origin + thit * ray.direction - center);
}

bool SolveRaySphereIntersectionEquation(Ray ray, float radius, out float t0, out float t1) {
    float a = dot(ray.direction, ray.direction);
    float b = 2.0 * dot(ray.origin, ray.direction);
    float c = dot(ray.origin, ray.origin) - radius * radius;
    return SolveQuadraticEqn(a, b, c, t0, t1);
}

// Hollow sphere — test both entry/exit
bool RaySphereIntersectionTest(Ray ray, float radius, out float thit, out vec3 hitNormal) {
    float t0, t1;
    if (!SolveRaySphereIntersectionEquation(ray, radius, t0, t1)) return false;
    // Prefer smallest positive t inside [Tmin, Tmax]
    if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
    thit      = IsInRange(t0, gl_RayTminEXT, gl_RayTmaxEXT) ? t0 :
                IsInRange(t1, gl_RayTminEXT, gl_RayTmaxEXT) ? t1 : -1.0;
    if (thit < 0.0) return false;
    hitNormal = CalculateNormalForARaySphereHit(ray, thit, vec3(0.0));
    return true;
}

// Solid sphere — only the front-most positive hit counts
bool RaySolidSphereIntersectionTest(Ray ray, float radius, out float thit, out vec3 hitNormal) {
    float t0, t1;
    if (!SolveRaySphereIntersectionEquation(ray, radius, t0, t1)) return false;
    if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
    thit = t0 >= gl_RayTminEXT ? t0 : (t1 >= gl_RayTminEXT ? t1 : -1.0);
    if (thit < 0.0) return false;
    hitNormal = CalculateNormalForARaySphereHit(ray, thit, vec3(0.0));
    return true;
}

// Multiple spheres — scene-dependent layout matching DieXaR
bool RaySpheresIntersectionTest(uint sceneIndex, Ray ray, out float thit, out vec3 hitNormal) {
    // Sphere layouts (in AABB local [-1,1]^3 space):
    //   Demo        : 3 spheres
    //   CornellBox  : 1 sphere at origin r=1.0
    //   PbrShowcase : 1 sphere at origin r=0.9

    const int MAX_SPHERES = 3;
    vec3  centers[MAX_SPHERES];
    float radii[MAX_SPHERES];
    int   numSpheres;

    if (sceneIndex == SCENE_DEMO) {
        numSpheres = 3;
        centers[0] = vec3(-0.3, -0.3, -0.3); radii[0] = 0.6;
        centers[1] = vec3( 0.1,  0.1,  0.4); radii[1] = 0.3;
        centers[2] = vec3( 0.35, 0.35, 0.0); radii[2] = 0.15;
    } else if (sceneIndex == SCENE_CORNELL_BOX) {
        numSpheres = 1;
        centers[0] = vec3(0.0); radii[0] = 1.0;
        centers[1] = vec3(0.0); radii[1] = 0.0; // unused
        centers[2] = vec3(0.0); radii[2] = 0.0;
    } else { // PbrShowcase
        numSpheres = 1;
        centers[0] = vec3(0.0); radii[0] = 0.9;
        centers[1] = vec3(0.0); radii[1] = 0.0;
        centers[2] = vec3(0.0); radii[2] = 0.0;
    }

    thit      = INFINITY;
    hitNormal = vec3(0.0, 1.0, 0.0);
    bool hit  = false;

    for (int i = 0; i < numSpheres; i++) {
        // Shift ray origin by sphere center
        Ray r = ray;
        r.origin -= centers[i];
        float t0, t1;
        if (!SolveRaySphereIntersectionEquation(r, radii[i], t0, t1)) continue;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        float t = t0 >= gl_RayTminEXT ? t0 : (t1 >= gl_RayTminEXT ? t1 : -1.0);
        if (t > 0.0 && t < thit && IsInRange(t, gl_RayTminEXT, gl_RayTmaxEXT)) {
            thit      = t;
            hitNormal = normalize(ray.origin + t * ray.direction - centers[i]);
            hit       = true;
        }
    }
    return hit;
}

// ================================================================== //
// AABB intersection
// ================================================================== //

// Test if ray hits AABB in local [-1,1]^3 space; returns thit and outward normal
bool RayAABBIntersectionTest(Ray ray, out float thit, out vec3 hitNormal) {
    vec3 invDir = 1.0 / ray.direction;
    vec3 t0s = (vec3(-1.0) - ray.origin) * invDir;
    vec3 t1s = (vec3( 1.0) - ray.origin) * invDir;
    vec3 tMin3 = min(t0s, t1s);
    vec3 tMax3 = max(t0s, t1s);
    float tEnter = max(max(tMin3.x, tMin3.y), tMin3.z);
    float tExit  = min(min(tMax3.x, tMax3.y), tMax3.z);
    if (tEnter >= tExit) return false;

    thit = (tEnter >= gl_RayTminEXT) ? tEnter : tExit;
    if (!IsInRange(thit, gl_RayTminEXT, gl_RayTmaxEXT)) return false;

    // Normal: axis where |t| is closest to thit
    vec3 hitPos = ray.origin + thit * ray.direction;
    vec3 absHP  = abs(hitPos);
    if (absHP.x >= absHP.y && absHP.x >= absHP.z)
        hitNormal = vec3(sign(hitPos.x), 0.0, 0.0);
    else if (absHP.y >= absHP.x && absHP.y >= absHP.z)
        hitNormal = vec3(0.0, sign(hitPos.y), 0.0);
    else
        hitNormal = vec3(0.0, 0.0, sign(hitPos.z));
    return true;
}

// ================================================================== //
// Dispatch: analytic primitive intersection
// ================================================================== //
bool RayAnalyticGeometryIntersectionTest(uint sceneIndex, Ray localRay, uint primitiveType,
                                         out float thit, out vec3 hitNormal) {
    if (primitiveType == ANALYTIC_AABB)
        return RayAABBIntersectionTest(localRay, thit, hitNormal);
    else // ANALYTIC_SPHERES
        return RaySpheresIntersectionTest(sceneIndex, localRay, thit, hitNormal);
}

#endif // ANALYTIC_PRIMITIVES_GLSL
