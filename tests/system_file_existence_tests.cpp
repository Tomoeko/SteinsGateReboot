#include "sgre/core/frame_clock.hpp"
#include "sgre/resource/archive_mounts.hpp"
#include "sgre/script/system_bindings.hpp"

#include <array>
#include <cstdint>
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

int main() {
	try {
		TestActualArchiveLookup();
		std::cout << "System native wrapped/raw archive existence checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
