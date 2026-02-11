#include "AccelerationStructure.h"
#include "Scene.h"
#include "Renderer.h"

#include "Walnut/Application.h"
#include "Walnut/VulkanRayTracing.h"

#include <cstring>

namespace Vlkrt
{
    AccelerationStructure::AccelerationStructure() { m_Device = Walnut::Application::GetDevice(); }

    AccelerationStructure::~AccelerationStructure() { Cleanup(); }

    void AccelerationStructure::Build(const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer)
    {
        if (meshes.empty() || vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE) return;

        // Get command buffer
        VkCommandBuffer cmd = Walnut::Application::GetCommandBuffer(true);

        // Build BLAS with triangle geometry
        BuildBLAS(meshes, vertexBuffer, indexBuffer, cmd);

        // Build TLAS
        BuildTLAS(1, cmd);

        // Submit command buffer
        Walnut::Application::FlushCommandBuffer(cmd);
    }

    void AccelerationStructure::Rebuild(const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer)
    {
        // For simplicity, we do a full rebuild instead of an update
        // This is fine for small dynamic scenes
        Cleanup();
        Build(meshes, vertexBuffer, indexBuffer);
    }

    void AccelerationStructure::Cleanup()
    {
        if (m_TLAS != VK_NULL_HANDLE) {
            Walnut::Application::SubmitResourceFree([device = m_Device, tlas = m_TLAS]() {
                pvkDestroyAccelerationStructureKHR(device, tlas, nullptr);
            });
            m_TLAS = VK_NULL_HANDLE;
        }
        if (m_TLASBuffer != VK_NULL_HANDLE) {
            Walnut::Application::SubmitResourceFree(
                    [device = m_Device, buffer = m_TLASBuffer, memory = m_TLASMemory]() {
                        vkDestroyBuffer(device, buffer, nullptr);
                        vkFreeMemory(device, memory, nullptr);
                    });
            m_TLASBuffer = VK_NULL_HANDLE;
            m_TLASMemory = VK_NULL_HANDLE;
        }

        if (m_BLAS != VK_NULL_HANDLE) {
            Walnut::Application::SubmitResourceFree([device = m_Device, blas = m_BLAS]() {
                pvkDestroyAccelerationStructureKHR(device, blas, nullptr);
            });
            m_BLAS = VK_NULL_HANDLE;
        }
        if (m_BLASBuffer != VK_NULL_HANDLE) {
            Walnut::Application::SubmitResourceFree(
                    [device = m_Device, buffer = m_BLASBuffer, memory = m_BLASMemory]() {
                        vkDestroyBuffer(device, buffer, nullptr);
                        vkFreeMemory(device, memory, nullptr);
                    });
            m_BLASBuffer = VK_NULL_HANDLE;
            m_BLASMemory = VK_NULL_HANDLE;
        }

        if (m_InstanceBuffer != VK_NULL_HANDLE) {
            Walnut::Application::SubmitResourceFree(
                    [device = m_Device, buffer = m_InstanceBuffer, memory = m_InstanceMemory]() {
                        vkDestroyBuffer(device, buffer, nullptr);
                        vkFreeMemory(device, memory, nullptr);
                    });
            m_InstanceBuffer = VK_NULL_HANDLE;
            m_InstanceMemory = VK_NULL_HANDLE;
        }

        if (m_ScratchBuffer != VK_NULL_HANDLE) {
            Walnut::Application::SubmitResourceFree(
                    [device = m_Device, buffer = m_ScratchBuffer, memory = m_ScratchMemory]() {
                        vkDestroyBuffer(device, buffer, nullptr);
                        vkFreeMemory(device, memory, nullptr);
                    });
            m_ScratchBuffer = VK_NULL_HANDLE;
            m_ScratchMemory = VK_NULL_HANDLE;
        }
    }

    void AccelerationStructure::BuildBLAS(
            const std::vector<Mesh>& meshes, VkBuffer vertexBuffer, VkBuffer indexBuffer, VkCommandBuffer cmd)
    {
        // Calculate total triangle count across all meshes
        uint32_t totalTriangles = 0;
        for (const auto& mesh : meshes) { totalTriangles += static_cast<uint32_t>(mesh.Indices.size() / 3); }

        // Define triangle geometry
        VkAccelerationStructureGeometryTrianglesDataKHR trianglesData
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
        trianglesData.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
        trianglesData.vertexData.deviceAddress = GetBufferDeviceAddress(vertexBuffer);
        trianglesData.vertexStride             = sizeof(GPUVertex);
        trianglesData.maxVertex                = 0;
        for (const auto& mesh : meshes) { trianglesData.maxVertex += static_cast<uint32_t>(mesh.Vertices.size()); }
        trianglesData.maxVertex -= 1;
        trianglesData.indexType               = VK_INDEX_TYPE_UINT32;
        trianglesData.indexData.deviceAddress = GetBufferDeviceAddress(indexBuffer);

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

        // Get size requirements
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        pvkGetAccelerationStructureBuildSizesKHR(
                m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &totalTriangles, &sizeInfo);

        // Create BLAS buffer
        m_BLASBuffer = CreateBuffer(sizeInfo.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_BLASMemory);

        // Create BLAS
        VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer                               = m_BLASBuffer;
        createInfo.size                                 = sizeInfo.accelerationStructureSize;
        createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        pvkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &m_BLAS);

