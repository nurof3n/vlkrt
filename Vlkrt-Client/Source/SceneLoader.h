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
    class SceneLoader
    {
    public:
        // Load a scene from a YAML file
        static Scene LoadFromYAML(const std::string& filepath);

        // Save a scene to a YAML file
        static void SaveToYAML(const std::string& filepath, const Scene& scene);

    private:
        // Parse a single entity from YAML node
        static SceneEntity ParseEntity(const YAML::Node& entityNode);

        // Parse transform from YAML node
        static Transform ParseTransform(const YAML::Node& transformNode);

        // Flatten hierarchical entities into flat Scene arrays
        static void FlattenEntity(const SceneEntity& entity, const glm::mat4& parentWorldTransform, Scene& outScene,
                const std::unordered_map<std::string, int>& materialMap);
    };
}  // namespace Vlkrt
