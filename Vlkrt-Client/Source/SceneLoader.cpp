#include "SceneLoader.h"
#include "MeshLoader.h"
#include "Utils.h"

#include "Walnut/Core/Log.h"

#include <fstream>
#include <yaml-cpp/yaml.h>

namespace Vlkrt
{
    auto SceneLoader::LoadFromYAML(const std::string& filename) -> Scene
    {
        auto [scene, root] = LoadFromYAMLWithHierarchy(filename);
        return scene;
    }

    auto SceneLoader::LoadFromYAMLWithHierarchy(const std::string& filename) -> std::pair<Scene, SceneEntity>
    {
        auto filepath = Vlkrt::SCENES_DIR + filename;

        try {
            YAML::Node root = YAML::LoadFile(filepath);

            Scene scene;
            SceneEntity sceneRoot;
            sceneRoot.Type = EntityType::Empty;
            sceneRoot.Name = "scene_root";

            // Parse materials
            if (root["materials"]) {
                WL_INFO_TAG("SceneLoader", "Found materials section");
                for (size_t idx = 0; idx < root["materials"].size(); ++idx) {
                    const auto& matNode = root["materials"][idx];
                    Material mat;

                    if (matNode["name"]) { mat.Name = matNode["name"].as<std::string>(); }
                    else {
                        mat.Name = "Material_" + std::to_string(idx);
                    }

                    if (matNode["albedo"]) {
                        auto albedo = matNode["albedo"].as<std::vector<float>>();
                        mat.Albedo  = glm::vec3(albedo[0], albedo[1], albedo[2]);
                    }

                    // Disney BSDF parameters (with Phong fallback for old YAML files)
                    if (matNode["roughness"]) { mat.Roughness = matNode["roughness"].as<float>(); }
                    else if (matNode["shininess"]) {
                        float s       = matNode["shininess"].as<float>();
                        mat.Roughness = 1.0f - glm::clamp(s / 128.0f, 0.0f, 1.0f);
                    }

                    if (matNode["metallic"]) { mat.Metallic = matNode["metallic"].as<float>(); }
                    else if (matNode["specular"]) {
                        // Legacy: convert greyscale specular to metallic
                        auto spec    = matNode["specular"].as<std::vector<float>>();
                        mat.Metallic = (spec[0] + spec[1] + spec[2]) / 3.0f;
                    }

                    if (matNode["subsurface"]) { mat.Subsurface = matNode["subsurface"].as<float>(); }
                    if (matNode["anisotropic"]) { mat.Anisotropic = matNode["anisotropic"].as<float>(); }
                    if (matNode["sheen"]) { mat.Sheen = matNode["sheen"].as<float>(); }
                    if (matNode["sheen_tint"]) { mat.SheenTint = matNode["sheen_tint"].as<float>(); }
                    if (matNode["clearcoat"]) { mat.Clearcoat = matNode["clearcoat"].as<float>(); }
                    if (matNode["clearcoat_gloss"]) { mat.ClearcoatGloss = matNode["clearcoat_gloss"].as<float>(); }
                    if (matNode["specular_tint"]) { mat.SpecularTint = matNode["specular_tint"].as<float>(); }
                    if (matNode["specular_transmission"]) {
                        mat.SpecularTransmission = matNode["specular_transmission"].as<float>();
                    }
                    if (matNode["eta"]) { mat.Eta = matNode["eta"].as<float>(); }
                    if (matNode["at_distance"]) { mat.AtDistance = matNode["at_distance"].as<float>(); }
                    if (matNode["step_scale"]) { mat.StepScale = matNode["step_scale"].as<float>(); }

                    if (matNode["emission"]) {
                        auto em      = matNode["emission"].as<std::vector<float>>();
                        mat.Emission = glm::vec3(em[0], em[1], em[2]);
                    }
                    if (matNode["extinction"]) {
                        auto ex        = matNode["extinction"].as<std::vector<float>>();
                        mat.Extinction = glm::vec3(ex[0], ex[1], ex[2]);
                    }
                    if (matNode["at_distance"]) { mat.AtDistance = matNode["at_distance"].as<float>(); }
                    if (matNode["light_index"]) { mat.LightIndex = matNode["light_index"].as<int32_t>(); }
                    if (matNode["material_index"]) { mat.MaterialIndex = matNode["material_index"].as<uint32_t>(); }

                    if (matNode["texture"]) { mat.TextureFilename = matNode["texture"].as<std::string>(); }

                    if (matNode["tiling"]) { mat.Tiling = matNode["tiling"].as<float>(); }

                    scene.Materials.push_back(mat);
                }
                WL_INFO_TAG("SceneLoader", "Loaded {} materials", scene.Materials.size());
            }

            // Create material index map
            std::unordered_map<std::string, int> materialMap;
            for (size_t i = 0; i < scene.Materials.size(); i++) {
                materialMap[std::to_string(i)] = static_cast<int>(i);
            }

            // Parse entities and build hierarchy
            if (root["entities"]) {
                WL_INFO_TAG("SceneLoader", "Found entities section");
                for (const auto& entityNode : root["entities"]) {
                    SceneEntity entity = ParseEntity(entityNode, &sceneRoot);
                    sceneRoot.Children.push_back(entity);
                    FlattenEntity(entity, glm::mat4(1.0f), scene, materialMap);
                }
            }

            // Parse optional scene settings
            if (root["scene_settings"]) {
                const auto& ss = root["scene_settings"];
                if (ss["background_color"]) {
                    auto bg               = ss["background_color"].as<std::vector<float>>();
                    scene.BackgroundColor = glm::vec3(bg[0], bg[1], bg[2]);
                }
                if (ss["max_recursion_depth"]) { scene.MaxRecursionDepth = ss["max_recursion_depth"].as<uint32_t>(); }
                if (ss["max_shadow_recursion_depth"]) {
                    scene.MaxShadowRecursionDepth = ss["max_shadow_recursion_depth"].as<uint32_t>();
                }
                if (ss["path_sqrt_samples"]) { scene.PathSqrtSamplesPerPixel = ss["path_sqrt_samples"].as<uint32_t>(); }
                if (ss["russian_roulette_depth"]) {
                    scene.RussianRouletteDepth = ss["russian_roulette_depth"].as<uint32_t>();
                }
                if (ss["apply_jitter"]) { scene.ApplyJitter = ss["apply_jitter"].as<bool>(); }
                if (ss["anisotropic_bsdf"]) { scene.AnisotropicBSDF = ss["anisotropic_bsdf"].as<bool>(); }
                if (ss["enable_fsr"]) { scene.EnableFSR = ss["enable_fsr"].as<bool>(); }
                if (ss["fsr_quality_mode"]) { scene.FSRQualityMode = ss["fsr_quality_mode"].as<uint32_t>(); }
                if (ss["fsr_sharpness"]) { scene.FSRSharpness = ss["fsr_sharpness"].as<float>(); }
                if (ss["scene_index"]) { scene.SceneIndex = ss["scene_index"].as<uint32_t>(); }
                if (ss["camera_position"]) {
                    auto cp              = ss["camera_position"].as<std::vector<float>>();
                    scene.CameraPosition = glm::vec3(cp[0], cp[1], cp[2]);
                    scene.HasCameraHint  = true;
                }
                if (ss["camera_target"]) {
                    auto ct             = ss["camera_target"].as<std::vector<float>>();
                    scene.CameraTarget  = glm::vec3(ct[0], ct[1], ct[2]);
                    scene.HasCameraHint = true;
                }
            }

            WL_INFO_TAG("SceneLoader", "Scene loaded - Materials: {}, Meshes: {}, Lights: {}, Procedurals: {}",
                    scene.Materials.size(), scene.StaticMeshes.size(), scene.Lights.size(),
                    scene.ProceduralEntities.size());

            return { scene, sceneRoot };
        }
        catch (const std::exception& e) {
            WL_ERROR_TAG("SceneLoader", "Error loading YAML scene: {} - {}", filepath, e.what());
            return { Scene(), SceneEntity() };
        }
    }

