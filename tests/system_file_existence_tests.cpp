#include "sgre/core/frame_clock.hpp"
#include "sgre/resource/archive_mounts.hpp"
#include "sgre/script/system_bindings.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::script::SquirrelValue;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestActualArchiveLookup() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	sgre::resource::ArchiveMounts mounts;
	system.ConfigureArchiveMounts(mounts);
	sgre::resource::ArchiveIndex index;
	index.ExpireSuffixes = {".psb.m"};
	for (const std::string name : {"Panel", "71", "true"}) {
		sgre::resource::ArchiveEntry entry;
		entry.Name = name;
		entry.Offset = index.Entries.size() * 4;
		entry.Size = 4;
		index.Entries.push_back(std::move(entry));
	}
	index.SerializedExtent = 12;
	mounts.Prepare("not-a-physical-index", "not-a-physical-body", std::move(index));
	mounts.QueueMount("motion/", "not-a-physical-index", "not-a-physical-body");
	const auto query = [&](bool raw, const SquirrelValue& name, bool extra = false) {
		const auto method = vm.GetSlot(system.SystemClass(), std::string(raw ? "isExistentFileRaw" : "isExistentFile"));
		std::vector<SquirrelValue> arguments{system.SystemClass(), name};
		if (extra) {
			arguments.emplace_back(std::string("ignored-file-name"));
		}
		return std::get<bool>(vm.Invoke(method, arguments).Value);
	};
	Require(!query(false, std::string("motion/Panel.psb")), "pending mounts must not report existence before actual publication");
	mounts.Advance();
	Require(!query(false, std::string("motion/Panel.psb")), "dispatch alone must not publish an index");
	mounts.Advance();
	Require(query(false, std::string("motion/Panel.psb")), "wrapped existence must append .m and use the actual mounted index without physical files");
	Require(query(true, std::string("motion/Panel.psb.m")), "raw existence must preserve the complete lookup name");
	Require(!query(false, std::string("motion/Panel.psb.m")) && !query(true, std::string("motion/Panel.psb")), "raw and wrapped callbacks must not invent extension retries");
	Require(query(false, std::string("motion\\Panel.psb")) && query(false, std::string("motion///Panel.psb")), "file existence must preserve native slash normalization");
	Require(!query(false, std::string("motion/panel.psb")) && !query(false, std::string("motion/Missing.psb")), "native mounted lookup must remain case-sensitive and reject absent keys");
	Require(query(false, std::string("motion/Panel.psb\0ignored", 24), true), "C-string truncation must precede suffix append and ignore extra arguments");
	Require(!query(true, std::string()) && !query(false, std::string()), "empty names must remain absent without a physical-file fallback");

	const auto string_class = vm.CreateClass();
	std::size_t calls = 0;
	vm.NewSlot(string_class, std::string("_tostring"), vm.CreateNativeClosure("fixture_filename", [&](auto&, auto) {
		++calls;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("motion/71.psb"));
	}));
	Require(query(false, vm.CreateInstance(string_class)) && calls == 1, "native wrapper must stringify the actual supplied value rather than require a String variant");
	sgre::resource::ArchiveIndex alias;
	alias.ExpireSuffixes = {".psb.m"};
	sgre::resource::ArchiveEntry alias_entry;
	alias_entry.Name = "Self";
	alias_entry.Size = 4;
	alias.Entries.push_back(std::move(alias_entry));
	alias.SerializedExtent = 4;
	mounts.Prepare("self-index", "alias/Self.psb.m", std::move(alias));
	mounts.QueueMount("alias/", "self-index", "alias/Self.psb.m");
	mounts.Advance();
	mounts.Advance();
	Require(mounts.Resolve("alias/Self.psb.m").has_value() && !query(false, std::string("alias/Self.psb")), "native existence compares remapped body identity against the original path, even when an index key exists");
	mounts.Unmount("motion/");
	Require(!query(false, std::string("motion/Panel.psb")), "actual unmount must immediately change file existence");
}

template <typename Operation>
static void RequireDiskError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected native disk file rejection");
}

static bool QueryDisk(sgre::script::SquirrelVm& vm, const SquirrelValue& callback, const SquirrelValue& filename, bool extra = false) {
	std::vector<SquirrelValue> arguments{vm.RootTable(), filename};
	if (extra) {
		arguments.emplace_back(std::string("ignored filename"));
	}
	const auto result = vm.Invoke(callback, arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed && std::holds_alternative<bool>(result.Value), "root disk predicate must return one completed Boolean value");

	return std::get<bool>(result.Value);
}

