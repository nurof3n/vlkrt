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
    /**
     * @brief Struct that maintains a bidirectional mapping between the hierarchical SceneEntity structure and the flat
     * arrays in Scene.
     *
     */
    struct HierarchyMapping
    {
        std::unordered_map<SceneEntity*, uint32_t> EntityToMeshIdx;
        std::unordered_map<SceneEntity*, uint32_t> EntityToLightIdx;
        std::vector<SceneEntity*>                  MeshIndexToEntity;
        std::vector<SceneEntity*>                  LightIndexToEntity;
    };

    /**
     * @brief Class responsible for loading and saving scenes from/to YAML files, as well as maintaining the mapping
     * between the hierarchical SceneEntity structure and the flat Scene arrays.
     *
     */
    class SceneLoader
    {
    public:
        static Scene LoadFromYAML(const std::string& filename);
        static std::pair<Scene, SceneEntity> LoadFromYAMLWithHierarchy(const std::string& filename);
        static void SaveToYAML(const std::string& filename, const Scene& scene);
        static void SaveToYAMLWithHierarchy(
                const std::string& filename, const Scene& scene, const SceneEntity& rootEntity);

        static HierarchyMapping CreateMapping(const SceneEntity& rootEntity, const Scene& scene);
        static void UpdateFlatScene(const SceneEntity& entity, const glm::mat4& parentWorldTransform, Scene& outScene,
                const HierarchyMapping& mapping, std::vector<uint32_t>& outModifiedMeshes,
                std::vector<uint32_t>& outModifiedLights);

    private:
        static SceneEntity ParseEntity(const YAML::Node& entityNode, SceneEntity* parent = nullptr);
        static Transform ParseTransform(const YAML::Node& transformNode);

        static void FlattenEntity(const SceneEntity& entity, const glm::mat4& parentWorldTransform, Scene& outScene,
                const std::unordered_map<std::string, int>& materialMap);
        static void PopulateMappingRecursive(const SceneEntity& entity, const Scene& scene, HierarchyMapping& mapping,
                uint32_t& meshIndex, uint32_t& lightIndex);

        static void SaveEntityToYAML(std::ofstream& file, const SceneEntity& entity, int indentLevel);
    };
}  // namespace Vlkrt
