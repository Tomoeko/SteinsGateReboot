#pragma once

#include "sgre/io/raw_read_destination.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sgre::resource {
	class ArchiveMounts;
}

namespace sgre::io {
	class PreparedScriptBufferReader;

	struct PreparedWrappedBinaryRead {
		PreparedRawFileRead Region;
		std::string OriginalFilename;
		std::string Seed;
		std::size_t ShellKeyPeriod = 131;
		std::size_t MaxDecodedBytes = 160 * 1024 * 1024;
	};

	enum class ScriptBufferStorage {
		OwnedRaw,
		ManagerBinaryPin
	};

	struct ScriptBufferObservation {
		bool Ready = false;
		FileReadCompletion Completion = FileReadCompletion::Pending;
		// Native2431f0 reports readiness even on failure, with a null payload and the actual receiving length.
		// Borrows the live buffer only until Close or the compile/release handoff.
		std::span<const std::uint8_t> Bytes;
		std::uint32_t ByteCount = 0;
	};

	class ScriptReadBuffer {
	public:
		// The actual load owner owns this raw miss receiver; it is not a ResourceCache Loading entry.
		ScriptReadBuffer();
		ScriptReadBuffer(resource::ResourceCache& actual_cache, std::string_view actual_filename);
		~ScriptReadBuffer() noexcept;
		ScriptReadBuffer(const ScriptReadBuffer&) = delete;
		ScriptReadBuffer& operator=(const ScriptReadBuffer&) = delete;
		ScriptReadBuffer(ScriptReadBuffer&&) = delete;
		ScriptReadBuffer& operator=(ScriptReadBuffer&&) = delete;
		[[nodiscard]] ScriptBufferStorage Storage() const noexcept;
		[[nodiscard]] FileReadCompletion Completion() const noexcept;
		[[nodiscard]] ScriptBufferObservation Observe() const;
		[[nodiscard]] const std::string& Error() const;
		[[nodiscard]] bool CanClose() const noexcept;
		void RequireCanClose() const;
		void Close();

	private:
		friend class PreparedScriptBufferReader;
		void BeginReadBorrow();
		void ProduceSelected(const PreparedWrappedBinaryRead& source, const resource::ArchiveMounts* actual_mounts = nullptr) noexcept;
		void FinishReadBorrow(bool publish_ready);
		void RequireOwnerThread() const;
		void StoreDiagnostic(std::string_view error) noexcept;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		ScriptBufferStorage m_Storage = ScriptBufferStorage::OwnedRaw;
		std::shared_ptr<const std::vector<std::uint8_t>> m_ManagerPin;
		std::vector<std::uint8_t> m_Bytes;
		std::string m_Error;
		std::atomic<FileReadCompletion> m_Completion = FileReadCompletion::Pending;
		bool m_ReadBorrow = false;
		bool m_Ready = false;
		bool m_Closed = false;
	};
} // namespace sgre::io
