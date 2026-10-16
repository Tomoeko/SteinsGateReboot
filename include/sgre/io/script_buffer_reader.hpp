#pragma once

#include "sgre/io/file_read_serial.hpp"
#include "sgre/io/script_read_buffer.hpp"
#include "sgre/resource/archive_mounts.hpp"

#include <string_view>
#include <thread>

namespace sgre::io {
	struct PreparedMountedScriptRead {
		PreparedWrappedBinaryRead Read;
		// The actual receiving map/processor outlive this queued reader and its worker fence.
		const resource::ArchiveMounts* MountedSource = nullptr;
	};

	[[nodiscard]] PreparedMountedScriptRead PrepareQueuedMountedScriptBufferRead(const resource::ArchiveMounts& actual_mounts, std::string_view original_read_filename, std::string_view seed);
	[[nodiscard]] PreparedWrappedBinaryRead PrepareMountedScriptBufferRead(const resource::ArchiveMounts& mounts, std::string_view original_read_filename, std::string_view seed);

	class PreparedScriptBufferReader {
	public:
		// Fresh one-shot disk reader. Receiver, actual mounts and background manager enclose Close.
		// Source is genuinely prepared; pending/global mount bootstrap remains unsupported in this narrow owner.
		PreparedScriptBufferReader(ScriptReadBuffer& receiver, PreparedWrappedBinaryRead source, resource::ArchiveMounts& mounts, core::BackgroundCallManager& background);
		// Logical names remain queued until the containing processor publishes all pending mount indexes.
		PreparedScriptBufferReader(ScriptReadBuffer& receiver, PreparedMountedScriptRead source, resource::ArchiveMounts& mounts, core::BackgroundCallManager& background);
		~PreparedScriptBufferReader() noexcept;
		PreparedScriptBufferReader(const PreparedScriptBufferReader&) = delete;
		PreparedScriptBufferReader& operator=(const PreparedScriptBufferReader&) = delete;
		PreparedScriptBufferReader(PreparedScriptBufferReader&&) = delete;
		PreparedScriptBufferReader& operator=(PreparedScriptBufferReader&&) = delete;
		void Update();
		void Draw(std::uint32_t argument);
		[[nodiscard]] scene::TaskStatus Status() const noexcept;
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		void RequireOwnerThread() const;
		void RequireQuiescentMountDomain() const;
		void ReadLoop();
		scene::CommonProcessTasks m_Common;
		ScriptReadBuffer* m_Receiver;
		PreparedWrappedBinaryRead m_Source;
		const resource::ArchiveMounts* m_QueuedMounts = nullptr;
		resource::ArchiveMounts& m_Mounts;
		FileReadSerialAttachment m_Serial;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		scene::TaskStatus m_Status = scene::TaskStatus::Active;
		bool m_FinishPhase = false;
		bool m_Closed = false;
	};

	// The actual driver parallel child owns the physical reader. This adds no receiving/VM/cache root.
	[[nodiscard]] scene::ParallelTaskHandle AppendQueuedMountedScriptBufferReader(scene::CommonProcessTasks& actual_driver, core::BackgroundCallManager& background, resource::ArchiveMounts& mounts, ScriptReadBuffer& receiver, PreparedMountedScriptRead source);
	[[nodiscard]] scene::ParallelTaskHandle AppendPreparedScriptBufferReader(scene::CommonProcessTasks& actual_driver, core::BackgroundCallManager& background, resource::ArchiveMounts& mounts, ScriptReadBuffer& receiver, PreparedWrappedBinaryRead source);
} // namespace sgre::io