    auto SceneLoader::ParseEntity(const YAML::Node& entityNode, SceneEntity* parent) -> SceneEntity
    {
        SceneEntity entity;
        entity.Type           = EntityType::Empty;
        entity.LocalTransform = Transform();
        entity.Parent         = parent;

        if (entityNode["name"]) { entity.Name = entityNode["name"].as<std::string>(); }

        if (entityNode["script"]) { entity.ScriptPath = entityNode["script"].as<std::string>(); }

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
            else if (typeStr == "procedural")
                entity.Type = EntityType::Procedural;
        }

        if (entityNode["mesh"]) { entity.MeshData.Filename = entityNode["mesh"].as<std::string>(); }

        if (entityNode["material"]) { entity.MeshData.MaterialIndex = entityNode["material"].as<int>(); }

        if (entity.Type == EntityType::Procedural) {
            if (entityNode["material"]) entity.ProceduralData.MaterialIndex = entityNode["material"].as<int>();
            if (entityNode["procedural_analytic"])
                entity.ProceduralData.IsAnalytic = entityNode["procedural_analytic"].as<bool>();
            if (entityNode["procedural_type"])
                entity.ProceduralData.PrimitiveType = entityNode["procedural_type"].as<uint32_t>();
        }

        if (entityNode["light_emission"]) {
            auto em                   = entityNode["light_emission"].as<std::vector<float>>();
            entity.LightData.Emission = glm::vec3(em[0], em[1], em[2]);
        }
        else if (entityNode["light_color"]) {
            auto color                = entityNode["light_color"].as<std::vector<float>>();
            entity.LightData.Emission = glm::vec3(color[0], color[1], color[2]);
        }

