#pragma once

#include "archive_psb_fixture.hpp"
#include "prepared_driver_close_binary_fixture.hpp"
#include "resource_meter_fixture.hpp"

#include "sgre/script/native_format.hpp"
#include "sgre/script/prepared_driver_close.hpp"
#include "sgre/script/squirrel_bytecode.hpp"
#include "sgre/script/thread_bindings.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <thread>
#include <utility>

static sgre::script::SquirrelVmLimits CloseFixtureVmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static std::uint32_t CloseFixtureWord(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		result |= static_cast<std::uint32_t>(bytes.at(offset + index)) << (index * 8);
	}

	return result;
}

static void CloseFixturePutWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t word) {
	for (std::size_t index = 0; index < 4; ++index) {
		bytes.at(offset + index) = static_cast<std::uint8_t>(word >> (index * 8));
	}
}

static void CloseFixtureInsert(std::vector<std::uint8_t>& bytes, std::size_t offset, std::span<const std::uint8_t> inserted) {
	bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(offset), inserted.begin(), inserted.end());
	for (std::size_t field = 8; field < 40; field += 4) {
		const auto value = CloseFixtureWord(bytes, field);
		if (value >= offset) {
			CloseFixturePutWord(bytes, field, value + static_cast<std::uint32_t>(inserted.size()));
		}
	}
}

static std::vector<std::uint8_t> CloseFixtureScriptIndex(std::size_t body_size) {
	Require(body_size < 32768, "close fixture body must fit independent signed two-byte atom");
	std::vector<std::uint8_t> bytes(kArchivePsbFixture.begin(), kArchivePsbFixture.end());
	CloseFixtureInsert(bytes, 385, std::array<std::uint8_t, 3>{'.', 'm', 0});
	bytes[374] = 2;
	CloseFixtureInsert(bytes, 377, std::array<std::uint8_t, 1>{8});
	bytes[371] = 6;
	bytes[372] = static_cast<std::uint8_t>(body_size);
	CloseFixtureInsert(bytes, 373, std::array<std::uint8_t, 1>{static_cast<std::uint8_t>(body_size >> 8)});
	bytes[353] = 1;
	bytes[346] = 11;
	CloseFixtureInsert(bytes, 355, std::array<std::uint8_t, 3>{0, 0x15, 1});
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first = (first + bytes[index]) % 65521;
		second = (second + first) % 65521;
	}
	CloseFixturePutWord(bytes, 40, (second << 16) | first);
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	const auto index = sgre::resource::ReadArchiveIndex(document, body_size);
	Require(index.Entries.size() == 1 && index.Entries.front().Name == "item" && index.Entries.front().Size == body_size, "close fixture index must reach the actual metadata reader");

	return bytes;
}

static std::vector<std::uint8_t> CloseFixtureWrappedIndex(std::span<const std::uint8_t> decoded) {
	Require(decoded.size() >= 256 && decoded.size() < 65536, "close fixture index wrapper requires independent two-byte size");
	const auto size = static_cast<std::uint32_t>(decoded.size() - 256);
	std::vector<std::uint8_t> frame{0x28, 0xb5, 0x2f, 0xfd, 0x60, static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8)};
	const auto block = static_cast<std::uint32_t>(decoded.size() << 3) | 1;
	frame.insert(frame.end(), {static_cast<std::uint8_t>(block), static_cast<std::uint8_t>(block >> 8), static_cast<std::uint8_t>(block >> 16)});
	frame.insert(frame.end(), decoded.begin(), decoded.end());
	sgre::resource::ResourceCipher cipher("fixture_info.psb.m", "synthetic-seed", 131);
	cipher.Transform(frame);
	std::vector<std::uint8_t> bytes{'m', 'z', 's', 0};
	AppendWord(bytes, static_cast<std::uint32_t>(decoded.size()));
	bytes.insert(bytes.end(), frame.begin(), frame.end());

	return bytes;
}

