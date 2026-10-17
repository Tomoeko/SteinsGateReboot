#include "sgre/platform/scene_jump_preparation.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::platform {

	class SceneJumpPreparation::Implementation {
	public:
		Implementation(scenario::ScenarioSearchLocation location, CheckpointPreparationRequest request, std::shared_ptr<save::SceneCheckpointCache> cache) : Location(std::move(location)), Request(std::move(request)), Cache(std::move(cache)) {
			if (!Cache || Request.Target.empty()) {
				throw std::runtime_error("scene jump preparation requires a target and its installation cache");
			}
			Worker = std::thread([this] {
				Run();
			});
		}

		~Implementation() {
			Cancelled.store(true);
			Changed.notify_one();
			Worker.join();
		}

		PreparedSceneJump Prepare() {
			const auto separator = Request.Target.find_last_of(':');
			if (separator == std::string::npos) {
				throw std::runtime_error("scene jump requires scene.ks:line");
			}
			std::int32_t line = 0;
			const auto* first = Request.Target.data() + separator + 1;
			const auto* last = Request.Target.data() + Request.Target.size();
			const auto parsed = std::from_chars(first, last, line);
			if (parsed.ec != std::errc{} || parsed.ptr != last || line < 0) {
				throw std::runtime_error("scene jump line must be a nonnegative integer");
			}
			scenario::ScenarioSearchCatalog catalog(Location);
			const auto point = catalog.ValidateJump(std::string_view(Request.Target).substr(0, separator), line);
			if (point.SnapshotIndex > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
				throw std::runtime_error("scene jump snapshot exceeds the original signed record cursor");
			}
			PreparedSceneJump result;
			result.Location = {point.CanonicalStorage, point.SceneLabel, point.SnapshotLine, point.Point, static_cast<std::int32_t>(point.SnapshotIndex)};
			Request.Key = script::OriginalCheckpointKey(result.Location);
			if (auto cached = Cache->Find(Request.Key)) {
				result.Checkpoint = std::move(*cached);
				return result;
			}
			if (Cancelled.load()) {
				return result;
			}
			Request.Target = point.CanonicalStorage + ":" + std::to_string(point.SourceLine);
			CheckpointPreparation preparation(Request, Cache);
			while (!Cancelled.load()) {
				if (auto checkpoint = preparation.Result()) {
					result.Checkpoint = std::move(*checkpoint);
					return result;
				}
				const auto error = preparation.Error();
				if (!error.empty()) {
					throw std::runtime_error(error);
				}
				std::unique_lock lock(Mutex);
				Changed.wait_for(lock, std::chrono::milliseconds(25), [this] {
					return Cancelled.load();
				});
			}

			return result;
		}

		void Run() noexcept {
			try {
				auto result = Prepare();
				const std::lock_guard lock(Mutex);
				if (!Cancelled.load()) {
					Result = std::move(result);
				}
			} catch (const std::exception& error) {
				const std::lock_guard lock(Mutex);
				try {
					Failure = error.what();
				} catch (...) {
					FailedWithoutMessage = true;
				}
			} catch (...) {
				const std::lock_guard lock(Mutex);
				FailedWithoutMessage = true;
			}
		}

		scenario::ScenarioSearchLocation Location;
		CheckpointPreparationRequest Request;
		std::shared_ptr<save::SceneCheckpointCache> Cache;
		std::atomic_bool Cancelled{false};
		mutable std::mutex Mutex;
		std::condition_variable Changed;
		std::optional<PreparedSceneJump> Result;
		std::string Failure;
		bool FailedWithoutMessage = false;
		std::thread Worker;
	};

	SceneJumpPreparation::SceneJumpPreparation(scenario::ScenarioSearchLocation location, CheckpointPreparationRequest request, std::shared_ptr<save::SceneCheckpointCache> cache) : m_Implementation(std::make_unique<Implementation>(std::move(location), std::move(request), std::move(cache))) {
	}

	SceneJumpPreparation::~SceneJumpPreparation() = default;

	std::optional<PreparedSceneJump> SceneJumpPreparation::TakeResult() {
		const std::lock_guard lock(m_Implementation->Mutex);
		auto result = std::move(m_Implementation->Result);
		m_Implementation->Result.reset();

		return result;
	}

	std::string SceneJumpPreparation::Error() const {
		const std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->FailedWithoutMessage ? "scene jump preparation failed" : m_Implementation->Failure;
	}

} // namespace sgre::platform
