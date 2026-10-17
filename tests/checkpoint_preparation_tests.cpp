#include "sgre/platform/checkpoint_preparation.hpp"

#include "sgre/platform/content_digest.hpp"

#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static int FixtureChild(int argc, char** argv) {
	Require(argc >= 5 && std::string_view(argv[2]) == "--headless", "fixture child lost explicit hidden policy");
	std::cout << "fixtureChild=" << getpid() << '\n'
	          << std::flush;
	const std::string_view target(argv[4]);
	if (target == "exit.ks:1") {
		return 7;
	}
	if (target == "choice.ks:1") {
		std::cout << "originalStatefulSeek phase=4\n"
		          << std::flush;
	}
	if (target == "ready.ks:1" || target == "readyexit.ks:1") {
		sgre::save::SceneCheckpointCache cache(argv[1], std::string(64, 'a'), sgre::platform::ContentSha256);
		static_cast<void>(cache.Submit("fixture-key", {1, 2, 3}));
		cache.Flush();
		if (target == "readyexit.ks:1") {
			return 0;
		}
	}
	std::this_thread::sleep_for(std::chrono::seconds(20));

	return 0;
}

static std::int32_t ReadChild(const std::filesystem::path& jobs) {
	for (const auto& entry : std::filesystem::directory_iterator(jobs)) {
		std::ifstream log(entry.path() / "worker.log");
		std::string line;
		while (std::getline(log, line)) {
			if (line.starts_with("fixtureChild=")) {
				return std::stoi(line.substr(13));
			}
		}
	}

	throw std::runtime_error("fixture child start was not retained");
}

static void TestPreparation(const std::filesystem::path& executable, const std::filesystem::path& root, std::string target) {
	const auto directory = root / target.substr(0, target.find('.'));
	std::filesystem::create_directory(directory);
	auto cache = std::make_shared<sgre::save::SceneCheckpointCache>(directory, std::string(64, 'a'), sgre::platform::ContentSha256);
	sgre::platform::CheckpointPreparationRequest request;
	request.Executable = executable;
	request.Installation = directory;
	request.JobRoot = directory / "jobs";
	request.Key = "fixture-key";
	request.Target = target;
	const auto started = std::chrono::steady_clock::now();
	{
		sgre::platform::CheckpointPreparation preparation(request, cache);
		while (!preparation.Result() && preparation.Error().empty() && std::chrono::steady_clock::now() - started < std::chrono::seconds(4)) {
			if (target == "cancel.ks:1") {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (target == "ready.ks:1" || target == "readyexit.ks:1") {
			const auto result = preparation.Result();
			Require(result && result->Bytes == std::vector<std::uint8_t>{1, 2, 3} && preparation.Error().empty(), "background preparation failed to publish the complete checkpoint");
			Require(kill(ReadChild(request.JobRoot), 0) != 0, "checkpoint was published while its worker remained alive");
		} else if (target != "cancel.ks:1") {
			Require(!preparation.Result() && !preparation.Error().empty(), "background failure disappeared or fabricated a checkpoint");
		}
	}
	Require(kill(ReadChild(request.JobRoot), 0) != 0, "cancelled/failed checkpoint worker remained alive");
	Require(std::chrono::steady_clock::now() - started < std::chrono::seconds(5), "owned worker retirement exceeded its bound");
}

int main(int argc, char** argv) {
	try {
		if (argc > 3 && std::string_view(argv[3]) == "--checkpoint-worker") {
			return FixtureChild(argc, argv);
		}
		const auto root = std::filesystem::current_path() / ("checkpoint-preparation-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		Require(std::filesystem::create_directory(root), "fixture namespace already exists");
		for (const auto* target : {"ready.ks:1", "readyexit.ks:1", "exit.ks:1", "choice.ks:1", "cancel.ks:1"}) {
			TestPreparation(std::filesystem::canonical(argv[0]), root, target);
		}
		std::filesystem::remove_all(root);
		std::cout << "checkpoint background success, original-choice rejection, early exit and owned cancellation/reaping passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
