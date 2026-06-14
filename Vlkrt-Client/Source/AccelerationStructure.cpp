#include "AccelerationStructure.h"
#include "Scene.h"
#include "Renderer.h"

#include "Walnut/Application.h"
#include "Walnut/VulkanRayTracing.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include <glm/glm.hpp>

namespace Vlkrt
{
    AccelerationStructure::AccelerationStructure() { m_Device = Walnut::Application::GetDevice(); }

    AccelerationStructure::~AccelerationStructure() { Cleanup(); }

    void AccelerationStructure::Build(const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer,
            const std::vector<ProceduralEntity>& procedurals)
    {
        if (meshes.empty() || vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE) return;

        VkCommandBuffer cmd = Walnut::Application::GetCommandBuffer(true);

        BuildTriangleBLAS(meshes, vertexBuffer, indexBuffer, cmd);

        if (!procedurals.empty()) BuildAABBBLAS(procedurals, cmd);

        m_ProceduralCount = static_cast<uint32_t>(procedurals.size());
        BuildTLAS(cmd);

        Walnut::Application::FlushCommandBuffer(cmd);
    }

    void AccelerationStructure::Rebuild(const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer,
            const std::vector<ProceduralEntity>& procedurals)
    {
        Cleanup();
        Build(meshes, vertexBuffer, indexBuffer, procedurals);
    }

    void AccelerationStructure::Cleanup()
    {
        auto defer = [&](auto&& fn) { Walnut::Application::SubmitResourceFree(std::forward<decltype(fn)>(fn)); };

        if (m_TLAS != VK_NULL_HANDLE) {
            defer([device = m_Device, h = m_TLAS]() { pvkDestroyAccelerationStructureKHR(device, h, nullptr); });
            m_TLAS = VK_NULL_HANDLE;
        }
        if (m_TLASBuffer != VK_NULL_HANDLE) {
            defer([device = m_Device, b = m_TLASBuffer, m = m_TLASMemory]() {
                vkDestroyBuffer(device, b, nullptr);
                vkFreeMemory(device, m, nullptr);
            });
            m_TLASBuffer = VK_NULL_HANDLE;
        }

        if (m_AABBBLAS != VK_NULL_HANDLE) {
            defer([device = m_Device, h = m_AABBBLAS]() { pvkDestroyAccelerationStructureKHR(device, h, nullptr); });
            m_AABBBLAS = VK_NULL_HANDLE;
        }
        if (m_AABBBLASBuffer != VK_NULL_HANDLE) {
            defer([device = m_Device, b = m_AABBBLASBuffer, m = m_AABBBLASMemory]() {
                vkDestroyBuffer(device, b, nullptr);
                vkFreeMemory(device, m, nullptr);
            });
            m_AABBBLASBuffer = VK_NULL_HANDLE;
        }
        if (m_AABBGeomBuffer != VK_NULL_HANDLE) {
            defer([device = m_Device, b = m_AABBGeomBuffer, m = m_AABBGeomMemory]() {
                vkDestroyBuffer(device, b, nullptr);
                vkFreeMemory(device, m, nullptr);
            });
            m_AABBGeomBuffer = VK_NULL_HANDLE;
        }

        if (m_TriangleBLAS != VK_NULL_HANDLE) {
            defer([device = m_Device, h = m_TriangleBLAS]() {
                pvkDestroyAccelerationStructureKHR(device, h, nullptr);
            });
            m_TriangleBLAS = VK_NULL_HANDLE;
        }
        if (m_TriangleBLASBuffer != VK_NULL_HANDLE) {
            defer([device = m_Device, b = m_TriangleBLASBuffer, m = m_TriangleBLASMemory]() {
                vkDestroyBuffer(device, b, nullptr);
                vkFreeMemory(device, m, nullptr);
            });
            m_TriangleBLASBuffer = VK_NULL_HANDLE;
        }

        if (m_InstanceBuffer != VK_NULL_HANDLE) {
            defer([device = m_Device, b = m_InstanceBuffer, m = m_InstanceMemory]() {
                vkDestroyBuffer(device, b, nullptr);
                vkFreeMemory(device, m, nullptr);
            });
            m_InstanceBuffer = VK_NULL_HANDLE;
        }

        if (m_ScratchBuffer != VK_NULL_HANDLE) {
            defer([device = m_Device, b = m_ScratchBuffer, m = m_ScratchMemory]() {
                vkDestroyBuffer(device, b, nullptr);
                vkFreeMemory(device, m, nullptr);
            });
            m_ScratchBuffer     = VK_NULL_HANDLE;
            m_ScratchBufferSize = 0;
        }

        m_ProceduralCount = 0;
    }

