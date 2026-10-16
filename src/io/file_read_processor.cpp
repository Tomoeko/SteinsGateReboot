#include "sgre/io/file_read_processor.hpp"

#include "sgre/io/file_read_serial.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::io {

	resource::ArchiveIndex PrepareIndexOnlyMount(const PublishedPsbFile& file, std::uint64_t body_bytes) {
		const auto& document = file.Document();
		if (!std::holds_alternative<resource::PsbObject>(document.Root.Data) || !document.Root.Find("id") || document.Root.Find("spec")) {
			throw std::runtime_error("index-only mount requires the observed object root with id and no spec; derived texture/purge production is not recovered");
		}

		return resource::ReadArchiveIndex(document, body_bytes);
	}

	static FileReadCompletion ReadActualPreparedRegion(RawFileReadDestination& destination, const PreparedRawFileRead& source) {
		return destination.ReadSelected(source);
	}

	class FileReadProcessor::Implementation {
	public:
		class OneShotIndexReader {
		public:
			OneShotIndexReader(resource::ArchiveMounts::ReadBatch& batch, core::BackgroundCallManager& background) : Batch(batch), Serial(Common, background, [this]() {
				                                                                                                         Run();
			                                                                                                         }),
			                                                                                                         RemainingPsbBytes(batch.AvailablePsbBytes), RemainingEntries(batch.AvailableEntries) {
				Pending.reserve(batch.Rows.size());
				for (auto& row : batch.Rows) {
					if (row->Destination.State() != WrappedPsbReadState::Unassigned) {
						throw std::runtime_error("fresh archive reader requires unassigned receiving cells before serial publication");
					}

					Pending.push_back(row.get());
				}
			}

			~OneShotIndexReader() noexcept {
				try {
					Close();
				} catch (...) {
					if (!Closed) {
						std::terminate();
					}
				}
			}

			resource::ArchiveMounts::ReadBatch& Batch;
			scene::CommonProcessTasks Common;
			FileReadSerialAttachment Serial;
			std::mutex Mutex;
			std::vector<resource::ArchiveMounts::ReadMountRow*> Pending;
			std::size_t RemainingPsbBytes = 0;
			std::size_t RemainingEntries = 0;
			std::thread::id OwnerThread = std::this_thread::get_id();
			std::atomic<bool> Fatal = false;
			bool FinishPhase = false;
			bool Closing = false;
			bool Closed = false;
			scene::TaskStatus Status = scene::TaskStatus::Active;

			resource::ArchiveMounts::ReadMountRow* SelectNext() {
				std::lock_guard lock(Mutex);
				if (Closing || Pending.empty()) {
					return nullptr;
				}

				auto* row = Pending.front();
				row->Destination.Select();
				Pending.erase(Pending.begin());

				return row;
			}

			void MarkQueued() {
				for (auto* row : Pending) {
					row->Destination.MarkQueued();
				}
			}

			void ValidateIndexNames(const resource::ArchiveIndex& index) const {
				for (const auto& entry : index.Entries) {
					if (entry.Name.empty() || entry.Name.size() > Batch.MaxNameBytes || entry.Name.find('\0') != std::string::npos) {
						throw std::runtime_error("archive entry identity exceeds its actual receiving-owner name domain");
					}
				}

				for (const auto& suffix : index.ExpireSuffixes) {
					if (suffix.size() > Batch.MaxNameBytes || suffix.find('\0') != std::string::npos) {
						throw std::runtime_error("archive expire suffix exceeds its actual receiving-owner name domain");
					}
				}
			}

			void Run() {
				try {
					auto* current = SelectNext();
					while (current) {
						auto source = current->Input.IndexRead;
						source.MaxDecodedBytes = std::min(source.MaxDecodedBytes, RemainingPsbBytes);
						source.MaxMetadataBytes = std::min(source.MaxMetadataBytes, source.MaxDecodedBytes);
						if (!current->Destination.ReadPhysicalSelected(source)) {
							static_cast<void>(current->Destination.PrepareSelectedObject(source));
							current->Destination.PublishSelected();
							Fatal.store(true, std::memory_order_release);
							return;
						}

						auto* next = SelectNext();
						if (!current->Destination.PrepareSelectedObject(source)) {
							current->Destination.PublishSelected();
							Fatal.store(true, std::memory_order_release);
							return;
						}

						const auto& output = *current->Destination.m_Output;
						current->PreparedIndex = PrepareIndexOnlyMount(output, current->Input.BodyBytes);
						if (current->PreparedIndex->Entries.size() > RemainingEntries) {
							throw std::runtime_error("archive index exceeds its enclosing retained entry budget");
						}

						ValidateIndexNames(*current->PreparedIndex);
						RemainingEntries -= current->PreparedIndex->Entries.size();
						for (std::size_t index = 0; index < current->PreparedIndex->Entries.size(); ++index) {
							current->Lookup.emplace(current->PreparedIndex->Entries[index].Name, index);
						}

						current->ProducedBytes = output.Bytes().size();
						RemainingPsbBytes -= current->ProducedBytes;
						current->Destination.PublishSelected();
						current = next;
					}
				} catch (...) {
					Fatal.store(true, std::memory_order_release);
					throw;
				}
			}

			void Update() {
				if (Closed) {
					throw std::runtime_error("one-shot archive reader requires its actual live serial owner");
				}

				Common.Update([this]() {
					if (Fatal.load(std::memory_order_acquire)) {
						return;
					}
					if (!FinishPhase) {
						static_cast<void>(Serial.AppendReadLoop());
						FinishPhase = true;
					} else {
						Status = scene::TaskStatus::Retire;
					}
				});
			}

			bool CanClose() const noexcept {
				return std::this_thread::get_id() == OwnerThread && !Fatal.load(std::memory_order_acquire) && (Closed || Serial.CanClose());
			}

			void Close() {
				if (!CanClose()) {
					throw std::runtime_error("archive reader retirement requires supported actual worker/serial owners");
				}
				if (Closed) {
					return;
				}

				{
					std::lock_guard lock(Mutex);
					Closing = true;
					for (auto* row : Pending) {
						if (row->Destination.State() == WrappedPsbReadState::Queued && !row->Destination.AbandonQueued()) {
							throw std::runtime_error("actual archive pending receiver lost its queued borrow");
						}
					}

					Pending.clear();
				}

				Serial.Close();
				if (Fatal.load(std::memory_order_acquire)) {
					throw std::runtime_error("fatal archive worker retains its actual receiving owners");
				}

				Closed = true;
			}
		};

		class MountRecord {
		public:
			MountRecord(resource::ArchiveMounts& mounts, core::BackgroundCallManager& background, std::unique_ptr<resource::ArchiveMounts::ReadBatch>&& batch) : Mounts(mounts), Background(background), Batch(std::move(batch)) {
			}

			~MountRecord() noexcept {
				try {
					Close();
				} catch (...) {
					if (!Closed) {
						std::terminate();
					}
				}
			}

			scene::CommonProcessTasks Common;
			resource::ArchiveMounts& Mounts;
			core::BackgroundCallManager& Background;
			std::unique_ptr<resource::ArchiveMounts::ReadBatch> Batch;
			std::thread::id OwnerThread = std::this_thread::get_id();
			bool RegisterPhase = false;
			bool Closed = false;
			scene::TaskStatus Status = scene::TaskStatus::Active;

			void Update() {
				Common.Update([this]() {
					if (!RegisterPhase) {
						auto reader = std::make_shared<OneShotIndexReader>(*Batch, Background);
						scene::SerialTaskCallbacks callbacks;
						callbacks.Update = [reader](scene::SerialTask& child) {
							reader->Update();
							child.SetStatus(reader->Status);
						};
						callbacks.Draw = [reader](scene::SerialTask&, std::uint32_t argument) {
							reader->Common.Draw(argument);
						};
						callbacks.CanRelease = [reader]() {
							return reader->CanClose();
						};
						callbacks.Release = [reader]() {
							reader->Close();
						};
						static_cast<void>(Common.Serial().Append(std::make_unique<scene::SerialTask>(std::move(callbacks))));
						// No worker can run before the actual serial child Update below; allocation failures leave cells unassigned.
						reader->MarkQueued();
						RegisterPhase = true;
					} else {
						Mounts.PublishReadBatch(*Batch);
						Status = scene::TaskStatus::Retire;
					}
				});
			}

			bool CanClose() const noexcept {
				return std::this_thread::get_id() == OwnerThread && (Closed || Common.CanClear());
			}

			void Close() {
				if (!CanClose()) {
					throw std::runtime_error("actual mount record cannot release its current nested serial owner");
				}
				if (Closed) {
					return;
				}

				Common.Clear();
				Mounts.RetireReadBatch(*Batch);
				Closed = true;
			}
		};

		Implementation(resource::ResourceCache& cache, resource::ArchiveMounts& mounts, core::BackgroundCallManager& background, RawReadQueueLimits limits) : Mounts(mounts), Background(background), Requests(cache, ReadActualPreparedRegion, limits), Serial(Common, background, [this]() {
			                                                                                                                                                      Requests.Run();
		                                                                                                                                                      }) {
			Mounts.BindReadProcessor();
			Requests.BindWorkerOwner();
		}

		resource::ArchiveMounts& Mounts;
		core::BackgroundCallManager& Background;
		scene::CommonProcessTasks Common;
		PendingRawReadRequests Requests;
		FileReadSerialAttachment Serial;

		void PublishMount(std::unique_ptr<resource::ArchiveMounts::ReadBatch> batch) {
			std::shared_ptr<MountRecord> record;
			try {
				record = std::make_shared<MountRecord>(Mounts, Background, std::move(batch));
			} catch (...) {
				if (batch) {
					Mounts.RestoreUnstartedReadBatch(*batch);
				}

				throw;
			}
			try {
				scene::SerialTaskCallbacks callbacks;
				callbacks.Update = [record](scene::SerialTask& child) {
					record->Update();
					child.SetStatus(record->Status);
				};
				callbacks.Draw = [record](scene::SerialTask&, std::uint32_t argument) {
					record->Common.Draw(argument);
				};
				callbacks.CanRelease = [record]() {
					return record->CanClose();
				};
				callbacks.Release = [record]() {
					record->Close();
				};
				static_cast<void>(Common.Serial().Append(std::make_unique<scene::SerialTask>(std::move(callbacks))));
			} catch (...) {
				record->Close();
				Mounts.RestoreUnstartedReadBatch(*record->Batch);
				throw;
			}
		}
	};

	FileReadProcessor::FileReadProcessor(resource::ResourceCache& cache, resource::ArchiveMounts& mounts, core::BackgroundCallManager& background, RawReadQueueLimits limits) : m_Implementation(std::make_unique<Implementation>(cache, mounts, background, limits)) {
	}

	FileReadProcessor::~FileReadProcessor() noexcept {
		try {
			Close();
		} catch (...) {
			if (!IsClosed()) {
				std::terminate();
			}
		}
	}

	RawReadRequestHandle FileReadProcessor::QueuePreparedRaw(resource::CachedResourceHandle resource, std::string request_filename, PreparedRawFileRead source) {
		return m_Implementation->Requests.QueuePrepared(resource, std::move(request_filename), std::move(source));
	}

	RawReadRequestHandle FileReadProcessor::QueuePreparedResource(resource::CachedResourceHandle resource, std::string request_filename, PreparedResourceFileRead source) {
		if (source.MountedSource && source.MountedSource != &m_Implementation->Mounts) {
			throw std::runtime_error("queued mounted resource requires this actual processor mount owner");
		}

		return m_Implementation->Requests.QueuePreparedResource(resource, std::move(request_filename), std::move(source));
	}

	bool FileReadProcessor::UsesReceivingOwners(const resource::ResourceCache& cache, const resource::ArchiveMounts& mounts) const noexcept {
		return m_Implementation->Requests.UsesCache(cache) && &m_Implementation->Mounts == &mounts;
	}

	void FileReadProcessor::QueueReadMount(resource::ArchiveReadMountInput input) {
		m_Implementation->Requests.RequireOpen();
		m_Implementation->Mounts.QueueReadMount(std::move(input));
	}

	void FileReadProcessor::Update() {
		auto& owner = *m_Implementation;
		owner.Requests.RequireOpen();
		owner.Common.Update([&owner]() {
			if (owner.Mounts.HasInFlightReadMount()) {
				return;
			}

			if (auto batch = owner.Mounts.ClaimReadBatch()) {
				owner.PublishMount(std::move(batch));
			} else if (owner.Requests.PendingCount() != 0) {
				static_cast<void>(owner.Serial.AppendReadLoop());
			}
		});
	}

	void FileReadProcessor::Draw(std::uint32_t argument) {
		m_Implementation->Requests.RequireOpen();
		m_Implementation->Common.Draw(argument);
	}

	const scene::CommonProcessTasks& FileReadProcessor::Tasks() const noexcept {
		return m_Implementation->Common;
	}

	std::size_t FileReadProcessor::PendingRawCount() const {
		return m_Implementation->Requests.PendingCount();
	}

	bool FileReadProcessor::CanClose() const noexcept {
		const auto& owner = *m_Implementation;

		return owner.Requests.CanBeginClose() && (owner.Requests.IsClosed() || owner.Serial.CanClose());
	}

	bool FileReadProcessor::IsClosed() const noexcept {
		return m_Implementation->Requests.IsClosed();
	}

	void FileReadProcessor::Close() {
		if (!CanClose()) {
			throw std::runtime_error("encompassing file reader requires its actual main/common/serial owner preflight");
		}
		if (IsClosed()) {
			return;
		}

		auto& owner = *m_Implementation;
		owner.Requests.BeginClose();
		owner.Serial.Close();
		owner.Requests.CompleteCloseAfterWorkerFence();
		owner.Mounts.UnbindReadProcessor();
	}

} // namespace sgre::io
