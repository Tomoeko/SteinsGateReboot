#include "sgre/save/scene_checkpoint_cache.hpp"

#include "sgre/io/file_reader.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace sgre::save {

	static constexpr std::size_t kMaximumCheckpointBytes = 16 * 1024 * 1024;
	static constexpr std::size_t kMaximumQueuedBytes = 64 * 1024 * 1024;
	static constexpr std::size_t kMaximumCacheEntries = 1000000;

	static void RequireDigest(std::string_view digest) {
		if (digest.size() != 64 || !std::all_of(digest.begin(), digest.end(), [](char value) {
			    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
		    })) {
			throw std::runtime_error("scene checkpoint cache requires a lowercase SHA-256 digest");
		}
	}

	static std::vector<std::uint8_t> TextBytes(std::string_view text) {
		std::vector<std::uint8_t> result;
		result.reserve(text.size());
		for (const auto byte : text) {
			result.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
		}

		return result;
	}

	class SceneCheckpointCache::Implementation {
	public:
		struct Pending {
			std::filesystem::path Path;
			std::vector<std::uint8_t> Bytes;
		};

		Implementation(std::filesystem::path directory, std::string identity, Digest digest) : Hash(std::move(digest)) {
			RequireDigest(identity);
			if (!Hash || directory.empty()) {
				throw std::runtime_error("scene checkpoint cache requires directory and digest service");
			}
			Directory = std::move(directory) / identity;
			std::random_device entropy;
			WriterId = std::to_string(entropy()) + "-" + std::to_string(entropy()) + "-" + std::to_string(entropy());
			Writer = std::thread([this] {
				Run();
			});
		}

		~Implementation() {
			{
				const std::lock_guard lock(Mutex);
				Stopping = true;
			}
			Changed.notify_all();
			Writer.join();
		}

		std::string Address(std::string_view key) const {
			if (key.empty() || key.size() > 16384) {
				throw std::runtime_error("scene checkpoint cache key exceeds its limit");
			}
			const auto result = Hash(TextBytes(key));
			RequireDigest(result);

			return result;
		}

		bool ExistingMatches(const Pending& pending) const {
			if (!std::filesystem::exists(pending.Path)) {
				return false;
			}
			io::FileReader reader(pending.Path);
			if (reader.Size() != pending.Bytes.size() || reader.Read(0, pending.Bytes.size()) != pending.Bytes) {
				throw std::runtime_error("existing content-addressed checkpoint has corrupted bytes");
			}

			return true;
		}

		void Write(const Pending& pending) {
			std::filesystem::create_directories(pending.Path.parent_path());
			if (ExistingMatches(pending)) {
				return;
			}
			const auto temporary = pending.Path.string() + "." + WriterId + "-" + std::to_string(++Sequence) + ".pending";
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output) {
				throw std::runtime_error("cannot create scene checkpoint cache output");
			}
			output.write(reinterpret_cast<const char*>(pending.Bytes.data()), static_cast<std::streamsize>(pending.Bytes.size()));
			output.close();
			if (!output) {
				throw std::runtime_error("cannot finish scene checkpoint cache output");
			}
			try {
				std::filesystem::rename(temporary, pending.Path);
			} catch (...) {
				// A concurrent writer may already have published the identical content.
				if (!ExistingMatches(pending)) {
					throw;
				}
				std::filesystem::remove(temporary);
			}
		}

		void Run() noexcept {
			for (;;) {
				Pending pending;
				{
					std::unique_lock lock(Mutex);
					Changed.wait(lock, [this] {
						return Stopping || !Queue.empty();
					});
					if (Queue.empty()) {
						return;
					}
					pending = std::move(Queue.front());
					Queue.pop_front();
					Writing = true;
				}
				try {
					Write(pending);
				} catch (const std::exception& error) {
					const std::lock_guard lock(Mutex);
					Failure = error.what();
				} catch (...) {
					const std::lock_guard lock(Mutex);
					Failure = "scene checkpoint writer failed";
				}
				{
					const std::lock_guard lock(Mutex);
					QueuedBytes -= pending.Bytes.size();
					Writing = false;
				}
				Changed.notify_all();
			}
		}

		std::filesystem::path Directory;
		std::string WriterId;
		std::uint64_t Sequence = 0;
		Digest Hash;
		mutable std::mutex Mutex;
		std::condition_variable Changed;
		std::deque<Pending> Queue;
		std::size_t QueuedBytes = 0;
		bool Writing = false;
		bool Stopping = false;
		std::string Failure;
		std::thread Writer;
	};

	SceneCheckpointCache::SceneCheckpointCache(std::filesystem::path directory, std::string identity, Digest digest) : m_Implementation(std::make_unique<Implementation>(std::move(directory), std::move(identity), std::move(digest))) {
	}

	SceneCheckpointCache::~SceneCheckpointCache() = default;

	std::optional<CachedSceneCheckpoint> SceneCheckpointCache::Find(std::string_view key, std::string_view selected_digest) const {
		const auto& state = *m_Implementation;
		const auto prefix = state.Address(key) + "-";
		if (!selected_digest.empty()) {
			RequireDigest(selected_digest);
		}
		if (!std::filesystem::exists(state.Directory)) {
			return {};
		}
		std::optional<std::filesystem::directory_entry> selected;
		std::size_t count = 0;
		std::size_t variants = 0;
		for (const auto& entry : std::filesystem::directory_iterator(state.Directory)) {
			if (++count > kMaximumCacheEntries) {
				throw std::runtime_error("scene checkpoint cache entry count exceeds its bound");
			}
			const auto name = entry.path().filename().string();
			if (!name.starts_with(prefix) || !name.ends_with(".sgcp") || name.size() != prefix.size() + 64 + 5 || !entry.is_regular_file()) {
				continue;
			}
			++variants;
			if (!selected_digest.empty() && name.substr(prefix.size(), 64) != selected_digest) {
				continue;
			}
			if (!selected || entry.last_write_time() > selected->last_write_time() || (entry.last_write_time() == selected->last_write_time() && name > selected->path().filename().string())) {
				selected = entry;
			}
		}
		if (!selected) {
			return {};
		}
		io::FileReader reader(selected->path());
		if (reader.Size() == 0 || reader.Size() > kMaximumCheckpointBytes) {
			throw std::runtime_error("scene checkpoint cache file exceeds its byte limit");
		}
		CachedSceneCheckpoint result;
		result.Bytes = reader.Read(0, static_cast<std::size_t>(reader.Size()));
		result.Digest = state.Hash(result.Bytes);
		RequireDigest(result.Digest);
		result.Variants = variants;
		if (selected->path().filename().string() != prefix + result.Digest + ".sgcp") {
			throw std::runtime_error("scene checkpoint cache content digest mismatch");
		}

		return result;
	}

	std::optional<SceneCheckpointSelection> SceneCheckpointCache::FindFirst(std::span<const std::string> keys) const {
		if (keys.size() > 100000) {
			throw std::runtime_error("scene checkpoint candidate count exceeds its bound");
		}
		const auto& state = *m_Implementation;
		std::unordered_map<std::string, std::size_t> addresses;
		for (std::size_t index = 0; index < keys.size(); ++index) {
			addresses.try_emplace(state.Address(keys[index]), index);
		}
		if (keys.empty() || !std::filesystem::exists(state.Directory)) {
			return {};
		}
		std::optional<std::size_t> selected;
		std::size_t count = 0;
		for (const auto& entry : std::filesystem::directory_iterator(state.Directory)) {
			if (++count > kMaximumCacheEntries) {
				throw std::runtime_error("scene checkpoint cache entry count exceeds its bound");
			}
			const auto name = entry.path().filename().string();
			if (name.size() != 134 || name[64] != '-' || !name.ends_with(".sgcp") || !entry.is_regular_file()) {
				continue;
			}
			const auto found = addresses.find(name.substr(0, 64));
			if (found != addresses.end() && (!selected || found->second < *selected)) {
				selected = found->second;
			}
		}
		if (!selected) {
			return {};
		}
		auto checkpoint = Find(keys[*selected]);
		if (!checkpoint) {
			throw std::runtime_error("selected scene checkpoint disappeared during startup lookup");
		}

		return SceneCheckpointSelection{*selected, std::move(*checkpoint)};
	}

	std::string SceneCheckpointCache::Submit(std::string_view key, std::vector<std::uint8_t> bytes) {
		auto& state = *m_Implementation;
		if (bytes.empty() || bytes.size() > kMaximumCheckpointBytes) {
			throw std::runtime_error("scene checkpoint cache submission exceeds its byte limit");
		}
		const auto digest = state.Hash(bytes);
		RequireDigest(digest);
		Implementation::Pending pending;
		pending.Path = state.Directory / (state.Address(key) + "-" + digest + ".sgcp");
		pending.Bytes = std::move(bytes);
		{
			const std::lock_guard lock(state.Mutex);
			if (!state.Failure.empty()) {
				throw std::runtime_error(state.Failure);
			}
			if (state.Stopping || pending.Bytes.size() > kMaximumQueuedBytes - state.QueuedBytes) {
				throw std::runtime_error("scene checkpoint cache writer queue is unavailable or full");
			}
			state.QueuedBytes += pending.Bytes.size();
			state.Queue.push_back(std::move(pending));
		}
		state.Changed.notify_all();

		return digest;
	}

	void SceneCheckpointCache::Flush() {
		auto& state = *m_Implementation;
		std::unique_lock lock(state.Mutex);
		state.Changed.wait(lock, [&state] {
			return state.Queue.empty() && !state.Writing;
		});
		if (!state.Failure.empty()) {
			throw std::runtime_error(state.Failure);
		}
	}

	std::string SceneCheckpointCache::Error() const {
		const std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->Failure;
	}

} // namespace sgre::save
