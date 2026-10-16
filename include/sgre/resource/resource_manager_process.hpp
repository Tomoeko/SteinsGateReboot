#pragma once

#include "sgre/io/file_read_processor.hpp"
#include "sgre/resource/resource_cache.hpp"
#include "sgre/scene/common_process_tasks.hpp"

#include <memory>
#include <thread>

namespace sgre::resource {
	class ResourceManagerProcess {
	public:
		// Native ResourceManager starts with limit zero and owns its actual persistent reader in Parallel.
		// Existing receiving mount/background owners enclose this prepared-disk owner; no application factory is substituted.
		ResourceManagerProcess(ArchiveMounts& actual_mounts, core::BackgroundCallManager& actual_background, ResourceCacheLimits cache_limits = {}, io::RawReadQueueLimits read_limits = {});
		~ResourceManagerProcess() noexcept;
		ResourceManagerProcess(const ResourceManagerProcess&) = delete;
		ResourceManagerProcess& operator=(const ResourceManagerProcess&) = delete;
		ResourceManagerProcess(ResourceManagerProcess&&) = delete;
		ResourceManagerProcess& operator=(ResourceManagerProcess&&) = delete;
		[[nodiscard]] ResourceCache& Cache();
		[[nodiscard]] io::FileReadProcessor& Processor();
		[[nodiscard]] const scene::CommonProcessTasks& Tasks() const noexcept;
		void Update();
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		// Preflight, then fence/retire processor Parallel before Serial and before any owned receiving-cell destruction.
		// Cache/pin scope and native arbitrary-time/index/application shutdown remain separate boundaries.
		void Close();

	private:
		void RequireOpen() const;
		void RetireProcessor();
		scene::CommonProcessTasks m_Common;
		ResourceCache m_Cache;
		std::unique_ptr<io::FileReadProcessor> m_Processor;
		scene::ParallelTaskHandle m_ReaderRoot;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		bool m_ReaderRetired = false;
		bool m_Closed = false;
	};
} // namespace sgre::resource
