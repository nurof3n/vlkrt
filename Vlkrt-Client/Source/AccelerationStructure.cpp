#include "AccelerationStructure.h"
#include "Scene.h"
#include "Walnut/Application.h"
#include "Walnut/VulkanRayTracing.h"

#include <cstring>

namespace Vlkrt
{
	// AABB structure for GPU
	struct AABB
	{
		float minX, minY, minZ;
		float maxX, maxY, maxZ;
	};

	AccelerationStructure::AccelerationStructure()
	{
		m_Device = Walnut::Application::GetDevice();
	}

	AccelerationStructure::~AccelerationStructure()
	{
		Cleanup();
	}

	void AccelerationStructure::Build(const std::vector<Sphere>& spheres)
	{
		if (spheres.empty())
			return;

		// Get command buffer
		VkCommandBuffer cmd = Walnut::Application::GetCommandBuffer(true);

		// Create AABB buffer for spheres
		CreateAABBBuffer(spheres);

		// Build BLAS
		BuildBLAS(spheres, cmd);

		// Build TLAS
		BuildTLAS(1, cmd);  // Single instance

		// Submit command buffer
		Walnut::Application::FlushCommandBuffer(cmd);
	}

	void AccelerationStructure::Rebuild(const std::vector<Sphere>& spheres)
	{
		// For simplicity, we'll do a full rebuild instead of an update
		// This is fine for small dynamic scenes
		Cleanup();
		Build(spheres);
	}

