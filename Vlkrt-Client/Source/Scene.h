#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

namespace Vlkrt
{
    /**
     * @brief CPU material definition for Phong shading.
     */
    struct Material
    {
        std::string Name;
        glm::vec3 Albedo{ 1.0f };
        float Roughness{ 1.0f };
        float Metallic{};

        glm::vec3 EmissionColor{};
        float EmissionPower{};

        std::string TextureFilename;  // Filename of diffuse texture (empty = use Albedo)
        float Tiling{ 1.0f };

        auto GetEmission() const -> glm::vec3 { return EmissionColor * EmissionPower; }
    };

    /**
     * @brief CPU vertex definition.
     */
    struct Vertex
    {
        glm::vec3 Position{};
        glm::vec3 Normal{};
        glm::vec2 TexCoord{};
    };

    /**
     * @brief Mesh definition containing CPU vertex/index data and material reference.
     */
    struct Mesh
    {
        std::string Filename;  // Source filename (if empty, mesh was created procedurally)
        std::string Name;
        std::vector<Vertex> Vertices;
        std::vector<uint32_t> Indices;

        int MaterialIndex = 0;

        glm::mat4 Transform = glm::mat4(1.0f);

        glm::vec3 AABBMin;
        glm::vec3 AABBMax;
    };

    /**
     * @brief CPU light definition for both directional and point lights.
     */
    struct Light
    {
        glm::vec3 Position{};     // World position
        float Intensity{ 1.0f };  // Brightness 0-2
        glm::vec3 Color{ 1.0f };
        float Type{ 0.0f };           // 0=Directional, 1=Point
        glm::vec3 Direction{ 0.0f };  // For directional lights (normalized)
        float Radius{ 10.0f };        // Falloff radius for point lights
    };

    /**
     * @brief Decomposed, hierarchical transform with position, rotation (quaternion), and scale.
     */
    struct Transform
    {
        glm::vec3 Position{};
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };

        auto GetLocalMatrix() const -> glm::mat4
        {
            auto translation = glm::translate(glm::mat4(1.0f), Position);
            auto rotation    = glm::mat4_cast(Rotation);
            auto scale       = glm::scale(glm::mat4(1.0f), Scale);
            return translation * rotation * scale;
        }

        auto GetWorldMatrix(const glm::mat4& parentWorld) const -> glm::mat4 { return parentWorld * GetLocalMatrix(); }
    };

    /**
     * @brief Type of scene entity, used for determining how to render and update each node in the hierarchy.
     */
    enum class EntityType
    {
        Empty,  // Transform-only, used for grouping
        Mesh,   // Mesh with material
        Light,  // Light source
        Camera  // Camera
    };

    /**
     * @brief Scene entity that supports hierarchical transforms, type-specific data, and scripting.
     */
    struct SceneEntity
    {
        std::string Name;
        EntityType Type{ EntityType::Empty };
        Transform LocalTransform;
        glm::mat4 WorldTransform{ 1.0f };

        std::string ScriptPath;
        bool ScriptInitialized{ false };

        bool IsDirty{ true };
        SceneEntity* Parent{ nullptr };

        std::vector<SceneEntity> Children;

        struct MeshData
        {
            std::string Filename;
            int MaterialIndex{};
        } MeshData;

        struct LightData
        {
            glm::vec3 Color{ 1.0f };
            float Intensity{ 1.0f };
            float Type{ 0.0f };  // 0=Directional, 1=Point
            float Radius{ 10.0f };
        } LightData;

        struct CameraData
        {
            float FOV{ 45.0f };
            float Near{ 0.1f };
            float Far{ 100.0f };
        } CameraData;

        void MarkDirtyRecursive()
        {
            IsDirty = true;
            for (auto& child : Children) child.MarkDirtyRecursive();
        }

        void SetLocalTransform(const Transform& newTransform)
        {
            LocalTransform = newTransform;
            IsDirty        = true;
        }
    };

    /**
     * @brief Flat scene definition.
     */
    struct Scene
    {
        std::vector<Mesh> StaticMeshes;
        std::vector<Mesh> DynamicMeshes;
        std::vector<Material> Materials;
        std::vector<Light> Lights;
    };

    /**
     * @brief Manages the scene hierarchy for efficient transform updates.
     */
    class SceneHierarchy
    {
    public:
        void SetLocalTransform(SceneEntity& entity, const Transform& newTransform)
        {
            entity.SetLocalTransform(newTransform);
        }

        void UpdateDirtyTransforms(SceneEntity& root, const glm::mat4& parentWorld = glm::mat4(1.0f))
        {
            UpdateDirtyTransformsRecursive(root, parentWorld);
        }

    private:
        void UpdateDirtyTransformsRecursive(SceneEntity& entity, const glm::mat4& parentWorld)
        {
            // Recompute world transform if this node or its parent is dirty
            if (entity.IsDirty) {
                entity.WorldTransform = entity.LocalTransform.GetWorldMatrix(parentWorld);
                entity.IsDirty        = false;
            }

            // Recursively update children with this node's world transform
            for (auto& child : entity.Children) { UpdateDirtyTransformsRecursive(child, entity.WorldTransform); }
        }
    };
}  // namespace Vlkrt