    void AccelerationStructure::BuildTriangleBLAS(
            const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer, VkCommandBuffer cmd)
    {
        uint32_t totalTriangles = 0;
        uint32_t totalVertices  = 0;
        for (const auto& mesh : meshes) {
            totalTriangles += static_cast<uint32_t>(mesh.Indices.size() / 3);
            totalVertices += static_cast<uint32_t>(mesh.Vertices.size());
        }

        VkAccelerationStructureGeometryTrianglesDataKHR trianglesData
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
        trianglesData.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
        trianglesData.vertexData.deviceAddress = GetBufferDeviceAddress(vertexBuffer);
        trianglesData.vertexStride             = sizeof(GPUVertex);
        trianglesData.maxVertex                = totalVertices > 0 ? totalVertices - 1 : 0;
        trianglesData.indexType                = VK_INDEX_TYPE_UINT32;
        trianglesData.indexData.deviceAddress  = GetBufferDeviceAddress(indexBuffer);

        VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType                       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles                 = trianglesData;
        geometry.flags                              = VK_GEOMETRY_OPAQUE_BIT_KHR;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries   = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        pvkGetAccelerationStructureBuildSizesKHR(
                m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &totalTriangles, &sizeInfo);

        m_TriangleBLASBuffer = CreateBuffer(sizeInfo.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_TriangleBLASMemory);

        VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer                               = m_TriangleBLASBuffer;
        createInfo.size                                 = sizeInfo.accelerationStructureSize;
        createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        pvkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &m_TriangleBLAS);

        CreateScratchBuffer(sizeInfo.buildScratchSize);
        buildInfo.dstAccelerationStructure  = m_TriangleBLAS;
        buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(m_ScratchBuffer);