        if (entityNode["light_intensity"]) { entity.LightData.Intensity = entityNode["light_intensity"].as<float>(); }

        if (entityNode["light_type"]) {
            entity.LightData.Type = static_cast<LightType>(entityNode["light_type"].as<uint32_t>());
        }

        if (entityNode["light_size"]) { entity.LightData.Size = entityNode["light_size"].as<float>(); }
        else if (entityNode["light_radius"]) {
            entity.LightData.Size = entityNode["light_radius"].as<float>();
        }

        if (entityNode["transform"]) { entity.LocalTransform = ParseTransform(entityNode["transform"]); }

        // For directional lights with explicit direction in YAML, rotate the transform to match
        if (entityNode["light_direction"]) {
            auto dirVec                = entityNode["light_direction"].as<std::vector<float>>();
            glm::vec3 desiredDirection = glm::normalize(glm::vec3(dirVec[0], dirVec[1], dirVec[2]));

            glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 axis             = glm::cross(defaultDirection, desiredDirection);
            float dot                  = glm::dot(defaultDirection, desiredDirection);

            if (glm::length(axis) > 0.001f) {
                float angle                    = glm::acos(glm::clamp(dot, -1.0f, 1.0f));
                entity.LocalTransform.Rotation = glm::angleAxis(angle, glm::normalize(axis));
            }
            else if (dot < 0.0f) {
                entity.LocalTransform.Rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }

        if (entityNode["children"]) {
            for (const auto& childNode : entityNode["children"]) {
                SceneEntity child = ParseEntity(childNode, &entity);
                entity.Children.push_back(child);
            }
        }

        return entity;
    }

