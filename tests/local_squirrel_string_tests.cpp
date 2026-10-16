#include "sgre/resource/archive_reader.hpp"
#include "sgre/script/squirrel_bytecode.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("local_archive_tests GAME_ROOT SEED_FILE");
		}

		std::ifstream seed_input(argv[2]);
		std::string seed;
		std::getline(seed_input, seed);
		if (!seed.empty() && seed.back() == '\r') {
			seed.pop_back();
		}

		const auto data = std::filesystem::path(argv[1]) / "wind3d11data";
		sgre::resource::ArchiveReader scripts(data / "script_info.psb.m", data / "script_body.bin", seed);
		if (!scripts.Contains("main") || scripts.Contains("main.nut")) {
			throw std::runtime_error("original script index lookup identities differ");
		}

		std::size_t count = 0;
		for (const auto& entry : scripts.Index().Entries) {
			const auto bytes = scripts.Read(entry.Name, entry.Name + ".nut.m");
			auto program = sgre::script::ReadSquirrelBytecode(bytes);
			if (program.SerializedSize != bytes.size()) {
				throw std::runtime_error("script entry has unexplained trailing data");
			}

			sgre::script::SquirrelVmLimits limits;
			limits.ReclaimObjects = true;
			sgre::script::SquirrelVm vm(limits);
			const auto closure = vm.CreateClosure(std::move(program));
			if (vm.Type(closure) != sgre::script::SquirrelValueType::Closure) {
				throw std::runtime_error("original closure lost its recovered prototype ownership");
			}

			vm.Close();
			vm.RethrowReleaseFailure();
			++count;
		}

		sgre::resource::ArchiveReader map(data / "map_info.psb.m", data / "map_body.bin", seed);
		if (count != 72 || !map.Index().Entries.empty()) {
			throw std::runtime_error("exact supplied script/map corpus identities differ");
		}

		std::cout << "immutable prototype owners loaded and closed all " << count << " script entries and the empty map index\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
