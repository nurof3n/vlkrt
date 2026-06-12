// ================================================================== //
// analytic.slang — sphere and AABB intersection routines
// ================================================================== //

import common;

// ------------------------------------------------------------------ //
// Quadratic solver
// ------------------------------------------------------------------ //
bool SolveQuadratic(float a, float b, float c, out float t0, out float t1) {
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) { t0 = t1 = 0.0f; return false; }
    float sq   = sqrt(disc);
    t0 = (-b - sq) / (2.0f * a);
    t1 = (-b + sq) / (2.0f * a);
    return true;
}

// ------------------------------------------------------------------ //
// Single sphere (centered at origin)
// ------------------------------------------------------------------ //
float3 SphereNormal(Ray ray, float thit) {
    return normalize(ray.origin + thit * ray.direction);
}

bool SolveRaySphere(Ray ray, float radius, out float t0, out float t1) {
    return SolveQuadratic(
        dot(ray.direction, ray.direction),
        2.0f * dot(ray.origin, ray.direction),
        dot(ray.origin, ray.origin) - radius * radius,
        t0, t1);
}

/// Hollow sphere — prefer closest positive t
bool RaySphereTest(Ray ray, float radius,
                   out float thit, out float3 hitNormal) {
    thit = 0.0f; hitNormal = float3(0.0f, 1.0f, 0.0f);
    float t0, t1;
    if (!SolveRaySphere(ray, radius, t0, t1)) return false;
    if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
    float tMin = RayTMin();
    float tMax = RayTCurrent();
    thit = IsInRange(t0, tMin, tMax) ? t0 :
           IsInRange(t1, tMin, tMax) ? t1 : -1.0f;
    if (thit < 0.0f) return false;
    hitNormal = SphereNormal(ray, thit);
    return true;
}

/// Solid sphere — only front-face
bool RaySolidSphereTest(Ray ray, float radius,
                        out float thit, out float3 hitNormal) {
    thit = 0.0f; hitNormal = float3(0.0f, 1.0f, 0.0f);
    float t0, t1;
    if (!SolveRaySphere(ray, radius, t0, t1)) return false;
    if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
    float tMin = RayTMin();
    thit = (t0 >= tMin) ? t0 : ((t1 >= tMin) ? t1 : -1.0f);
    if (thit < 0.0f) return false;
    hitNormal = SphereNormal(ray, thit);
    return true;
}

// ------------------------------------------------------------------ //
// Multiple spheres — scene-dependent
// ------------------------------------------------------------------ //
bool RaySpheresTest(uint sceneIndex, Ray ray,
                    out float thit, out float3 hitNormal) {
    const int MAX_SPHERES = 3;
    float3 centers[MAX_SPHERES];
    float  radii[MAX_SPHERES];
    int    numSpheres;

    if (sceneIndex == SCENE_DEMO) {
        numSpheres = 3;
        centers[0] = float3(-0.3f, -0.3f, -0.3f); radii[0] = 0.6f;
        centers[1] = float3( 0.1f,  0.1f,  0.4f); radii[1] = 0.3f;
        centers[2] = float3( 0.35f, 0.35f, 0.0f); radii[2] = 0.15f;
    } else if (sceneIndex == SCENE_CORNELL_BOX) {
        numSpheres = 1;
        centers[0] = float3(0.0f); radii[0] = 1.0f;
        centers[1] = float3(0.0f); radii[1] = 0.0f;
        centers[2] = float3(0.0f); radii[2] = 0.0f;
    } else { // PBR_SHOWCASE
        numSpheres = 1;
        centers[0] = float3(0.0f); radii[0] = 0.9f;
        centers[1] = float3(0.0f); radii[1] = 0.0f;
        centers[2] = float3(0.0f); radii[2] = 0.0f;
    }

    float tMin = RayTMin();
    float tMax = RayTCurrent();
    thit      = INFINITY_F;
    hitNormal = float3(0.0f, 1.0f, 0.0f);
    bool hit  = false;

    for (int i = 0; i < numSpheres; i++) {
        Ray r = ray;
        r.origin -= centers[i];
        float t0, t1;
        if (!SolveRaySphere(r, radii[i], t0, t1)) continue;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        float t = (t0 >= tMin) ? t0 : ((t1 >= tMin) ? t1 : -1.0f);
        if (t > 0.0f && t < thit && IsInRange(t, tMin, tMax)) {
            thit      = t;
            hitNormal = normalize(ray.origin + t * ray.direction - centers[i]);
            hit       = true;
        }
    }
    return hit;
}

// ------------------------------------------------------------------ //
// AABB in local [-1,1]³ space
// ------------------------------------------------------------------ //
bool RayAABBTest(Ray ray, out float thit, out float3 hitNormal) {
    thit = 0.0f; hitNormal = float3(0.0f, 1.0f, 0.0f);
    float3 invDir = 1.0f / ray.direction;
    float3 t0s    = (float3(-1.0f) - ray.origin) * invDir;
    float3 t1s    = (float3( 1.0f) - ray.origin) * invDir;
    float3 tMin3  = min(t0s, t1s);
    float3 tMax3  = max(t0s, t1s);
    float tEnter  = max(max(tMin3.x, tMin3.y), tMin3.z);
    float tExit   = min(min(tMax3.x, tMax3.y), tMax3.z);
    if (tEnter >= tExit) return false;

    float tMin = RayTMin();
    float tMaxR = RayTCurrent();
    thit = (tEnter >= tMin) ? tEnter : tExit;
    if (!IsInRange(thit, tMin, tMaxR)) return false;

    float3 hitPos = ray.origin + thit * ray.direction;
    float3 absHP  = abs(hitPos);
    if (absHP.x >= absHP.y && absHP.x >= absHP.z)
        hitNormal = float3(sign(hitPos.x), 0.0f, 0.0f);
    else if (absHP.y >= absHP.x && absHP.y >= absHP.z)
        hitNormal = float3(0.0f, sign(hitPos.y), 0.0f);
    else
        hitNormal = float3(0.0f, 0.0f, sign(hitPos.z));
    return true;
}

// ------------------------------------------------------------------ //
// Dispatch
// ------------------------------------------------------------------ //
bool RayAnalyticTest(uint sceneIndex, Ray localRay, uint primitiveType,
                     out float thit, out float3 hitNormal) {
    if (primitiveType == ANALYTIC_AABB)
        return RayAABBTest(localRay, thit, hitNormal);
    else
        return RaySpheresTest(sceneIndex, localRay, thit, hitNormal);
}
