#include "sgre/io/raw_read_destination.hpp"

#include "sgre/io/file_reader.hpp"

#include <exception>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sgre::io {

	static constexpr std::uint32_t kDestinationUnassigned = 5;
	static constexpr std::uint32_t kDestinationQueued = 6;
	static constexpr std::uint32_t kDestinationSelected = 7;
	static constexpr std::size_t kMaxRawFileBytes = 160 * 1024 * 1024;

	RawFileReadDestination::RawFileReadDestination() : m_State(kDestinationUnassigned), m_OwnerThread(std::this_thread::get_id()) {
		m_Result.Error = "raw read request did not complete";
	}

	RawFileReadDestination::~RawFileReadDestination() noexcept {
		if (!CanRetire()) {
			std::terminate();
		}
	}

	void RawFileReadDestination::RequireOwnerThread() const {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("raw read destination ownership requires its actual receiving thread");
		}
	}

	void RawFileReadDestination::MarkQueued() {
		RequireOwnerThread();
		std::uint32_t expected = kDestinationUnassigned;
		if (!m_State.compare_exchange_strong(expected, kDestinationQueued, std::memory_order_release, std::memory_order_relaxed)) {
			throw std::runtime_error("raw read destination can receive only one actual pending request");
		}
	}

	bool RawFileReadDestination::CancelQueued() {
		RequireOwnerThread();
		std::uint32_t expected = kDestinationQueued;

		return m_State.compare_exchange_strong(expected, static_cast<std::uint32_t>(FileReadCompletion::Cancelled), std::memory_order_release, std::memory_order_relaxed);
	}

	void RawFileReadDestination::Select() {
		std::uint32_t expected = kDestinationQueued;
		if (!m_State.compare_exchange_strong(expected, kDestinationSelected, std::memory_order_acq_rel, std::memory_order_relaxed)) {
			throw std::runtime_error("raw read selection requires the actual uncancelled pending queue node");
		}
	}

	void RawFileReadDestination::ProduceSelected(const PreparedRawFileRead& source) noexcept {
		static_cast<void>(ReadSelected(source));
		PublishSelected();
	}

	FileReadCompletion RawFileReadDestination::ReadSelected(const PreparedRawFileRead& source) noexcept {
		if (m_State.load(std::memory_order_acquire) != kDestinationSelected || m_ReadPrepared) {
			std::terminate();
		}

		try {
			if (source.Filename.empty() || source.Filename.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos || source.MaxBytes == 0 || source.MaxBytes > kMaxRawFileBytes) {
				throw std::runtime_error("raw file read requires a concrete prepared filename and bounded allocation");
			}

			FileReader reader(source.Filename);
			const auto size = source.Length == 0 ? reader.Size() : source.Length;
			if (size > source.MaxBytes || size >= std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("raw file read length exceeds its supported native word/allocation bound");
			}

			m_Result.Bytes = reader.Read(source.Offset, static_cast<std::size_t>(size));
			m_Result.Completion = FileReadCompletion::Succeeded;
			m_Result.Error.clear();
		} catch (const std::exception& error) {
			m_Result.Completion = FileReadCompletion::ReadFailed;
			try {
				const auto diagnostic = std::string_view(error.what()).substr(0, 4096);
				if (!diagnostic.empty()) {
					m_Result.Error = std::string(diagnostic);
				}
			} catch (...) {
				// The receiving entry prepared a bounded diagnostic before worker publication.
			}
		} catch (...) {
			m_Result.Completion = FileReadCompletion::ReadFailed;
		}

		m_ReadPrepared = true;

		return m_Result.Completion;
	}

	void RawFileReadDestination::PublishSelected() noexcept {
		if (m_State.load(std::memory_order_acquire) != kDestinationSelected || !m_ReadPrepared) {
			std::terminate();
		}

		// Native writes payload/count before completion. This is the last access through the worker borrow.
		m_State.store(static_cast<std::uint32_t>(m_Result.Completion), std::memory_order_release);
	}

	FileReadCompletion RawFileReadDestination::Completion() const noexcept {
		const auto state = m_State.load(std::memory_order_acquire);

		return state >= kDestinationUnassigned ? FileReadCompletion::Pending : static_cast<FileReadCompletion>(state);
	}

	bool RawFileReadDestination::CanRetire() const noexcept {
		const auto state = m_State.load(std::memory_order_acquire);

		return state != kDestinationQueued && state != kDestinationSelected;
	}

	void RawFileReadDestination::RequireCanRetire() const {
		RequireOwnerThread();
		if (!CanRetire()) {
			throw std::runtime_error("raw read destination still belongs to an actual queued or selected request");
		}
	}

	std::size_t RawFileReadDestination::PreparedByteCount() const {
		RequireOwnerThread();
		if (Completion() == FileReadCompletion::Pending || m_Taken) {
			throw std::runtime_error("raw read byte count requires an actual completed unconsumed result");
		}

		return m_Result.Bytes.size();
	}

	PreparedRawFileResult RawFileReadDestination::TakePrepared() {
		RequireOwnerThread();
		const auto completion = Completion();
		if (completion == FileReadCompletion::Pending || m_Taken) {
			throw std::runtime_error("raw read destination requires one actual completed prepared result");
		}

		m_Taken = true;
		auto result = std::move(m_Result);
		result.Completion = completion;

		return result;
	}

} // namespace sgre::io
