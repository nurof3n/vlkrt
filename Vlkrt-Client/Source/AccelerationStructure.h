#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Vlkrt
{
	struct Sphere;

	class AccelerationStructure
	{
	public:
		AccelerationStructure();
		~AccelerationStructure();

		void Build(const std::vector<Sphere>& spheres);
		void Rebuild(const std::vector<Sphere>& spheres);
		void Cleanup();

		VkAccelerationStructureKHR GetTLAS() const { return m_TLAS; }
		bool IsBuilt() const { return m_TLAS != VK_NULL_HANDLE; }

	private:
		void BuildBLAS(const std::vector<Sphere>& spheres, VkCommandBuffer cmd);
		void BuildTLAS(uint32_t instanceCount, VkCommandBuffer cmd);
		void CreateAABBBuffer(const std::vector<Sphere>& spheres);
		void CreateScratchBuffer(VkDeviceSize size);

		VkBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkDeviceMemory& bufferMemory);
		VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer);

		// BLAS management
		VkAccelerationStructureKHR m_BLAS = VK_NULL_HANDLE;
		VkBuffer m_BLASBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_BLASMemory = VK_NULL_HANDLE;

		// TLAS management
		VkAccelerationStructureKHR m_TLAS = VK_NULL_HANDLE;
		VkBuffer m_TLASBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_TLASMemory = VK_NULL_HANDLE;

		// AABB buffer for procedural geometry
		VkBuffer m_AABBBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_AABBMemory = VK_NULL_HANDLE;
		VkDeviceSize m_AABBBufferSize = 0;

		// Instance buffer
		VkBuffer m_InstanceBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_InstanceMemory = VK_NULL_HANDLE;

		// Scratch buffer (reused for builds)
		VkBuffer m_ScratchBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_ScratchMemory = VK_NULL_HANDLE;
		VkDeviceSize m_ScratchBufferSize = 0;

		VkDevice m_Device = VK_NULL_HANDLE;
	};
}  // namespace Vlkrt
