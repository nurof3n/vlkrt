#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload {
    vec3 hitValue;
    float coneWidth;
    float spreadAngle;
};

layout(location = 0) rayPayloadInEXT RayPayload payload;

void main()
{
    // Sky color
    payload.hitValue = vec3(0.6, 0.7, 0.9);
}
