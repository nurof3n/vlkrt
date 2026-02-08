#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace Vlkrt
{
    struct Material
    {
        glm::vec3 Albedo{ 1.0f };
        float     Roughness = 1.0f;
        float     Metallic  = 0.0f;

        glm::vec3 EmissionColor{ 0.0f };
        float     EmissionPower = 0.0f;

        glm::vec3 GetEmission() const
        {
            return EmissionColor * EmissionPower;
        }
    };

    struct Vertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
    };

    struct Mesh
    {
        std::vector<Vertex>   Vertices;
        std::vector<uint32_t> Indices;
        int                   MaterialIndex = 0;

        glm::mat4 Transform = glm::mat4(1.0f);  // For positioning/scaling

        // AABB for culling/debugging
        glm::vec3 AABBMin;
        glm::vec3 AABBMax;
    };

    struct Light
    {
        glm::vec3 Position = glm::vec3(0.0f);     // World position
        float     Intensity = 1.0f;               // Brightness 0-2
        glm::vec3 Color = glm::vec3(1.0f);        // RGB color
        float     Type = 0.0f;                    // 0=Directional, 1=Point
        glm::vec3 Direction = glm::vec3(0.0f);   // For directional lights (normalized)
        float     Radius = 10.0f;                 // Falloff radius for point lights
    };

    struct Scene
    {
        std::vector<Mesh>     StaticMeshes;   // Meshes that don't change (ground, objects, etc)
        std::vector<Mesh>     DynamicMeshes;  // Meshes that change each frame (players)
        std::vector<Material> Materials;
        std::vector<Light>    Lights;         // Light sources for shading
    };
}  // namespace Vlkrt
