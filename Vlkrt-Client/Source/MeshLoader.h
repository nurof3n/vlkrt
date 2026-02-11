#pragma once

#include "Scene.h"
#include <string>

namespace Vlkrt
{
    /**
     * @brief Class used to load meshes (from OBJ files) and generate procedural meshes.
     *
     */
    class MeshLoader
    {
    public:
        static auto LoadOBJ(const std::string& filename, const glm::mat4& transform = glm::mat4(1.0f)) -> Mesh;
        static auto GenerateCube(float size, const glm::mat4& transform = glm::mat4(1.0f)) -> Mesh;
        static auto GenerateQuad(float size, const glm::mat4& transform = glm::mat4(1.0f)) -> Mesh;

    private:
        static void CalculateNormals(Mesh& mesh);
    };
}  // namespace Vlkrt
