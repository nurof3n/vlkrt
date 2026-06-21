#include "MeshLoader.h"
#include "Utils.h"

#include "Walnut/Core/Log.h"

#include <algorithm>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <functional>
#include <cstring>
#include <vector>

#include "tiny_obj_loader.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "stb_image.h"
#include "tiny_gltf.h"


namespace Vlkrt
{
    namespace
    {
        static auto ResolveModelPath(const std::string& filename) -> std::filesystem::path
        {
            std::filesystem::path path(filename);
            if (path.is_absolute()) return path;

            std::filesystem::path modelsPath = std::filesystem::path(Vlkrt::MODELS_DIR) / filename;
            if (std::filesystem::exists(modelsPath)) return modelsPath;

            std::filesystem::path scenesPath = std::filesystem::path(Vlkrt::SCENES_DIR) / filename;
            if (std::filesystem::exists(scenesPath)) return scenesPath;

            return path;
        }

        static auto ToRendererRelativeTexturePath(const std::filesystem::path& absoluteTexturePath) -> std::string
        {
            std::filesystem::path modelsRoot = std::filesystem::path(Vlkrt::MODELS_DIR);
            std::error_code ec;
            auto rel = std::filesystem::relative(absoluteTexturePath, modelsRoot, ec);
            if (!ec && !rel.empty() && rel.string().rfind("..", 0) != 0) { return rel.generic_string(); }
            return absoluteTexturePath.lexically_normal().generic_string();
        }

