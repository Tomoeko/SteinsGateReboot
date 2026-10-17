#include "sgre/platform/checkpoint_preparation.hpp"

#include <crt_externs.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::platform {

	class CheckpointSpawnActions {
	public:
		explicit CheckpointSpawnActions(const std::filesystem::path& log) {
			if (posix_spawn_file_actions_init(&Actions) != 0) {
				throw std::runtime_error("cannot initialize checkpoint worker file actions");
			}
			const auto input = posix_spawn_file_actions_addopen(&Actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
			const auto output = posix_spawn_file_actions_addopen(&Actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
			const auto error = posix_spawn_file_actions_adddup2(&Actions, STDOUT_FILENO, STDERR_FILENO);
			if (input != 0 || output != 0 || error != 0) {
				posix_spawn_file_actions_destroy(&Actions);
				throw std::runtime_error("cannot configure checkpoint worker streams");
			}
		}

		~CheckpointSpawnActions() {
			posix_spawn_file_actions_destroy(&Actions);
		}
		CheckpointSpawnActions(const CheckpointSpawnActions&) = delete;
		CheckpointSpawnActions& operator=(const CheckpointSpawnActions&) = delete;
		posix_spawn_file_actions_t Actions{};
	};

	class CheckpointPreparation::Implementation {
	public:
		Implementation(CheckpointPreparationRequest request, std::shared_ptr<save::SceneCheckpointCache> cache) : Request(std::move(request)), Cache(std::move(cache)) {
			if (!Cache || Request.Executable.empty() || Request.Installation.empty() || Request.JobRoot.empty() || Request.Target.empty() || Request.Key.empty()) {
				throw std::runtime_error("checkpoint preparation requires its executable, installation, target and cache");
			}
			std::filesystem::create_directories(Request.JobRoot);
			std::random_device entropy;
			const auto name = std::to_string(entropy()) + "-" + std::to_string(entropy()) + "-" + std::to_string(entropy());
			Directory = Request.JobRoot / name;
			if (!std::filesystem::create_directory(Directory)) {
				throw std::runtime_error("checkpoint preparation job directory already exists");
			}
			Worker = std::thread([this] {
				Run();
			});
		}

		~Implementation() {
			Cancel.store(true);
			Worker.join();
		}

		void Spawn() {
			// Explicit headless child policy prevents a second window, activation or
			// hardware audio owner. Arguments go directly to exec, never through a shell.
			std::vector<std::string> arguments{Request.Executable.string(), Request.Installation.string(), "--headless", "--checkpoint-worker", Request.Target, "--save-root", (Directory / "user-state").string(), "--maximum-milliseconds", "900000", "--maximum-iterations", "100000"};
			std::vector<char*> pointers;
			for (auto& argument : arguments) {
				pointers.push_back(argument.data());
			}
			pointers.push_back(nullptr);
			CheckpointSpawnActions actions(Directory / "worker.log");
			const auto error = posix_spawn(&Child, Request.Executable.c_str(), &actions.Actions, nullptr, pointers.data(), *_NSGetEnviron());
			if (error != 0) {
				Child = 0;
				throw std::runtime_error("cannot launch original checkpoint worker: " + std::to_string(error));
			}
		}

		bool Reaped() {
			if (Child == 0) {
				return true;
			}
			int status = 0;
			const auto result = waitpid(Child, &status, WNOHANG);
			if (result == Child) {
				Child = 0;
				if (WIFEXITED(status)) {
					ExitStatus = "exit " + std::to_string(WEXITSTATUS(status));
				} else if (WIFSIGNALED(status)) {
					ExitStatus = "signal " + std::to_string(WTERMSIG(status));
				} else {
					ExitStatus = "unknown exit status";
				}
				return true;
			}
			if (result < 0 && errno == ECHILD) {
				Child = 0;
				throw std::runtime_error("owned checkpoint worker was already reaped");
			}
			if (result < 0 && errno != EINTR) {
				throw std::runtime_error("cannot reap owned original checkpoint worker");
			}

			return false;
		}

		void StopChild() noexcept {
			if (Child == 0) {
				return;
			}
			static_cast<void>(kill(Child, SIGTERM));
			int status = 0;
			for (std::size_t attempt = 0; attempt < 50; ++attempt) {
				const auto result = waitpid(Child, &status, WNOHANG);
				if (result == Child || (result < 0 && errno == ECHILD)) {
					Child = 0;
					return;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			static_cast<void>(kill(Child, SIGKILL));
			while (waitpid(Child, &status, 0) < 0 && errno == EINTR) {
			}
			Child = 0;
		}

		void ReadProgress() {
			std::ifstream input(Directory / "worker.log", std::ios::binary);
			if (!input) {
				return;
			}
			input.seekg(LogOffset);
			std::array<char, 65536> bytes{};
			input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			const auto count = input.gcount();
			if (count < 0) {
				throw std::runtime_error("cannot read original checkpoint worker progress");
			}
			LogOffset += count;
			Progress.append(bytes.data(), static_cast<std::size_t>(count));
			if (Progress.find("originalStatefulSeek phase=4") != std::string::npos) {
				throw std::runtime_error("jump reconstruction requires an earlier route or phone choice; play that route to record its original state");
			}
			if (Progress.find("checkpoint recording stopped:") != std::string::npos || Progress.find("original startup failed:") != std::string::npos) {
				const auto marker = Progress.find("original startup failed:");
				const auto message = marker == std::string::npos ? std::string("checkpoint recording stopped") : Progress.substr(marker, Progress.find('\n', marker) - marker).substr(0, 1024);
				throw std::runtime_error("reconstruction for " + Request.Target + " failed: " + message);
			}
			if (Progress.size() > 131072) {
				Progress.erase(0, Progress.size() - 65536);
			}
		}

		void Run() noexcept {
			std::optional<save::CachedSceneCheckpoint> prepared;
			try {
				Spawn();
				const auto started = std::chrono::steady_clock::now();
				while (!Cancel.load()) {
					if (auto result = Cache->Find(Request.Key)) {
						prepared = std::move(result);
						break;
					}
					if (Reaped()) {
						// A final publication may race the preceding directory scan. The
						// child has now flushed and retired, so validate the cache once more.
						if (auto result = Cache->Find(Request.Key)) {
							prepared = std::move(result);
							break;
						}
						ReadProgress();
						throw std::runtime_error("checkpoint worker for " + Request.Target + " ended with " + ExitStatus + " before recording the requested state");
					}
					ReadProgress();
					if (std::chrono::steady_clock::now() - started > std::chrono::minutes(15)) {
						throw std::runtime_error("original checkpoint preparation exceeded its bounded execution time");
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			} catch (const std::exception& error) {
				const std::lock_guard lock(Mutex);
				Failure = error.what();
			} catch (...) {
				const std::lock_guard lock(Mutex);
				Failure = "original checkpoint worker failed";
			}
			StopChild();
			if (prepared) {
				const std::lock_guard lock(Mutex);
				Prepared = std::move(prepared);
			}
		}

		CheckpointPreparationRequest Request;
		std::shared_ptr<save::SceneCheckpointCache> Cache;
		std::filesystem::path Directory;
		std::atomic<bool> Cancel{false};
		mutable std::mutex Mutex;
		std::optional<save::CachedSceneCheckpoint> Prepared;
		std::string Failure;
		std::string ExitStatus;
		std::string Progress;
		std::streamoff LogOffset = 0;
		pid_t Child = 0;
		std::thread Worker;
	};

	CheckpointPreparation::CheckpointPreparation(CheckpointPreparationRequest request, std::shared_ptr<save::SceneCheckpointCache> cache) : m_Implementation(std::make_unique<Implementation>(std::move(request), std::move(cache))) {
	}

	CheckpointPreparation::~CheckpointPreparation() = default;

	std::optional<save::CachedSceneCheckpoint> CheckpointPreparation::Result() const {
		const std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->Prepared;
	}

	std::string CheckpointPreparation::Error() const {
		const std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->Failure;
	}

} // namespace sgre::platform
