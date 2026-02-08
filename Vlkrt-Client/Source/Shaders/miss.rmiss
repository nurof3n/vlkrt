#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
    // Sky color
    hitValue = vec3(0.6, 0.7, 0.9);
}
