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
    int textureIndex;
    float tiling;
    float _pad3;
    vec3 emissionColor;
    float emissionPower;
};

struct Light {
    vec3 position;
    float intensity;
    vec3 color;
    float type;  // 0 = Directional, 1 = Point
    vec3 direction;
    float radius;
};

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
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
    uint materialIndices[];
};

layout(binding = 6, set = 0) buffer Lights {
    Light lights[];
};

layout(binding = 7, set = 0) uniform sampler2D textures[16];

struct RayPayload {
    vec3 hitValue;
    float coneWidth;
    float spreadAngle;
};

// Primary ray payload only
layout(location = 0) rayPayloadInEXT RayPayload payload;

hitAttributeEXT vec2 attribs;

void main()
{
    // Update cone width for this hit
    float currentConeWidth = payload.coneWidth + payload.spreadAngle * gl_HitTEXT;
    payload.coneWidth = currentConeWidth;

    // Get triangle vertex indices
    uint idx0 = indices[gl_PrimitiveID * 3 + 0];
    uint idx1 = indices[gl_PrimitiveID * 3 + 1];
    uint idx2 = indices[gl_PrimitiveID * 3 + 2];

    Vertex v0 = vertices[idx0];
    Vertex v1 = vertices[idx1];
    Vertex v2 = vertices[idx2];

    // Barycentric interpolation
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    vec3 normal = normalize(
        v0.normal * barycentrics.x +
        v1.normal * barycentrics.y +
        v2.normal * barycentrics.z
    );

    vec3 worldPos =
        v0.position * barycentrics.x +
        v1.position * barycentrics.y +
        v2.position * barycentrics.z;

    // Get material
    uint matIndex = materialIndices[gl_PrimitiveID];
    Material mat = materials[matIndex];

    vec3 albedo = mat.albedo;
    if (mat.textureIndex >= 0) {
        vec2 uv = (v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z) * mat.tiling;
        
        // EA Advanced Ray Cone LOD - https://media.contentapi.ea.com/content/dam/ea/seed/presentations/2019-ray-tracing-gems-chapter-20-akenine-moller-et-al.pdf
        // 1. Compute Triangle Areas
        vec3 edge1 = v1.position - v0.position;
        vec3 edge2 = v2.position - v0.position;
        float triArea = length(cross(edge1, edge2)); // 2 * Area_world

        vec2 texEdge1 = v1.texCoord - v0.texCoord;
        vec2 texEdge2 = v2.texCoord - v0.texCoord;
        float uvArea = abs(texEdge1.x * texEdge2.y - texEdge1.y * texEdge2.x); // 2 * Area_uv
        uvArea *= (mat.tiling * mat.tiling);

        // 2. Calculate LOD constant based on geometry ratio
        float lodConstant = 0.5 * log2(uvArea / max(triArea, 1e-10));
        
        // 3. Texture resolution term
        ivec2 texSize = textureSize(textures[mat.textureIndex], 0);
        float texScale = 0.5 * log2(float(texSize.x * texSize.y));
        
        // 4. Ray term including incidence angle
        float cosTheta = abs(dot(normal, gl_WorldRayDirectionEXT));
        float rayTerm = log2(max(currentConeWidth, 1e-10) / max(cosTheta, 1e-10));
        
        // Final LOD calculation (EA Equation 34)
        // Manual bias to fine-tune sharpness. -0.5 is usually a good balance.
        float lod = lodConstant + rayTerm + texScale - 0.5;
        lod = max(0.0, lod);
        
        albedo *= textureLod(textures[mat.textureIndex], uv, lod).rgb;
    }

    vec3 result = albedo * 0.2;  // Ambient

    // Process all lights
    if (lights.length() > 0)
    {
        for (uint i = 0; i < min(uint(lights.length()), 2u); i++)
        {
            Light light = lights[i];
            vec3 lightDir;
            float attenuation = 1.0;

            // Determine light direction and attenuation
            if (light.type > 0.5)  // Point light
            {
                lightDir = light.position - worldPos;
                float dist = length(lightDir);
                lightDir = normalize(lightDir);
                attenuation = 1.0 / (1.0 + (dist / light.radius) * (dist / light.radius));
            }
            else  // Directional light
            {
                lightDir = -light.direction;
                attenuation = 1.0;
            }

            // Diffuse
            float diffuse = max(0.0, dot(normal, lightDir));
            vec3 diffuseContrib = albedo * diffuse * light.color * light.intensity * attenuation;

            // Specular (Blinn-Phong)
            vec3 viewDir = normalize(-gl_WorldRayDirectionEXT);
            vec3 halfDir = normalize(lightDir + viewDir);
            float specular = pow(max(0.0, dot(normal, halfDir)), 32.0);
            vec3 specularContrib = specular * 0.3 * light.color * light.intensity * attenuation;

            result += diffuseContrib + specularContrib;
        }
    }

    payload.hitValue = result;
}