        // Create scratch buffer
        CreateScratchBuffer(sizeInfo.buildScratchSize);

        // Build BLAS
        buildInfo.dstAccelerationStructure  = m_BLAS;
        buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(m_ScratchBuffer);

        VkAccelerationStructureBuildRangeInfoKHR buildRange = {};
        buildRange.primitiveCount                           = totalTriangles;
        buildRange.primitiveOffset                          = 0;
        buildRange.firstVertex                              = 0;
        buildRange.transformOffset                          = 0;

        const VkAccelerationStructureBuildRangeInfoKHR* pBuildRange = &buildRange;

        pvkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pBuildRange);

        // Add memory barrier
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    void AccelerationStructure::BuildTLAS(uint32_t instanceCount, VkCommandBuffer cmd)
    {
        // Get BLAS device address
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
        addressInfo.accelerationStructure = m_BLAS;
        VkDeviceAddress blasAddress       = pvkGetAccelerationStructureDeviceAddressKHR(m_Device, &addressInfo);

        // Create instance
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform.matrix[0][0]                 = 1.0f;
        instance.transform.matrix[1][1]                 = 1.0f;
        instance.transform.matrix[2][2]                 = 1.0f;
        instance.instanceCustomIndex                    = 0;
        instance.mask                                   = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference         = blasAddress;

        // Create instance buffer
        VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR);
        m_InstanceBuffer                = CreateBuffer(instanceBufferSize,
                               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                       | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_InstanceMemory);

        // Upload instance data
        void* data;
        vkMapMemory(m_Device, m_InstanceMemory, 0, instanceBufferSize, 0, &data);
        memcpy(data, &instance, instanceBufferSize);
        vkUnmapMemory(m_Device, m_InstanceMemory);

        // Define instance geometry
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

        // Get size requirements
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo
                = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        pvkGetAccelerationStructureBuildSizesKHR(
                m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &instanceCount, &sizeInfo);

        // Create TLAS buffer
        m_TLASBuffer = CreateBuffer(sizeInfo.accelerationStructureSize,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_TLASMemory);

        // Create TLAS
        VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer                               = m_TLASBuffer;
        createInfo.size                                 = sizeInfo.accelerationStructureSize;
        createInfo.type                                 = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        pvkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &m_TLAS);

        // Ensure scratch buffer is large enough
        if (sizeInfo.buildScratchSize > m_ScratchBufferSize) { CreateScratchBuffer(sizeInfo.buildScratchSize); }

        // Build TLAS
        buildInfo.dstAccelerationStructure  = m_TLAS;
        buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(m_ScratchBuffer);

        VkAccelerationStructureBuildRangeInfoKHR buildRange = {};
        buildRange.primitiveCount                           = instanceCount;
        buildRange.primitiveOffset                          = 0;
        buildRange.firstVertex                              = 0;
        buildRange.transformOffset                          = 0;

        const VkAccelerationStructureBuildRangeInfoKHR* pBuildRange = &buildRange;

        pvkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pBuildRange);

        // Add memory barrier
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    void AccelerationStructure::CreateScratchBuffer(VkDeviceSize size)
    {
        // Clean up old scratch buffer if too small
        if (m_ScratchBuffer != VK_NULL_HANDLE && size > m_ScratchBufferSize) {
            Walnut::Application::SubmitResourceFree(
                    [device = m_Device, buffer = m_ScratchBuffer, memory = m_ScratchMemory]() {
                        vkDestroyBuffer(device, buffer, nullptr);
                        vkFreeMemory(device, memory, nullptr);
                    });
            m_ScratchBuffer = VK_NULL_HANDLE;
        }

        if (m_ScratchBuffer == VK_NULL_HANDLE) {
            m_ScratchBuffer
                    = CreateBuffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_ScratchMemory);
            m_ScratchBufferSize = size;
        }
    }

    auto AccelerationStructure::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties, VkDeviceMemory& bufferMemory) const -> VkBuffer
    {
        if (size == 0) size = 16;  // Minimum size

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = size;
        bufferInfo.usage              = usage;
        bufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer;
        vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

        // Find memory type
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(Walnut::Application::GetPhysicalDevice(), &memProperties);

        uint32_t memoryTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i))
                    && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                memoryTypeIndex = i;
                break;
            }
        }

        VkMemoryAllocateFlagsInfo allocFlagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        allocFlagsInfo.flags                     = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.pNext                = &allocFlagsInfo;
        allocInfo.allocationSize       = memRequirements.size;
        allocInfo.memoryTypeIndex      = memoryTypeIndex;

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
