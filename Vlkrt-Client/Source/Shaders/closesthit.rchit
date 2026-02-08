#version 460
#extension GL_EXT_ray_tracing : require

struct Vertex {
    vec3 position;
    float _pad1;
    vec3 normal;
    float _pad2;
    vec2 texCoord;
    vec2 _pad3;
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

layout(binding = 2, set = 0) buffer Vertices {
    Vertex vertices[];
};

layout(binding = 3, set = 0) buffer Indices {
    uint indices[];
};

layout(binding = 4, set = 0) buffer Materials {
    Material materials[];
};

layout(binding = 5, set = 0) buffer MaterialIndices {
    uint materialIndices[];  // Maps triangle ID to material index
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;  // Barycentric coordinates (automatically provided by Vulkan)

void main()
{
    // Get triangle vertex indices
    uint idx0 = indices[gl_PrimitiveID * 3 + 0];
    uint idx1 = indices[gl_PrimitiveID * 3 + 1];
    uint idx2 = indices[gl_PrimitiveID * 3 + 2];

    Vertex v0 = vertices[idx0];
    Vertex v1 = vertices[idx1];
    Vertex v2 = vertices[idx2];

    // Barycentric interpolation of normal
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 normal = normalize(
        v0.normal * barycentrics.x +
        v1.normal * barycentrics.y +
        v2.normal * barycentrics.z
    );

    // Get material index for this triangle and fetch material
    uint matIndex = materialIndices[gl_PrimitiveID];
    Material mat = materials[matIndex];

    // Simple directional lighting
    vec3 lightDir = normalize(vec3(-1, -1, -1));
    float light = max(dot(normal, -lightDir), 0.0) * 0.8 + 0.2;

    hitValue = mat.albedo * light;
}
