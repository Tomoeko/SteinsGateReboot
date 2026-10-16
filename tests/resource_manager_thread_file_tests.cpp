#include "archive_psb_fixture.hpp"

#include "sgre/resource/resource_cipher.hpp"
#include "sgre/resource/resource_manager_process.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/resource_file_service.hpp"
#include "sgre/script/thread_bindings.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Bytes = std::vector<std::uint8_t>;
using sgre::core::BackgroundCallManager;
using sgre::resource::ResourceManagerProcess;
using sgre::script::ScriptThreadStatus;
using sgre::script::SquirrelValue;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void AppendWord(Bytes& bytes, std::uint32_t value) {
	for (std::size_t index = 0; index < 4; ++index) {
		bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
	}
}

static void AppendString(Bytes& bytes, std::string_view text) {
	AppendWord(bytes, 0x08000010);
	AppendWord(bytes, static_cast<std::uint32_t>(text.size()));
	bytes.insert(bytes.end(), text.begin(), text.end());
}

static Bytes CompiledFixture() {
	Bytes bytes{0xfa, 0xfa, 0x52, 0x49, 0x51, 0x53, 1, 0, 0, 0};
	const auto part = [&]() {
		AppendWord(bytes, 0x50415254);
	};
	part();
	AppendString(bytes, "independent-manager.nut");
	AppendString(bytes, "main");
	part();
	for (const auto count : std::array<std::uint32_t, 8>{0, 1, 0, 0, 0, 0, 2, 0}) {
		AppendWord(bytes, count);
	}
	part();
	part();
	AppendString(bytes, "this");
	for (std::size_t section = 0; section < 5; ++section) {
		part();
	}
	AppendWord(bytes, 42);
	bytes.insert(bytes.end(), {2, 1, 0, 0});
	AppendWord(bytes, 1);
	bytes.insert(bytes.end(), {19, 1, 0, 0});
	part();
	AppendWord(bytes, 2);
	bytes.insert(bytes.end(), {0, 0});
	AppendWord(bytes, 0x5441494c);

	return bytes;
}

static void PutWord(Bytes& bytes, std::size_t offset, std::uint32_t word) {
	for (std::size_t index = 0; index < 4; ++index) {
		bytes.at(offset + index) = static_cast<std::uint8_t>(word >> (8 * index));
	}
}

static std::uint32_t Word(const Bytes& bytes, std::size_t offset) {
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value |= static_cast<std::uint32_t>(bytes.at(offset + index)) << (8 * index);
	}

	return value;
}

static void InsertIndexBytes(Bytes& bytes, std::size_t at, std::span<const std::uint8_t> inserted) {
	bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), inserted.begin(), inserted.end());
	for (std::size_t field = 8; field < 40; field += 4) {
		const auto offset = Word(bytes, field);
		if (offset >= at) {
			PutWord(bytes, field, offset + static_cast<std::uint32_t>(inserted.size()));
		}
	}
}

static Bytes ScriptIndex(std::size_t body_bytes) {
	Require(body_bytes < 32768, "independent script body exceeds signed two-byte PSB atom");
	Bytes bytes(kArchivePsbFixture.begin(), kArchivePsbFixture.end());
	// Independent one-entry schema with '.m' expiration and a checked signed two-byte body extent.
	InsertIndexBytes(bytes, 385, std::array<std::uint8_t, 3>{'.', 'm', 0});
	bytes[374] = 2;
	InsertIndexBytes(bytes, 377, std::array<std::uint8_t, 1>{8});
	bytes[371] = 6;
	bytes[372] = static_cast<std::uint8_t>(body_bytes);
	InsertIndexBytes(bytes, 373, std::array<std::uint8_t, 1>{static_cast<std::uint8_t>(body_bytes >> 8)});
	bytes[353] = 1;
	bytes[346] = 11;
	InsertIndexBytes(bytes, 355, std::array<std::uint8_t, 3>{0, 0x15, 1});
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first = (first + bytes[index]) % 65521;
		second = (second + first) % 65521;
	}
	PutWord(bytes, 40, (second << 16) | first);
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	const auto index = sgre::resource::ReadArchiveIndex(document, body_bytes);
	Require(index.Entries.size() == 1 && index.Entries[0].Name == "item" && index.Entries[0].Size == body_bytes && index.ExpireSuffixes == std::vector<std::string>{".m"}, "independent compiled-script index differs from its stated schema");

	return bytes;
}