        static auto ReadAccessorUInt(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
                -> uint32_t
        {
            const auto& view    = model.bufferViews[accessor.bufferView];
            const auto& buffer  = model.buffers[view.buffer];
            const size_t stride = accessor.ByteStride(view) ? accessor.ByteStride(view)
                                                            : tinygltf::GetComponentSizeInBytes(accessor.componentType)
                                                                      * tinygltf::GetNumComponentsInType(accessor.type);
            const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset + index * stride;

            switch (accessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return *reinterpret_cast<const uint8_t*>(data);
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return *reinterpret_cast<const uint16_t*>(data);
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return *reinterpret_cast<const uint32_t*>(data);
                default: return 0u;
            }
        }

        static auto ReadAccessorVec2(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
                -> glm::vec2
        {
            const auto& view    = model.bufferViews[accessor.bufferView];
            const auto& buffer  = model.buffers[view.buffer];
            const size_t stride = accessor.ByteStride(view)
                                          ? accessor.ByteStride(view)
                                          : sizeof(float) * tinygltf::GetNumComponentsInType(accessor.type);
            const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset + index * stride;
            const float* f      = reinterpret_cast<const float*>(data);
            return glm::vec2(f[0], f[1]);
        }

        static auto ReadAccessorVec3(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index)
                -> glm::vec3
        {
            const auto& view    = model.bufferViews[accessor.bufferView];
            const auto& buffer  = model.buffers[view.buffer];
            const size_t stride = accessor.ByteStride(view)
                                          ? accessor.ByteStride(view)
                                          : sizeof(float) * tinygltf::GetNumComponentsInType(accessor.type);
            const uint8_t* data = buffer.data.data() + view.byteOffset + accessor.byteOffset + index * stride;
            const float* f      = reinterpret_cast<const float*>(data);
            return glm::vec3(f[0], f[1], f[2]);
        }
    }  // namespace

    auto MeshLoader::LoadOBJ(const std::string& filename, const glm::mat4& transform) -> Mesh
    {
        Mesh mesh;

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        auto filepath = Vlkrt::MODELS_DIR + filename;
        bool success  = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str(), Vlkrt::MODELS_DIR,
                true,  // convert quads to triangles
                true);
        if (!success) {
            WL_ERROR_TAG("MeshLoader", "Failed to load OBJ file '{}': {}", filepath, err);
            return mesh;
        }

        if (!warn.empty()) { WL_WARN_TAG("MeshLoader", "OBJ loading warnings for '{}': {}", filepath, warn); }

        if (shapes.empty()) [[unlikely]] {
            WL_WARN_TAG("MeshLoader", "OBJ file '{}' contains no shapes", filepath);
            return mesh;
        }

        // Combine all shapes into a single mesh
        // (obj files can have multiple named groups, we'll merge them)
        uint32_t vertexOffset = 0;
        for (const auto& shape : shapes) {
            const auto& mesh_data = shape.mesh;
            if (mesh_data.indices.empty()) continue;

            for (size_t faceIdx = 0; faceIdx < mesh_data.indices.size(); faceIdx += 3) {
                auto idx0 = mesh_data.indices[faceIdx + 0];
                auto idx1 = mesh_data.indices[faceIdx + 1];
                auto idx2 = mesh_data.indices[faceIdx + 2];

                // Create three vertices for this triangle
                for (uint32_t i = 0; i < 3; ++i) {
                    auto idx = mesh_data.indices[faceIdx + i];
                    Vertex vertex;

                    // Position
                    if (idx.vertex_index >= 0) {
                        vertex.Position.x = attrib.vertices[3 * idx.vertex_index + 0];
                        vertex.Position.y = attrib.vertices[3 * idx.vertex_index + 1];
                        vertex.Position.z = attrib.vertices[3 * idx.vertex_index + 2];
                    }
                    else {
                        vertex.Position = glm::vec3(0.0f);
                        WL_WARN_TAG("MeshLoader", "Vertex without position in OBJ file");
                    }

                    // Normal
                    if (idx.normal_index >= 0) {
                        vertex.Normal.x = attrib.normals[3 * idx.normal_index + 0];
                        vertex.Normal.y = attrib.normals[3 * idx.normal_index + 1];
                        vertex.Normal.z = attrib.normals[3 * idx.normal_index + 2];
                    }
                    else {
                        vertex.Normal = glm::vec3(0.0f);  // Will be calculated later
                    }

                    // Texture coordinate
                    if (idx.texcoord_index >= 0) {
                        vertex.TexCoord.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                        vertex.TexCoord.y = attrib.texcoords[2 * idx.texcoord_index + 1];
                    }
                    else {
                        vertex.TexCoord = glm::vec2(0.0f);
                    }

                    mesh.Vertices.push_back(vertex);
                }

                mesh.Indices.push_back(vertexOffset + 0);
                mesh.Indices.push_back(vertexOffset + 1);
                mesh.Indices.push_back(vertexOffset + 2);
                vertexOffset += 3;
            }
        }

        // If the obj file didn't have normals, calculate them
        bool hasNormals = true;
        for (const auto& vertex : mesh.Vertices) {
            if (glm::length(vertex.Normal) < 0.001f) {
                hasNormals = false;
                break;
            }
        }
        if (!hasNormals) {
            WL_INFO_TAG("MeshLoader", "Calculating normals for OBJ file '{}'", filepath);
            CalculateNormals(mesh);
        }

        // Set material index from obj
        if (!shapes.empty() && !shapes[0].mesh.material_ids.empty()) {
            mesh.MaterialIndex = shapes[0].mesh.material_ids[0];
        }

        // Apply transform
        mesh.Transform = transform;

        WL_INFO_TAG("MeshLoader", "Loaded OBJ file '{}': {} vertices", filepath, mesh.Vertices.size());

        return mesh;
    }

    auto MeshLoader::LoadGLTF(const std::string& filename, const glm::mat4& transform) -> LoadedGLTFScene
    {
        LoadedGLTFScene result;

        std::filesystem::path filepath = ResolveModelPath(filename);
        if (!std::filesystem::exists(filepath)) {
            WL_ERROR_TAG("MeshLoader", "glTF file not found: {}", filepath.string());
            return result;
        }

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string warn, err;

        bool loaded     = false;
        std::string ext = filepath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".glb")
            loaded = loader.LoadBinaryFromFile(&model, &err, &warn, filepath.string());
        else
            loaded = loader.LoadASCIIFromFile(&model, &err, &warn, filepath.string());

