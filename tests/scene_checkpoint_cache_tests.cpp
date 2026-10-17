#include "sgre/save/scene_checkpoint_cache.hpp"

#include "sgre/platform/content_digest.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void Reject(const auto& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("expected cache rejection");
}

int main() {
	try {
		const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
		const auto directory = std::filesystem::current_path() / ("checkpoint-cache-fixture-" + unique);
		Require(!std::filesystem::exists(directory), "fixture directory already exists");
		const auto identity = std::string(64, 'a');
		const auto digest = sgre::platform::ContentSha256;
		Require(digest({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty independent vector failed");
		const std::array<std::uint8_t, 3> abc{'a', 'b', 'c'};
		Require(digest(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc independent vector failed");
		std::string first;
		std::string second;
		{
			sgre::save::SceneCheckpointCache cache(directory, identity, digest);
			Require(!cache.Find("first scene"), "missing cache fabricated a checkpoint");
			first = cache.Submit("first scene", {1, 2, 3});
			second = cache.Submit("first scene", {4, 5, 6});
			const auto other = cache.Submit("different scene", {7, 8, 9});
			cache.Flush();
			const auto selected_first = cache.Find("first scene", first);
			const auto selected_second = cache.Find("first scene", second);
			Require(selected_first && selected_first->Bytes == std::vector<std::uint8_t>{1, 2, 3} && selected_first->Variants == 2, "original route variant was overwritten");
			Require(selected_second && selected_second->Bytes == std::vector<std::uint8_t>{4, 5, 6} && selected_second->Variants == 2, "second route variant was lost");
			Require(cache.Find("different scene")->Digest == other, "cache mixed different scene addresses");
			const auto preferred = cache.FindFirst(std::array<std::string, 4>{"missing scene", "first scene", "different scene", "first scene"});
			Require(preferred && preferred->Candidate == 1 && preferred->Checkpoint.Variants == 2, "cache ignored caller predecessor priority or duplicate candidate order");
			const auto opposite = cache.FindFirst(std::array<std::string, 2>{"different scene", "first scene"});
			Require(opposite && opposite->Candidate == 0 && opposite->Checkpoint.Digest == other, "cache substituted newest unrelated state for the first available predecessor");
			Require(!cache.FindFirst({}) && !cache.FindFirst(std::array<std::string, 1>{"absent"}), "empty or absent predecessor set fabricated a state");
			Require(cache.Error().empty(), "background writer failed");
		}
		{
			sgre::save::SceneCheckpointCache reopened(directory, identity, digest);
			Require(reopened.Find("first scene", first)->Digest == first, "fresh cache owner lost original route variant");
			Require(!reopened.Find("first scene", std::string(64, '0')), "absent selected variant fell back silently");
			for (const auto& entry : std::filesystem::directory_iterator(directory / identity)) {
				if (entry.path().filename().string().ends_with(first + ".sgcp")) {
					std::ofstream output(entry.path(), std::ios::binary | std::ios::trunc);
					output << "corrupt";
				}
			}
			Reject([&] {
				static_cast<void>(reopened.Find("first scene", first));
			});
		}
		{
			sgre::save::SceneCheckpointCache first_writer(directory, identity, digest);
			sgre::save::SceneCheckpointCache second_writer(directory, identity, digest);
			std::string concurrent_digest;
			for (std::size_t index = 0; index < 32; ++index) {
				concurrent_digest = first_writer.Submit("concurrent scene", {10, 11, 12});
				Require(second_writer.Submit("concurrent scene", {10, 11, 12}) == concurrent_digest, "concurrent same-content identity changed");
			}
			first_writer.Flush();
			second_writer.Flush();
			const auto saved = first_writer.Find("concurrent scene", concurrent_digest);
			Require(saved && saved->Variants == 1 && saved->Bytes == std::vector<std::uint8_t>{10, 11, 12}, "concurrent publication exposed partial bytes or duplicate content");
		}
		{
			const auto obstruction = directory / "obstructed";
			std::ofstream output(obstruction);
			output << "fixture blocks directory creation";
			output.close();
			sgre::save::SceneCheckpointCache failing(obstruction, identity, digest);
			static_cast<void>(failing.Submit("scene", {1}));
			Reject([&] {
				failing.Flush();
			});
			Require(!failing.Error().empty(), "background I/O failure disappeared");
		}
		std::filesystem::remove_all(directory);
		std::cout << "cache background writes, retained route variants, cold lookup, identity, corruption and SHA-256 vectors passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
