#pragma once

#include "Scene.h"
#include <string>

namespace Vlkrt
{
    /**
     * @brief Class used to load meshes (from OBJ files) and generate procedural meshes.
     * Contains utility functions for calculating normals and AABBs as well.
     *
     */
    class MeshLoader
    {
    public:
        static Mesh LoadOBJ(const std::string& filename, const glm::mat4& transform = glm::mat4(1.0f));
        static Mesh GenerateCube(float size, const glm::mat4& transform = glm::mat4(1.0f));
        static Mesh GenerateQuad(float size, const glm::mat4& transform = glm::mat4(1.0f));

    private:
        static void CalculateNormals(Mesh& mesh);
        static void CalculateAABB(Mesh& mesh);
    };
}  // namespace Vlkrt
