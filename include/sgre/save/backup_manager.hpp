#pragma once

#include "sgre/core/background_calls.hpp"
#include "sgre/resource/struct_storage.hpp"
#include "sgre/save/backup_files.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sgre::save {

	enum class BackupProfile {
		Ordinary,
		Adventure
	};

	struct BackupSegmentMetadata {
		std::int32_t FileId = 0;
		bool RecordFileId = true;
		bool NoConfirm = false;
		bool NoticeComplete = false;
		std::string Comment;
		std::string Detail;
	};

	class BackupManager;

	class BackupSegment {
	public:
		BackupSegment(BackupManager& manager, resource::StructStorage& storage, std::uint32_t ordinal, std::uint32_t maximum_files);
		// The caller retains the exact mutable Struct owner through the segment and all pending jobs.
		[[nodiscard]] resource::StructStorage& Storage() const noexcept;
		[[nodiscard]] std::uint32_t Ordinal() const noexcept;
		[[nodiscard]] std::uint32_t MaxFileCount() const noexcept;
		[[nodiscard]] bool Initialized() const noexcept;
		[[nodiscard]] bool Success() const noexcept;
		[[nodiscard]] bool Crypted() const noexcept;
		[[nodiscard]] bool NoSpace() const noexcept;
		[[nodiscard]] bool Broken() const noexcept;
		[[nodiscard]] bool NoFile() const noexcept;
		[[nodiscard]] bool Offline() const noexcept;
		[[nodiscard]] const BackupSegmentMetadata& Metadata() const noexcept;
		void SetFileId(std::int32_t value);
		void SetRecordFileId(bool value);
		void SetNoConfirm(bool value);
		void SetNoticeComplete(bool value);
		void SetComment(std::string_view value);
		void SetDetail(std::string_view value);

	private:
		BackupManager& m_Manager;
		resource::StructStorage& m_Storage;
		std::uint32_t m_Ordinal = 0;
		std::uint32_t m_MaximumFiles = 0;
		BackupSegmentMetadata m_Metadata;
		std::atomic<bool> m_Initialized = false;
		std::atomic<bool> m_Success = false;
		std::atomic<bool> m_Crypted = false;
		std::atomic<bool> m_NoSpace = false;
		std::atomic<bool> m_Broken = false;
		std::atomic<bool> m_NoFile = false;
		std::atomic<bool> m_Offline = false;
		std::string m_LastError;
		friend class BackupManager;
	};

	class BackupManager {
	public:
		// All supplied owners enclose Close. InitializeDirectory performs the real platform directory create/probe.
		// Children is the actual SQLayerBase camera Parallel group; this owner appends its genuine work set.
		BackupManager(core::BackgroundCallManager& background, scene::ParallelTaskSet& children, std::function<bool()> initialize_directory, std::size_t maximum_segments = 1024, std::size_t maximum_jobs = 128, BackupProfile profile = BackupProfile::Ordinary);
		~BackupManager() noexcept;
		BackupManager(const BackupManager&) = delete;
		BackupManager& operator=(const BackupManager&) = delete;
		BackupManager(BackupManager&&) = delete;
		BackupManager& operator=(BackupManager&&) = delete;
		[[nodiscard]] BackupSegment& AddSegment(resource::StructStorage& storage, std::uint32_t maximum_files);
		[[nodiscard]] BackupSegment& Segment(std::uint32_t index);
		[[nodiscard]] std::size_t SegmentCount() const noexcept;
		void Initialize();
		// Configure before native segments; the actual directory and codec enclose every pending file job.
		void ConfigureFiles(BackupFileServices services);
		// Native empty input means every actual segment. No alternate Struct target is fabricated.
		void Load(std::span<const BackupLoadRequest> requests = {});
		// Native SQAdvBackupSegment autoload queues only this manager-owned segment and preserves other results.
		void LoadSegment(std::uint32_t ordinal);
		void SaveSegment(std::uint32_t ordinal);
		[[nodiscard]] bool NoSpace() const noexcept;
		[[nodiscard]] bool Broken() const noexcept;
		[[nodiscard]] bool NoFile() const noexcept;
		[[nodiscard]] bool Offline() const noexcept;
		[[nodiscard]] bool Running() const;
		[[nodiscard]] bool Initialized() const noexcept;
		[[nodiscard]] bool Success() const noexcept;
		[[nodiscard]] bool InitSuccess() const noexcept;
		[[nodiscard]] BackupProfile Profile() const noexcept;
		[[nodiscard]] bool EnableMetaData() const;
		void SetEnableMetaData(bool enabled);
		[[nodiscard]] std::string_view Title() const noexcept;
		[[nodiscard]] std::string_view SecureFileId() const noexcept;
		void SetTitle(std::string_view value);
		void SetSecureFileId(std::string_view value);
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		void Close();
		void RequireOwner() const;
		void RequireInputMutation() const;

	private:
		class BackupJob;
		void AppendJob(std::unique_ptr<BackupJob> job);
		void QueueLoad(std::span<const BackupLoadRequest> requests, bool reset_all_segments);
		void PublishLoad(const BackupJob& job);
		void PublishSave(const BackupJob& job);
		void ExecuteInitialize();
		void ReleaseJob(std::uint64_t identity, bool publish = false);
		core::BackgroundCallManager& m_Background;
		scene::ParallelTaskSet& m_Children;
		std::function<bool()> m_InitializeDirectory;
		std::thread::id m_OwnerThread = std::this_thread::get_id();
		std::size_t m_MaximumSegments = 0;
		std::size_t m_MaximumJobs = 0;
		BackupProfile m_Profile = BackupProfile::Ordinary;
		bool m_EnableMetaData = false;
		scene::ParallelTaskSet m_Work;
		scene::ParallelTaskHandle m_Bridge;
		std::vector<std::unique_ptr<BackupSegment>> m_Segments;
		std::map<std::uint64_t, std::unique_ptr<BackupJob>> m_Jobs;
		std::optional<BackupFileServices> m_Files;
		std::uint64_t m_NextJob = 1;
		std::atomic<bool> m_Initialized = false;
		std::atomic<bool> m_Success = false;
		std::string m_Title;
		std::string m_SecureFileId;
		bool m_Attached = false;
		bool m_Closed = false;
	};

} // namespace sgre::save
