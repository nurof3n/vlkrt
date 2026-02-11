#include "MeshLoader.h"
#include "Utils.h"

#include "Walnut/Core/Log.h"

#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

#include "tiny_obj_loader.h"


namespace Vlkrt
{
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

        // Apply transform and calculate AABB
        mesh.Transform = transform;
        CalculateAABB(mesh);

        WL_INFO_TAG("MeshLoader", "Loaded OBJ file '{}': {} vertices", filepath, mesh.Vertices.size());

        return mesh;
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

        // Apply transform and calculate AABB
        mesh.Transform = transform;
        CalculateAABB(mesh);

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

        // Apply transform and calculate AABB
        mesh.Transform = transform;
        CalculateAABB(mesh);

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

    void MeshLoader::CalculateAABB(Mesh& mesh)
    {
        if (mesh.Vertices.empty()) return;

        mesh.AABBMin = mesh.Vertices[0].Position;
        mesh.AABBMax = mesh.Vertices[0].Position;

        for (const auto& vertex : mesh.Vertices) {
            mesh.AABBMin = glm::min(mesh.AABBMin, vertex.Position);
            mesh.AABBMax = glm::max(mesh.AABBMax, vertex.Position);
        }
    }
}  // namespace Vlkrt
