#include "SceneLoader.h"
#include "MeshLoader.h"
#include <yaml-cpp/yaml.h>

#include "Walnut/Core/Log.h"

#include <fstream>

namespace Vlkrt
{
    Scene SceneLoader::LoadFromYAML(const std::string& filepath)
    {
        try {
            WL_INFO_TAG("SceneLoader", "Loading YAML from: {}", filepath);
            YAML::Node root = YAML::LoadFile(filepath);

            Scene scene;

            // Parse materials
            if (root["materials"]) {
                WL_INFO_TAG("SceneLoader", "Found materials section");
                for (size_t idx = 0; idx < root["materials"].size(); ++idx) {
                    const auto& matNode = root["materials"][idx];
                    Material    mat;

                    // Set material name
                    if (matNode["name"]) {
                        mat.Name = matNode["name"].as<std::string>();
                    }
                    else {
                        mat.Name = "Material_" + std::to_string(idx);
                    }

                    if (matNode["albedo"]) {
                        auto albedo = matNode["albedo"].as<std::vector<float>>();
                        mat.Albedo  = glm::vec3(albedo[0], albedo[1], albedo[2]);
                    }

                    if (matNode["roughness"]) {
                        mat.Roughness = matNode["roughness"].as<float>();
                    }

                    if (matNode["metallic"]) {
                        mat.Metallic = matNode["metallic"].as<float>();
                    }

                    if (matNode["emission_color"]) {
                        auto color        = matNode["emission_color"].as<std::vector<float>>();
                        mat.EmissionColor = glm::vec3(color[0], color[1], color[2]);
                    }

                    if (matNode["emission_power"]) {
                        mat.EmissionPower = matNode["emission_power"].as<float>();
                    }

                    scene.Materials.push_back(mat);
                }
                WL_INFO_TAG("SceneLoader", "Loaded {} materials", scene.Materials.size());
            }

            // Create material index map
            std::unordered_map<std::string, int> materialMap;
            for (size_t i = 0; i < scene.Materials.size(); i++) {
                materialMap[std::to_string(i)] = i;
            }

            // Parse entities
            if (root["entities"]) {
                WL_INFO_TAG("SceneLoader", "Found entities section");
                for (const auto& entityNode : root["entities"]) {
                    SceneEntity entity = ParseEntity(entityNode);
                    FlattenEntity(entity, glm::mat4(1.0f), scene, materialMap);
                }
            }

            WL_INFO_TAG("SceneLoader", "Scene loaded - Materials: {}, Meshes: {}, Lights: {}", scene.Materials.size(),
                    scene.StaticMeshes.size(), scene.Lights.size());

            return scene;
        }
        catch (const std::exception& e) {
            WL_ERROR_TAG("SceneLoader", "Error loading YAML scene: {} - {}", filepath, e.what());
            return Scene();
        }
    }

    SceneEntity SceneLoader::ParseEntity(const YAML::Node& entityNode)
    {
        SceneEntity entity;
        entity.Type           = EntityType::Empty;
        entity.LocalTransform = Transform();

        // Parse name
        if (entityNode["name"]) {
            entity.Name = entityNode["name"].as<std::string>();
        }

        // Parse type
        if (entityNode["type"]) {
            std::string typeStr = entityNode["type"].as<std::string>();
            if (typeStr == "empty")
                entity.Type = EntityType::Empty;
            else if (typeStr == "mesh")
                entity.Type = EntityType::Mesh;
            else if (typeStr == "light")
                entity.Type = EntityType::Light;
            else if (typeStr == "camera")
                entity.Type = EntityType::Camera;
        }

        // Parse mesh-specific data
        if (entityNode["mesh_path"]) {
            entity.MeshData.FilePath = entityNode["mesh_path"].as<std::string>();
        }

        if (entityNode["material"]) {
            entity.MeshData.MaterialIndex = entityNode["material"].as<int>();
        }

        // Parse light-specific data
        if (entityNode["light_color"]) {
            auto color             = entityNode["light_color"].as<std::vector<float>>();
            entity.LightData.Color = glm::vec3(color[0], color[1], color[2]);
        }

        if (entityNode["light_intensity"]) {
            entity.LightData.Intensity = entityNode["light_intensity"].as<float>();
        }

        if (entityNode["light_type"]) {
            entity.LightData.Type = entityNode["light_type"].as<float>();
        }

        if (entityNode["light_radius"]) {
            entity.LightData.Radius = entityNode["light_radius"].as<float>();
        }

        // Parse transform
        if (entityNode["transform"]) {
            entity.LocalTransform = ParseTransform(entityNode["transform"]);
        }

        // Parse children
        if (entityNode["children"]) {
            for (const auto& childNode : entityNode["children"]) {
                SceneEntity child = ParseEntity(childNode);
                entity.Children.push_back(child);
            }
        }

        return entity;
    }

