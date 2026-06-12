#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Vlkrt
{
    struct Mesh;
    struct ProceduralEntity;

    /**
     * @brief Manages two BLASes (triangle geometry + AABB procedural geometry) and a single TLAS.
     *
     * Triangle BLAS  — geometry for all triangle meshes.  TLAS instanceSBTOffset = 0.
     * AABB BLAS      — one VkAabbPositionsKHR geometry per ProceduralEntity.  TLAS instanceSBTOffset = 2.
     *
     * The SBT layout expected by the renderer:
     *   HG[0]           : triangle radiance
     *   HG[1]           : triangle shadow
     *   HG[2+i*2+0]     : procedural entity i radiance
     *   HG[2+i*2+1]     : procedural entity i shadow
     */
    class AccelerationStructure
    {
    public:
        AccelerationStructure();
        ~AccelerationStructure();

        void Build(const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer,
                const std::vector<ProceduralEntity>& procedurals = {});
        void Rebuild(const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer,
                const std::vector<ProceduralEntity>& procedurals = {});
        void Cleanup();

        auto GetTLAS() const -> VkAccelerationStructureKHR { return m_TLAS; }
        auto IsBuilt() const -> bool { return m_TLAS != VK_NULL_HANDLE; }
        auto GetProceduralCount() const -> uint32_t { return m_ProceduralCount; }

    private:
        void BuildTriangleBLAS(const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer,
                VkCommandBuffer cmd);
        void BuildAABBBLAS(const std::vector<ProceduralEntity>& procedurals, VkCommandBuffer cmd);
        void BuildTLAS(VkCommandBuffer cmd);
        void CreateScratchBuffer(VkDeviceSize size);

        auto CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                VkDeviceMemory& bufferMemory) const -> VkBuffer;
        auto GetBufferDeviceAddress(VkBuffer buffer) const -> VkDeviceAddress;

    private:
        // Triangle BLAS
        VkAccelerationStructureKHR m_TriangleBLAS       { VK_NULL_HANDLE };
        VkBuffer                   m_TriangleBLASBuffer  { VK_NULL_HANDLE };
        VkDeviceMemory             m_TriangleBLASMemory  { VK_NULL_HANDLE };

        // AABB BLAS (procedural primitives)
        VkAccelerationStructureKHR m_AABBBLAS            { VK_NULL_HANDLE };
        VkBuffer                   m_AABBBLASBuffer      { VK_NULL_HANDLE };
        VkDeviceMemory             m_AABBBLASMemory      { VK_NULL_HANDLE };
        // Host-visible buffer holding one VkAabbPositionsKHR per procedural entity
        VkBuffer                   m_AABBGeomBuffer      { VK_NULL_HANDLE };
        VkDeviceMemory             m_AABBGeomMemory      { VK_NULL_HANDLE };

        // TLAS
        VkAccelerationStructureKHR m_TLAS                { VK_NULL_HANDLE };
        VkBuffer                   m_TLASBuffer          { VK_NULL_HANDLE };
        VkDeviceMemory             m_TLASMemory          { VK_NULL_HANDLE };

        // Instance buffer (host-visible, ≤2 entries: triangle + optional AABB)
        VkBuffer                   m_InstanceBuffer      { VK_NULL_HANDLE };
        VkDeviceMemory             m_InstanceMemory      { VK_NULL_HANDLE };

        // Shared scratch buffer (reused for every BLAS/TLAS build)
        VkBuffer                   m_ScratchBuffer       { VK_NULL_HANDLE };
        VkDeviceMemory             m_ScratchMemory       { VK_NULL_HANDLE };
        VkDeviceSize               m_ScratchBufferSize   { 0 };

        uint32_t                   m_ProceduralCount     { 0 };
        VkDevice                   m_Device              { VK_NULL_HANDLE };
    };
}  // namespace Vlkrt
