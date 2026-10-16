#include "sgre/io/script_read_buffer.hpp"

#include "sgre/io/file_reader.hpp"
#include "sgre/io/mounted_resource_read.hpp"
#include "sgre/resource/mzs_shell.hpp"
#include "sgre/resource/resource_cache.hpp"
#include "sgre/resource/zstd_decoder.hpp"

#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::io {
	static constexpr std::size_t kMaxScriptReadBytes = 160 * 1024 * 1024;

	ScriptReadBuffer::ScriptReadBuffer() : m_Error("script buffer read did not complete") {
	}

	ScriptReadBuffer::ScriptReadBuffer(resource::ResourceCache& actual_cache, std::string_view actual_filename) : ScriptReadBuffer() {
		m_ManagerPin = actual_cache.TryPinActiveBinary(actual_filename);
		if (m_ManagerPin) {
			m_Storage = ScriptBufferStorage::ManagerBinaryPin;
			m_Completion.store(FileReadCompletion::Succeeded, std::memory_order_release);
			m_Ready = true;
			m_Error.clear();
		}
	}

	ScriptReadBuffer::~ScriptReadBuffer() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void ScriptReadBuffer::RequireOwnerThread() const {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("script buffer access requires its actual receiving thread");
		}
	}

	void ScriptReadBuffer::BeginReadBorrow() {
		RequireOwnerThread();
		if (m_Closed || m_Storage != ScriptBufferStorage::OwnedRaw || m_ReadBorrow || m_Ready || Completion() != FileReadCompletion::Pending) {
			throw std::runtime_error("script raw receiver accepts one genuine reader borrow");
		}

		m_ReadBorrow = true;
	}

	void ScriptReadBuffer::StoreDiagnostic(std::string_view error) noexcept {
		try {
			if (!error.empty()) {
				m_Error = std::string(error.substr(0, 4096));
			}
		} catch (...) {
		}
	}

	void ScriptReadBuffer::ProduceSelected(const PreparedWrappedBinaryRead& source, const resource::ArchiveMounts* actual_mounts) noexcept {
		FileReadCompletion completion = FileReadCompletion::ReadFailed;
		try {
			auto region = source.Region;
			if (actual_mounts) {
				if (!region.Filename.empty() || region.Offset != 0 || region.Length != 0) {
					throw std::runtime_error("logical script selection cannot carry a physical fallback region");
				}

				region = ResolveMountedResourceReadRegion(*actual_mounts, source.OriginalFilename, region.MaxBytes);
			}
			if (region.Filename.empty() || region.Filename.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos || region.MaxBytes == 0 || region.MaxBytes > kMaxScriptReadBytes) {
				throw std::runtime_error("script raw receiver requires its actual checked physical region");
			}

			FileReader reader(region.Filename);
			const auto length = region.Length == 0 ? reader.Size() : region.Length;
			if (length > region.MaxBytes || length >= std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("script raw input exceeds its native word/allocation bound");
			}

			auto packed = reader.Read(region.Offset, static_cast<std::size_t>(length));
			completion = FileReadCompletion::PostprocessFailed;
			if (source.OriginalFilename.empty() || source.OriginalFilename.size() > 65536 || source.OriginalFilename.find('\0') != std::string::npos || source.Seed.empty() || source.Seed.size() > 65536 || source.Seed.find('\0') != std::string::npos || source.ShellKeyPeriod == 0 || source.ShellKeyPeriod > 65536 || source.MaxDecodedBytes == 0 || source.MaxDecodedBytes > kMaxScriptReadBytes) {
				throw std::runtime_error("script wrapper requires actual original identity/cipher and checked decoded bounds");
			}

			const auto shell = resource::DecodeMzsShell(packed, source.OriginalFilename, source.Seed, source.ShellKeyPeriod, source.MaxDecodedBytes);
			m_Bytes = resource::DecodeZstd(shell.CompressedBytes, shell.DecodedSize, source.MaxDecodedBytes);
			m_Error.clear();
			completion = FileReadCompletion::Succeeded;
		} catch (const std::exception& error) {
			StoreDiagnostic(error.what());
		} catch (...) {
		}

		// Payload/count is owned before the last worker access publishes its status. Gate+14 remains false.
		m_Completion.store(completion, std::memory_order_release);
	}

	void ScriptReadBuffer::FinishReadBorrow(bool publish_ready) {
		RequireOwnerThread();
		if (m_Closed || !m_ReadBorrow) {
			throw std::runtime_error("script reader finish requires its actual outstanding receiver borrow");
		}
		if (publish_ready && Completion() == FileReadCompletion::Pending) {
			throw std::runtime_error("script readiness cannot precede actual payload/status production");
		}

		m_ReadBorrow = false;
		m_Ready = publish_ready;
	}

	ScriptBufferStorage ScriptReadBuffer::Storage() const noexcept {
		return m_Storage;
	}

	FileReadCompletion ScriptReadBuffer::Completion() const noexcept {
		return m_Completion.load(std::memory_order_acquire);
	}

	ScriptBufferObservation ScriptReadBuffer::Observe() const {
		RequireOwnerThread();
		if (m_Closed) {
			throw std::runtime_error("script readiness observation requires its actual live buffer");
		}

		ScriptBufferObservation result;
		result.Ready = m_Ready;
		if (!m_Ready) {
			return result;
		}

		result.Completion = Completion();
		const auto& bytes = m_ManagerPin ? *m_ManagerPin : m_Bytes;
		result.ByteCount = static_cast<std::uint32_t>(bytes.size());
		if (result.Completion == FileReadCompletion::Succeeded) {
			result.Bytes = bytes;
		}

		return result;
	}

	const std::string& ScriptReadBuffer::Error() const {
		RequireOwnerThread();
		if (m_Closed || Completion() == FileReadCompletion::Pending) {
			throw std::runtime_error("script diagnostic requires actual completed production");
		}

		return m_Error;
	}

	bool ScriptReadBuffer::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && !m_ReadBorrow;
	}

	void ScriptReadBuffer::RequireCanClose() const {
		if (!CanClose()) {
			throw std::runtime_error("script buffer retirement must follow its real reader/gate borrow or explicit fence");
		}
	}

	void ScriptReadBuffer::Close() {
		RequireCanClose();
		if (m_Closed) {
			return;
		}

		m_ManagerPin.reset();
		std::vector<std::uint8_t>{}.swap(m_Bytes);
		m_Closed = true;
	}
} // namespace sgre::io
