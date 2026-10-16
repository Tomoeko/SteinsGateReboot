#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace sgre::resource {
	class ResourceCache;
}

namespace sgre::io {
	class PendingRawReadRequests;

	enum class FileReadCompletion : std::uint32_t {
		Pending = 0,
		Succeeded = 1,
		ReadFailed = 2,
		Cancelled = 3,
		PostprocessFailed = 4
	};

	struct PreparedRawFileRead {
		std::filesystem::path Filename;
		std::uint64_t Offset = 0;
		// Native zero selects the complete physical file size; it does not select the remaining range.
		std::uint32_t Length = 0;
		std::size_t MaxBytes = 160 * 1024 * 1024;
	};

	struct PreparedRawFileResult {
		FileReadCompletion Completion = FileReadCompletion::Pending;
		std::vector<std::uint8_t> Bytes;
		std::string Error;
	};

	class RawFileReadDestination {
	public:
		// The actual receiving Loading entry owns this cell. Request/worker pointers borrow it without a pin.
		RawFileReadDestination();
		~RawFileReadDestination() noexcept;
		RawFileReadDestination(const RawFileReadDestination&) = delete;
		RawFileReadDestination& operator=(const RawFileReadDestination&) = delete;
		RawFileReadDestination(RawFileReadDestination&&) = delete;
		RawFileReadDestination& operator=(RawFileReadDestination&&) = delete;

		// Actual request owner calls these while holding its pending queue lock.
		void MarkQueued();
		[[nodiscard]] bool CancelQueued();
		void Select();
		// Worker executes a real checked FileReader region read: native request type0, wrapper flag0 only.
		void ProduceSelected(const PreparedRawFileRead& source) noexcept;
		// Native loop reads current bytes, selects next, then publishes current success; failures publish before next selection.
		[[nodiscard]] FileReadCompletion ReadSelected(const PreparedRawFileRead& source) noexcept;
		void PublishSelected() noexcept;
		[[nodiscard]] FileReadCompletion Completion() const noexcept;
		[[nodiscard]] bool CanRetire() const noexcept;
		void RequireCanRetire() const;

	private:
		friend class sgre::resource::ResourceCache;
		friend class PendingRawReadRequests;
		[[nodiscard]] std::size_t PreparedByteCount() const;
		[[nodiscard]] PreparedRawFileResult TakePrepared();
		void RequireOwnerThread() const;
		std::atomic<std::uint32_t> m_State;
		PreparedRawFileResult m_Result;
		std::thread::id m_OwnerThread;
		bool m_Taken = false;
		bool m_ReadPrepared = false;
	};

} // namespace sgre::io