    auto SceneLoader::ParseTransform(const YAML::Node& transformNode) -> Transform
    {
        Transform transform;

        if (transformNode["position"]) {
            auto pos           = transformNode["position"].as<std::vector<float>>();
            transform.Position = glm::vec3(pos[0], pos[1], pos[2]);
        }

        if (transformNode["rotation"]) {
            auto rot           = transformNode["rotation"].as<std::vector<float>>();
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
        glm::mat4 worldTransform = entity.LocalTransform.GetWorldMatrix(parentWorldTransform);

        if (entity.Type == EntityType::Mesh) {
            if (!entity.MeshData.Filename.empty()) {
                try {
                    Mesh mesh          = MeshLoader::LoadOBJ(entity.MeshData.Filename);
                    mesh.Filename      = entity.MeshData.Filename;
                    mesh.Name          = entity.Name;
                    mesh.Transform     = worldTransform;
                    mesh.MaterialIndex = entity.MeshData.MaterialIndex;
                    outScene.StaticMeshes.push_back(mesh);
                }
                catch (const std::exception& e) {
                    WL_ERROR_TAG("SceneLoader", "Error loading mesh: {} - {}", entity.MeshData.Filename, e.what());
                }
            }
        }
        else if (entity.Type == EntityType::Light) {
            Light light;
            light.Emission  = entity.LightData.Emission;
            light.Intensity = entity.LightData.Intensity;
            light.Type      = entity.LightData.Type;
            light.Size      = entity.LightData.Size;

            light.Position             = glm::vec3(worldTransform[3]);
            glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
            light.Direction            = glm::normalize(glm::vec3(worldTransform * glm::vec4(defaultDirection, 0.0f)));

            outScene.Lights.push_back(light);
        }
        else if (entity.Type == EntityType::Procedural) {
            ProceduralEntity pe;
            pe.Name          = entity.Name;
            pe.Transform     = worldTransform;
            pe.IsAnalytic    = entity.ProceduralData.IsAnalytic;
            pe.PrimitiveType = entity.ProceduralData.PrimitiveType;
            pe.MaterialIndex = entity.ProceduralData.MaterialIndex;
            outScene.ProceduralEntities.push_back(pe);
        }

        for (const auto& child : entity.Children) { FlattenEntity(child, worldTransform, outScene, materialMap); }
    }

    void SceneLoader::SaveToYAML(const std::string& filename, const Scene& scene)
    {
        auto filepath = Vlkrt::SCENES_DIR + filename;

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
                if (mat.Subsurface > 0.0f) file << "  subsurface: " << mat.Subsurface << "\n";
                if (mat.Anisotropic > 0.0f) file << "  anisotropic: " << mat.Anisotropic << "\n";
                if (mat.Sheen > 0.0f) file << "  sheen: " << mat.Sheen << "\n";
                if (mat.Clearcoat > 0.0f) file << "  clearcoat: " << mat.Clearcoat << "\n";
                if (mat.SpecularTransmission > 0.0f)
                    file << "  specular_transmission: " << mat.SpecularTransmission << "\n";
                if (mat.Eta != 1.5f) file << "  eta: " << mat.Eta << "\n";
                glm::vec3 em = mat.Emission;
                if (em.x > 0 || em.y > 0 || em.z > 0)
                    file << "  emission: [ " << em.x << ", " << em.y << ", " << em.z << " ]\n";
                if (!mat.TextureFilename.empty()) file << "  texture: " << mat.TextureFilename << "\n";
                if (mat.Tiling != 1.0f) file << "  tiling: " << mat.Tiling << "\n";
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
                file << "    mesh: " << (mesh.Filename.empty() ? "unknown.obj" : mesh.Filename) << "\n";
                file << "    material: " << mesh.MaterialIndex << "\n";

                // Extract position, rotation, and scale from transform matrix
                glm::vec3 position = glm::vec3(mesh.Transform[3]);

                glm::vec3 scaleX = glm::vec3(mesh.Transform[0]);
                glm::vec3 scaleY = glm::vec3(mesh.Transform[1]);
                glm::vec3 scaleZ = glm::vec3(mesh.Transform[2]);
                glm::vec3 scale(glm::length(scaleX), glm::length(scaleY), glm::length(scaleZ));

                glm::mat3 rotMatrix(glm::normalize(scaleX), glm::normalize(scaleY), glm::normalize(scaleZ));
                glm::quat rotation = glm::quat_cast(rotMatrix);
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
                file << "    light_emission: [ " << light.Emission.x << ", " << light.Emission.y << ", "
                     << light.Emission.z << " ]\n";
                file << "    light_intensity: " << light.Intensity << "\n";
                file << "    light_type: " << static_cast<uint32_t>(light.Type) << "\n";
                file << "    light_size: " << light.Size << "\n";
            }

            file.close();

            WL_INFO_TAG("SceneLoader", "Scene saved successfully with {} materials, {} meshes, and {} lights",
                    scene.Materials.size(), scene.StaticMeshes.size(), scene.Lights.size());
        }
        catch (const std::exception& e) {
            WL_ERROR_TAG("SceneLoader", "Error saving YAML scene: {} - {}", filepath, e.what());
        }
    }

    auto SceneLoader::CreateMapping(const SceneEntity& rootEntity, const Scene& scene) -> HierarchyMapping
    {
        HierarchyMapping mapping;
        uint32_t meshIndex       = 0;
        uint32_t lightIndex      = 0;
        uint32_t proceduralIndex = 0;
        PopulateMappingRecursive(rootEntity, scene, mapping, meshIndex, lightIndex, proceduralIndex);
        return mapping;
    }

    void SceneLoader::PopulateMappingRecursive(const SceneEntity& entity, const Scene& scene, HierarchyMapping& mapping,
            uint32_t& meshIndex, uint32_t& lightIndex, uint32_t& proceduralIndex)
    {
        // Map this entity to its index in the flat arrays
        if (entity.Type == EntityType::Mesh) {
            if (meshIndex < scene.StaticMeshes.size()) {
                mapping.EntityToMeshIdx[const_cast<SceneEntity*>(&entity)] = meshIndex;
                mapping.MeshIndexToEntity.push_back(const_cast<SceneEntity*>(&entity));
                meshIndex++;
            }
        }
        else if (entity.Type == EntityType::Light) {
            if (lightIndex < scene.Lights.size()) {
                mapping.EntityToLightIdx[const_cast<SceneEntity*>(&entity)] = lightIndex;
                mapping.LightIndexToEntity.push_back(const_cast<SceneEntity*>(&entity));
                lightIndex++;
            }
        }
        else if (entity.Type == EntityType::Procedural) {
            if (proceduralIndex < scene.ProceduralEntities.size()) {
                mapping.EntityToProceduralIdx[const_cast<SceneEntity*>(&entity)] = proceduralIndex;
                mapping.ProceduralIndexToEntity.push_back(const_cast<SceneEntity*>(&entity));
                proceduralIndex++;
            }
        }

        // Recursively map children
        for (const auto& child : entity.Children) {
            PopulateMappingRecursive(child, scene, mapping, meshIndex, lightIndex, proceduralIndex);
        }
    }

    void SceneLoader::UpdateFlatScene(const SceneEntity& entity, const glm::mat4& parentWorldTransform, Scene& outScene,
            const HierarchyMapping& mapping, std::vector<uint32_t>& outModifiedMeshes,
            std::vector<uint32_t>& outModifiedLights)
    {
        // Compute world transform for this entity
        glm::mat4 worldTransform = entity.LocalTransform.GetWorldMatrix(parentWorldTransform);

        // Update flat arrays if this entity is dirty or parent was (implies this is also dirty)
        if (entity.Type == EntityType::Mesh) {
            auto it = mapping.EntityToMeshIdx.find(const_cast<SceneEntity*>(&entity));
            if (it != mapping.EntityToMeshIdx.end()) {
                uint32_t meshIdx = it->second;
                if (meshIdx < outScene.StaticMeshes.size()) {
                    outScene.StaticMeshes[meshIdx].Transform = worldTransform;
                    outModifiedMeshes.push_back(meshIdx);
                }
            }
        }
        else if (entity.Type == EntityType::Light) {
            auto it = mapping.EntityToLightIdx.find(const_cast<SceneEntity*>(&entity));
            if (it != mapping.EntityToLightIdx.end()) {
                uint32_t lightIdx = it->second;
                if (lightIdx < outScene.Lights.size()) {
                    Light& light               = outScene.Lights[lightIdx];
                    light.Position             = glm::vec3(worldTransform[3]);
                    glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
                    light.Direction = glm::normalize(glm::vec3(worldTransform * glm::vec4(defaultDirection, 0.0f)));
                    outModifiedLights.push_back(lightIdx);
                }
            }
        }

        // Recursively update children
        for (const auto& child : entity.Children) {
            UpdateFlatScene(child, worldTransform, outScene, mapping, outModifiedMeshes, outModifiedLights);
        }
    }

    void SceneLoader::SaveToYAMLWithHierarchy(
            const std::string& filename, const Scene& scene, const SceneEntity& rootEntity)
    {
        auto filepath = Vlkrt::SCENES_DIR + filename;

        try {
            WL_INFO_TAG("SceneLoader", "Saving scene with hierarchy to: {}", filepath);

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
                if (mat.Subsurface > 0.0f) file << "  subsurface: " << mat.Subsurface << "\n";
                if (mat.Anisotropic > 0.0f) file << "  anisotropic: " << mat.Anisotropic << "\n";
                if (mat.Sheen > 0.0f) file << "  sheen: " << mat.Sheen << "\n";
                if (mat.Clearcoat > 0.0f) file << "  clearcoat: " << mat.Clearcoat << "\n";
                if (mat.SpecularTransmission > 0.0f)
                    file << "  specular_transmission: " << mat.SpecularTransmission << "\n";
                if (mat.Eta != 1.5f) file << "  eta: " << mat.Eta << "\n";
                glm::vec3 em = mat.Emission;
                if (em.x > 0 || em.y > 0 || em.z > 0)
                    file << "  emission: [ " << em.x << ", " << em.y << ", " << em.z << " ]\n";

                if (!mat.TextureFilename.empty()) {
                    file << "  texture: " << mat.TextureFilename << "\n";
                    file << "  tiling: " << mat.Tiling << "\n";
                }

                file << "  material_index: " << mat.MaterialIndex << "\n";
            }

            // Write entities section
            file << "\nentities:\n";
            for (const auto& child : rootEntity.Children) { SaveEntityToYAML(file, child, 0); }

            // Preserve scene-wide render settings in YAML
            file << "\nscene_settings:\n";
            file << "  background_color: [ " << scene.BackgroundColor.x << ", " << scene.BackgroundColor.y << ", "
                 << scene.BackgroundColor.z << " ]\n";
            file << "  scene_index: " << scene.SceneIndex << "\n";
            file << "  max_recursion_depth: " << scene.MaxRecursionDepth << "\n";
            file << "  max_shadow_recursion_depth: " << scene.MaxShadowRecursionDepth << "\n";
            file << "  path_sqrt_samples: " << scene.PathSqrtSamplesPerPixel << "\n";
            file << "  russian_roulette_depth: " << scene.RussianRouletteDepth << "\n";
            file << "  apply_jitter: " << (scene.ApplyJitter ? "true" : "false") << "\n";
            file << "  anisotropic_bsdf: " << (scene.AnisotropicBSDF ? "true" : "false") << "\n";
            file << "  enable_fsr: " << (scene.EnableFSR ? "true" : "false") << "\n";
            file << "  fsr_quality_mode: " << scene.FSRQualityMode << "\n";
            file << "  fsr_sharpness: " << scene.FSRSharpness << "\n";
            if (scene.HasCameraHint) {
                file << "  camera_position: [ " << scene.CameraPosition.x << ", " << scene.CameraPosition.y << ", "
                     << scene.CameraPosition.z << " ]\n";
                file << "  camera_target: [ " << scene.CameraTarget.x << ", " << scene.CameraTarget.y << ", "
                     << scene.CameraTarget.z << " ]\n";
            }

            file.close();

            WL_INFO_TAG("SceneLoader", "Scene saved successfully with {} materials, {} meshes, and {} lights",
                    scene.Materials.size(), scene.StaticMeshes.size(), scene.Lights.size());
        }
        catch (const std::exception& e) {
            WL_ERROR_TAG("SceneLoader", "Error saving YAML scene: {} - {}", filepath, e.what());
        }
    }

    void SceneLoader::SaveEntityToYAML(std::ofstream& file, const SceneEntity& entity, int indentLevel)
    {
        std::string indent(indentLevel * 2, ' ');
        file << indent << "- name: " << entity.Name << "\n";

        if (!entity.ScriptPath.empty()) { file << indent << "  script: " << entity.ScriptPath << "\n"; }

        // Write type
        std::string typeStr = "empty";
        if (entity.Type == EntityType::Mesh)
            typeStr = "mesh";
        else if (entity.Type == EntityType::Light)
            typeStr = "light";
        else if (entity.Type == EntityType::Procedural)
            typeStr = "procedural";
        else if (entity.Type == EntityType::Camera)
            typeStr = "camera";
        file << indent << "  type: " << typeStr << "\n";

        // Write transform
        file << indent << "  transform:\n";
        file << indent << "    position: [ " << entity.LocalTransform.Position.x << ", "
             << entity.LocalTransform.Position.y << ", " << entity.LocalTransform.Position.z << " ]\n";
        file << indent << "    rotation: [ " << entity.LocalTransform.Rotation.x << ", "
             << entity.LocalTransform.Rotation.y << ", " << entity.LocalTransform.Rotation.z << ", "
             << entity.LocalTransform.Rotation.w << " ]\n";
        file << indent << "    scale: [ " << entity.LocalTransform.Scale.x << ", " << entity.LocalTransform.Scale.y
             << ", " << entity.LocalTransform.Scale.z << " ]\n";

        // Write mesh-specific data
        if (entity.Type == EntityType::Mesh) {
            file << indent
                 << "  mesh: " << (entity.MeshData.Filename.empty() ? "unknown.obj" : entity.MeshData.Filename) << "\n";
            file << indent << "  material: " << entity.MeshData.MaterialIndex << "\n";
        }

        // Write light-specific data
        if (entity.Type == EntityType::Light) {
            file << indent << "  light_emission: [ " << entity.LightData.Emission.x << ", "
                 << entity.LightData.Emission.y << ", " << entity.LightData.Emission.z << " ]\n";
            file << indent << "  light_intensity: " << entity.LightData.Intensity << "\n";
            file << indent << "  light_type: " << static_cast<uint32_t>(entity.LightData.Type) << "\n";

            glm::vec3 defaultDirection = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 direction        = glm::normalize(
                    glm::vec3(glm::mat4_cast(entity.LocalTransform.Rotation) * glm::vec4(defaultDirection, 0.0f)));
            file << indent << "  light_direction: [ " << direction.x << ", " << direction.y << ", " << direction.z
                 << " ]\n";

            if (entity.LightData.Type == LightType::Square) {
                file << indent << "  light_size: " << entity.LightData.Size << "\n";
            }
        }

        // Write procedural-specific data
        if (entity.Type == EntityType::Procedural) {
            file << indent << "  procedural_analytic: " << (entity.ProceduralData.IsAnalytic ? "true" : "false")
                 << "\n";
            file << indent << "  procedural_type: " << entity.ProceduralData.PrimitiveType << "\n";
            file << indent << "  material: " << entity.ProceduralData.MaterialIndex << "\n";
        }

        // Write children recursively
        if (!entity.Children.empty()) {
            file << indent << "  children:\n";
            for (const auto& child : entity.Children) { SaveEntityToYAML(file, child, indentLevel + 1); }
        }
    }
}  // namespace Vlkrt
