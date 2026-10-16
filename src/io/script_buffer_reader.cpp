#include "sgre/io/script_buffer_reader.hpp"

#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace sgre::io {
	PreparedMountedScriptRead PrepareQueuedMountedScriptBufferRead(const resource::ArchiveMounts& actual_mounts, std::string_view original_read_filename, std::string_view seed) {
		if (original_read_filename.empty() || original_read_filename.size() > 65536 || original_read_filename.find('\0') != std::string_view::npos || seed.empty() || seed.size() > 65536 || seed.find('\0') != std::string_view::npos) {
			throw std::runtime_error("queued script reader requires its actual bounded original name and cipher identity");
		}

		PreparedMountedScriptRead source;
		source.MountedSource = &actual_mounts;
		source.Read.Region.MaxBytes = 160 * 1024 * 1024;
		source.Read.OriginalFilename = std::string(original_read_filename);
		source.Read.Seed = std::string(seed);

		return source;
	}

	PreparedWrappedBinaryRead PrepareMountedScriptBufferRead(const resource::ArchiveMounts& mounts, std::string_view original_read_filename, std::string_view seed) {
		if (mounts.QueuedReadMountCount() != 0 || mounts.HasInFlightReadMount() || original_read_filename.empty() || original_read_filename.size() > 65536 || original_read_filename.find('\0') != std::string_view::npos || seed.empty() || seed.size() > 65536 || seed.find('\0') != std::string_view::npos) {
			throw std::runtime_error("script buffer preparation requires the quiescent actual mounted/name/cipher domain");
		}

		const auto resolved = mounts.Resolve(original_read_filename);
		if (!resolved || resolved->BodyFilename.empty() || resolved->BodyFilename.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos || resolved->Entry.Size == 0 || resolved->Entry.Size > 160 * 1024 * 1024 || resolved->Entry.Size >= std::numeric_limits<std::uint32_t>::max() || resolved->Entry.Offset > resolved->BodyBytes || resolved->Entry.Size > resolved->BodyBytes - resolved->Entry.Offset) {
			throw std::runtime_error("script buffer requires a real mounted body region; native loose-root fallback remains open");
		}

		PreparedWrappedBinaryRead source;
		source.Region.Filename = resolved->BodyFilename;
		source.Region.Offset = resolved->Entry.Offset;
		source.Region.Length = static_cast<std::uint32_t>(resolved->Entry.Size);
		source.OriginalFilename = std::string(original_read_filename);
		source.Seed = std::string(seed);

		return source;
	}

	PreparedScriptBufferReader::PreparedScriptBufferReader(ScriptReadBuffer& receiver, PreparedWrappedBinaryRead source, resource::ArchiveMounts& mounts, core::BackgroundCallManager& background) : m_Receiver(&receiver), m_Source(std::move(source)), m_Mounts(mounts), m_Serial(m_Common, background, [this]() {
		                                                                                                                                                                                                 ReadLoop();
	                                                                                                                                                                                                 }) {
		RequireQuiescentMountDomain();
		if (m_Source.Region.Filename.empty() || m_Source.OriginalFilename.empty() || m_Source.OriginalFilename.size() > 65536 || m_Source.OriginalFilename.find('\0') != std::string::npos) {
			throw std::runtime_error("script reader requires a genuinely prepared bounded original file identity");
		}

		m_Receiver->BeginReadBorrow();
	}

	PreparedScriptBufferReader::PreparedScriptBufferReader(ScriptReadBuffer& receiver, PreparedMountedScriptRead source, resource::ArchiveMounts& mounts, core::BackgroundCallManager& background) : m_Receiver(&receiver), m_Source(std::move(source.Read)), m_QueuedMounts(source.MountedSource), m_Mounts(mounts), m_Serial(m_Common, background, [this]() {
		                                                                                                                                                                                                 ReadLoop();
	                                                                                                                                                                                                 }) {
		if (m_QueuedMounts != &m_Mounts || !m_Source.Region.Filename.empty() || m_Source.Region.Offset != 0 || m_Source.Region.Length != 0 || m_Source.Region.MaxBytes == 0 || m_Source.Region.MaxBytes > 160 * 1024 * 1024 || m_Source.OriginalFilename.empty() || m_Source.OriginalFilename.size() > 65536 || m_Source.OriginalFilename.find('\0') != std::string::npos || m_Source.Seed.empty() || m_Source.Seed.size() > 65536 || m_Source.Seed.find('\0') != std::string::npos || m_Source.ShellKeyPeriod == 0 || m_Source.ShellKeyPeriod > 65536 || m_Source.MaxDecodedBytes == 0 || m_Source.MaxDecodedBytes > 160 * 1024 * 1024) {
			throw std::runtime_error("queued script reader requires its singular actual mount owner and bounded logical source without a physical fallback");
		}

		m_Receiver->BeginReadBorrow();
	}

	PreparedScriptBufferReader::~PreparedScriptBufferReader() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void PreparedScriptBufferReader::RequireOwnerThread() const {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("script reader lifecycle requires its actual owner thread");
		}
	}

	void PreparedScriptBufferReader::RequireQuiescentMountDomain() const {
		if (m_Mounts.QueuedReadMountCount() != 0 || m_Mounts.HasInFlightReadMount()) {
			throw std::runtime_error("prepared script reader requires the quiescent mounted domain; native +170=0 bootstrap remains open");
		}
	}

	void PreparedScriptBufferReader::ReadLoop() {
		m_Receiver->ProduceSelected(m_Source, m_QueuedMounts);
	}

	void PreparedScriptBufferReader::Update() {
		RequireOwnerThread();
		if (m_Closed || m_Status != scene::TaskStatus::Active) {
			throw std::runtime_error("script reader update requires its actual active owner");
		}

		if (!m_QueuedMounts) {
			RequireQuiescentMountDomain();
		}

		m_Common.Update([this]() {
			if (!m_FinishPhase) {
				// Native4d6f10 checks the shared in-flight/pending mount domain before selecting its ordinary request.
				if (m_QueuedMounts && (m_Mounts.HasInFlightReadMount() || m_Mounts.QueuedReadMountCount() != 0)) {
					return;
				}

				static_cast<void>(m_Serial.AppendReadLoop());
				m_FinishPhase = true;
			} else {
				// Native4d76c0 publishes the separate+d8 readiness gate after serial-empty, then retires.
				m_Receiver->FinishReadBorrow(true);
				m_Receiver = nullptr;
				m_Status = scene::TaskStatus::Retire;
			}
		});
	}

	void PreparedScriptBufferReader::Draw(std::uint32_t argument) {
		RequireOwnerThread();
		if (m_Closed) {
			throw std::runtime_error("script reader draw requires its actual common owner");
		}

		m_Common.Draw(argument);
	}

	scene::TaskStatus PreparedScriptBufferReader::Status() const noexcept {
		return m_Status;
	}

	bool PreparedScriptBufferReader::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && (m_Closed || m_Serial.CanClose());
	}

	void PreparedScriptBufferReader::Close() {
		if (!CanClose()) {
			throw std::runtime_error("script reader retirement requires inactive actual common/serial ownership");
		}
		if (m_Closed) {
			return;
		}

		// Authored forced-close policy fences real jobs before a raw buffer or gate can retire.
		m_Serial.Close();
		if (m_Receiver) {
			m_Receiver->FinishReadBorrow(false);
			m_Receiver = nullptr;
		}
		m_Closed = true;
	}

	static scene::ParallelTaskHandle AppendActualScriptBufferReader(scene::CommonProcessTasks& actual_driver, std::shared_ptr<PreparedScriptBufferReader> reader) {
		scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [reader, &actual_driver](scene::ParallelTaskHandle child) {
			reader->Update();
			actual_driver.Parallel().SetStatus(child, reader->Status());
		};
		callbacks.Draw = [reader](scene::ParallelTaskHandle, std::uint32_t argument) {
			reader->Draw(argument);
		};
		callbacks.Release = [reader](scene::ParallelTaskHandle) {
			reader->Close();
		};

		return actual_driver.Parallel().Append(std::move(callbacks));
	}

	scene::ParallelTaskHandle AppendPreparedScriptBufferReader(scene::CommonProcessTasks& actual_driver, core::BackgroundCallManager& background, resource::ArchiveMounts& mounts, ScriptReadBuffer& receiver, PreparedWrappedBinaryRead source) {
		if (actual_driver.Parallel().Status() != scene::TaskStatus::Active) {
			throw std::runtime_error("script reader attachment requires the actual active driver parallel group");
		}

		return AppendActualScriptBufferReader(actual_driver, std::make_shared<PreparedScriptBufferReader>(receiver, std::move(source), mounts, background));
	}

	scene::ParallelTaskHandle AppendQueuedMountedScriptBufferReader(scene::CommonProcessTasks& actual_driver, core::BackgroundCallManager& background, resource::ArchiveMounts& mounts, ScriptReadBuffer& receiver, PreparedMountedScriptRead source) {
		if (actual_driver.Parallel().Status() != scene::TaskStatus::Active) {
			throw std::runtime_error("script reader attachment requires the actual active driver parallel group");
		}

		return AppendActualScriptBufferReader(actual_driver, std::make_shared<PreparedScriptBufferReader>(receiver, std::move(source), mounts, background));
	}
} // namespace sgre::io