        VkAccelerationStructureBuildRangeInfoKHR buildRange{};
        buildRange.primitiveCount                              = totalTriangles;
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &buildRange;
        pvkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);

        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    void AccelerationStructure::BuildAABBBLAS(const std::vector<ProceduralEntity>& procedurals, VkCommandBuffer cmd)
    {
        uint32_t count = static_cast<uint32_t>(procedurals.size());

        // Compute world-space tight AABB for each entity (unit cube [-1,1]^3 transformed)
        static const glm::vec3 kCorners[8] = {
            { -1, -1, -1 },
            { +1, -1, -1 },
            { -1, +1, -1 },
            { +1, +1, -1 },
            { -1, -1, +1 },
            { +1, -1, +1 },
            { -1, +1, +1 },
            { +1, +1, +1 },
        };

        std::vector<VkAabbPositionsKHR> aabbs(count);
        for (uint32_t i = 0; i < count; i++) {
            glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
            for (const auto& c : kCorners) {
                glm::vec3 w = glm::vec3(procedurals[i].Transform * glm::vec4(c, 1.0f));
                mn          = glm::min(mn, w);
                mx          = glm::max(mx, w);
            }
            aabbs[i] = { mn.x, mn.y, mn.z, mx.x, mx.y, mx.z };
        }

        // Upload AABBs to a host-visible buffer
        VkDeviceSize aabbBufSize = sizeof(VkAabbPositionsKHR) * count;
        m_AABBGeomBuffer         = CreateBuffer(aabbBufSize,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_AABBGeomMemory);
        void* mapped;
        vkMapMemory(m_Device, m_AABBGeomMemory, 0, aabbBufSize, 0, &mapped);
        memcpy(mapped, aabbs.data(), aabbBufSize);
        vkUnmapMemory(m_Device, m_AABBGeomMemory);

        VkDeviceAddress aabbBufAddr = GetBufferDeviceAddress(m_AABBGeomBuffer);

        // One VkAccelerationStructureGeometryKHR per procedural entity so each gets
        // its own SBT hit-group record (indexed by geometryIndex).
        std::vector<VkAccelerationStructureGeometryKHR> geometries(count);
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges(count);
        std::vector<uint32_t> primCounts(count, 1u);

        for (uint32_t i = 0; i < count; i++) {
            auto& geom          = geometries[i];
            geom                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geom.geometryType   = VK_GEOMETRY_TYPE_AABBS_KHR;
            geom.flags          = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geom.geometry.aabbs = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR };
            geom.geometry.aabbs.data.deviceAddress = aabbBufAddr + i * sizeof(VkAabbPositionsKHR);
            geom.geometry.aabbs.stride             = sizeof(VkAabbPositionsKHR);

            buildRanges[i].primitiveCount  = 1;
            buildRanges[i].primitiveOffset = 0;
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = count;
        buildInfo.pGeometries   = geometries.data();

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        pvkGetAccelerationStructureBuildSizesKHR(
                m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, primCounts.data(), &sizeInfo);

        m_AABBBLASBuffer = CreateBuffer(sizeInfo.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_AABBBLASMemory);

        VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer                               = m_AABBBLASBuffer;
        createInfo.size                                 = sizeInfo.accelerationStructureSize;
        createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        pvkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &m_AABBBLAS);

        if (sizeInfo.buildScratchSize > m_ScratchBufferSize) CreateScratchBuffer(sizeInfo.buildScratchSize);

        buildInfo.dstAccelerationStructure  = m_AABBBLAS;
        buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(m_ScratchBuffer);

        // ppBuildRangeInfos[0] points to the array of N build ranges (one per geometry)
        const VkAccelerationStructureBuildRangeInfoKHR* pRanges = buildRanges.data();
        pvkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRanges);

        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    void AccelerationStructure::BuildTLAS(VkCommandBuffer cmd)
    {
        bool hasAABB           = (m_AABBBLAS != VK_NULL_HANDLE);
        uint32_t instanceCount = hasAABB ? 2u : 1u;

        // Instance 0: triangle BLAS
        auto blasAddr = [&](VkAccelerationStructureKHR as) -> VkDeviceAddress {
            VkAccelerationStructureDeviceAddressInfoKHR info
                    = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
            info.accelerationStructure = as;
            return pvkGetAccelerationStructureDeviceAddressKHR(m_Device, &info);
        };

        std::vector<VkAccelerationStructureInstanceKHR> instances(instanceCount);

        // Triangle instance — SBT offset 0, all faces double-sided
        auto& tri                                  = instances[0];
        tri                                        = {};
        tri.transform.matrix[0][0]                 = 1.0f;
        tri.transform.matrix[1][1]                 = 1.0f;
        tri.transform.matrix[2][2]                 = 1.0f;
        tri.instanceCustomIndex                    = 0;
        tri.mask                                   = 0xFF;
        tri.instanceShaderBindingTableRecordOffset = 0;
        tri.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        tri.accelerationStructureReference         = blasAddr(m_TriangleBLAS);

        // AABB instance — SBT offset 2, no face culling
        if (hasAABB) {
            auto& aabb                                  = instances[1];
            aabb                                        = {};
            aabb.transform.matrix[0][0]                 = 1.0f;
            aabb.transform.matrix[1][1]                 = 1.0f;
            aabb.transform.matrix[2][2]                 = 1.0f;
            aabb.instanceCustomIndex                    = 0;
            aabb.mask                                   = 0xFF;
            aabb.instanceShaderBindingTableRecordOffset = 2;
            aabb.flags                                  = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
            aabb.accelerationStructureReference         = blasAddr(m_AABBBLAS);
        }

        // Upload instance data
        VkDeviceSize instBufSize = sizeof(VkAccelerationStructureInstanceKHR) * instanceCount;
        m_InstanceBuffer         = CreateBuffer(instBufSize,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_InstanceMemory);
        void* mapped;
        vkMapMemory(m_Device, m_InstanceMemory, 0, instBufSize, 0, &mapped);
        memcpy(mapped, instances.data(), instBufSize);
        vkUnmapMemory(m_Device, m_InstanceMemory);

        // Build TLAS
        VkAccelerationStructureGeometryInstancesDataKHR instancesData
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
        instancesData.arrayOfPointers    = VK_FALSE;
        instancesData.data.deviceAddress = GetBufferDeviceAddress(m_InstanceBuffer);

        VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        geometry.geometryType                       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances                 = instancesData;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries   = &geometry;

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        pvkGetAccelerationStructureBuildSizesKHR(
                m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &instanceCount, &sizeInfo);

        m_TLASBuffer = CreateBuffer(sizeInfo.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_TLASMemory);

        VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer                               = m_TLASBuffer;
        createInfo.size                                 = sizeInfo.accelerationStructureSize;
        createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        pvkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &m_TLAS);

        if (sizeInfo.buildScratchSize > m_ScratchBufferSize) CreateScratchBuffer(sizeInfo.buildScratchSize);

        buildInfo.dstAccelerationStructure  = m_TLAS;
        buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(m_ScratchBuffer);

        VkAccelerationStructureBuildRangeInfoKHR buildRange{};
        buildRange.primitiveCount                              = instanceCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &buildRange;
        pvkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);

        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    void AccelerationStructure::CreateScratchBuffer(VkDeviceSize size)
    {
        if (m_ScratchBuffer != VK_NULL_HANDLE) {
            Walnut::Application::SubmitResourceFree([device = m_Device, b = m_ScratchBuffer, m = m_ScratchMemory]() {
                vkDestroyBuffer(device, b, nullptr);
                vkFreeMemory(device, m, nullptr);
            });
            m_ScratchBuffer     = VK_NULL_HANDLE;
            m_ScratchBufferSize = 0;
        }
        m_ScratchBuffer
                = CreateBuffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_ScratchMemory);
        m_ScratchBufferSize = size;
    }

    auto AccelerationStructure::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties, VkDeviceMemory& bufferMemory) const -> VkBuffer
    {
        if (size == 0) size = 16;

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = size;
        bufferInfo.usage              = usage;
        bufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer;
        vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_Device, buffer, &memReq);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(Walnut::Application::GetPhysicalDevice(), &memProps);

        uint32_t memTypeIdx = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((memReq.memoryTypeBits & (1u << i))
                    && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                memTypeIdx = i;
                break;
            }
        }

        VkMemoryAllocateFlagsInfo flagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        flagsInfo.flags                     = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.pNext                = &flagsInfo;
        allocInfo.allocationSize       = memReq.size;
        allocInfo.memoryTypeIndex      = memTypeIdx;

        vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory);
        vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);

        return buffer;
    }

    auto AccelerationStructure::GetBufferDeviceAddress(VkBuffer buffer) const -> VkDeviceAddress
    {
        VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        info.buffer                    = buffer;
        return pvkGetBufferDeviceAddressKHR(m_Device, &info);
    }
}  // namespace Vlkrt