    Transform SceneLoader::ParseTransform(const YAML::Node& transformNode)
    {
        Transform transform;

        if (transformNode["position"]) {
            auto pos           = transformNode["position"].as<std::vector<float>>();
            transform.Position = glm::vec3(pos[0], pos[1], pos[2]);
        }

        if (transformNode["rotation"]) {
            auto rot = transformNode["rotation"].as<std::vector<float>>();
            // YAML format is [x, y, z, w], convert to glm::quat [w, x, y, z]
            transform.Rotation = glm::quat(rot[3], rot[0], rot[1], rot[2]);
        }

        if (transformNode["scale"]) {
            auto scl        = transformNode["scale"].as<std::vector<float>>();
            transform.Scale = glm::vec3(scl[0], scl[1], scl[2]);
        }

        return transform;
    }

    void SceneLoader::FlattenEntity(const SceneEntity& entity, const glm::mat4& parentWorldTransform, Scene& outScene,
            const std::unordered_map<std::string, int>& materialMap)
    {
        // Compute world transform
        glm::mat4 worldTransform = entity.LocalTransform.GetWorldMatrix(parentWorldTransform);

        // Add entity to scene based on type
        if (entity.Type == EntityType::Mesh) {
            // Load mesh from file and add to scene
            if (!entity.MeshData.FilePath.empty()) {
                try {
                    Mesh mesh          = MeshLoader::LoadOBJ(entity.MeshData.FilePath);
                    mesh.FilePath      = entity.MeshData.FilePath;  // Store original path
                    mesh.Name          = entity.Name;               // Store mesh name
                    mesh.Transform     = worldTransform;
                    mesh.MaterialIndex = entity.MeshData.MaterialIndex;
                    outScene.StaticMeshes.push_back(mesh);
                }
                catch (const std::exception& e) {
                    WL_ERROR_TAG("SceneLoader", "Error loading mesh: {} - {}", entity.MeshData.FilePath, e.what());
                }
            }
        }
        else if (entity.Type == EntityType::Light) {
            // Add light to scene
            Light light;
            light.Color     = entity.LightData.Color;
            light.Intensity = entity.LightData.Intensity;
            light.Type      = entity.LightData.Type;
            light.Radius    = entity.LightData.Radius;

            // Compute world position and direction
            light.Position = glm::vec3(worldTransform[3]);  // Translation component

            // For directional lights, compute direction from rotation
            glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
            light.Direction            = glm::normalize(glm::vec3(worldTransform * glm::vec4(defaultDirection, 0.0f)));

            outScene.Lights.push_back(light);
        }
        // Empty and Camera types don't add to scene, just pass through to children

        // Recursively process children
        for (const auto& child : entity.Children) {
            FlattenEntity(child, worldTransform, outScene, materialMap);
        }
    }

