#pragma once

#include "Scene.h"
#include <string>
#include <unordered_map>

namespace YAML
{
    class Node;
}

namespace Vlkrt
{
    // Bidirectional mapping between SceneEntity hierarchy and flat Scene arrays
    struct HierarchyMapping
    {
        std::unordered_map<SceneEntity*, uint32_t> EntityToMeshIdx;
        std::unordered_map<SceneEntity*, uint32_t> EntityToLightIdx;
        std::vector<SceneEntity*>                  MeshIndexToEntity;
        std::vector<SceneEntity*>                  LightIndexToEntity;
    };

    class SceneLoader
    {
    public:
        // Load a scene from a YAML file (returns flat scene only)
        static Scene LoadFromYAML(const std::string& filepath);

        // Load a scene with its hierarchy (returns both flat scene and root entity)
        static std::pair<Scene, SceneEntity> LoadFromYAMLWithHierarchy(const std::string& filepath);

        // Save a scene to a YAML file (uses flat arrays only - loses hierarchy)
        static void SaveToYAML(const std::string& filepath, const Scene& scene);

        // Save a scene with its hierarchy to YAML (preserves structure)
        static void SaveToYAMLWithHierarchy(
                const std::string& filepath, const Scene& scene, const SceneEntity& rootEntity);

        // Create bidirectional mapping between entity hierarchy and flat arrays
        // Must be called after LoadFromYAML() to establish entity-to-array indices
        static HierarchyMapping CreateMapping(const SceneEntity& rootEntity, const Scene& scene);

        // Update flat Scene arrays from modified hierarchy nodes
        // Returns modified mesh/light indices for GPU buffer updates
        static void UpdateFlatScene(const SceneEntity& entity, const glm::mat4& parentWorldTransform, Scene& outScene,
                const HierarchyMapping& mapping, std::vector<uint32_t>& outModifiedMeshes,
                std::vector<uint32_t>& outModifiedLights);

    private:
        // Parse a single entity from YAML node (sets up parent pointers)
        static SceneEntity ParseEntity(const YAML::Node& entityNode, SceneEntity* parent = nullptr);

        // Parse transform from YAML node
        static Transform ParseTransform(const YAML::Node& transformNode);

        // Flatten hierarchical entities into flat Scene arrays
        static void FlattenEntity(const SceneEntity& entity, const glm::mat4& parentWorldTransform, Scene& outScene,
                const std::unordered_map<std::string, int>& materialMap);

        // Populate mapping structure during hierarchy traversal
        static void PopulateMappingRecursive(const SceneEntity& entity, const Scene& scene, HierarchyMapping& mapping,
                uint32_t& meshIndex, uint32_t& lightIndex);

        // Recursively save entity hierarchy to YAML
        static void SaveEntityToYAML(std::ofstream& file, const SceneEntity& entity, int indentLevel);
    };
}  // namespace Vlkrt