        if (!warn.empty()) { WL_WARN_TAG("MeshLoader", "glTF warnings for '{}': {}", filepath.string(), warn); }
        if (!loaded) {
            WL_ERROR_TAG("MeshLoader", "Failed to load glTF '{}': {}", filepath.string(), err);
            return result;
        }

        auto texturePathFromIndex = [&](int textureIndex) -> std::string {
            if (textureIndex < 0 || textureIndex >= (int) model.textures.size()) return {};
            const auto& tex = model.textures[textureIndex];
            if (tex.source < 0 || tex.source >= (int) model.images.size()) return {};

            const auto& img = model.images[tex.source];
            if (img.uri.empty()) {
                WL_WARN_TAG("MeshLoader", "Embedded image '{}' is not exported to file path; skipping texture path",
                        img.name);
                return {};
            }

            std::filesystem::path imageAbsolute = filepath.parent_path() / img.uri;
            return ToRendererRelativeTexturePath(imageAbsolute);
        };

        result.Materials.reserve(model.materials.empty() ? 1 : model.materials.size());
        std::vector<bool> skippedMaterials(model.materials.size(), false);
        if (model.materials.empty()) { result.Materials.push_back(Material{}); }
        else {
            for (size_t i = 0; i < model.materials.size(); ++i) {
                const auto& gm = model.materials[i];

                if (gm.alphaMode == "BLEND") {
                    skippedMaterials[i] = true;
                    WL_INFO_TAG("MeshLoader", "Skipping unsupported BLEND material '{}' in glTF '{}'", gm.name,
                            filepath.string());
                    result.Materials.push_back(Material{});
                    continue;
                }

                Material mat{};
                mat.Name = gm.name.empty() ? ("GLTF_Material_" + std::to_string(i)) : gm.name;

                if (gm.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                    mat.Albedo = glm::vec3((float) gm.pbrMetallicRoughness.baseColorFactor[0],
                            (float) gm.pbrMetallicRoughness.baseColorFactor[1],
                            (float) gm.pbrMetallicRoughness.baseColorFactor[2]);
                }
                mat.Metallic  = (float) gm.pbrMetallicRoughness.metallicFactor;
                mat.Roughness = (float) gm.pbrMetallicRoughness.roughnessFactor;

                if (gm.emissiveFactor.size() == 3) {
                    mat.Emission = glm::vec3(
                            (float) gm.emissiveFactor[0], (float) gm.emissiveFactor[1], (float) gm.emissiveFactor[2]);
                }

                mat.TextureAlbedoFilename = texturePathFromIndex(gm.pbrMetallicRoughness.baseColorTexture.index);
                mat.TextureFilename       = mat.TextureAlbedoFilename;
                mat.TextureNormalFilename = texturePathFromIndex(gm.normalTexture.index);
                mat.TextureMetallicRoughnessFilename
                        = texturePathFromIndex(gm.pbrMetallicRoughness.metallicRoughnessTexture.index);
                mat.TextureEmissiveFilename = texturePathFromIndex(gm.emissiveTexture.index);
                // Skip occlusion texture to save VRAM (not sampled in shader)
                mat.TextureOcclusionFilename = "";

                result.Materials.push_back(mat);
            }
        }

        auto nodeTransform = [](const tinygltf::Node& node) -> glm::mat4 {
            if (node.matrix.size() == 16) {
                glm::mat4 m(1.0f);
                for (int i = 0; i < 16; ++i) { m[i / 4][i % 4] = (float) node.matrix[i]; }
                return m;
            }

            glm::vec3 translation(0.0f);
            glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 scale(1.0f);
            if (node.translation.size() == 3) {
                translation = glm::vec3(
                        (float) node.translation[0], (float) node.translation[1], (float) node.translation[2]);
            }
            if (node.rotation.size() == 4) {
                rotation = glm::quat((float) node.rotation[3], (float) node.rotation[0], (float) node.rotation[1],
                        (float) node.rotation[2]);
            }
            if (node.scale.size() == 3) {
                scale = glm::vec3((float) node.scale[0], (float) node.scale[1], (float) node.scale[2]);
            }
            return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation)
                   * glm::scale(glm::mat4(1.0f), scale);
        };