    void SceneLoader::SaveToYAML(const std::string& filepath, const Scene& scene)
    {
        try {
            WL_INFO_TAG("SceneLoader", "Saving scene to: {}", filepath);

            std::ofstream file(filepath);
            if (!file.is_open()) {
                WL_ERROR_TAG("SceneLoader", "Failed to open file for writing: {}", filepath);
                return;
            }

            // Write materials section
            file << "materials:\n";
            for (const auto& mat : scene.Materials) {
                file << "- name: " << mat.Name << "\n";
                file << "  albedo: [ " << mat.Albedo.x << ", " << mat.Albedo.y << ", " << mat.Albedo.z << " ]\n";
                file << "  roughness: " << mat.Roughness << "\n";
                file << "  metallic: " << mat.Metallic << "\n";

                if (mat.EmissionPower > 0.0f) {
                    file << "  emission_color: [ " << mat.EmissionColor.x << ", " << mat.EmissionColor.y << ", "
                         << mat.EmissionColor.z << " ]\n";
                    file << "  emission_power: " << mat.EmissionPower << "\n";
                }
            }

            // Write entities section with meshes and lights as children
            file << "\nentities:\n";
            file << "- name: scene_root\n";
            file << "  type: empty\n";
            file << "  transform:\n";
            file << "    position: [ 0, 0, 0 ]\n";
            file << "    rotation: [ 0, 0, 0, 1 ]\n";
            file << "    scale: [ 1, 1, 1 ]\n";
            file << "  children:\n";

            // Save static meshes as children
            for (const auto& mesh : scene.StaticMeshes) {
                file << "  - name: " << (mesh.Name.empty() ? "Mesh" : mesh.Name) << "\n";
                file << "    type: mesh\n";
                file << "    mesh_path: " << (mesh.FilePath.empty() ? "unknown.obj" : mesh.FilePath) << "\n";
                file << "    material: " << mesh.MaterialIndex << "\n";

                // Extract position, rotation, and scale from transform matrix
                glm::vec3 position = glm::vec3(mesh.Transform[3]);  // Translation is in column 3

                // Extract scale from the length of basis vectors
                glm::vec3 scaleX = glm::vec3(mesh.Transform[0]);
                glm::vec3 scaleY = glm::vec3(mesh.Transform[1]);
                glm::vec3 scaleZ = glm::vec3(mesh.Transform[2]);
                glm::vec3 scale(glm::length(scaleX), glm::length(scaleY), glm::length(scaleZ));

                // Extract rotation by normalizing the basis vectors to remove scale
                glm::mat3 rotMatrix(glm::normalize(scaleX), glm::normalize(scaleY), glm::normalize(scaleZ));
                glm::quat rotation = glm::quat_cast(rotMatrix);
                // Convert from GLM [w, x, y, z] to YAML [x, y, z, w] format
                glm::vec4 rotQuat(rotation.x, rotation.y, rotation.z, rotation.w);

                file << "    transform:\n";
                file << "      position: [ " << position.x << ", " << position.y << ", " << position.z << " ]\n";
                file << "      rotation: [ " << rotQuat.x << ", " << rotQuat.y << ", " << rotQuat.z << ", " << rotQuat.w
                     << " ]\n";
                file << "      scale: [ " << scale.x << ", " << scale.y << ", " << scale.z << " ]\n";
            }

            // Save lights as children
            for (size_t i = 0; i < scene.Lights.size(); ++i) {
                const auto& light = scene.Lights[i];
                file << "  - name: Light_" << i << "\n";
                file << "    type: light\n";
                file << "    transform:\n";
                file << "      position: [ " << light.Position.x << ", " << light.Position.y << ", " << light.Position.z
                     << " ]\n";
                file << "      rotation: [ 0, 0, 0, 1 ]\n";
                file << "      scale: [ 1, 1, 1 ]\n";
                file << "    light_color: [ " << light.Color.x << ", " << light.Color.y << ", " << light.Color.z
                     << " ]\n";
                file << "    light_intensity: " << light.Intensity << "\n";
                file << "    light_type: " << (int) light.Type << "\n";
                file << "    light_radius: " << light.Radius << "\n";
            }

            file.close();

            WL_INFO_TAG("SceneLoader", "Scene saved successfully with {} materials, {} meshes, and {} lights",
                    scene.Materials.size(), scene.StaticMeshes.size(), scene.Lights.size());
        }
        catch (const std::exception& e) {
            WL_ERROR_TAG("SceneLoader", "Error saving YAML scene: {} - {}", filepath, e.what());
        }
    }
}  // namespace Vlkrt
