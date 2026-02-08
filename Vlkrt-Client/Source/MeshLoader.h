#pragma once

#include "Scene.h"
#include <string>

namespace Vlkrt
{
    class MeshLoader
    {
    public:
        // Load OBJ file and return mesh
        static Mesh LoadOBJ(const std::string& filepath, const glm::mat4& transform = glm::mat4(1.0f));

        // Generate procedural meshes (for player representation)
        static Mesh GenerateCube(float size, const glm::mat4& transform = glm::mat4(1.0f));
        static Mesh GenerateQuad(float size, const glm::mat4& transform = glm::mat4(1.0f));
        static Mesh GenerateIcosphere(float radius, int subdivisions, const glm::mat4& transform = glm::mat4(1.0f));

    private:
        static void CalculateNormals(Mesh& mesh);
        static void CalculateAABB(Mesh& mesh);
    };
}  // namespace Vlkrt