        std::function<void(int, const glm::mat4&)> loadNode;
        loadNode = [&](int nodeIndex, const glm::mat4& parent) {
            const tinygltf::Node& node = model.nodes[nodeIndex];
            glm::mat4 world            = parent * nodeTransform(node);

            if (node.mesh >= 0 && node.mesh < (int) model.meshes.size()) {
                const auto& gltfMesh = model.meshes[node.mesh];

                for (size_t primIdx = 0; primIdx < gltfMesh.primitives.size(); ++primIdx) {
                    const auto& primitive = gltfMesh.primitives[primIdx];
                    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;
                    if (primitive.attributes.find("POSITION") == primitive.attributes.end()) continue;
                    if (primitive.material >= 0 && primitive.material < (int) skippedMaterials.size()
                            && skippedMaterials[(size_t) primitive.material]) {
                        continue;
                    }

                    Mesh mesh{};
                    mesh.Name          = gltfMesh.name.empty() ? ("Mesh_" + std::to_string(node.mesh)) : gltfMesh.name;
                    mesh.Filename      = filename;
                    mesh.Transform     = transform * world;
                    mesh.MaterialIndex = (primitive.material >= 0) ? (uint32_t) primitive.material : 0u;

                    const tinygltf::Accessor& posAccessor    = model.accessors[primitive.attributes.at("POSITION")];
                    const tinygltf::Accessor* normalAccessor = nullptr;
                    const tinygltf::Accessor* uvAccessor     = nullptr;
                    if (auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end()) {
                        normalAccessor = &model.accessors[it->second];
                    }
                    if (auto it = primitive.attributes.find("TEXCOORD_0"); it != primitive.attributes.end()) {
                        uvAccessor = &model.accessors[it->second];
                    }

                    mesh.Vertices.reserve(posAccessor.count);
                    for (size_t vi = 0; vi < posAccessor.count; ++vi) {
                        Vertex v{};
                        v.Position = ReadAccessorVec3(model, posAccessor, vi);
                        v.Normal   = normalAccessor ? ReadAccessorVec3(model, *normalAccessor, vi)
                                                    : glm::vec3(0.0f, 1.0f, 0.0f);
                        v.TexCoord = uvAccessor ? ReadAccessorVec2(model, *uvAccessor, vi) : glm::vec2(0.0f);
                        mesh.Vertices.push_back(v);
                    }

                    if (primitive.indices >= 0) {
                        const tinygltf::Accessor& idxAccessor = model.accessors[primitive.indices];
                        mesh.Indices.reserve(idxAccessor.count);
                        for (size_t ii = 0; ii < idxAccessor.count; ++ii) {
                            mesh.Indices.push_back(ReadAccessorUInt(model, idxAccessor, ii));
                        }
                    }
                    else {
                        mesh.Indices.reserve(mesh.Vertices.size());
                        for (uint32_t i = 0; i < (uint32_t) mesh.Vertices.size(); ++i) { mesh.Indices.push_back(i); }
                    }

                    result.Meshes.push_back(std::move(mesh));
                }
            }

            for (int child : node.children) { loadNode(child, world); }
        };

        int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (sceneIndex < (int) model.scenes.size()) {
            for (int rootNode : model.scenes[sceneIndex].nodes) { loadNode(rootNode, glm::mat4(1.0f)); }
        }
        else {
            for (size_t i = 0; i < model.nodes.size(); ++i) { loadNode((int) i, glm::mat4(1.0f)); }
        }