static Bytes Wrap(std::span<const std::uint8_t> decoded, std::string_view filename) {
	Require(decoded.size() < 65536, "independent raw Zstandard fixture exceeds its two-byte size field");
	Bytes frame{0x28, 0xb5, 0x2f, 0xfd};
	if (decoded.size() < 256) {
		frame.insert(frame.end(), {0x20, static_cast<std::uint8_t>(decoded.size())});
	} else {
		const auto count = decoded.size() - 256;
		frame.insert(frame.end(), {0x60, static_cast<std::uint8_t>(count), static_cast<std::uint8_t>(count >> 8)});
	}
	const auto block = static_cast<std::uint32_t>((decoded.size() << 3) | 1);
	frame.insert(frame.end(), {static_cast<std::uint8_t>(block), static_cast<std::uint8_t>(block >> 8), static_cast<std::uint8_t>(block >> 16)});
	frame.insert(frame.end(), decoded.begin(), decoded.end());
	sgre::resource::ResourceCipher cipher(filename, "synthetic-seed", 131);
	cipher.Transform(frame);
	Bytes bytes{'m', 'z', 's', 0};
	AppendWord(bytes, static_cast<std::uint32_t>(decoded.size()));
	bytes.insert(bytes.end(), frame.begin(), frame.end());

	return bytes;
}

static void WriteFile(const std::filesystem::path& file, std::span<const std::uint8_t> bytes) {
	std::ofstream output(file, std::ios::binary);
	for (const auto byte : bytes) {
		output.put(static_cast<char>(byte));
	}
	Require(static_cast<bool>(output), "independent manager/script fixture write failed");
}

