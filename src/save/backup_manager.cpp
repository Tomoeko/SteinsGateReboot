#include "sgre/save/backup_manager.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::save {

	static std::string BackupText(std::string_view value) {
		value = value.substr(0, value.find('\0'));
		if (value.size() > 1024 * 1024) {
			throw std::runtime_error("Backup metadata exceeds its bounded string domain");
		}

		return std::string(value);
	}

	class BackupManager::BackupJob {
	public:
		struct LoadInput {
			std::uint32_t Ordinal = 0;
			BackupFileReadRequest Request;
		};
		bool ResetAllSegments = true;
		std::vector<LoadInput> Inputs;
		std::vector<BackupFileReadResult> Results;
		std::vector<BackupFileWriteRequest> SaveInputs;
		std::vector<BackupFileWriteResult> SaveResults;
		std::function<void()> Complete;
		// Task destruction joins the worker before any borrowed input/result storage expires.
		std::unique_ptr<core::BackgroundCallTask> Task;
	};

	BackupSegment::BackupSegment(BackupManager& manager, resource::StructStorage& storage, std::uint32_t ordinal, std::uint32_t maximum_files)
	    : m_Manager(manager), m_Storage(storage), m_Ordinal(ordinal), m_MaximumFiles(maximum_files) {
		m_Success.store(manager.Profile() == BackupProfile::Adventure, std::memory_order_release);
	}

	resource::StructStorage& BackupSegment::Storage() const noexcept {
		return m_Storage;
	}

	std::uint32_t BackupSegment::Ordinal() const noexcept {
		return m_Ordinal;
	}

	std::uint32_t BackupSegment::MaxFileCount() const noexcept {
		return m_MaximumFiles;
	}

	bool BackupSegment::Initialized() const noexcept {
		return m_Initialized.load(std::memory_order_acquire);
	}

	bool BackupSegment::Success() const noexcept {
		return m_Success.load(std::memory_order_acquire);
	}

	bool BackupSegment::Crypted() const noexcept {
		return m_Crypted.load(std::memory_order_acquire);
	}

	bool BackupSegment::NoSpace() const noexcept {
		return m_NoSpace.load(std::memory_order_acquire);
	}

	bool BackupSegment::Broken() const noexcept {
		return m_Broken.load(std::memory_order_acquire);
	}

	bool BackupSegment::NoFile() const noexcept {
		return m_NoFile.load(std::memory_order_acquire);
	}

	bool BackupSegment::Offline() const noexcept {
		return m_Offline.load(std::memory_order_acquire);
	}

	const BackupSegmentMetadata& BackupSegment::Metadata() const noexcept {
		return m_Metadata;
	}

	void BackupSegment::SetFileId(std::int32_t value) {
		m_Manager.RequireInputMutation();
		m_Metadata.FileId = value;
	}

	void BackupSegment::SetRecordFileId(bool value) {
		m_Manager.RequireInputMutation();
		m_Metadata.RecordFileId = value;
	}

	void BackupSegment::SetNoConfirm(bool value) {
		m_Manager.RequireInputMutation();
		m_Metadata.NoConfirm = value;
	}

	void BackupSegment::SetNoticeComplete(bool value) {
		m_Manager.RequireInputMutation();
		m_Metadata.NoticeComplete = value;
	}

	void BackupSegment::SetComment(std::string_view value) {
		m_Manager.RequireInputMutation();
		m_Metadata.Comment = BackupText(value);
	}

	void BackupSegment::SetDetail(std::string_view value) {
		m_Manager.RequireInputMutation();
		m_Metadata.Detail = BackupText(value);
	}

	BackupManager::BackupManager(core::BackgroundCallManager& background, scene::ParallelTaskSet& children, std::function<bool()> initialize_directory, std::size_t maximum_segments, std::size_t maximum_jobs, BackupProfile profile)
	    : m_Background(background), m_Children(children), m_InitializeDirectory(std::move(initialize_directory)), m_MaximumSegments(maximum_segments), m_MaximumJobs(maximum_jobs), m_Profile(profile), m_Work(maximum_jobs) {
		if ((profile != BackupProfile::Ordinary && profile != BackupProfile::Adventure) || !m_InitializeDirectory || maximum_segments == 0 || maximum_segments > 65536 || maximum_jobs == 0 || maximum_jobs > 1024) {
			throw std::runtime_error("BackupManager requires its actual directory/background/camera owners and bounded capacity");
		}

		m_Work.SetRetireWhenEmpty(false);
		scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [this](auto) {
			m_Work.Update();
		};
		callbacks.Draw = [this](auto, auto argument) {
			m_Work.Draw(argument);
		};
		callbacks.CanRelease = [this](auto) {
			return m_Work.CanClear();
		};
		callbacks.Release = [this](auto) {
			m_Attached = false;
			m_Work.Clear();
		};
		m_Bridge = m_Children.Append(std::move(callbacks));
		m_Attached = true;
	}

	BackupManager::~BackupManager() noexcept {
		try {
			Close();
		} catch (...) {
			if (!m_Closed) {
				std::terminate();
			}
		}
	}

	void BackupManager::RequireOwner() const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Closed) {
			throw std::runtime_error("BackupManager requires its live actual owner thread");
		}
	}

	BackupSegment& BackupManager::AddSegment(resource::StructStorage& storage, std::uint32_t maximum_files) {
		RequireOwner();
		if (m_Work.Count() != 0 || m_Segments.size() == m_MaximumSegments || maximum_files > 65536) {
			throw std::runtime_error("Backup segment construction requires stable real worker inputs and bounded capacity");
		}

		auto segment = std::make_unique<BackupSegment>(*this, storage, static_cast<std::uint32_t>(m_Segments.size()), maximum_files);
		m_Segments.push_back(std::move(segment));

		return *m_Segments.back();
	}

	BackupSegment& BackupManager::Segment(std::uint32_t index) {
		RequireOwner();
		if (index >= m_Segments.size()) {
			throw std::runtime_error("Backup segment index exceeds its actual manager");
		}

		return *m_Segments[index];
	}

	std::size_t BackupManager::SegmentCount() const noexcept {
		return m_Segments.size();
	}

	void BackupManager::RequireInputMutation() const {
		RequireOwner();
		if (m_Work.Count() != 0) {
			throw std::runtime_error("Backup input mutation requires its actual pending worker borrows to retire");
		}
	}

	void BackupManager::Initialize() {
		RequireOwner();
		auto job = std::make_unique<BackupJob>();
		job->Task = std::make_unique<core::BackgroundCallTask>(m_Background, core::BackgroundCallLane::BelowNormal, [this] {
			ExecuteInitialize();
		});
		AppendJob(std::move(job));
	}

	void BackupManager::ConfigureFiles(BackupFileServices services) {
		RequireInputMutation();
		if (m_Profile != BackupProfile::Adventure || m_Files || !m_Segments.empty() || services.Directory.empty() || !services.Decrypt) {
			throw std::runtime_error("Adventure backup files require one real directory/codec configuration before native segments");
		}

		m_Files = std::move(services);
	}

	void BackupManager::Load(std::span<const BackupFileSelection> requests) {
		QueueLoad(requests, true);
	}

	void BackupManager::LoadSegment(std::uint32_t ordinal) {
		RequireInputMutation();
		static_cast<void>(Segment(ordinal));
		BackupFileSelection request;
		request.SegmentId = ordinal;
		QueueLoad(std::span(&request, 1), false);
	}

	void BackupManager::QueueLoad(std::span<const BackupFileSelection> requests, bool reset_all_segments) {
		RequireInputMutation();
		if (m_Profile != BackupProfile::Adventure || !m_Files || !InitSuccess() || requests.size() > 65536) {
			throw std::runtime_error("Adventure backup load requires its real initialized directory/codec and bounded native requests");
		}

		auto job = std::make_unique<BackupJob>();
		job->ResetAllSegments = reset_all_segments;
		std::vector<std::int32_t> selected_file_ids;
		selected_file_ids.reserve(m_Segments.size());
		for (const auto& segment : m_Segments) {
			selected_file_ids.push_back(segment->Metadata().FileId);
		}
		const auto add_input = [&](std::uint32_t ordinal, std::int32_t file_id) {
			if (ordinal >= m_Segments.size()) {
				return;
			}
			const auto& segment = *m_Segments[ordinal];
			if (file_id >= 0) {
				selected_file_ids[ordinal] = file_id;
			}
			BackupJob::LoadInput input;
			input.Ordinal = ordinal;
			input.Request.SegmentId = ordinal;
			input.Request.FileId = selected_file_ids[ordinal];
			input.Request.RootBytes = segment.Storage().RootSize();
			input.Request.EnableMetaData = m_EnableMetaData;
			input.Request.SecureFileId = m_SecureFileId;
			job->Inputs.push_back(std::move(input));
		};
		if (requests.empty()) {
			for (std::uint32_t ordinal = 0; ordinal < m_Segments.size(); ++ordinal) {
				add_input(ordinal, -1);
			}
		} else {
			for (const auto& request : requests) {
				add_input(request.SegmentId, request.FileId);
			}
		}
		std::size_t total_bytes = 0;
		for (const auto& input : job->Inputs) {
			constexpr std::size_t kMaximumPublicationBytes = 160 * 1024 * 1024;
			if (input.Request.RootBytes > kMaximumPublicationBytes - total_bytes) {
				throw std::runtime_error("Adventure backup request exceeds its bounded main-thread publication storage");
			}
			total_bytes += input.Request.RootBytes;
		}
		auto* actual_job = job.get();
		job->Results.reserve(job->Inputs.size());
		job->Complete = [this, actual_job] {
			PublishLoad(*actual_job);
		};
		job->Task = std::make_unique<core::BackgroundCallTask>(m_Background, core::BackgroundCallLane::BelowNormal, [this, actual_job] {
			for (const auto& input : actual_job->Inputs) {
				actual_job->Results.push_back(ReadAdventureBackupFiles(m_Files->Directory, input.Request, m_Files->Decrypt));
			}
		});
		AppendJob(std::move(job));
	}

	void BackupManager::Save(std::span<const BackupFileSelection> requests) {
		QueueSave(requests, true);
	}

	void BackupManager::SaveSegment(std::uint32_t ordinal) {
		RequireInputMutation();
		static_cast<void>(Segment(ordinal));
		BackupFileSelection request;
		request.SegmentId = ordinal;
		QueueSave(std::span(&request, 1), false);
	}

	void BackupManager::QueueSave(std::span<const BackupFileSelection> requests, bool reset_all_segments) {
		RequireInputMutation();
		if (m_Profile != BackupProfile::Adventure || !m_Files || !m_Files->Encrypt || !m_Files->ReadTimestamp || !InitSuccess() || requests.size() > 65536) {
			throw std::runtime_error("Adventure save requires its initialized directory, encryption, timestamp services and bounded native requests");
		}
		auto job = std::make_unique<BackupJob>();
		job->ResetAllSegments = reset_all_segments;
		std::vector<std::int32_t> selected_file_ids;
		selected_file_ids.reserve(m_Segments.size());
		for (const auto& segment : m_Segments) {
			selected_file_ids.push_back(segment->Metadata().FileId);
		}
		std::size_t total_bytes = 0;
		const auto add_input = [&](std::uint32_t ordinal, std::int32_t file_id) {
			if (ordinal >= m_Segments.size()) {
				return;
			}
			const auto& segment = *m_Segments[ordinal];
			constexpr std::size_t kMaximumPublicationBytes = 160 * 1024 * 1024;
			if (segment.Storage().RootSize() > kMaximumPublicationBytes - total_bytes) {
				throw std::runtime_error("Adventure save exceeds its bounded aggregate Struct extent");
			}
			if (file_id >= 0) {
				selected_file_ids[ordinal] = file_id;
			}
			BackupFileWriteRequest input;
			input.SegmentId = ordinal;
			input.FileId = selected_file_ids[ordinal];
			input.EnableMetaData = m_EnableMetaData;
			input.SecureFileId = m_SecureFileId;
			// Serialize on the Struct owner thread; workers consume this job's immutable snapshots.
			input.Bytes = segment.Storage().Serialize();
			total_bytes += input.Bytes.size();
			job->SaveInputs.push_back(std::move(input));
		};
		if (requests.empty()) {
			for (std::uint32_t ordinal = 0; ordinal < m_Segments.size(); ++ordinal) {
				add_input(ordinal, -1);
			}
		} else {
			for (const auto& request : requests) {
				add_input(request.SegmentId, request.FileId);
			}
		}
		job->SaveResults.reserve(job->SaveInputs.size());
		auto* actual_job = job.get();
		job->Complete = [this, actual_job] {
			PublishSave(*actual_job);
		};
		job->Task = std::make_unique<core::BackgroundCallTask>(m_Background, core::BackgroundCallLane::BelowNormal, [this, actual_job] {
			for (auto& input : actual_job->SaveInputs) {
				auto& result = actual_job->SaveResults.emplace_back();
				try {
					input.Timestamp = m_Files->ReadTimestamp();
					result = WriteAdventureBackupFiles(m_Files->Directory, input, m_Files->Encrypt);
				} catch (const std::exception& error) {
					result.Error = error.what();
					throw;
				} catch (...) {
					result.Error = "Adventure backup worker failed before physical completion";
					throw;
				}
			}
		});
		AppendJob(std::move(job));
	}

	void BackupManager::PublishSave(const BackupJob& job) {
		RequireOwner();
		if (job.SaveResults.size() > job.SaveInputs.size()) {
			throw std::runtime_error("Adventure save completion lost its actual ordered input/results");
		}
		// Native0761c0 initializes every batch result, then075a40 updates each requested segment in order.
		if (job.ResetAllSegments) {
			for (auto& segment : m_Segments) {
				segment->m_Success.store(true, std::memory_order_release);
				segment->m_NoSpace.store(false, std::memory_order_release);
				segment->m_Broken.store(false, std::memory_order_release);
				segment->m_NoFile.store(false, std::memory_order_release);
				segment->m_Crypted.store(false, std::memory_order_release);
				segment->m_Offline.store(false, std::memory_order_release);
				segment->m_LastError.clear();
			}
		}
		for (std::size_t index = 0; index != job.SaveResults.size(); ++index) {
			const auto& input = job.SaveInputs[index];
			auto& segment = *m_Segments[input.SegmentId];
			const auto& result = job.SaveResults[index];
			segment.m_Metadata.FileId = input.FileId;
			segment.m_Success.store(false, std::memory_order_release);
			segment.m_NoSpace.store(result.NoSpace, std::memory_order_release);
			segment.m_Broken.store(false, std::memory_order_release);
			segment.m_NoFile.store(false, std::memory_order_release);
			segment.m_Crypted.store(false, std::memory_order_release);
			segment.m_Offline.store(false, std::memory_order_release);
			segment.m_LastError = result.Error;
			segment.m_Success.store(result.Success, std::memory_order_release);
		}
	}

	void BackupManager::PublishLoad(const BackupJob& job) {
		RequireOwner();
		if (job.Results.size() != job.Inputs.size()) {
			throw std::runtime_error("Adventure backup completion lost its real worker results");
		}
		// Native0761c0 resets every batch result;075330 changes only its actual segment.
		for (auto& segment : m_Segments) {
			if (!job.ResetAllSegments && std::none_of(job.Inputs.begin(), job.Inputs.end(), [&](const auto& input) {
				    return input.Ordinal == segment->Ordinal();
			    })) {
				continue;
			}
			segment->m_Success.store(job.ResetAllSegments, std::memory_order_release);
			segment->m_NoSpace.store(false, std::memory_order_release);
			segment->m_Broken.store(false, std::memory_order_release);
			segment->m_NoFile.store(false, std::memory_order_release);
			segment->m_Crypted.store(false, std::memory_order_release);
			segment->m_Offline.store(false, std::memory_order_release);
			segment->m_LastError.clear();
		}
		for (std::size_t index = 0; index < job.Inputs.size(); ++index) {
			const auto& input = job.Inputs[index];
			const auto& result = job.Results[index];
			auto& segment = *m_Segments[input.Ordinal];
			segment.m_Metadata.FileId = input.Request.FileId;
			if (result.Success) {
				if (result.Bytes.size() == segment.Storage().RootSize()) {
					segment.Storage().Unserialize(result.Bytes);
				} else {
					auto bytes = segment.Storage().Serialize();
					std::copy_n(result.Bytes.begin(), std::min(bytes.size(), result.Bytes.size()), bytes.begin());
					segment.Storage().Unserialize(bytes);
				}
			}
			segment.m_NoFile.store(result.NoFile, std::memory_order_release);
			segment.m_Broken.store(result.Broken, std::memory_order_release);
			segment.m_Crypted.store(result.Crypted, std::memory_order_release);
			segment.m_LastError = result.Error;
			segment.m_Success.store(result.Success, std::memory_order_release);
		}
	}

	void BackupManager::AppendJob(std::unique_ptr<BackupJob> job) {
		RequireOwner();
		if (m_Jobs.size() == m_MaximumJobs || m_NextJob == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("Backup request exceeds its real bounded job ownership");
		}

		const auto identity = m_NextJob++;
		scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [this, identity](auto handle) {
			auto& current = *m_Jobs.at(identity);
			current.Task->Update();
			m_Work.SetStatus(handle, current.Task->Status());
		};
		callbacks.CanRelease = [this, identity](auto) {
			return m_Jobs.at(identity)->Task->CanClose();
		};
		callbacks.Release = [this, identity](auto handle) {
			ReleaseJob(identity, m_Work.State(handle).Status == scene::TaskStatus::Retire);
		};
		m_Jobs.emplace(identity, std::move(job));
		try {
			static_cast<void>(m_Work.Append(std::move(callbacks)));
		} catch (...) {
			const auto failure = std::current_exception();
			ReleaseJob(identity);
			std::rethrow_exception(failure);
		}
	}

	void BackupManager::ExecuteInitialize() {
		// Native537355 writes the whole word+0x18: Init1 and Success0 before the directory attempt.
		m_Success.store(false, std::memory_order_release);
		m_Initialized.store(true, std::memory_order_release);
		if (!m_InitializeDirectory()) {
			return;
		}
		for (auto& segment : m_Segments) {
			segment->m_Initialized.store(true, std::memory_order_release);
		}
		m_Success.store(true, std::memory_order_release);
	}

	void BackupManager::ReleaseJob(std::uint64_t identity, bool publish) {
		const auto found = m_Jobs.find(identity);
		if (found == m_Jobs.end()) {
			throw std::runtime_error("Backup release lost its actual background task");
		}

		std::exception_ptr failure;
		try {
			found->second->Task->Close();
		} catch (...) {
			if (!found->second->Task->IsClosed()) {
				throw;
			}
			failure = std::current_exception();
		}
		if (publish && found->second->Complete && (!failure || !found->second->SaveResults.empty())) {
			try {
				auto complete = std::exchange(found->second->Complete, {});
				complete();
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
		}
		m_Jobs.erase(found);
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	bool BackupManager::Running() const {
		RequireOwner();
		// Native079970 returns the actual count's low byte through a bool adapter.
		return static_cast<std::uint8_t>(m_Work.Count()) != 0;
	}

	bool BackupManager::NoSpace() const noexcept {
		return std::any_of(m_Segments.begin(), m_Segments.end(), [](const auto& segment) {
			return segment->NoSpace();
		});
	}

	bool BackupManager::Broken() const noexcept {
		return std::any_of(m_Segments.begin(), m_Segments.end(), [](const auto& segment) {
			return segment->Broken();
		});
	}

	bool BackupManager::NoFile() const noexcept {
		return std::any_of(m_Segments.begin(), m_Segments.end(), [](const auto& segment) {
			return segment->NoFile();
		});
	}

	bool BackupManager::Offline() const noexcept {
		return std::any_of(m_Segments.begin(), m_Segments.end(), [](const auto& segment) {
			return segment->Offline();
		});
	}

	bool BackupManager::Initialized() const noexcept {
		return m_Initialized.load(std::memory_order_acquire);
	}

	bool BackupManager::Success() const noexcept {
		if (m_Profile == BackupProfile::Adventure) {
			// Native076ce0 returns the conjunction of actual segment Success bytes, including the empty set.
			for (const auto& segment : m_Segments) {
				if (!segment->Success()) {
					return false;
				}
			}

			return true;
		}

		return InitSuccess();
	}

	bool BackupManager::InitSuccess() const noexcept {
		return m_Success.load(std::memory_order_acquire);
	}

	BackupProfile BackupManager::Profile() const noexcept {
		return m_Profile;
	}

	bool BackupManager::EnableMetaData() const {
		RequireOwner();
		if (m_Profile != BackupProfile::Adventure) {
			throw std::runtime_error("Backup metadata mode requires its actual Adventure manager");
		}

		return m_EnableMetaData;
	}

	void BackupManager::SetEnableMetaData(bool enabled) {
		RequireInputMutation();
		if (m_Profile != BackupProfile::Adventure) {
			throw std::runtime_error("Backup metadata mode requires its actual Adventure manager");
		}

		m_EnableMetaData = enabled;
	}

	std::string_view BackupManager::Title() const noexcept {
		return m_Title;
	}

	std::string_view BackupManager::SecureFileId() const noexcept {
		return m_SecureFileId;
	}

	void BackupManager::SetTitle(std::string_view value) {
		RequireOwner();
		m_Title = BackupText(value);
	}

	void BackupManager::SetSecureFileId(std::string_view value) {
		RequireInputMutation();
		m_SecureFileId = BackupText(value);
	}

	bool BackupManager::CanClose() const noexcept {
		return m_Closed || (std::this_thread::get_id() == m_OwnerThread && m_Work.CanClear() && (!m_Attached || m_Children.CanExtract(m_Bridge)));
	}

	bool BackupManager::IsClosed() const noexcept {
		return m_Closed;
	}

	void BackupManager::Close() {
		if (m_Closed) {
			return;
		}
		if (!CanClose()) {
			throw std::runtime_error("BackupManager retirement requires its actual idle camera/work/background owners");
		}

		std::exception_ptr failure;
		try {
			if (m_Attached) {
				auto bridge = m_Children.Extract(m_Bridge);
				bridge.Release();
			} else {
				m_Work.Clear();
			}
		} catch (...) {
			failure = std::current_exception();
		}
		if (m_Work.Count() != 0 || !m_Jobs.empty() || m_Attached) {
			throw std::runtime_error("BackupManager retirement did not finish its actual worker task set");
		}
		m_Segments.clear();
		m_Closed = true;
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::save