        WL_INFO_TAG("MeshLoader", "Loaded glTF '{}' with {} meshes and {} materials", filepath.string(),
                result.Meshes.size(), result.Materials.size());
        return result;
    }

    auto MeshLoader::GenerateCube(float size, const glm::mat4& transform) -> Mesh
    {
        Mesh mesh;
        float h = size * 0.5f;

        glm::vec3 positions[8] = { { -h, -h, -h }, { h, -h, -h }, { h, h, -h }, { -h, h, -h }, { -h, -h, h },
            { h, -h, h }, { h, h, h }, { -h, h, h } };

        // +Z
        mesh.Vertices.push_back({ { -h, -h, h }, { 0, 0, 1 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, -h, h }, { 0, 0, 1 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, h, h }, { 0, 0, 1 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, h, h }, { 0, 0, 1 }, { 0, 1 } });

        // -Z
        mesh.Vertices.push_back({ { h, -h, -h }, { 0, 0, -1 }, { 0, 0 } });
        mesh.Vertices.push_back({ { -h, -h, -h }, { 0, 0, -1 }, { 1, 0 } });
        mesh.Vertices.push_back({ { -h, h, -h }, { 0, 0, -1 }, { 1, 1 } });
        mesh.Vertices.push_back({ { h, h, -h }, { 0, 0, -1 }, { 0, 1 } });

        // +X
        mesh.Vertices.push_back({ { h, -h, h }, { 1, 0, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, -h, -h }, { 1, 0, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, h, -h }, { 1, 0, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { h, h, h }, { 1, 0, 0 }, { 0, 1 } });

        // -X
        mesh.Vertices.push_back({ { -h, -h, -h }, { -1, 0, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { -h, -h, h }, { -1, 0, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { -h, h, h }, { -1, 0, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, h, -h }, { -1, 0, 0 }, { 0, 1 } });

        // +Y
        mesh.Vertices.push_back({ { -h, h, h }, { 0, 1, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, h, h }, { 0, 1, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, h, -h }, { 0, 1, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, h, -h }, { 0, 1, 0 }, { 0, 1 } });

        // -Y
        mesh.Vertices.push_back({ { -h, -h, -h }, { 0, -1, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, -h, -h }, { 0, -1, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, -h, h }, { 0, -1, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, -h, h }, { 0, -1, 0 }, { 0, 1 } });

        for (uint32_t i = 0; i < 6; ++i) {
            auto base = i * 4;
            mesh.Indices.push_back(base + 0);
            mesh.Indices.push_back(base + 1);
            mesh.Indices.push_back(base + 2);
            mesh.Indices.push_back(base + 2);
            mesh.Indices.push_back(base + 3);
            mesh.Indices.push_back(base + 0);
        }

        // Apply transform
        mesh.Transform = transform;

        return mesh;
    }

    auto MeshLoader::GenerateQuad(float size, const glm::mat4& transform) -> Mesh
    {
        Mesh mesh;
        float h = size * 0.5f;

        // Quad in XZ plane (y=0) with upward normal (+Y)
        mesh.Vertices.push_back({ { -h, 0.0f, h }, { 0, 1, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, 0.0f, h }, { 0, 1, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, 0.0f, -h }, { 0, 1, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, 0.0f, -h }, { 0, 1, 0 }, { 0, 1 } });

        mesh.Indices.push_back(0);
        mesh.Indices.push_back(1);
        mesh.Indices.push_back(2);
        mesh.Indices.push_back(2);
        mesh.Indices.push_back(3);
        mesh.Indices.push_back(0);

        // Apply transform
        mesh.Transform = transform;

        return mesh;
    }

    void MeshLoader::CalculateNormals(Mesh& mesh)
    {
        // Reset all normals to zero
        for (auto& vertex : mesh.Vertices) { vertex.Normal = glm::vec3(0.0f); }

        // Calculate face normals and accumulate
        for (size_t i = 0; i < mesh.Indices.size(); i += 3) {
            auto i0 = mesh.Indices[i + 0];
            auto i1 = mesh.Indices[i + 1];
            auto i2 = mesh.Indices[i + 2];

            glm::vec3& v0 = mesh.Vertices[i0].Position;
            glm::vec3& v1 = mesh.Vertices[i1].Position;
            glm::vec3& v2 = mesh.Vertices[i2].Position;

            glm::vec3 edge1      = v1 - v0;
            glm::vec3 edge2      = v2 - v0;
            glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

            mesh.Vertices[i0].Normal += faceNormal;
            mesh.Vertices[i1].Normal += faceNormal;
            mesh.Vertices[i2].Normal += faceNormal;
        }
    }
}  // namespace Vlkrt
