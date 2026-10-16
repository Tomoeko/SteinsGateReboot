#include "sgre/resource/archive_reader.hpp"
#include "sgre/resource/psb_jobs.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("local_psb_jobs_tests GAME_ROOT SEED_FILE");
		}

		std::ifstream seed_input(argv[2]);
		std::string seed;
		std::getline(seed_input, seed);
		if (!seed.empty() && seed.back() == '\r') {
			seed.pop_back();
		}

		const auto data = std::filesystem::path(argv[1]) / "wind3d11data";
		sgre::resource::ArchiveReader config(data / "config_info.psb.m", data / "config_body.bin", seed);
		sgre::resource::ResourceCache cache(8 * 1024 * 1024);
		sgre::resource::PsbResourceJobs jobs(cache);
		std::size_t count = 0;
		for (const auto& entry : config.Index().Entries) {
			if (entry.Name == "license.txt.m") {
				continue;
			}

			const auto name = "config/" + entry.Name + ".psb";
			const auto handle = cache.Acquire(name);
			jobs.Queue(handle, [&config, key = entry.Name, filename = name + ".m"] {
				return config.Read(key, filename);
			});
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!jobs.PublishCompleted()) {
				if (std::chrono::steady_clock::now() >= deadline) {
					throw std::runtime_error("local PSB job exceeded its bounded publication deadline: " + name);
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			const auto info = cache.Info(handle);
			if (info.Completion != sgre::resource::ResourceCompletion::Succeeded) {
				throw std::runtime_error("local PSB job failed: " + name + ": " + info.Error);
			}

			const auto document = jobs.Document(handle);
			const auto backing = cache.PinBytes(handle);
			if (!document || document->Header.DataOffset > backing->size() || info.ChargedBytes != backing->size()) {
				throw std::runtime_error("local typed PSB ownership/charge differs from retained backing: " + name);
			}

			cache.Release(handle);
			++count;
		}
		if (count != 20 || jobs.OutstandingJobs() != 0 || cache.State().ActiveEntryNum != 0) {
			throw std::runtime_error("original config job count or final active ownership differs");
		}
		cache.ClearCache();
		jobs.CollectEvictedDocuments();
		std::cout << "all 20 original PSB config entries decoded on the worker and published with complete native charge and retained backing\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
