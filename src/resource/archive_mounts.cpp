#include "sgre/resource/archive_mounts.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::resource {
	std::string NormalizeResourcePath(std::string_view path) {
		if (path.size() > 65536 || path.find('\0') != std::string_view::npos) {
			throw std::runtime_error("resource path contains NUL or exceeds its normalization bound");
		}

		std::string result;
		result.reserve(path.size());
		for (const auto character : path) {
			const auto normalized = character == '\\' ? '/' : character;
			if (normalized != '/' || result.empty() || result.back() != '/') {
				result.push_back(normalized);
			}
		}

		return result;
	}

	struct PreparedArchiveMount {
		std::string IndexIdentity;
		std::string BodyIdentity;
		ArchiveIndex Index;
		std::filesystem::path BodyFilename;
		std::uint64_t BodyBytes = 0;
		std::map<std::string, std::size_t, std::less<>> Entries;
	};

	struct PendingArchiveMount {
		std::string Prefix;
		std::size_t PreparedIndex = 0;
		bool Dispatched = false;
	};

	class ArchiveMounts::Implementation {
	public:
		explicit Implementation(ArchiveMountLimits limits) : Limits(limits) {
			if (limits.MaxPreparedArchives == 0 || limits.MaxPreparedArchives > 1024 || limits.MaxMounts == 0 || limits.MaxMounts > 4096 || limits.MaxPendingMounts == 0 || limits.MaxPendingMounts > 4096 || limits.MaxNameBytes == 0 || limits.MaxNameBytes > 65536 || limits.MaxTotalEntries == 0 || limits.MaxTotalEntries > 1024 * 1024 || limits.MaxRetainedPsbBytes == 0 || limits.MaxRetainedPsbBytes > 256 * 1024 * 1024) {
				throw std::runtime_error("archive mounts require bounded positive registration limits");
			}

			Prepared.reserve(limits.MaxPreparedArchives);
			Pending.reserve(limits.MaxPendingMounts);
			ReadPending.reserve(limits.MaxPendingMounts);
			RetainedObjects.reserve(limits.MaxPreparedArchives);
		}

		ArchiveMountLimits Limits;
		mutable std::mutex Mutex;
		std::thread::id OwnerThread = std::this_thread::get_id();
		std::size_t ReadProcessors = 0;
		std::size_t InFlightRows = 0;
		std::size_t RetainedPsbBytes = 0;
		bool ActualReaderMode = false;
		bool HostPrepared = false;
		std::vector<std::unique_ptr<ReadMountRow>> ReadPending;
		std::vector<std::unique_ptr<io::PublishedPsbFile>> RetainedObjects;

		void RequireReceivingThread() const {
			if (std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("archive receiving mutations require their actual owner thread");
			}
		}
		std::size_t TotalEntries = 0;
		std::size_t Count = 0;
		std::vector<PreparedArchiveMount> Prepared;
		std::vector<PendingArchiveMount> Pending;
		std::map<std::string, std::vector<std::size_t>, std::less<>> Mounted;

		void ValidateName(std::string_view name, bool allow_empty = false) const {
			if ((!allow_empty && name.empty()) || name.size() > Limits.MaxNameBytes || name.find('\0') != std::string_view::npos) {
				throw std::runtime_error("archive mount identity is empty, contains NUL, or exceeds its limit");
			}
		}
	};

	ArchiveMounts::ArchiveMounts(ArchiveMountLimits limits) : m_Implementation(std::make_unique<Implementation>(limits)) {
	}

	ArchiveMounts::~ArchiveMounts() {
		if (m_Implementation->ReadProcessors != 0 || m_Implementation->InFlightRows != 0) {
			std::terminate();
		}
	}

	void ArchiveMounts::Prepare(std::string index_identity, std::string body_identity, ArchiveIndex index) {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (state.ActualReaderMode) {
			throw std::runtime_error("prepared host mount phases cannot replace the actual file-reader receiving owner");
		}

		state.ValidateName(index_identity);
		state.ValidateName(body_identity);
		if (state.Prepared.size() >= state.Limits.MaxPreparedArchives || index.Entries.size() > state.Limits.MaxTotalEntries - state.TotalEntries) {
			throw std::runtime_error("prepared archive count or total entries exceed their allocation limits");
		}

		const auto normalized_index = NormalizeResourcePath(index_identity);
		const auto normalized_body = NormalizeResourcePath(body_identity);
		if (std::any_of(state.Prepared.begin(), state.Prepared.end(), [&](const auto& existing) {
			    return existing.IndexIdentity == normalized_index && existing.BodyIdentity == normalized_body;
		    })) {
			throw std::runtime_error("archive source identity was already prepared");
		}

		PreparedArchiveMount prepared;
		prepared.IndexIdentity = normalized_index;
		prepared.BodyIdentity = normalized_body;
		prepared.Index = std::move(index);
		for (std::size_t entry = 0; entry < prepared.Index.Entries.size(); ++entry) {
			state.ValidateName(prepared.Index.Entries[entry].Name);
			if (!prepared.Entries.emplace(prepared.Index.Entries[entry].Name, entry).second) {
				throw std::runtime_error("prepared archive contains duplicate entry identities");
			}
		}

		for (const auto& suffix : prepared.Index.ExpireSuffixes) {
			state.ValidateName(suffix, true);
		}

		state.TotalEntries += prepared.Index.Entries.size();
		state.Prepared.push_back(std::move(prepared));
		state.HostPrepared = true;
	}

	void ArchiveMounts::QueueMount(std::string prefix, std::string_view index_identity, std::string_view body_identity, bool alternate_read_mode) {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (state.ActualReaderMode) {
			throw std::runtime_error("prepared host mount phases cannot replace the actual file-reader receiving owner");
		}

		state.ValidateName(prefix, true);
		state.ValidateName(index_identity);
		state.ValidateName(body_identity);
		if (alternate_read_mode) {
			throw std::runtime_error("alternate native archive read mode is not recovered");
		}

		if (state.Pending.size() >= state.Limits.MaxPendingMounts || state.Pending.size() >= state.Limits.MaxMounts - state.Count) {
			throw std::runtime_error("archive mount queue or total mount count exceeds its allocation limit");
		}

		const auto normalized_index = NormalizeResourcePath(index_identity);
		const auto normalized_body = NormalizeResourcePath(body_identity);
		const auto found = std::find_if(state.Prepared.begin(), state.Prepared.end(), [&](const auto& prepared) {
			return prepared.IndexIdentity == normalized_index && prepared.BodyIdentity == normalized_body;
		});
		if (found == state.Prepared.end()) {
			throw std::runtime_error("archive source has not finished host preparation: " + normalized_index);
		}

		PendingArchiveMount pending;
		// Native registration stores the supplied prefix unchanged; normalization belongs to requests.
		pending.Prefix = std::move(prefix);
		pending.PreparedIndex = static_cast<std::size_t>(found - state.Prepared.begin());
		const auto [registration, inserted] = state.Mounted.try_emplace(pending.Prefix);
		if (inserted) {
			registration->second.reserve(state.Limits.MaxMounts);
		}
		state.Pending.push_back(std::move(pending));
	}

	void ArchiveMounts::Advance() {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (state.ActualReaderMode) {
			throw std::runtime_error("prepared host mount phases cannot replace the actual file-reader receiving owner");
		}

		for (auto pending = state.Pending.begin(); pending != state.Pending.end();) {
			if (!pending->Dispatched) {
				pending->Dispatched = true;
				++pending;

				continue;
			}

			// Native 4d5760 preserves registration order within a byte-sorted prefix map.
			state.Mounted.at(pending->Prefix).push_back(pending->PreparedIndex);
			++state.Count;
			pending = state.Pending.erase(pending);
		}
	}

	void ArchiveMounts::Unmount(std::string_view prefix) {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		state.ValidateName(prefix, true);
		const auto found = state.Mounted.find(prefix);
		if (found != state.Mounted.end()) {
			state.Count -= found->second.size();
			if (state.InFlightRows == 0) {
				state.Mounted.erase(found);
			} else {
				found->second.clear();
			}
		}

		std::erase_if(state.Pending, [&](const auto& pending) {
			return pending.Prefix == prefix;
		});
		std::erase_if(state.ReadPending, [&](const auto& pending) {
			return pending->Input.Prefix == prefix;
		});
	}

	std::optional<ResolvedArchiveEntry> ArchiveMounts::Resolve(std::string_view resource_filename) const {
		const auto& state = *m_Implementation;
		std::lock_guard lock(state.Mutex);
		state.ValidateName(resource_filename);
		const auto normalized = NormalizeResourcePath(resource_filename);
		for (const auto& [prefix, archives] : state.Mounted) {
			if (normalized.size() <= prefix.size() || !normalized.starts_with(prefix)) {
				continue;
			}

			for (const auto index : archives) {
				const auto& prepared = state.Prepared[index];
				auto key = std::string_view(normalized).substr(prefix.size());
				for (const auto& suffix : prepared.Index.ExpireSuffixes) {
					const auto first = key.find(suffix);
					if (first != std::string_view::npos && first + suffix.size() == key.size()) {
						key = key.substr(0, first);

						break;
					}
				}

				const auto entry = prepared.Entries.find(key);
				if (entry != prepared.Entries.end()) {
					ResolvedArchiveEntry result;
					result.IndexIdentity = prepared.IndexIdentity;
					result.BodyIdentity = prepared.BodyIdentity;
					result.ResourceFilename = normalized;
					result.Entry = prepared.Index.Entries[entry->second];
					result.BodyFilename = prepared.BodyFilename;
					result.BodyBytes = prepared.BodyBytes;

					return result;
				}
			}
		}

		return std::nullopt;
	}

	std::size_t ArchiveMounts::PendingMountCount() const {
		std::lock_guard lock(m_Implementation->Mutex);
		return m_Implementation->Pending.size();
	}

	std::size_t ArchiveMounts::MountCount() const {
		std::lock_guard lock(m_Implementation->Mutex);
		return m_Implementation->Count;
	}
	void ArchiveMounts::BindReadProcessor() {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (state.HostPrepared || !state.Pending.empty()) {
			throw std::runtime_error("actual archive reader cannot attach to a surrogate prepared host model");
		}

		state.ActualReaderMode = true;
		++state.ReadProcessors;
	}

	void ArchiveMounts::UnbindReadProcessor() {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (state.ReadProcessors == 0) {
			throw std::runtime_error("archive reader detachment has no actual enclosing owner");
		}

		--state.ReadProcessors;
	}

	void ArchiveMounts::QueueReadMount(ArchiveReadMountInput input) {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		state.ValidateName(input.Prefix, true);
		state.ValidateName(input.IndexIdentity);
		state.ValidateName(input.BodyIdentity);
		if (!state.ActualReaderMode || state.ReadProcessors == 0 || state.ReadPending.size() == state.Limits.MaxPendingMounts || state.RetainedObjects.size() + state.ReadPending.size() + state.InFlightRows >= state.Limits.MaxPreparedArchives || state.Count + state.ReadPending.size() + state.InFlightRows >= state.Limits.MaxMounts || input.IndexRead.Region.Filename.empty()) {
			throw std::runtime_error("actual archive read admission requires its enclosing processor and bounded prepared source");
		}

		auto row = std::make_unique<ReadMountRow>();
		row->Input = std::move(input);
		const auto [reservation, inserted] = state.Mounted.try_emplace(row->Input.Prefix);
		if (inserted) {
			reservation->second.reserve(state.Limits.MaxMounts);
		}

		state.ReadPending.push_back(std::move(row));
	}

	std::unique_ptr<ArchiveMounts::ReadBatch> ArchiveMounts::ClaimReadBatch() {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (state.InFlightRows != 0 || state.ReadPending.empty()) {
			return {};
		}

		auto batch = std::make_unique<ReadBatch>();
		batch->Rows.reserve(state.Limits.MaxPendingMounts);
		batch->Rows.swap(state.ReadPending);
		state.InFlightRows = batch->Rows.size();
		batch->AvailablePsbBytes = state.Limits.MaxRetainedPsbBytes - state.RetainedPsbBytes;
		batch->AvailableEntries = state.Limits.MaxTotalEntries - state.TotalEntries;
		batch->MaxNameBytes = state.Limits.MaxNameBytes;

		return batch;
	}

	void ArchiveMounts::RestoreUnstartedReadBatch(ReadBatch& batch) {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (batch.Published || (state.InFlightRows != 0 && state.InFlightRows != batch.Rows.size()) || !state.ReadPending.empty()) {
			throw std::runtime_error("unstarted archive admission cannot restore a changed actual queue");
		}

		for (const auto& row : batch.Rows) {
			if (row->Destination.State() != io::WrappedPsbReadState::Unassigned) {
				throw std::runtime_error("archive receiver was already published to its nested reader");
			}
		}

		batch.Rows.swap(state.ReadPending);
		state.InFlightRows = 0;
	}

	void ArchiveMounts::PublishReadBatch(ReadBatch& batch) {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (batch.Published || state.InFlightRows != batch.Rows.size() || batch.Rows.empty()) {
			throw std::runtime_error("archive publication requires its actual one-shot in-flight batch");
		}

		std::size_t entries = 0;
		std::size_t bytes = 0;
		for (const auto& row : batch.Rows) {
			if (row->Destination.State() != io::WrappedPsbReadState::Succeeded || !row->PreparedIndex) {
				throw std::runtime_error("archive batch still lacks an actual parsed receiving object");
			}

			if (row->PreparedIndex->Entries.size() > state.Limits.MaxTotalEntries - state.TotalEntries - entries) {
				throw std::runtime_error("archive batch exceeds its retained entry capacity");
			}

			entries += row->PreparedIndex->Entries.size();
			// Decoder admission bounds each output; output transfer below retains it in this actual owner.
			if (row->ProducedBytes > state.Limits.MaxRetainedPsbBytes - state.RetainedPsbBytes - bytes) {
				throw std::runtime_error("archive batch exceeds its actual retained output budget");
			}

			bytes += row->ProducedBytes;
		}

		for (auto& row : batch.Rows) {
			auto object = row->Destination.TakePublishedObject();
			state.RetainedPsbBytes += object->Bytes().size();
			state.RetainedObjects.push_back(std::move(object));
			PreparedArchiveMount prepared;
			prepared.IndexIdentity = std::move(row->Input.IndexIdentity);
			prepared.BodyIdentity = std::move(row->Input.BodyIdentity);
			prepared.BodyFilename = std::move(row->Input.BodyFilename);
			prepared.BodyBytes = row->Input.BodyBytes;
			prepared.Index = std::move(*row->PreparedIndex);
			prepared.Entries = std::move(row->Lookup);
			state.Mounted.at(row->Input.Prefix).push_back(state.Prepared.size());
			state.TotalEntries += prepared.Index.Entries.size();
			state.Prepared.push_back(std::move(prepared));
			++state.Count;
		}

		batch.Published = true;
	}

	void ArchiveMounts::RetireReadBatch(ReadBatch& batch) {
		auto& state = *m_Implementation;
		state.RequireReceivingThread();
		std::lock_guard lock(state.Mutex);
		if (state.InFlightRows != batch.Rows.size()) {
			throw std::runtime_error("archive batch retirement does not match its actual in-flight owner");
		}

		for (const auto& row : batch.Rows) {
			if (!row->Destination.CanRetire()) {
				throw std::runtime_error("archive receiving row still belongs to its actual reader worker");
			}
		}

		if (!batch.Published) {
			for (auto& row : batch.Rows) {
				if (row->Destination.State() == io::WrappedPsbReadState::Succeeded) {
					auto object = row->Destination.TakePublishedObject();
					state.RetainedPsbBytes += object->Bytes().size();
					state.RetainedObjects.push_back(std::move(object));
				}
			}
		}

		state.InFlightRows = 0;
	}

	std::size_t ArchiveMounts::QueuedReadMountCount() const {
		std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->ReadPending.size();
	}

	bool ArchiveMounts::HasInFlightReadMount() const {
		std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->InFlightRows != 0;
	}

	std::size_t ArchiveMounts::RetainedPsbObjectCount() const {
		std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->RetainedObjects.size();
	}

	const io::PublishedPsbFile& ArchiveMounts::RetainedPsbObject(std::size_t index) const {
		std::lock_guard lock(m_Implementation->Mutex);

		return *m_Implementation->RetainedObjects.at(index);
	}

} // namespace sgre::resource
