#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

namespace Vlkrt
{
    struct Material
    {
        std::string Name;  // Material name
        glm::vec3   Albedo{ 1.0f };
        float       Roughness = 1.0f;
        float       Metallic  = 0.0f;

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
        std::string           FilePath;  // Source file path for serialization
        std::string           Name;      // Mesh name
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
        glm::vec3 Position  = glm::vec3(0.0f);  // World position
        float     Intensity = 1.0f;             // Brightness 0-2
        glm::vec3 Color     = glm::vec3(1.0f);  // RGB color
        float     Type      = 0.0f;             // 0=Directional, 1=Point
        glm::vec3 Direction = glm::vec3(0.0f);  // For directional lights (normalized)
        float     Radius    = 10.0f;            // Falloff radius for point lights
    };

    // Decomposed transform for hierarchical entities
    struct Transform
    {
        glm::vec3 Position = glm::vec3(0.0f);
        glm::quat Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // [w, x, y, z]
        glm::vec3 Scale    = glm::vec3(1.0f);

        // Compute local transformation matrix (position × rotation × scale)
        glm::mat4 GetLocalMatrix() const
        {
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
            glm::mat4 rotation    = glm::mat4_cast(Rotation);
            glm::mat4 scale       = glm::scale(glm::mat4(1.0f), Scale);
            return translation * rotation * scale;
        }

        // Compute world transformation matrix given parent's world transform
        glm::mat4 GetWorldMatrix(const glm::mat4& parentWorld) const
        {
            return parentWorld * GetLocalMatrix();
        }
    };

    // Entity types for scene hierarchy
    enum class EntityType
    {
        Empty,  // Transform-only, used for grouping
        Mesh,   // Mesh with material
        Light,  // Light source
        Camera  // Camera
    };

    // Hierarchical scene entity
    struct SceneEntity
    {
        std::string Name;
        EntityType  Type = EntityType::Empty;
        Transform   LocalTransform;
        glm::mat4   WorldTransform = glm::mat4(1.0f);

        std::vector<SceneEntity> Children;

        // Type-specific data
        struct MeshData
        {
            std::string FilePath;
            int         MaterialIndex = 0;
        } MeshData;

        struct LightData
        {
            glm::vec3 Color     = glm::vec3(1.0f);
            float     Intensity = 1.0f;
            float     Type      = 0.0f;  // 0=Directional, 1=Point
            float     Radius    = 10.0f;
        } LightData;

        struct CameraData
        {
            float FOV  = 45.0f;
            float Near = 0.1f;
            float Far  = 100.0f;
        } CameraData;
    };

    struct Scene
    {
        std::vector<Mesh>     StaticMeshes;   // Meshes that don't change (ground, objects, etc)
        std::vector<Mesh>     DynamicMeshes;  // Meshes that change each frame (players)
        std::vector<Material> Materials;
        std::vector<Light>    Lights;  // Light sources for shading
    };
}  // namespace Vlkrt
