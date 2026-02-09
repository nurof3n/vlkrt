#include "MeshLoader.h"
#include "Utils.h"

#include "Walnut/Core/Log.h"

#include <filesystem>

#include <glm/gtc/matrix_transform.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"


namespace Vlkrt
{
    Mesh MeshLoader::GenerateCube(float size, const glm::mat4& transform)
    {
        Mesh  mesh;
        float h = size * 0.5f;

        // Define 8 vertices of a cube
        glm::vec3 positions[8] = {
            { -h, -h, -h },  // 0: left-bottom-back
            { h, -h, -h },   // 1: right-bottom-back
            { h, h, -h },    // 2: right-top-back
            { -h, h, -h },   // 3: left-top-back
            { -h, -h, h },   // 4: left-bottom-front
            { h, -h, h },    // 5: right-bottom-front
            { h, h, h },     // 6: right-top-front
            { -h, h, h }     // 7: left-top-front
        };

        // Define 6 faces with normals (each face = 2 triangles = 6 vertices)
        // Front face (+Z)
        mesh.Vertices.push_back({ { -h, -h, h }, { 0, 0, 1 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, -h, h }, { 0, 0, 1 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, h, h }, { 0, 0, 1 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, h, h }, { 0, 0, 1 }, { 0, 1 } });

        // Back face (-Z)
        mesh.Vertices.push_back({ { h, -h, -h }, { 0, 0, -1 }, { 0, 0 } });
        mesh.Vertices.push_back({ { -h, -h, -h }, { 0, 0, -1 }, { 1, 0 } });
        mesh.Vertices.push_back({ { -h, h, -h }, { 0, 0, -1 }, { 1, 1 } });
        mesh.Vertices.push_back({ { h, h, -h }, { 0, 0, -1 }, { 0, 1 } });

        // Right face (+X)
        mesh.Vertices.push_back({ { h, -h, h }, { 1, 0, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, -h, -h }, { 1, 0, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, h, -h }, { 1, 0, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { h, h, h }, { 1, 0, 0 }, { 0, 1 } });

        // Left face (-X)
        mesh.Vertices.push_back({ { -h, -h, -h }, { -1, 0, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { -h, -h, h }, { -1, 0, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { -h, h, h }, { -1, 0, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, h, -h }, { -1, 0, 0 }, { 0, 1 } });

        // Top face (+Y)
        mesh.Vertices.push_back({ { -h, h, h }, { 0, 1, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, h, h }, { 0, 1, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, h, -h }, { 0, 1, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, h, -h }, { 0, 1, 0 }, { 0, 1 } });

        // Bottom face (-Y)
        mesh.Vertices.push_back({ { -h, -h, -h }, { 0, -1, 0 }, { 0, 0 } });
        mesh.Vertices.push_back({ { h, -h, -h }, { 0, -1, 0 }, { 1, 0 } });
        mesh.Vertices.push_back({ { h, -h, h }, { 0, -1, 0 }, { 1, 1 } });
        mesh.Vertices.push_back({ { -h, -h, h }, { 0, -1, 0 }, { 0, 1 } });

        // Define indices for all 6 faces (2 triangles per face)
        for (uint32_t i = 0; i < 6; i++) {
            uint32_t base = i * 4;
            // First triangle
            mesh.Indices.push_back(base + 0);
            mesh.Indices.push_back(base + 1);
            mesh.Indices.push_back(base + 2);
            // Second triangle
            mesh.Indices.push_back(base + 2);
            mesh.Indices.push_back(base + 3);
            mesh.Indices.push_back(base + 0);
        }

        mesh.Transform = transform;
        CalculateAABB(mesh);

        return mesh;
    }

    Mesh MeshLoader::GenerateQuad(float size, const glm::mat4& transform)
    {
        Mesh  mesh;
        float h = size * 0.5f;

        // Quad in XZ plane (Y=0) with upward normal (+Y)
        // 4 vertices for a simple quad
        mesh.Vertices.push_back({ { -h, 0.0f, h }, { 0, 1, 0 }, { 0, 0 } });   // bottom-left
        mesh.Vertices.push_back({ { h, 0.0f, h }, { 0, 1, 0 }, { 1, 0 } });    // bottom-right
        mesh.Vertices.push_back({ { h, 0.0f, -h }, { 0, 1, 0 }, { 1, 1 } });   // top-right
        mesh.Vertices.push_back({ { -h, 0.0f, -h }, { 0, 1, 0 }, { 0, 1 } });  // top-left

        // Two triangles (6 indices)
        // First triangle
        mesh.Indices.push_back(0);
        mesh.Indices.push_back(1);
        mesh.Indices.push_back(2);
        // Second triangle
        mesh.Indices.push_back(2);
        mesh.Indices.push_back(3);
        mesh.Indices.push_back(0);

        mesh.Transform = transform;
        CalculateAABB(mesh);

        return mesh;
    }

    Mesh MeshLoader::LoadOBJ(const std::string& filename, const glm::mat4& transform)
    {
        Mesh mesh;

        auto filepath = Vlkrt::MODELS_DIR + filename;

        tinyobj::attrib_t                attrib;
        std::vector<tinyobj::shape_t>    shapes;
        std::vector<tinyobj::material_t> materials;
        std::string                      warn, err;

        bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str(), Vlkrt::MODELS_DIR,
                true,  // convert quads to triangles
                true);

        // Handle errors
        if (!success) {
            WL_ERROR_TAG("MeshLoader", "Failed to load OBJ file '{}': {}", filepath, err);
            return mesh;
        }

        if (!warn.empty()) {
            WL_WARN_TAG("MeshLoader", "OBJ loading warnings for '{}': {}", filepath, warn);
        }

        // If file is empty
        if (shapes.empty()) {
            WL_WARN_TAG("MeshLoader", "OBJ file '{}' contains no shapes", filepath);
            return mesh;
        }

        // Combine all shapes into a single mesh
        // (OBJ files can have multiple named groups, we'll merge them)
        uint32_t vertexOffset = 0;

        for (const auto& shape : shapes) {
            const auto& mesh_data = shape.mesh;

            // If there are no vertices in this shape, skip
            if (mesh_data.indices.empty())
                continue;

            // Process each face (triangle after triangulation)
            for (size_t faceIdx = 0; faceIdx < mesh_data.indices.size(); faceIdx += 3) {
                // Get the three vertex indices for this triangle
                tinyobj::index_t idx0 = mesh_data.indices[faceIdx + 0];
                tinyobj::index_t idx1 = mesh_data.indices[faceIdx + 1];
                tinyobj::index_t idx2 = mesh_data.indices[faceIdx + 2];

                // Create three vertices for this triangle
                for (int i = 0; i < 3; ++i) {
                    tinyobj::index_t idx = mesh_data.indices[faceIdx + i];
                    Vertex           vertex;

                    // Position (always present in valid OBJ)
                    if (idx.vertex_index >= 0) {
                        vertex.Position.x = attrib.vertices[3 * idx.vertex_index + 0];
                        vertex.Position.y = attrib.vertices[3 * idx.vertex_index + 1];
                        vertex.Position.z = attrib.vertices[3 * idx.vertex_index + 2];
                    }
                    else {
                        vertex.Position = glm::vec3(0.0f);
                        WL_WARN_TAG("MeshLoader", "Vertex without position in OBJ file");
                    }

                    // Normal (may not be present)
                    if (idx.normal_index >= 0) {
                        vertex.Normal.x = attrib.normals[3 * idx.normal_index + 0];
                        vertex.Normal.y = attrib.normals[3 * idx.normal_index + 1];
                        vertex.Normal.z = attrib.normals[3 * idx.normal_index + 2];
                    }
                    else {
                        vertex.Normal = glm::vec3(0.0f);  // Will be calculated later
                    }

                    // Texture coordinate (may not be present)
                    if (idx.texcoord_index >= 0) {
                        vertex.TexCoord.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                        vertex.TexCoord.y = attrib.texcoords[2 * idx.texcoord_index + 1];
                    }
                    else {
                        vertex.TexCoord = glm::vec2(0.0f);
                    }

                    mesh.Vertices.push_back(vertex);
                }

                // Add indices for this triangle
                mesh.Indices.push_back(vertexOffset + 0);
                mesh.Indices.push_back(vertexOffset + 1);
                mesh.Indices.push_back(vertexOffset + 2);
                vertexOffset += 3;
            }
        }

        // If the OBJ file didn't have normals, calculate them
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

        // Set material index from OBJ (default to 0)
        // Per-face material support would require storing material indices per triangle
        if (!shapes.empty() && !shapes[0].mesh.material_ids.empty()) {
            mesh.MaterialIndex = shapes[0].mesh.material_ids[0];
        }

        // Apply transform and calculate AABB
        mesh.Transform = transform;
        CalculateAABB(mesh);

        WL_INFO_TAG("MeshLoader", "Loaded OBJ file '{}': {} vertices, {} triangles", filepath, mesh.Vertices.size(),
                mesh.Indices.size() / 3);

        return mesh;
    }

    void MeshLoader::CalculateNormals(Mesh& mesh)
    {
        // Reset all normals to zero
        for (auto& vertex : mesh.Vertices) {
            vertex.Normal = glm::vec3(0.0f);
        }

        // Calculate face normals and accumulate
        for (size_t i = 0; i < mesh.Indices.size(); i += 3) {
            uint32_t i0 = mesh.Indices[i + 0];
            uint32_t i1 = mesh.Indices[i + 1];
            uint32_t i2 = mesh.Indices[i + 2];

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

        // Normalize all vertex normals
        for (auto& vertex : mesh.Vertices) {
            vertex.Normal = glm::normalize(vertex.Normal);
        }
    }

    void MeshLoader::CalculateAABB(Mesh& mesh)
    {
        if (mesh.Vertices.empty())
            return;

        mesh.AABBMin = mesh.Vertices[0].Position;
        mesh.AABBMax = mesh.Vertices[0].Position;

        for (const auto& vertex : mesh.Vertices) {
            mesh.AABBMin = glm::min(mesh.AABBMin, vertex.Position);
            mesh.AABBMax = glm::max(mesh.AABBMax, vertex.Position);
        }
    }
}  // namespace Vlkrt
