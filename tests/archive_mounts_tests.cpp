#include "sgre/resource/archive_mounts.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::resource::ArchiveIndex Index(std::string name, std::uint64_t offset) {
	sgre::resource::ArchiveIndex index;
	index.ExpireSuffixes = {".psb.m", ".nut.m"};
	sgre::resource::ArchiveEntry entry;
	entry.Name = std::move(name);
	entry.Offset = offset;
	entry.Size = 4;
	index.Entries.push_back(std::move(entry));
	index.SerializedExtent = offset + 4;

	return index;
}

static void TestLookup() {
	Require(sgre::resource::NormalizeResourcePath("Case\\\\Name///File") == "Case/Name/File", "native slash normalization changed case or retained repeated separators");
	Require(sgre::resource::NormalizeResourcePath("a/../b") == "a/../b", "normalization invented dot-segment resolution");
	sgre::resource::ArchiveMounts mounts;
	mounts.Prepare("first_info.psb.m", "first_body.bin", Index("Test", 16));
	mounts.Prepare("second_info.psb.m", "second_body.bin", Index("Test", 32));
	mounts.QueueMount("script/", "first_info.psb.m", "first_body.bin");
	mounts.QueueMount("script/", "second_info.psb.m", "second_body.bin");
	Require(mounts.PendingMountCount() == 2 && !mounts.Resolve("script/Test.nut.m"), "queued mount became visible before processor dispatch");
	mounts.Advance();
	Require(mounts.MountCount() == 0, "mount was registered before prepared worker completion phase");
	mounts.Advance();
	const auto resolved = mounts.Resolve("script\\Test.nut.m");
	Require(resolved && resolved->Entry.Name == "Test" && resolved->Entry.Offset == 16 && resolved->ResourceFilename == "script/Test.nut.m" && resolved->BodyIdentity == "first_body.bin", "mount order, expired suffix, normalization, or body identity changed");
	Require(!mounts.Resolve("script/test.nut.m") && !mounts.Resolve("script/") && !mounts.Resolve("script/Test.nut.m.nut.m"), "lookup invented case-folding, empty keys, or last-occurrence suffix removal");
	mounts.Unmount("script/");
	Require(mounts.MountCount() == 0 && !mounts.Resolve("script/Test.nut.m"), "unmount retained its registrations");
	mounts.QueueMount("script/", "second_info.psb.m", "second_body.bin");
	mounts.Unmount("script/");
	mounts.Advance();
	mounts.Advance();
	Require(mounts.PendingMountCount() == 0 && mounts.MountCount() == 0, "unmount did not cancel queued registration");
}

int main() {
	try {
		TestLookup();
		std::cout << "archive mount phases, names, suffix matching, source order, and removal passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
