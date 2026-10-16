#include "sgre/io/wrapped_psb_destination.hpp"

#include "sgre/io/file_reader.hpp"
#include "sgre/resource/mzs_shell.hpp"
#include "sgre/resource/zstd_decoder.hpp"

#include <exception>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sgre::io {

	PublishedPsbFile::PublishedPsbFile(std::vector<std::uint8_t> bytes, resource::PsbDocument document) : m_Bytes(std::move(bytes)), m_Document(std::move(document)) {
	}

	std::span<const std::uint8_t> PublishedPsbFile::Bytes() const noexcept {
		return m_Bytes;
	}

	const std::vector<std::uint8_t>& PublishedPsbFile::BackingStorage() const noexcept {
		return m_Bytes;
	}

	const resource::PsbDocument& PublishedPsbFile::Document() const noexcept {
		return m_Document;
	}

	WrappedPsbReadDestination::WrappedPsbReadDestination() : m_OwnerThread(std::this_thread::get_id()), m_Error("wrapped PSB read did not produce its actual output") {
	}

	WrappedPsbReadDestination::~WrappedPsbReadDestination() noexcept {
		if (!CanRetire()) {
			std::terminate();
		}
	}

	void WrappedPsbReadDestination::RequireOwnerThread() const {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("wrapped PSB receiving ownership requires its actual owner thread");
		}
	}

	void WrappedPsbReadDestination::MarkQueued() {
		RequireOwnerThread();
		auto expected = WrappedPsbReadState::Unassigned;
		if (!m_State.compare_exchange_strong(expected, WrappedPsbReadState::Queued, std::memory_order_release, std::memory_order_relaxed)) {
			throw std::runtime_error("wrapped PSB receiver accepts only one actual request");
		}
	}

	void WrappedPsbReadDestination::Select() {
		auto expected = WrappedPsbReadState::Queued;
		if (!m_State.compare_exchange_strong(expected, WrappedPsbReadState::Selected, std::memory_order_acq_rel, std::memory_order_relaxed)) {
			throw std::runtime_error("wrapped PSB selection requires its actual queued receiver");
		}
	}

	void WrappedPsbReadDestination::StoreDiagnostic(std::string_view diagnostic) noexcept {
		try {
			if (!diagnostic.empty()) {
				m_Error.assign(diagnostic.substr(0, 4096));
			}
		} catch (...) {
			// Receiving owner prepared a diagnostic before publishing this worker borrow.
		}
	}

	bool WrappedPsbReadDestination::ReadPhysicalSelected(const PreparedWrappedPsbRead& source) noexcept {
		if (State() != WrappedPsbReadState::Selected || m_PhysicalPrepared) {
			std::terminate();
		}

		m_FailurePhase = WrappedPsbFailurePhase::PhysicalRead;
		try {
			const auto& region = source.Region;
			if (region.Filename.empty() || region.Filename.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos || region.MaxBytes == 0 || region.MaxBytes > 160 * 1024 * 1024) {
				throw std::runtime_error("wrapped PSB read requires a concrete bounded physical region");
			}

			FileReader reader(region.Filename);
			const auto size = region.Length == 0 ? reader.Size() : region.Length;
			if (size > region.MaxBytes || size >= std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("wrapped PSB physical size exceeds its native word/allocation bound");
			}

			m_PackedBytes = reader.Read(region.Offset, static_cast<std::size_t>(size));
			m_PhysicalSucceeded = true;
		} catch (const std::exception& error) {
			StoreDiagnostic(error.what());
		} catch (...) {
		}

		m_PhysicalPrepared = true;

		return m_PhysicalSucceeded;
	}

	bool WrappedPsbReadDestination::PrepareSelectedObject(const PreparedWrappedPsbRead& source) noexcept {
		if (State() != WrappedPsbReadState::Selected || !m_PhysicalPrepared || m_ReadPrepared) {
			std::terminate();
		}

		if (m_PhysicalSucceeded) {
			m_FailurePhase = WrappedPsbFailurePhase::ShellDecode;
			try {
				if (source.OriginalFilename.empty() || source.OriginalFilename.size() > 65536 || source.OriginalFilename.find('\0') != std::string::npos || source.Seed.empty() || source.Seed.size() > 65536 || source.Seed.find('\0') != std::string::npos || source.ShellKeyPeriod == 0 || source.ShellKeyPeriod > 65536 || source.MaxDecodedBytes == 0 || source.MaxDecodedBytes > 160 * 1024 * 1024 || source.MaxMetadataBytes == 0 || source.MaxMetadataBytes > source.MaxDecodedBytes || source.MaxMetadataBytes > 64 * 1024 * 1024) {
					throw std::runtime_error("wrapped PSB production requires exact cipher identity and checked decoding bounds");
				}

				const auto shell = resource::DecodeMzsShell(m_PackedBytes, source.OriginalFilename, source.Seed, source.ShellKeyPeriod, source.MaxDecodedBytes);
				auto bytes = resource::DecodeZstd(shell.CompressedBytes, shell.DecodedSize, source.MaxDecodedBytes);
				m_FailurePhase = WrappedPsbFailurePhase::PsbDecode;
				const auto header = resource::ReadPsbHeader(bytes);
				if (header.DataOffset > bytes.size() || header.DataOffset > source.MaxMetadataBytes) {
					throw std::runtime_error("wrapped PSB metadata exceeds its actual decoded extent/allocation bound");
				}

				auto document = resource::ReadPsbMetadata(std::span<const std::uint8_t>(bytes).first(header.DataOffset), bytes.size());
				if (document.SerializedSize != bytes.size()) {
					throw std::runtime_error("wrapped PSB object extent differs from its actual decoded bytes");
				}

				m_Output = std::make_unique<PublishedPsbFile>(std::move(bytes), std::move(document));
				m_FailurePhase = WrappedPsbFailurePhase::None;
				m_Error.clear();
			} catch (const std::exception& error) {
				StoreDiagnostic(error.what());
			} catch (...) {
			}
		}

		std::vector<std::uint8_t>{}.swap(m_PackedBytes);
		m_ReadPrepared = true;

		return static_cast<bool>(m_Output);
	}

	bool WrappedPsbReadDestination::ReadSelected(const PreparedWrappedPsbRead& source) noexcept {
		static_cast<void>(ReadPhysicalSelected(source));

		return PrepareSelectedObject(source);
	}

	void WrappedPsbReadDestination::PublishSelected() noexcept {
		if (State() != WrappedPsbReadState::Selected || !m_ReadPrepared) {
			std::terminate();
		}

		// The owning object pointer is ready before the release publication; no worker borrow follows.
		m_State.store(m_Output ? WrappedPsbReadState::Succeeded : WrappedPsbReadState::Failed, std::memory_order_release);
	}

	bool WrappedPsbReadDestination::AbandonQueued() {
		RequireOwnerThread();
		auto expected = WrappedPsbReadState::Queued;

		return m_State.compare_exchange_strong(expected, WrappedPsbReadState::Abandoned, std::memory_order_release, std::memory_order_relaxed);
	}

	WrappedPsbReadState WrappedPsbReadDestination::State() const noexcept {
		return m_State.load(std::memory_order_acquire);
	}

	bool WrappedPsbReadDestination::CanRetire() const noexcept {
		const auto state = State();

		return state != WrappedPsbReadState::Queued && state != WrappedPsbReadState::Selected;
	}

	WrappedPsbFailurePhase WrappedPsbReadDestination::FailurePhase() const {
		RequireOwnerThread();
		if (State() != WrappedPsbReadState::Failed) {
			throw std::runtime_error("wrapped PSB failure inspection requires actual published failure");
		}

		return m_FailurePhase;
	}

	const std::string& WrappedPsbReadDestination::Error() const {
		static_cast<void>(FailurePhase());

		return m_Error;
	}

	std::unique_ptr<PublishedPsbFile> WrappedPsbReadDestination::TakePublishedObject() {
		RequireOwnerThread();
		if (State() != WrappedPsbReadState::Succeeded || !m_Output) {
			throw std::runtime_error("wrapped PSB receiving owner requires one actual published object");
		}

		return std::move(m_Output);
	}

} // namespace sgre::io
