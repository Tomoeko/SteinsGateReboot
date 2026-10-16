#pragma once

#include "sgre/core/background_calls.hpp"
#include "sgre/scene/common_process_tasks.hpp"

#include <atomic>
#include <functional>
#include <thread>

namespace sgre::io {

	class FileReadSerialAttachment {
	public:
		// Borrows the genuine MFileReadTask's common groups, background manager and real read-loop owners.
		// The read processor's request cells, file/backend state and synchronization outlive Close's worker fence.
		FileReadSerialAttachment(scene::CommonProcessTasks& processor, core::BackgroundCallManager& background, std::function<void()> actual_read_loop);
		~FileReadSerialAttachment() noexcept;
		FileReadSerialAttachment(const FileReadSerialAttachment&) = delete;
		FileReadSerialAttachment& operator=(const FileReadSerialAttachment&) = delete;
		FileReadSerialAttachment(FileReadSerialAttachment&&) = delete;
		FileReadSerialAttachment& operator=(FileReadSerialAttachment&&) = delete;

		// Actual phase4d6f10 appends this exclusive background child during the processor's serial-empty onProcess.
		// Read loop/queue/mount decisions are concrete caller phases; this does not schedule substitute PSB jobs.
		[[nodiscard]] scene::SerialTaskHandle AppendReadLoop();
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		void RequireCanRetire() const;
		// Explicitly fences/clears the actual processor serial children BEFORE dependent processor resources drop.
		// This is authored forced-retirement policy; the native automatic status2 path already reaches serial-empty.
		void Close();

	private:
		void RequireOwnerThread() const;
		scene::CommonProcessTasks& m_Processor;
		core::BackgroundCallManager& m_Background;
		std::function<void()> m_ReadLoop;
		std::thread::id m_OwnerThread;
		std::atomic<bool> m_Closed = false;
	};

} // namespace sgre::io
