#include "sgre/io/file_read_serial.hpp"

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace sgre::io {

	struct FileReadBackgroundRecord {
		std::unique_ptr<core::BackgroundCallTask> Task;
	};

	FileReadSerialAttachment::FileReadSerialAttachment(scene::CommonProcessTasks& processor, core::BackgroundCallManager& background, std::function<void()> actual_read_loop) : m_Processor(processor), m_Background(background), m_ReadLoop(std::move(actual_read_loop)), m_OwnerThread(std::this_thread::get_id()) {
		if (!m_ReadLoop || !processor.CanClear() || processor.Serial().Count() != 0 || processor.Serial().Status() != scene::TaskStatus::Active) {
			throw std::runtime_error("file read attachment requires a real read loop and its stable empty active serial owner");
		}
	}

	FileReadSerialAttachment::~FileReadSerialAttachment() noexcept {
		try {
			Close();
		} catch (...) {
			if (!IsClosed()) {
				std::terminate();
			}
		}
	}

	void FileReadSerialAttachment::RequireOwnerThread() const {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("file read serial mutation requires the actual processor owner thread");
		}
	}

	scene::SerialTaskHandle FileReadSerialAttachment::AppendReadLoop() {
		RequireOwnerThread();
		auto& serial = m_Processor.Serial();
		if (IsClosed() || serial.Count() != 0 || serial.Status() != scene::TaskStatus::Active || !serial.CanClear()) {
			throw std::runtime_error("file read loop publication requires its actual empty active serial phase");
		}

		auto record = std::make_shared<FileReadBackgroundRecord>();
		record->Task = std::make_unique<core::BackgroundCallTask>(m_Background, core::BackgroundCallLane::BelowNormal, m_ReadLoop);
		scene::SerialTaskCallbacks callbacks;
		callbacks.Update = [record](scene::SerialTask& current) {
			record->Task->Update();
			current.SetStatus(record->Task->Status());
		};
		callbacks.Release = [record]() {
			record->Task->Close();
		};
		callbacks.CanRelease = [record]() {
			return record->Task->CanClose();
		};

		// Callback fields share this one typed record; the actual serial task exclusively owns the physical job.
		// Native background Draw is030f20. No renderer operation is invented for this child.
		return serial.Append(std::make_unique<scene::SerialTask>(std::move(callbacks)));
	}

	bool FileReadSerialAttachment::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && (IsClosed() || m_Processor.CanClear());
	}

	bool FileReadSerialAttachment::IsClosed() const noexcept {
		return m_Closed.load(std::memory_order_acquire);
	}

	void FileReadSerialAttachment::RequireCanRetire() const {
		RequireOwnerThread();
		if (!CanClose()) {
			throw std::runtime_error("file read processor retirement requires every actual common/serial owner preflight");
		}
	}

	void FileReadSerialAttachment::Close() {
		RequireCanRetire();
		if (IsClosed()) {
			return;
		}

		std::exception_ptr failure;
		try {
			m_Processor.ClearSerial();
		} catch (...) {
			if (m_Processor.Serial().Count() != 0) {
				throw;
			}

			failure = std::current_exception();
		}

		m_Closed.store(true, std::memory_order_release);
		m_ReadLoop = {};
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::io
