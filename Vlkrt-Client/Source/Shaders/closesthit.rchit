#version 460
#extension GL_EXT_ray_tracing : require

struct Sphere {
    vec3 position;
    float radius;
    int materialIndex;
    int _pad1;
    int _pad2;
    int _pad3;
};

struct Material {
    vec3 albedo;
    float roughness;
    float metallic;
    float _pad1;
    float _pad2;
    float _pad3;
    vec3 emissionColor;
    float emissionPower;
};

layout(binding = 2, set = 0) buffer Spheres {
    Sphere spheres[];
};

layout(binding = 3, set = 0) buffer Materials {
    Material materials[];
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

void main()
{
    Sphere sphere = spheres[gl_PrimitiveID];
    Material mat = materials[sphere.materialIndex];

    vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 normal = normalize(worldPos - sphere.position);

    // Simple lighting
    vec3 lightDir = normalize(vec3(-1, -1, -1));
    float light = max(dot(normal, -lightDir), 0.0) * 0.8 + 0.2;

    hitValue = mat.albedo * light;
}
