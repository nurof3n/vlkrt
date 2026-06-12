#ifndef SDF_FRACTALS_GLSL
#define SDF_FRACTALS_GLSL

#include "common.glsl"

// ================================================================== //
// Fractal SDF — recursive pyramid
// ================================================================== //
float sdFractalPyramid(vec3 p, float s) {
    float scale = 1.0;
    for (int i = 0; i < N_FRACTAL_ITERATIONS; i++) {
        if (p.x + p.y < 0.0) { float tmp = p.x; p.x = -p.y; p.y = -tmp; }
        if (p.x + p.z < 0.0) { float tmp = p.x; p.x = -p.z; p.z = -tmp; }
        if (p.y + p.z < 0.0) { float tmp = p.y; p.y = -p.z; p.z = -tmp; }
        p  = p * 2.0 - s * (2.0 - 1.0);
        scale *= 2.0;
    }
    return (length(p) - 2.0) / scale;
}

#endif // SDF_FRACTALS_GLSL
