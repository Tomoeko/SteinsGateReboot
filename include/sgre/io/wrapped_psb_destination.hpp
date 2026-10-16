#pragma once

#include "sgre/io/raw_read_destination.hpp"
#include "sgre/resource/psb.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sgre::io {
	class FileReadProcessor;

	struct PreparedWrappedPsbRead {
		PreparedRawFileRead Region;
		// Exact original resource identity supplies the native cipher basename; physical archive path is separate.
		std::string OriginalFilename;
		std::string Seed;
		std::size_t ShellKeyPeriod = 131;
		std::size_t MaxDecodedBytes = 160 * 1024 * 1024;
		std::size_t MaxMetadataBytes = 64 * 1024 * 1024;
	};

	class PublishedPsbFile {
	public:
		PublishedPsbFile(std::vector<std::uint8_t> bytes, resource::PsbDocument document);
		PublishedPsbFile(const PublishedPsbFile&) = delete;
		PublishedPsbFile& operator=(const PublishedPsbFile&) = delete;
		PublishedPsbFile(PublishedPsbFile&&) = delete;
		PublishedPsbFile& operator=(PublishedPsbFile&&) = delete;
		[[nodiscard]] std::span<const std::uint8_t> Bytes() const noexcept;
		[[nodiscard]] const std::vector<std::uint8_t>& BackingStorage() const noexcept;
		[[nodiscard]] const resource::PsbDocument& Document() const noexcept;

	private:
		std::vector<std::uint8_t> m_Bytes;
		resource::PsbDocument m_Document;
	};

	// Internal receiving-cell states, not native per-row completion values: mount rows have no status pointer.
	enum class WrappedPsbReadState : std::uint8_t {
		Unassigned,
		Queued,
		Selected,
		Succeeded,
		Failed,
		Abandoned
	};
	enum class WrappedPsbFailurePhase : std::uint8_t {
		None,
		PhysicalRead,
		ShellDecode,
		PsbDecode
	};

	class WrappedPsbReadDestination {
	public:
		WrappedPsbReadDestination();
		~WrappedPsbReadDestination() noexcept;
		WrappedPsbReadDestination(const WrappedPsbReadDestination&) = delete;
		WrappedPsbReadDestination& operator=(const WrappedPsbReadDestination&) = delete;
		WrappedPsbReadDestination(WrappedPsbReadDestination&&) = delete;
		WrappedPsbReadDestination& operator=(WrappedPsbReadDestination&&) = delete;
		void MarkQueued();
		void Select();
		// Native wrapped loop selects its following request between physical read and current object preparation.
		[[nodiscard]] bool ReadPhysicalSelected(const PreparedWrappedPsbRead& source) noexcept;
		[[nodiscard]] bool PrepareSelectedObject(const PreparedWrappedPsbRead& source) noexcept;
		// Convenience concrete FileReader -> MZS/Zstandard -> owning PSB object with no intervening selection.
		[[nodiscard]] bool ReadSelected(const PreparedWrappedPsbRead& source) noexcept;
		void PublishSelected() noexcept;
		// Authored forced-close boundary: owner removes a still-queued request before abandoning its borrow.
		[[nodiscard]] bool AbandonQueued();
		[[nodiscard]] WrappedPsbReadState State() const noexcept;
		[[nodiscard]] bool CanRetire() const noexcept;
		[[nodiscard]] WrappedPsbFailurePhase FailurePhase() const;
		[[nodiscard]] const std::string& Error() const;
		// Output owner must survive temporary reader/mount retirement and unmount. Native final retirement is open.
		[[nodiscard]] std::unique_ptr<PublishedPsbFile> TakePublishedObject();

	private:
		friend class FileReadProcessor;
		void RequireOwnerThread() const;
		void StoreDiagnostic(std::string_view diagnostic) noexcept;
		std::thread::id m_OwnerThread;
		std::atomic<WrappedPsbReadState> m_State = WrappedPsbReadState::Unassigned;
		std::unique_ptr<PublishedPsbFile> m_Output;
		std::vector<std::uint8_t> m_PackedBytes;
		std::string m_Error;
		WrappedPsbFailurePhase m_FailurePhase = WrappedPsbFailurePhase::None;
		bool m_PhysicalPrepared = false;
		bool m_PhysicalSucceeded = false;
		bool m_ReadPrepared = false;
	};

} // namespace sgre::io