class PreparedCloseFixture {
public:
	explicit PreparedCloseFixture(const std::filesystem::path& directory) : Directory(directory), Manager(Mounts, Background), Textures([this](auto, const auto& pixels) {
		                                                                        const bool font = pixels.Width == 1 && pixels.Height == 1 && pixels.Pixels.size() == 4;
		                                                                        const bool motion = pixels.Width == 256 && pixels.Height == 256 && pixels.Pixels.size() == 256 * 256 * 4;
		                                                                        Require(font || motion, "close fixture texture must match its actual font or motion dimensions");
		                                                                        const auto alpha = pixels.Pixels[3];
		                                                                        Require(motion ? alpha == 128 : alpha == 128 || alpha == 64, "decoded fixture A8 alpha differs from its independent backing");
		                                                                        for (std::size_t offset = 0; offset < pixels.Pixels.size(); offset += 4) {
			                                                                        Require(pixels.Pixels[offset] == 0 && pixels.Pixels[offset + 1] == 0 && pixels.Pixels[offset + 2] == 0 && pixels.Pixels[offset + 3] == alpha, "decoded fixture A8 channels differ from independent expected pixels");
		                                                                        }

		                                                                        return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	                                                                        },
	                                                                                                                               [this](auto) {
		                                                                                                                               if (CheckOrder) {
			                                                                                                                               Require(Vm.IsClosed() && !Logs.IsInitialized() && Manager.IsClosed() && Tree.ChildCount() == 0, "actual font must retire after VM/history/Manager/root clear");
			                                                                                                                               Events.emplace_back("font");
		                                                                                                                               }
		                                                                                                                               if (FailFont) {
			                                                                                                                               throw std::runtime_error("font retirement sentinel");
		                                                                                                                               }
	                                                                                                                               }),
	                                                                        Catalog(Textures), Asset(Catalog.Prepare(Document(), Backing())), Meter(Hooks, Textures, Asset, {}), Logs([this](std::string_view text) {
		                                                                        Reports.emplace_back(text);
	                                                                        }),
	                                                                        Vm(CloseFixtureVmLimits()), Objects(Vm), Layers(Vm, Objects, Tree), Scheduler(Vm), Threads(Vm, Scheduler, sgre::script::ScriptThreadFileServices{Manager.Cache(), Controller, Background, Mounts, "synthetic-seed"}, Objects) {
		Logs.Initialize();
		sgre::script::InstallNativePrinting(Vm, Logs.OutputCallback());
		sgre::scene::LayerManagerFrameOperations root;
		root.State = [this](auto layer) -> sgre::scene::LayerFrameProcessState {
			Require(Tree.Contains(layer), "fixture root state requires its actual Layer");

			throw std::runtime_error("close-only fixture has no prepared Layer frame state");
		};
		root.Update = [](auto) {
			throw std::runtime_error("close-only fixture has no prepared Layer update");
		};
		root.Draw = [](auto, auto) {
			throw std::runtime_error("close-only fixture has no renderer draw producer");
		};
		root.PrepareDraw = [] {
			throw std::runtime_error("close-only fixture has no renderer reset producer");
		};
		Close = std::make_unique<sgre::script::PreparedDriverClose>(sgre::script::PreparedDriverCloseOwners{Controller, Tree, Manager, Meter, Scheduler, Vm, Objects, Layers, Logs}, std::move(root));
	}

	~PreparedCloseFixture() noexcept {
		try {
			// Assertion unwind still retires any actual reader before the typed receiving owner drops.
			if (Close->Phase() == sgre::script::PreparedDriverClosePhase::Threads && !Vm.IsClosed()) {
				PumpReaders();
			}
			Close->Close();
			Background.Close();
			Hooks.Close();
		} catch (...) {
			std::terminate();
		}
	}
	PreparedCloseFixture(const PreparedCloseFixture&) = delete;
	PreparedCloseFixture& operator=(const PreparedCloseFixture&) = delete;

	void MountScript() {
		const auto body = WrappedFixture(CompiledFixture(), "script/item.m");
		WriteFixture(Directory / "body.bin", body);
		WriteFixture(Directory / "index.bin", CloseFixtureWrappedIndex(CloseFixtureScriptIndex(body.size())));
		sgre::resource::ArchiveReadMountInput input;
		input.Prefix = "script/";
		input.IndexIdentity = "fixture_info.psb.m";
		input.BodyIdentity = "fixture_body.bin";
		input.BodyFilename = Directory / "body.bin";
		input.BodyBytes = body.size();
		input.IndexRead.Region.Filename = Directory / "index.bin";
		input.IndexRead.Region.MaxBytes = 4096;
		input.IndexRead.OriginalFilename = "fixture_info.psb.m";
		input.IndexRead.Seed = "synthetic-seed";
		input.IndexRead.MaxDecodedBytes = 4096;
		input.IndexRead.MaxMetadataBytes = 4096;
		Manager.Processor().QueueReadMount(std::move(input));
		Manager.Update();
		DrainLane(Background);
		Manager.Update();
		Manager.Update();
		Manager.Update();
		Require(Mounts.MountCount() == 1 && Mounts.RetainedPsbObjectCount() == 1, "close fixture requires genuine mounted receiving ownership");
	}

