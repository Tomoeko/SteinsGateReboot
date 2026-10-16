#include "sgre/resource/resource_manager_process.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::resource {
	ResourceManagerProcess::ResourceManagerProcess(ArchiveMounts& actual_mounts, core::BackgroundCallManager& actual_background, ResourceCacheLimits cache_limits, io::RawReadQueueLimits read_limits) : m_Cache(0, cache_limits), m_Processor(std::make_unique<io::FileReadProcessor>(m_Cache, actual_mounts, actual_background, read_limits)) {
		scene::ParallelTaskCallbacks reader;
		reader.Update = [this](auto) {
			m_Processor->Update();
		};
		reader.Draw = [this](auto, std::uint32_t argument) {
			m_Processor->Draw(argument);
		};
		reader.Release = [this](auto) {
			RetireProcessor();
		};
		m_ReaderRoot = m_Common.Parallel().Append(std::move(reader));
	}

	ResourceManagerProcess::~ResourceManagerProcess() noexcept {
		try {
			Close();
		} catch (...) {
			// A rejected real worker fence must keep its receiving cells and captures alive.
			std::terminate();
		}
	}

	void ResourceManagerProcess::RequireOpen() const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Closed || !m_Common.Parallel().Contains(m_ReaderRoot) || m_Processor->IsClosed()) {
			throw std::runtime_error("ResourceManager process requires its actual live receiving/common/reader owners");
		}
	}

	ResourceCache& ResourceManagerProcess::Cache() {
		RequireOpen();

		return m_Cache;
	}

	io::FileReadProcessor& ResourceManagerProcess::Processor() {
		RequireOpen();

		return *m_Processor;
	}

	const scene::CommonProcessTasks& ResourceManagerProcess::Tasks() const noexcept {
		return m_Common;
	}

	void ResourceManagerProcess::Update() {
		RequireOpen();
		m_Common.Update([this]() {
			// Native249510 runs through MProcess's serial-empty gate before its owned Parallel reader updates.
			m_Cache.Advance();
		});
	}

	void ResourceManagerProcess::Draw(std::uint32_t argument) {
		RequireOpen();
		m_Common.Draw(argument);
	}

	bool ResourceManagerProcess::CanClose() const noexcept {
		if (std::this_thread::get_id() != m_OwnerThread) {
			return false;
		}
		if (m_Closed) {
			return true;
		}
		if (!m_Common.CanClear() || !m_Processor->CanClose()) {
			return false;
		}

		return m_Common.Parallel().Contains(m_ReaderRoot) || m_ReaderRetired;
	}

	bool ResourceManagerProcess::IsClosed() const noexcept {
		return m_Closed;
	}

	void ResourceManagerProcess::RetireProcessor() {
		m_Processor->Close();
		m_ReaderRetired = true;
	}

	void ResourceManagerProcess::Close() {
		if (!CanClose()) {
			throw std::runtime_error("ResourceManager retirement requires its inactive actual graph and concrete reader fence");
		}
		if (m_Closed) {
			return;
		}

		m_Common.Clear();
		if (!m_ReaderRetired || !m_Processor->IsClosed()) {
			throw std::runtime_error("ResourceManager processor has not retired before receiving-owner cleanup");
		}
		m_Cache.RequireCanRetireReadDestinations();
		m_Closed = true;
	}
} // namespace sgre::resource