	void AccelerationStructure::Cleanup()
	{
		if (m_TLAS != VK_NULL_HANDLE)
		{
			pvkDestroyAccelerationStructureKHR(m_Device, m_TLAS, nullptr);
			m_TLAS = VK_NULL_HANDLE;
		}
		if (m_TLASBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Device, m_TLASBuffer, nullptr);
			m_TLASBuffer = VK_NULL_HANDLE;
		}
		if (m_TLASMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_Device, m_TLASMemory, nullptr);
			m_TLASMemory = VK_NULL_HANDLE;
		}

		if (m_BLAS != VK_NULL_HANDLE)
		{
			pvkDestroyAccelerationStructureKHR(m_Device, m_BLAS, nullptr);
			m_BLAS = VK_NULL_HANDLE;
		}
		if (m_BLASBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Device, m_BLASBuffer, nullptr);
			m_BLASBuffer = VK_NULL_HANDLE;
		}
		if (m_BLASMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_Device, m_BLASMemory, nullptr);
			m_BLASMemory = VK_NULL_HANDLE;
		}

		if (m_AABBBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Device, m_AABBBuffer, nullptr);
			m_AABBBuffer = VK_NULL_HANDLE;
		}
		if (m_AABBMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_Device, m_AABBMemory, nullptr);
			m_AABBMemory = VK_NULL_HANDLE;
		}

		if (m_InstanceBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Device, m_InstanceBuffer, nullptr);
			m_InstanceBuffer = VK_NULL_HANDLE;
		}
		if (m_InstanceMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_Device, m_InstanceMemory, nullptr);
			m_InstanceMemory = VK_NULL_HANDLE;
		}

		if (m_ScratchBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Device, m_ScratchBuffer, nullptr);
			m_ScratchBuffer = VK_NULL_HANDLE;
		}
		if (m_ScratchMemory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_Device, m_ScratchMemory, nullptr);
			m_ScratchMemory = VK_NULL_HANDLE;
		}
	}

	void AccelerationStructure::CreateAABBBuffer(const std::vector<Sphere>& spheres)
	{
		// Create AABBs from spheres
		std::vector<AABB> aabbs(spheres.size());
		for (size_t i = 0; i < spheres.size(); i++)
		{
			const Sphere& sphere = spheres[i];
			aabbs[i].minX = sphere.Position.x - sphere.Radius;
			aabbs[i].minY = sphere.Position.y - sphere.Radius;
			aabbs[i].minZ = sphere.Position.z - sphere.Radius;
			aabbs[i].maxX = sphere.Position.x + sphere.Radius;
			aabbs[i].maxY = sphere.Position.y + sphere.Radius;
			aabbs[i].maxZ = sphere.Position.z + sphere.Radius;
		}

		VkDeviceSize bufferSize = sizeof(AABB) * aabbs.size();

		// Clean up old buffer if it exists
		if (m_AABBBuffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Device, m_AABBBuffer, nullptr);
			vkFreeMemory(m_Device, m_AABBMemory, nullptr);
		}

		// Create buffer
		m_AABBBuffer = CreateBuffer(bufferSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			m_AABBMemory);

		m_AABBBufferSize = bufferSize;

		// Upload AABB data
		void* data;
		vkMapMemory(m_Device, m_AABBMemory, 0, bufferSize, 0, &data);
		memcpy(data, aabbs.data(), bufferSize);
		vkUnmapMemory(m_Device, m_AABBMemory);
	}

	void AccelerationStructure::BuildBLAS(const std::vector<Sphere>& spheres, VkCommandBuffer cmd)
	{
		// Define AABB geometry
		VkAccelerationStructureGeometryAabbsDataKHR aabbsData = {};
		aabbsData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
		aabbsData.data.deviceAddress = GetBufferDeviceAddress(m_AABBBuffer);
		aabbsData.stride = sizeof(AABB);

		VkAccelerationStructureGeometryKHR geometry = {};
		geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
		geometry.geometry.aabbs = aabbsData;
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

		VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
		buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &geometry;

		uint32_t primitiveCount = static_cast<uint32_t>(spheres.size());

		// Get size requirements
		VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
		sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		pvkGetAccelerationStructureBuildSizesKHR(m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&buildInfo, &primitiveCount, &sizeInfo);

		// Create BLAS buffer
		m_BLASBuffer = CreateBuffer(sizeInfo.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			m_BLASMemory);

		// Create BLAS
		VkAccelerationStructureCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		createInfo.buffer = m_BLASBuffer;
		createInfo.size = sizeInfo.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		pvkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &m_BLAS);

		// Create scratch buffer
		CreateScratchBuffer(sizeInfo.buildScratchSize);

		// Build BLAS
		buildInfo.dstAccelerationStructure = m_BLAS;
		buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(m_ScratchBuffer);

		VkAccelerationStructureBuildRangeInfoKHR buildRange = {};
		buildRange.primitiveCount = primitiveCount;
		buildRange.primitiveOffset = 0;
		buildRange.firstVertex = 0;
		buildRange.transformOffset = 0;

		const VkAccelerationStructureBuildRangeInfoKHR* pBuildRange = &buildRange;

		pvkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pBuildRange);

		// Add memory barrier
		VkMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	void AccelerationStructure::BuildTLAS(uint32_t instanceCount, VkCommandBuffer cmd)
	{
		// Get BLAS device address
		VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {};
		addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		addressInfo.accelerationStructure = m_BLAS;
		VkDeviceAddress blasAddress = pvkGetAccelerationStructureDeviceAddressKHR(m_Device, &addressInfo);

		// Create instance
		VkAccelerationStructureInstanceKHR instance = {};
		instance.transform.matrix[0][0] = 1.0f;
		instance.transform.matrix[1][1] = 1.0f;
		instance.transform.matrix[2][2] = 1.0f;
		instance.instanceCustomIndex = 0;
		instance.mask = 0xFF;
		instance.instanceShaderBindingTableRecordOffset = 0;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance.accelerationStructureReference = blasAddress;

		// Create instance buffer
		VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR);
		m_InstanceBuffer = CreateBuffer(instanceBufferSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			m_InstanceMemory);

		// Upload instance data
		void* data;
		vkMapMemory(m_Device, m_InstanceMemory, 0, instanceBufferSize, 0, &data);
		memcpy(data, &instance, instanceBufferSize);
		vkUnmapMemory(m_Device, m_InstanceMemory);

		// Define instance geometry
		VkAccelerationStructureGeometryInstancesDataKHR instancesData = {};
		instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		instancesData.arrayOfPointers = VK_FALSE;
		instancesData.data.deviceAddress = GetBufferDeviceAddress(m_InstanceBuffer);

		VkAccelerationStructureGeometryKHR geometry = {};
		geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.geometry.instances = instancesData;

		VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
		buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &geometry;

		// Get size requirements
		VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
		sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		pvkGetAccelerationStructureBuildSizesKHR(m_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&buildInfo, &instanceCount, &sizeInfo);

		// Create TLAS buffer
		m_TLASBuffer = CreateBuffer(sizeInfo.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			m_TLASMemory);

		// Create TLAS
		VkAccelerationStructureCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		createInfo.buffer = m_TLASBuffer;
		createInfo.size = sizeInfo.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		pvkCreateAccelerationStructureKHR(m_Device, &createInfo, nullptr, &m_TLAS);

		// Ensure scratch buffer is large enough
		if (sizeInfo.buildScratchSize > m_ScratchBufferSize)
		{
			CreateScratchBuffer(sizeInfo.buildScratchSize);
		}

		// Build TLAS
		buildInfo.dstAccelerationStructure = m_TLAS;
		buildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(m_ScratchBuffer);

		VkAccelerationStructureBuildRangeInfoKHR buildRange = {};
		buildRange.primitiveCount = instanceCount;
		buildRange.primitiveOffset = 0;
		buildRange.firstVertex = 0;
		buildRange.transformOffset = 0;

		const VkAccelerationStructureBuildRangeInfoKHR* pBuildRange = &buildRange;

		pvkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pBuildRange);

		// Add memory barrier
		VkMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	void AccelerationStructure::CreateScratchBuffer(VkDeviceSize size)
	{
		// Clean up old scratch buffer if too small
		if (m_ScratchBuffer != VK_NULL_HANDLE && size > m_ScratchBufferSize)
		{
			vkDestroyBuffer(m_Device, m_ScratchBuffer, nullptr);
			vkFreeMemory(m_Device, m_ScratchMemory, nullptr);
			m_ScratchBuffer = VK_NULL_HANDLE;
		}

		if (m_ScratchBuffer == VK_NULL_HANDLE)
		{
			m_ScratchBuffer = CreateBuffer(size,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				m_ScratchMemory);
			m_ScratchBufferSize = size;
		}
	}

	VkBuffer AccelerationStructure::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties, VkDeviceMemory& bufferMemory)
	{
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer buffer;
		vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer);

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

		// Find memory type
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(Walnut::Application::GetPhysicalDevice(), &memProperties);

		uint32_t memoryTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
		{
			if ((memRequirements.memoryTypeBits & (1 << i)) &&
				(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				memoryTypeIndex = i;
				break;
			}
		}

		VkMemoryAllocateFlagsInfo allocFlagsInfo = {};
		allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.pNext = &allocFlagsInfo;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory);
		vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);

		return buffer;
	}

	VkDeviceAddress AccelerationStructure::GetBufferDeviceAddress(VkBuffer buffer)
	{
		VkBufferDeviceAddressInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		info.buffer = buffer;
		return pvkGetBufferDeviceAddressKHR(m_Device, &info);
	}
}  // namespace Vlkrt