	void PumpReaders() {
		Controller.Parallel().Update();
		DrainLane(Background);
		Controller.Parallel().Update();
		Controller.Parallel().Update();
	}

	sgre::resource::CachedResourceHandle CacheActualCompiled() {
		WriteFixture(Directory / "compiled.bin", CompiledFixture());
		const auto handle = Manager.Cache().Acquire("script/item");
		sgre::io::PreparedRawFileRead read;
		read.Filename = Directory / "compiled.bin";
		read.MaxBytes = 4096;
		static_cast<void>(Manager.Processor().QueuePreparedRaw(handle, "script/item", std::move(read)));
		Manager.Update();
		DrainLane(Background);
		Manager.Update();
		Manager.Update();
		Require(Manager.Cache().Info(handle).Residency == sgre::resource::ResourceResidency::Active, "actual processor/cache must publish Active before ScriptBuffer hit");

		return handle;
	}

	sgre::script::SquirrelValue Listener() {
		auto listener = Vm.CreateInstance(Objects.ObjectClass());
		Objects.AttachInstance(listener);

		return listener;
	}

	sgre::script::SquirrelValue DormantFunction() {
		// Typed Thread admission requires a real bytecode closure; this fixture never advances it.
		return Vm.CreateClosure(sgre::script::ReadSquirrelBytecode(CompiledFixture()));
	}

	void MonitorVmClose(bool fail) {
		auto userdata = Vm.CreateUserData(std::array<std::uint8_t, 1>{42});
		Vm.SetUserDataReleaseHook(userdata, [this, fail](auto&, const auto&, auto) {
			Require(Close->Phase() == sgre::script::PreparedDriverClosePhase::Vm && Logs.IsInitialized() && !Manager.IsClosed() && !Meter.IsClosed(), "VM release must occur after Thread phase while actual histories/scene owners survive");
			RequireFailure([&]() {
				Vm.RequireCanClose();
			});
			Events.emplace_back("vm");
			if (fail) {
				throw std::runtime_error("VM release sentinel");
			}
		});
		VmMonitor = Vm.Retain(userdata);
	}

	void MonitorActualSceneAndSerial(bool fail_serial) {
		CheckOrder = true;
		sgre::scene::ParallelTaskCallbacks priority;
		priority.Release = [this](auto) {
			Require(Vm.IsClosed() && !Logs.IsInitialized() && Manager.IsClosed() && !Meter.IsClosed(), "real Manager retirement must precede actual priority children and Meter");
			Events.emplace_back("priority");
		};
		static_cast<void>(Close->Scene().Group(sgre::scene::DefaultDriverTaskGroup::Priority).Append(std::move(priority)));
		sgre::scene::SerialTaskCallbacks serial;
		serial.Release = [this, fail_serial]() {
			Require(Vm.IsClosed() && !Logs.IsInitialized() && Manager.IsClosed() && Meter.IsClosed() && Controller.Parallel().Count() == 0, "real serial retirement follows completed Parallel owners");
			Events.emplace_back("serial");
			if (fail_serial) {
				throw std::runtime_error("serial retirement sentinel");
			}
		};
		static_cast<void>(Controller.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	}

	std::filesystem::path Directory;
	std::vector<std::string> Events;
	std::vector<std::string> Reports;
	std::uint32_t Uploads = 0;
	bool CheckOrder = false;
	bool FailFont = false;
	sgre::core::BackgroundCallManager Background;
	sgre::resource::ArchiveMounts Mounts;
	sgre::resource::ResourceManagerProcess Manager;
	sgre::scene::LooperHookTasks Hooks;
	sgre::scene::TextureCache Textures;
	sgre::scene::BitmapFontAssetCatalog Catalog;
	std::shared_ptr<const sgre::scene::LoadedBitmapFontAsset> Asset;
	sgre::scene::ResourceMeterProcess Meter;
	sgre::script::ScriptDriverLogs Logs;
	sgre::scene::CommonProcessTasks Controller;
	sgre::scene::LayerTree Tree;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptScheduler Scheduler;
	sgre::script::ScriptThreadBindings Threads;
	std::unique_ptr<sgre::script::PreparedDriverClose> Close;
	sgre::script::SquirrelHostRoot VmMonitor;
};
