#include "sgre/resource/archive_reader.hpp"
#include "sgre/resource/resource_cache.hpp"
#include "sgre/script/resource_bindings.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("local_resource_binary_tests GAME_ROOT SEED_FILE");
		}

		std::ifstream seed_input(argv[2]);
		std::string seed;
		std::getline(seed_input, seed);
		if (!seed.empty() && seed.back() == '\r') {
			seed.pop_back();
		}

		const auto data = std::filesystem::path(argv[1]) / "wind3d11data";
		sgre::resource::ArchiveReader scripts(data / "script_info.psb.m", data / "script_body.bin", seed);
		sgre::resource::ArchiveReader config(data / "config_info.psb.m", data / "config_body.bin", seed);
		sgre::resource::ResourceCache cache(8 * 1024 * 1024);
		std::map<std::string, sgre::script::ScriptResourceAsset, std::less<>> prepared;
		const auto prepare = [&](std::string name, sgre::resource::ArchiveReader& source, std::string_view key) {
			auto bytes = source.Read(key, sgre::script::ResourceReadFilename(name, sgre::script::ScriptResourceRequestMode::BinaryWrapped));
			const auto size = static_cast<std::uint32_t>(bytes.size());
			const auto handle = cache.Acquire(name);
			cache.Complete(handle, std::move(bytes), size);
			cache.Advance();
			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = cache.PinBytes(handle);
			asset.RetainBacking = [&cache, handle] {
				return cache.PinBytes(handle);
			};
			cache.Release(handle);
			prepared.emplace(std::move(name), std::move(asset));
		};
		prepare("script/util.nut", scripts, "util");
		prepare("config/license.txt", config, "license.txt.m");
		sgre::script::SquirrelVm vm;
		vm.InstallRecoveredBaseLibrary();
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptPsbBindings psb(vm, objects);
		sgre::script::ScriptBinaryBindings binary(vm, objects);
		sgre::script::ScriptResourceBindings resources(vm, objects, psb, binary, [&](std::string_view name, sgre::script::ScriptResourceRequestMode mode) {
			if (mode != sgre::script::ScriptResourceRequestMode::BinaryWrapped) {
				throw std::runtime_error("local binary fixture was asked for another resource mode");
			}
			const auto found = prepared.find(name);
			if (found == prepared.end()) {
				throw std::runtime_error("local binary fixture was asked for an unprepared identity");
			}
			const auto asset = found->second;

			return [asset]() -> std::optional<sgre::script::ScriptResourceAsset> {
				return asset;
			};
		},
		    [](std::string_view name, std::string_view error) {
			    throw std::runtime_error(std::string(name) + ": " + std::string(error));
		    });
		const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
		const auto instance = vm.Invoke(resources.ResourceClass(), root_receiver).Value;
		const auto names = vm.CreateArray(std::array<sgre::script::SquirrelValue, 2>{std::string("script/util.nut"), std::string("config/license.txt")});
		const std::array<sgre::script::SquirrelValue, 2> load_arguments{instance, names};
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("loadBinary")), load_arguments));
		if (std::get<bool>(vm.ReadSlot(instance, std::string("loading")))) {
			throw std::runtime_error("prepared original binary resources remained pending");
		}
		const std::array<sgre::script::SquirrelValue, 2> license_arguments{instance, std::string("config/license.txt")};
		const auto license = vm.Invoke(vm.GetSlot(instance, std::string("find")), license_arguments).Value;
		const std::array<sgre::script::SquirrelValue, 1> license_receiver{license};
		const auto size = std::get<std::int32_t>(vm.Invoke(vm.GetSlot(license, std::string("size")), license_receiver).Value);
		if (!vm.IsInstanceOf(license, binary.BinaryClass()) || size <= 0 || static_cast<std::size_t>(size) != prepared.at("config/license.txt").Backing->size()) {
			throw std::runtime_error("original raw-text entry did not retain its native binary extent");
		}
		const std::array<sgre::script::SquirrelValue, 2> execute_arguments{instance, std::string("script/util.nut")};
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("exec")), execute_arguments));
		if (vm.Type(vm.GetSlot(vm.RootTable(), std::string("CallbackRegister"))) != sgre::script::SquirrelValueType::Class) {
			throw std::runtime_error("original compiled binary did not install its actual CallbackRegister class");
		}
		std::cout << "original license bytes retained as native Binary; Resource.exec ran the supplied compiled util module on the actual VM root\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