static void TestDiskCallbackAndConversion() {
	sgre::script::SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	const auto callback = vm.GetSlot(vm.RootTable(), std::string("isExistFile"));
	RequireDiskError([&] {
		static_cast<void>(QueryDisk(vm, callback, std::string("file")));
	},
	    "actual configured disk-root service");
	RequireDiskError([&] {
		system.ConfigureDiskFileExistence({});
	},
	    "one live serialized service");
	std::vector<std::string> names;
	system.ConfigureDiskFileExistence([&](std::string_view filename) {
		names.emplace_back(filename);

		return filename == "file" || filename == "71" || filename == "true";
	});
	Require(QueryDisk(vm, callback, std::string("file\0ignored", 12), true) && names.back() == "file", "physical argument two must use its C prefix and ignore extras");
	Require(QueryDisk(vm, callback, std::int32_t{71}) && names.back() == "71", "native disk adapter must stringify Integer cells");
	Require(QueryDisk(vm, callback, true) && names.back() == "true", "native disk adapter must stringify Boolean cells");
	Require(!QueryDisk(vm, callback, std::string()) && names.back().empty(), "empty filename must reach the actual disk service unchanged");
	const auto class_value = vm.CreateClass();
	std::size_t conversions = 0;
	vm.NewSlot(class_value, std::string("_tostring"), vm.CreateNativeClosure("fixture_disk_name", [&](auto&, auto) {
		++conversions;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("file\0suffix", 11));
	}));
	Require(QueryDisk(vm, callback, vm.CreateInstance(class_value)) && conversions == 1 && names.back() == "file", "actual _tostring must execute once before the C-prefix file query");
	RequireDiskError([&] {
		system.ConfigureDiskFileExistence([](auto) {
			return false;
		});
	},
	    "one live serialized service");
}

class ScopedDiskRoot {
public:
	ScopedDiskRoot() : Root(std::filesystem::temp_directory_path() / ("sgre-disk-existence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
		Require(std::filesystem::create_directory(Root), "disk fixture requires one newly owned temporary root");
	}
	~ScopedDiskRoot() noexcept {
		std::error_code error;
		std::filesystem::remove_all(Root, error);
	}
	ScopedDiskRoot(const ScopedDiskRoot&) = delete;
	ScopedDiskRoot& operator=(const ScopedDiskRoot&) = delete;

	std::filesystem::path Root;
};

static void TestDiskStatusService() {
	ScopedDiskRoot disk;
	Require(std::filesystem::create_directory(disk.Root / "folder"), "disk fixture directory creation failed");
	{
		std::ofstream output(disk.Root / "folder" / "track.webm", std::ios::binary);
		output << 'x';
		Require(static_cast<bool>(output), "disk fixture file creation failed");
	}
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	const auto callback = vm.GetSlot(vm.RootTable(), std::string("isExistFile"));
	const auto disk_root = disk.Root.generic_string() + "/";
	// This fixture supplies the host boundary; it does not emulate GetFileAttributesW or archive remapping.
	system.ConfigureDiskFileExistence([disk_root](std::string_view filename) {
		std::string relative(filename);
		std::replace(relative.begin(), relative.end(), '\\', '/');
		const auto path_utf8 = disk_root + relative;
		const auto physical = std::filesystem::path(std::u8string(path_utf8.begin(), path_utf8.end()));
		std::error_code error;
		const auto status = std::filesystem::status(physical, error);

		return !error && std::filesystem::exists(status) && !std::filesystem::is_directory(status);
	});
	Require(QueryDisk(vm, callback, std::string("folder/track.webm")), "existing physical file must report true");
	Require(QueryDisk(vm, callback, std::string("folder\\track.webm")), "native path separators must address the same physical file");
	Require(QueryDisk(vm, callback, std::string("/folder/track.webm")), "native prefix concatenation must retain disk root for leading separators");
	Require(!QueryDisk(vm, callback, std::string("folder")) && !QueryDisk(vm, callback, std::string()), "native file predicate must reject directories including disk root");
	Require(!QueryDisk(vm, callback, std::string("missing.webm")), "missing physical file must report false");
	Require(!QueryDisk(vm, callback, std::string("folder/track.webm/child")), "actual filesystem status failure must report false");
	Require(QueryDisk(vm, callback, std::string("folder/track.webm\0missing", 25)), "NUL suffix must not alter the physical lookup");
	sgre::resource::ArchiveMounts mounts;
	system.ConfigureArchiveMounts(mounts);
	const auto wrapped = vm.GetSlot(system.SystemClass(), std::string("isExistentFile"));
	const std::array<SquirrelValue, 2> wrapped_arguments{system.SystemClass(), std::string("folder/track.webm")};
	Require(!std::get<bool>(vm.Invoke(wrapped, wrapped_arguments).Value), "physical disk existence must not fabricate a mounted archive identity");
}

static void TestDiskOwnerLifetimeAndFailure() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	SquirrelValue callback;
	std::size_t calls = 0;
	{
		sgre::script::ScriptSystemBindings system(vm, clock);
		callback = vm.GetSlot(vm.RootTable(), std::string("isExistFile"));
		system.ConfigureDiskFileExistence([&](auto) -> bool {
			++calls;
			if (calls == 1) {
				throw std::runtime_error("actual disk service failure");
			}

			return true;
		});
		RequireDiskError([&] {
			static_cast<void>(QueryDisk(vm, callback, std::string("file")));
		},
		    "actual disk service failure");
		Require(QueryDisk(vm, callback, std::string("file")) && calls == 2, "file service exceptions must preserve the actual callback for a later invocation");
	}
	RequireDiskError([&] {
		static_cast<void>(QueryDisk(vm, callback, std::string("file")));
	},
	    "live serialized System binding owner");
	Require(calls == 2, "expired System bindings must not call a dangling disk service");
}

int main() {
	try {
		TestActualArchiveLookup();
		TestDiskCallbackAndConversion();
		TestDiskStatusService();
		TestDiskOwnerLifetimeAndFailure();
		std::cout << "System native wrapped/raw archive existence checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