static void DrainLane(BackgroundCallManager& background) {
	std::promise<void> barrier;
	auto done = barrier.get_future();
	sgre::core::BackgroundCallTask after(background, sgre::core::BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after.Update();
	background.PostLoop();
	done.get();
	after.Close();
}

static void MountActual(ResourceManagerProcess& owner, sgre::resource::ArchiveMounts& mounts, BackgroundCallManager& background, const std::filesystem::path& directory) {
	const auto compiled = CompiledFixture();
	const auto body = Wrap(compiled, "script/item.m");
	WriteFile(directory / "body.bin", body);
	WriteFile(directory / "index.bin", Wrap(ScriptIndex(body.size()), "fixture_info.psb.m"));
	sgre::resource::ArchiveReadMountInput input;
	input.Prefix = "script/";
	input.IndexIdentity = "fixture_info.psb.m";
	input.BodyIdentity = "fixture_body.bin";
	input.BodyBytes = body.size();
	input.BodyFilename = directory / "body.bin";
	input.IndexRead.Region.Filename = directory / "index.bin";
	input.IndexRead.Region.MaxBytes = 4096;
	input.IndexRead.OriginalFilename = "fixture_info.psb.m";
	input.IndexRead.Seed = "synthetic-seed";
	input.IndexRead.MaxDecodedBytes = 4096;
	input.IndexRead.MaxMetadataBytes = 4096;
	owner.Processor().QueueReadMount(std::move(input));
	owner.Update();
	DrainLane(background);
	owner.Update();
	owner.Update();
	owner.Update();
	Require(mounts.MountCount() == 1 && mounts.RetainedPsbObjectCount() == 1 && !mounts.HasInFlightReadMount(), "real manager/processor receiving phases failed to register independent metadata");
}

class ActualReaderFence {
public:
	explicit ActualReaderFence(sgre::scene::CommonProcessTasks& driver) : m_Driver(driver) {
	}
	~ActualReaderFence() noexcept {
		try {
			m_Driver.ClearParallel();
		} catch (...) {
			std::terminate();
		}
	}
	ActualReaderFence(const ActualReaderFence&) = delete;
	ActualReaderFence& operator=(const ActualReaderFence&) = delete;

private:
	sgre::scene::CommonProcessTasks& m_Driver;
};

static void TestActualManagerToTypedThread(const std::filesystem::path& directory) {
	sgre::resource::ArchiveMounts mounts;
	BackgroundCallManager background;
	ResourceManagerProcess owner(mounts, background);
	MountActual(owner, mounts, background, directory);
	{
		sgre::script::ScriptResourceFileService resources(owner.Cache(), mounts, owner.Processor(), "synthetic-seed");
		auto poll = resources.RequestService()("script/item", sgre::script::ScriptResourceRequestMode::BinaryWrapped);
		const auto resource = *owner.Cache().FindNamedResource("script/item");
		owner.Update();
		DrainLane(background);
		Require(owner.Cache().Info(resource).Residency == sgre::resource::ResourceResidency::Loading, "actual worker must not synthesize Active residency");
		owner.Update();
		auto asset = poll();
		Require(asset && asset->Backing && *asset->Backing == CompiledFixture(), "manager's actual mounted binary must reach typed receiving publication");
		{
			sgre::script::SquirrelVmLimits limits;
			limits.ReclaimObjects = true;
			sgre::script::SquirrelVm vm(limits);
			sgre::script::ScriptObjectBindings objects(vm);
			sgre::script::ScriptScheduler scheduler(vm);
			sgre::scene::CommonProcessTasks driver;
			sgre::script::ScriptThreadFileServices services{owner.Cache(), driver, background, mounts, "synthetic-seed"};
			sgre::script::ScriptThreadBindings bindings(vm, scheduler, services, objects);
			ActualReaderFence fence(driver);
			const auto filename = vm.CreateString("script/item");
			const auto before = vm.RetainedReferenceCount(filename);
			const auto instance = vm.Retain(bindings.Spawn(filename));
			const auto handle = bindings.ThreadHandle(instance.Value());
			Require(driver.Parallel().Count() == 0 && owner.Cache().Info(resource).References == 2 && scheduler.Info(handle).HasScriptBuffer, "actual Active-only ScriptBuffer hit must add exactly one independent pin and no reader");
			scheduler.SetExecutionStopped(handle, true);
			static_cast<void>(scheduler.Advance(1));
			const auto loaded = scheduler.Info(handle);
			Require(loaded.Context && vm.ContextState(*loaded.Context) == sgre::script::SquirrelContextState::Idle && loaded.Status == ScriptThreadStatus::Running && !loaded.HasScriptBuffer && owner.Cache().Info(resource).References == 1, "real typed child/closure load must drop +70 pin before Running");
			Require(vm.RetainedReferenceCount(filename) == before + 1, "actual typed filename must remain owned beyond buffer handoff");
			asset.reset();
			poll = {};
			Require(resources.CanClose() && !owner.Cache().Contains(resource), "actual zero-limit manager must evict after target ends without retiring loaded prototype");
			static_cast<void>(vm.CollectGarbage());
			scheduler.SetExecutionStopped(handle, false);
			const auto result = scheduler.Advance(1);
			Require(result.Completed.size() == 1 && std::get<std::int32_t>(scheduler.Info(handle).Result) == 42, "real compiled closure must execute after manager buffer eviction");
			bindings.ReleaseInstance(instance.Value());
			Require(vm.RetainedReferenceCount(filename) == before, "actual derived Thread retirement must drop the singular filename field");

			const auto miss = vm.Retain(bindings.Spawn(filename));
			const auto miss_handle = bindings.ThreadHandle(miss.Value());
			Require(driver.Parallel().Count() == 1 && owner.Tasks().Parallel().Count() == 1 && owner.Processor().UsesReceivingOwners(owner.Cache(), mounts), "cache miss must own a separate driver Parallel reader without replacing the persistent manager processor");
			static_cast<void>(scheduler.Advance(1));
			Require(!scheduler.Info(miss_handle).Context && scheduler.Info(miss_handle).HasScriptBuffer && scheduler.Info(miss_handle).Status == ScriptThreadStatus::LoadingFile, "pending miss must preserve separate ScriptBuffer ownership and no child context");
			driver.Parallel().Update();
			DrainLane(background);
			driver.Parallel().Update();
			driver.Parallel().Update();
			Require(driver.Parallel().Count() == 0 && owner.Tasks().Parallel().Count() == 1 && !owner.Cache().FindNamedResource("script/item"), "temporary miss retirement must not publish a ResourceCache entry or retire the actual manager reader");
			scheduler.SetExecutionStopped(miss_handle, true);
			static_cast<void>(scheduler.Advance(1));
			Require(scheduler.Info(miss_handle).Context && !scheduler.Info(miss_handle).HasScriptBuffer && scheduler.Info(miss_handle).Status == ScriptThreadStatus::Running, "miss child load must consume the separately published readiness gate and release +70");
			scheduler.SetExecutionStopped(miss_handle, false);
			static_cast<void>(scheduler.Advance(1));
			Require(std::get<std::int32_t>(scheduler.Info(miss_handle).Result) == 42, "typed miss must execute actual compiled bytes after temporary reader retirement");
			bindings.ReleaseInstance(miss.Value());
			scheduler.ShutdownNativeThreads();
		}
		// Actual VM/bindings ended before the owning resource process. No application shutdown policy is substituted.
		resources.Close();
	}
	owner.Close();
	background.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("manager/typed Thread join requires one ignored fixture directory");
		}
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestActualManagerToTypedThread(directory);
		std::filesystem::remove(directory / "index.bin");
		std::filesystem::remove(directory / "body.bin");
		std::cout << "two real manager/typed Thread Active-pin and independent-reader groups pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
