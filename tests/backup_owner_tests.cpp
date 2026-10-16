#include "backup_file_fixture.hpp"
#include "sgre/core/md5.hpp"
#if defined(__APPLE__)
#include "sgre/platform/backup_crypto.hpp"
#endif
#include "sgre/save/backup_manager.hpp"
#include "sgre/script/backup_bindings.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireError(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual Backup owner rejection");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static std::shared_ptr<const sgre::resource::PsbDocument> Schema() {
	auto schema = std::make_shared<sgre::resource::PsbDocument>();
	const auto field = Atom(sgre::resource::PsbArray{Atom(std::int64_t{4})});
	schema->Root = Atom(sgre::resource::PsbObject{{"root", Atom(sgre::resource::PsbObject{{"value", field}})}});

	return schema;
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::string_view method, std::span<const sgre::script::SquirrelValue> inputs = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), inputs.begin(), inputs.end());

	return vm.Invoke(vm.GetSlot(receiver, std::string(method)), arguments).Value;
}

static sgre::scene::LayerCameraFrameState CameraState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {128, 72};
	state.RawScissor = {0, 0, 128, 72};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

static sgre::script::SquirrelVmLimits VmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

class BackupVmFixture {
public:
	BackupVmFixture()
	    : Vm(VmLimits()), Objects(Vm), Tree(16), Layers(Vm, Objects, Tree), Psb(Vm, Objects), Binary(Vm, Objects), Structs(Vm, Objects, Psb, Binary), Backups(Vm, Objects, Layers, Structs, Background, [this] {
		      Require(std::this_thread::get_id() != OwnerThread, "directory initialization must run in the actual background lane");
		      ++DirectoryAttempts;
		      if (ThrowDirectory.load()) {
			      throw std::runtime_error("actual directory callback failure");
		      }

		      return DirectorySucceeds.load();
	      }),
	      Renderer(CameraState(), Models, [this](const auto&) {
		      ++ScissorSubmissions;
	      }) {
	}

	~BackupVmFixture() {
		try {
			Close();
		} catch (const std::exception& error) {
			std::cerr << "Backup fixture cleanup: " << error.what() << '\n';
		}
	}

	void Close() {
		Vm.Close();
		Vm.RethrowReleaseFailure();
		Background.Close();
	}

	void Update(sgre::scene::LayerHandle handle) {
		Layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
			frame.Update();
		});
	}

	void Drain(const sgre::script::SquirrelValue& manager) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (std::get<bool>(Call(Vm, manager, "getRunning"))) {
			Require(std::chrono::steady_clock::now() < deadline, "actual Backup task did not retire before the bounded fixture deadline");
			Update(Layers.Handle(manager));
			Background.PostLoop();
			std::this_thread::yield();
		}
	}

	std::thread::id OwnerThread = std::this_thread::get_id();
	std::atomic<std::size_t> DirectoryAttempts = 0;
	std::atomic<bool> DirectorySucceeds = true;
	std::atomic<bool> ThrowDirectory = false;
	std::size_t ScissorSubmissions = 0;
	sgre::core::BackgroundCallManager Background;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptPsbBindings Psb;
	sgre::script::ScriptBinaryBindings Binary;
	sgre::script::ScriptStructBindings Structs;
	sgre::script::ScriptBackupBindings Backups;
	std::array<sgre::scene::MotionScissorMatrix, 1> Models;
	sgre::scene::LayerCameraDrawScope Renderer;
};

static void TestActualVmBaseAndWorker() {
	BackupVmFixture fixture;
	auto& vm = fixture.Vm;
	const auto manager = vm.CreateInstance(fixture.Backups.ManagerClass());
	static_cast<void>(Call(vm, manager, "constructor"));
	const auto manager_pin = vm.Retain(manager);
	fixture.Layers.AttachBaseFrame(manager, fixture.Renderer);
	const auto handle = fixture.Layers.Handle(manager);
	Require(fixture.Tree.State(handle).Kind == sgre::scene::LayerKind::Base && fixture.Layers.BaseTasks(manager).Children().Count() == 1, "Backup must append its actual work set to its genuine SQLayerBase camera group");
	Require(!std::get<bool>(Call(vm, manager, "getRunning")) && !std::get<bool>(Call(vm, manager, "getSuccess")), "new native Backup manager flags must remain zero");

	auto source = fixture.Psb.WrapDocument(Schema());
	auto document = vm.CreateInstance(fixture.Structs.StructClass());
	static_cast<void>(Call(vm, document, "constructor", std::array<sgre::script::SquirrelValue, 1>{source}));
	const auto document_identity = vm.BorrowObjectIdentity(document);
	auto& storage = fixture.Structs.BorrowStructStorage(document);
	const auto segment = Call(vm, manager, "addSegment", std::array<sgre::script::SquirrelValue, 2>{document, std::int32_t{96}});
	const auto segment_pin = vm.Retain(segment);
	Require(storage.RootSize() == 4 && !storage.HasBacking() && vm.RetainedReferenceCount(document) >= 1, "native segment must borrow and pin the exact still-lazy Struct document");
	Require(std::get<std::int32_t>(Call(vm, segment, "getMaxFileNum")) == 96 && std::get<bool>(Call(vm, segment, "getRecordFileId")) && !std::get<bool>(Call(vm, segment, "getSuccess")), "actual native segment defaults differ");
	vm.AssignSlot(segment, std::string("dialogName"), std::string("SYSTEMDATA"));
	vm.AssignSlot(segment, std::string("fileId"), std::int32_t{-7});
	static_cast<void>(Call(vm, segment, "setNoConfirm", std::array<sgre::script::SquirrelValue, 1>{0.5f}));
	Require(std::get<std::int32_t>(Call(vm, segment, "getFileIdLastOperate")) == -7 && std::get<bool>(Call(vm, segment, "getNoConfirm")), "native metadata setters must preserve signed bits and VM truth rather than float truncation");
	static_cast<void>(Call(vm, segment, "setNoCheckOwner", std::array<sgre::script::SquirrelValue, 1>{true}));
	Require(!std::get<bool>(Call(vm, segment, "getNoCheckOwner")), "actual Windows no-op setter must not invent state");
	const auto second_wrapper = Call(vm, manager, "getSegment", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{0}});
	const auto second_pin = vm.Retain(second_wrapper);
	Require(sgre::script::StringText(Call(vm, segment, "getDialogName")) == "SYSTEMDATA" && sgre::script::StringText(Call(vm, second_wrapper, "getDialogName")).empty(), "each native segment wrapper owns a separate DialogName while borrowing the same manager ordinal");
	RequireError([&] {
		static_cast<void>(Call(vm, segment, "singleload"));
	},
	    "BackupSegment.singleload");
	RequireError([&] {
		static_cast<void>(Call(vm, segment, "setIcon", std::array<sgre::script::SquirrelValue, 1>{sgre::script::SquirrelValue{}}));
	},
	    "BackupSegment.setIcon");

	document = {};
	source = {};
	static_cast<void>(vm.CollectGarbage());
	Require(vm.ResolveObjectIdentity(document_identity).has_value(), "actual segment must keep its original Struct receiver alive");
	static_cast<void>(Call(vm, manager, "init"));
	Require(std::get<bool>(Call(vm, manager, "getRunning")) && fixture.DirectoryAttempts.load() == 0, "Init owns a real pending work task before common Update enqueues it");
	fixture.Drain(manager);
	Require(fixture.DirectoryAttempts.load() == 1 && std::get<bool>(Call(vm, manager, "getSuccess")) && !std::get<bool>(Call(vm, segment, "getSuccess")) && !storage.HasBacking(), "ordinary native Init must not fabricate segment IO success or materialize save bytes");
	fixture.DirectorySucceeds.store(false);
	static_cast<void>(Call(vm, manager, "init"));
	fixture.Drain(manager);
	Require(fixture.DirectoryAttempts.load() == 2 && !std::get<bool>(Call(vm, manager, "getSuccess")), "native word initialization clears manager success before a failed directory retry");
	fixture.Layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
		frame.Draw(0xabcdef01);
	});
	Require(fixture.Renderer.Depth() == 1, "actual Base frame must restore its containing camera after the real work graph draws");
	fixture.Layers.ReleaseInstance(manager);
	Require(!fixture.Tree.Contains(handle) && !fixture.Objects.HasInstance(manager), "derived Backup cleanup must close actual jobs/segments before Base/Object retirement");
	RequireError([&] {
		static_cast<void>(Call(vm, segment, "getMaxFileNum"));
	},
	    "outlived its actual native manager");
	fixture.Close();
}

static void TestActualAdventureProfile() {
	BackupVmFixture fixture;
	auto& vm = fixture.Vm;
	const auto registered_class = vm.GetSlot(vm.RootTable(), std::string("AdvBackupManager"));
	const auto manager = vm.CreateInstance(registered_class);
	static_cast<void>(Call(vm, manager, "constructor"));
	const auto manager_pin = vm.Retain(manager);
	fixture.Layers.AttachBaseFrame(manager, fixture.Renderer);
	Require(vm.IsInstanceOf(manager, fixture.Layers.LayerBaseClass()) && !vm.IsInstanceOf(manager, fixture.Backups.ManagerClass()), "genuine AdvBackupManager derives SQLayerBase independently of ordinary BackupManager");
	Require(std::get<bool>(Call(vm, manager, "getSuccess")) && !std::get<bool>(Call(vm, manager, "getInitSuccess")) && !std::get<bool>(Call(vm, manager, "getEnableMetaData")), "native Adventure empty-set success is distinct from initialization and metadata flags");
	const auto source = fixture.Psb.WrapDocument(Schema());
	const auto document = vm.CreateInstance(fixture.Structs.StructClass());
	static_cast<void>(Call(vm, document, "constructor", std::array<sgre::script::SquirrelValue, 1>{source}));
	const auto segment = Call(vm, manager, "addSegment", std::array<sgre::script::SquirrelValue, 2>{document, std::int32_t{1}});
	const auto segment_pin = vm.Retain(segment);
	Require(vm.IsInstanceOf(segment, fixture.Backups.AdventureSegmentClass()) && !vm.IsInstanceOf(segment, fixture.Backups.SegmentClass()) && std::get<bool>(Call(vm, segment, "getSuccess")), "native Adventure segment class/default success differ from the ordinary profile");
	Require(sgre::script::StringText(vm.ReadSlot(manager, std::string("dialogName"))).empty(), "native Adventure manager dialog title starts independently empty");
	vm.AssignSlot(segment, std::string("dialogName"), std::string("authored fixture save dialog"));
	vm.AssignSlot(manager, std::string("dialogName"), vm.ReadSlot(segment, std::string("dialogName")));
	Require(sgre::script::StringText(vm.ReadSlot(manager, std::string("dialogName"))) == "authored fixture save dialog", "original doSaveSegment must copy the segment title into the actual Adventure manager through Object properties");
	vm.AssignSlot(segment, std::string("dialogName"), std::string("changed segment title"));
	Require(sgre::script::StringText(Call(vm, manager, "getDialogName")) == "authored fixture save dialog", "native manager owns its copied dialog string independently of a segment wrapper");
	Require(std::get<std::int32_t>(Call(vm, segment, "getSegmentId")) == 0 && !std::get<bool>(Call(vm, segment, "getCrypted")), "native Adventure ordinal and crypted-result byte start from the real constructor state");
	static_cast<void>(Call(vm, manager, "setEnableMetaData", std::array<sgre::script::SquirrelValue, 1>{0.5f}));
	Require(std::get<bool>(Call(vm, manager, "getEnableMetaData")), "Adventure metadata setter must mutate its real owner with native VM truth");
	static_cast<void>(Call(vm, manager, "setSecureFileId", std::array<sgre::script::SquirrelValue, 1>{std::string("authored-independent-fixture-key")}));
	Require(sgre::script::StringText(Call(vm, manager, "getSecureFileId")) == "authored-independent-fixture-key", "Adventure secure key metadata must belong to the same actual native manager");
	static_cast<void>(Call(vm, manager, "init"));
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, manager, "getInitSuccess")) && std::get<bool>(Call(vm, manager, "getSuccess")) && !fixture.Structs.BorrowStructStorage(document).HasBacking(), "real Adventure worker initializes without inventing batch read/write results or materializing save bytes");
	RequireError([&] {
		static_cast<void>(Call(vm, manager, "autoload"));
	},
	    "initialized directory/codec");
	RequireError([&] {
		const auto ordinary = vm.CreateInstance(fixture.Backups.ManagerClass());
		static_cast<void>(vm.Invoke(vm.GetSlot(registered_class, std::string("constructor")), std::array<sgre::script::SquirrelValue, 1>{ordinary}));
	},
	    "constructor");
	fixture.Close();
}

static void TestRealCountByteAndOwnBridgeRetirement() {
	sgre::core::BackgroundCallManager background(512);
	sgre::scene::ParallelTaskSet camera(4);
	sgre::resource::StructStorage storage(Schema()->Root);
	std::size_t executions = 0;
	bool release_unrelated = false;
	sgre::scene::ParallelTaskCallbacks unrelated;
	unrelated.CanRelease = [&](auto) {
		return release_unrelated;
	};
	const auto unrelated_handle = camera.Append(std::move(unrelated));
	{
		sgre::save::BackupManager manager(
		    background, camera, [&] {
			    ++executions;

			    return true;
		    },
		    1, 256);
		static_cast<void>(manager.AddSegment(storage, 1));
		for (std::size_t index = 0; index < 256; ++index) {
			manager.Initialize();
		}
		Require(!manager.Running() && executions == 0 && camera.Count() == 2, "native count256 low-byte result must be preserved without executing or substituting a flag");
		Require(manager.CanClose(), "unrelated sibling release veto must not block extracting the actual owned Backup bridge");
		manager.Close();
		Require(camera.Count() == 1 && camera.Contains(unrelated_handle) && executions == 0, "real pending task retirement must preserve unrelated camera membership");
	}
	release_unrelated = true;
	auto retained = camera.Extract(unrelated_handle);
	retained.Release();
	background.Close();
}

static void TestWorkerExceptionRetirement() {
	BackupVmFixture fixture;
	auto& vm = fixture.Vm;
	const auto manager = vm.CreateInstance(fixture.Backups.ManagerClass());
	static_cast<void>(Call(vm, manager, "constructor"));
	const auto pin = vm.Retain(manager);
	fixture.Layers.AttachBaseFrame(manager, fixture.Renderer);
	fixture.ThrowDirectory.store(true);
	static_cast<void>(Call(vm, manager, "init"));
	RequireError([&] {
		fixture.Drain(manager);
	},
	    "actual directory callback failure");
	Require(!std::get<bool>(Call(vm, manager, "getRunning")) && !std::get<bool>(Call(vm, manager, "getSuccess")), "actual failed worker must retire before reporting its failure and cannot publish success");
	fixture.Layers.ReleaseInstance(manager);
	fixture.Close();
}

static void TestPortableBackupFiles() {
	BackupFileFixture files;
	sgre::save::BackupFileReadRequest request;
	request.RootBytes = 4;
	const auto missing = sgre::save::ReadAdventureBackupFiles(files.Directory(), request, {});
	Require(missing.NoFile && !missing.Success && !missing.Broken && missing.Bytes.empty(), "portable actual missing file must report NoFile without a fabricated crypto service");
	const std::vector<std::uint8_t> clear{9, 8, 7, 6};
	files.Write("data_000_0000.bin", clear);
	const auto direct = sgre::save::ReadAdventureBackupFiles(files.Directory(), request, {});
	Require(direct.Success && direct.Bytes == clear && !direct.Crypted, "portable actual raw file must preserve exact Struct bytes without platform crypto");
	files.Write("data_000_0000.bin", std::array<std::uint8_t, 3>{1, 2, 3});
	const auto truncated = sgre::save::ReadAdventureBackupFiles(files.Directory(), request, {});
	Require(truncated.Broken && !truncated.Success && truncated.Bytes.empty(), "portable no-metadata input must enforce actual exact root extent");
	files.Write("data_000_0000.bin", clear);
	files.Write("meta_000_0000.bin", BackupMetadataFixture(clear, 4, 0));
	request.EnableMetaData = true;
	const auto metadata = sgre::save::ReadAdventureBackupFiles(files.Directory(), request, {});
	Require(metadata.Success && metadata.Bytes == clear && !metadata.Crypted, "portable PSB metadata must verify actual binary Digest and accept actual raw input");
	files.Write("data_000_0000.bin", std::array<std::uint8_t, 4>{1, 2, 3, 4});
	const auto bad_digest = sgre::save::ReadAdventureBackupFiles(files.Directory(), request, {});
	Require(bad_digest.Broken && !bad_digest.Success && bad_digest.Bytes.empty(), "portable actual MD5 mismatch cannot publish input bytes");
	files.Write("data_000_0000.bin", clear);
	files.Write("meta_000_0000.bin", BackupMetadataFixture(clear, 4, 0, 0x10100));
	const auto bad_version = sgre::save::ReadAdventureBackupFiles(files.Directory(), request, {});
	Require(bad_version.Broken && !bad_version.Success && bad_version.Bytes.empty(), "portable metadata FileVersion gate must retain native0x10200");
}

#if defined(__APPLE__)
static void TestActualAdventureFileLoad() {
	BackupFileFixture files;
	BackupVmFixture fixture;
	auto& vm = fixture.Vm;
	sgre::save::BackupFileServices services;
	services.Directory = files.Directory();
	services.Decrypt = sgre::platform::DecryptBackupBytes;
	fixture.Backups.ConfigureFiles(services);
	const auto manager = vm.CreateInstance(fixture.Backups.AdventureManagerClass());
	static_cast<void>(Call(vm, manager, "constructor"));
	const auto manager_pin = vm.Retain(manager);
	fixture.Layers.AttachBaseFrame(manager, fixture.Renderer);
	RequireError([&] {
		fixture.Backups.ConfigureFiles(services);
	},
	    "before native manager");
	const auto source = fixture.Psb.WrapDocument(Schema());
	const auto document = vm.CreateInstance(fixture.Structs.StructClass());
	static_cast<void>(Call(vm, document, "constructor", std::array<sgre::script::SquirrelValue, 1>{source}));
	const auto document_pin = vm.Retain(document);
	auto& storage = fixture.Structs.BorrowStructStorage(document);
	const auto segment = Call(vm, manager, "addSegment", std::array<sgre::script::SquirrelValue, 2>{document, std::int32_t{1}});
	const auto segment_pin = vm.Retain(segment);
	static_cast<void>(Call(vm, manager, "setEnableMetaData", std::array<sgre::script::SquirrelValue, 1>{true}));
	static_cast<void>(Call(vm, manager, "setSecureFileId", std::array<sgre::script::SquirrelValue, 1>{std::string("short")}));
	static_cast<void>(Call(vm, manager, "init"));
	fixture.Drain(manager);

	// A genuinely absent pair must finish its real worker with NoFile, leaving lazy Struct bytes untouched.
	static_cast<void>(Call(vm, manager, "autoload"));
	RequireError([&] {
		static_cast<void>(Call(vm, segment, "setFileId", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{9}}));
	},
	    "pending worker");
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, manager, "getNofile")) && !std::get<bool>(Call(vm, manager, "getBroken")) && !std::get<bool>(Call(vm, manager, "getSuccess")) && !storage.HasBacking(), "missing actual backup files cannot publish save bytes or success");

	// This independent AES vector encrypts16 authored bytes3+17*i, not a game save or an encrypt/decrypt round trip.
	const std::vector<std::uint8_t> cipher{0x45, 0xd0, 0xb9, 0x59, 0x03, 0x3a, 0xd3, 0x51, 0xae, 0xb6, 0x3d, 0x1a, 0xd2, 0xa7, 0xb7, 0xe0, 0x2a, 0x42, 0x26, 0xee, 0xba, 0xeb, 0xfa, 0x7c, 0x52, 0xe3, 0xf5, 0x4f, 0x50, 0x59, 0x57, 0xbd};
	files.Write("data_000_0000.bin", cipher);
	files.Write("meta_000_0000.bin", BackupMetadataFixture(cipher, 4, 0xca2b7fb4));
	static_cast<void>(Call(vm, manager, "autoload"));
	Require(!storage.HasBacking(), "a newly staged actual read must not publish Struct output before any worker Update");
	fixture.Drain(manager);
	Require(storage.Serialize() == std::vector<std::uint8_t>({3, 20, 37, 54}) && std::get<bool>(Call(vm, manager, "getSuccess")) && std::get<bool>(Call(vm, segment, "getCrypted")) && !std::get<bool>(Call(vm, manager, "getNofile")), "actual VM/metadata/MD5/AES worker must publish the exact recovered Struct prefix and reset prior file results");

	const auto expected_storage = storage.Serialize();
	auto damaged = cipher;
	damaged[0] ^= 1;
	files.Write("data_000_0000.bin", damaged);
	static_cast<void>(Call(vm, manager, "autoload"));
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, manager, "getBroken")) && !std::get<bool>(Call(vm, manager, "getSuccess")) && storage.Serialize() == expected_storage, "digest failure must retain the actual previous Struct bytes");
	files.Write("meta_000_0000.bin", BackupMetadataFixture(damaged, 4, 0xca2b7fb4));
	static_cast<void>(Call(vm, manager, "setSecureFileId", std::array<sgre::script::SquirrelValue, 1>{std::string("different-key")}));
	static_cast<void>(Call(vm, manager, "autoload"));
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, manager, "getBroken")) && storage.Serialize() == expected_storage, "accepted MD5 cannot bypass actual AES Final padding validation");

	// Actual FileData selects a different real file identity while retaining the same registered Struct owner.
	static_cast<void>(Call(vm, manager, "setSecureFileId", std::array<sgre::script::SquirrelValue, 1>{std::string{}}));
	const std::vector<std::uint8_t> clear{9, 8, 7, 6};
	files.Write("data_000_0003.bin", clear);
	files.Write("meta_000_0003.bin", BackupMetadataFixture(clear, 4, 0));
	const auto request = vm.CreateTable();
	vm.NewSlot(request, std::string("segmentId"), std::int32_t{0});
	vm.NewSlot(request, std::string("fileId"), std::int32_t{3});
	vm.NewSlot(request, std::string("struct"), document);
	const auto requests = vm.CreateArray(std::array<sgre::script::SquirrelValue, 1>{request});
	static_cast<void>(Call(vm, manager, "autoload", std::array<sgre::script::SquirrelValue, 1>{requests}));
	fixture.Drain(manager);
	Require(storage.Serialize() == clear && std::get<std::int32_t>(Call(vm, segment, "getFileId")) == 3 && !std::get<bool>(Call(vm, segment, "getCrypted")) && std::get<bool>(Call(vm, segment, "getSuccess")), "actual FileData array must update its registered file identity and exact same Struct storage");

	files.Write("meta_000_0003.bin", BackupMetadataFixture(clear, 4, 0x100));
	static_cast<void>(Call(vm, manager, "autoload"));
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, manager, "getBroken")) && std::get<bool>(Call(vm, segment, "getCrypted")) && storage.Serialize() == clear, "native whole-word CryptMagic setne result must survive invalid magic with a zero low byte");
	files.Write("meta_000_0003.bin", BackupMetadataFixture(clear, 5, 0));
	static_cast<void>(Call(vm, manager, "autoload"));
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, manager, "getBroken")) && storage.Serialize() == clear, "metadata root extent mismatch cannot change real Struct storage");
	files.Write("meta_000_0003.bin", BackupMetadataFixture(clear, 4, 0));
	files.Write("data_000_0003.bin", std::array<std::uint8_t, 4>{1, 2, 3, 4});
	static_cast<void>(Call(vm, manager, "autoload"));
	const auto retired_handle = fixture.Layers.Handle(manager);
	fixture.Update(retired_handle);
	fixture.Layers.ReleaseInstance(manager);
	Require(storage.Serialize() == clear && !fixture.Tree.Contains(retired_handle), "actual queued/running worker cancellation must join before the real Struct owner retires without publishing through a dead manager");
	fixture.Close();
}

static void TestActualAdventureSegmentLoad() {
	BackupFileFixture files;
	BackupVmFixture fixture;
	auto& vm = fixture.Vm;
	sgre::save::BackupFileServices services;
	services.Directory = files.Directory();
	services.Decrypt = sgre::platform::DecryptBackupBytes;
	fixture.Backups.ConfigureFiles(services);
	const auto manager = vm.CreateInstance(fixture.Backups.AdventureManagerClass());
	static_cast<void>(Call(vm, manager, "constructor"));
	const auto manager_pin = vm.Retain(manager);
	fixture.Layers.AttachBaseFrame(manager, fixture.Renderer);
	const auto schema = fixture.Psb.WrapDocument(Schema());
	const auto first = vm.CreateInstance(fixture.Structs.StructClass());
	const auto second = vm.CreateInstance(fixture.Structs.StructClass());
	static_cast<void>(Call(vm, first, "constructor", std::array<sgre::script::SquirrelValue, 1>{schema}));
	static_cast<void>(Call(vm, second, "constructor", std::array<sgre::script::SquirrelValue, 1>{schema}));
	const auto first_pin = vm.Retain(first);
	const auto second_pin = vm.Retain(second);
	const auto segment0 = Call(vm, manager, "addSegment", std::array<sgre::script::SquirrelValue, 2>{first, std::int32_t{4}});
	const auto segment1 = Call(vm, manager, "addSegment", std::array<sgre::script::SquirrelValue, 2>{second, std::int32_t{4}});
	const auto segment0_pin = vm.Retain(segment0);
	const auto segment1_pin = vm.Retain(segment1);
	static_cast<void>(Call(vm, manager, "init"));
	fixture.Drain(manager);
	auto& first_storage = fixture.Structs.BorrowStructStorage(first);
	auto& second_storage = fixture.Structs.BorrowStructStorage(second);
	const std::vector<std::uint8_t> initial{1, 3, 5, 7};
	files.Write("data_000_0000.bin", initial);
	// Native03a0d0 invokes the no-argument member and ignores extra script operands.
	static_cast<void>(Call(vm, segment0, "autoload", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{99}}));
	Require(std::get<bool>(Call(vm, segment0, "getRunning")) && !first_storage.HasBacking(), "segment autoload must enqueue its genuine worker before any Struct publication");
	fixture.Drain(manager);
	Require(first_storage.Serialize() == initial && !second_storage.HasBacking() && std::get<bool>(Call(vm, segment0, "getSuccess")), "segment load must publish only the actual registered target Struct");
	static_cast<void>(Call(vm, segment1, "autoload"));
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, segment1, "getNofile")) && !std::get<bool>(Call(vm, segment1, "getSuccess")) && first_storage.Serialize() == initial, "missing second segment must retain the first segment's actual bytes");

	const std::vector<std::uint8_t> selected{2, 4, 6, 8};
	files.Write("data_000_0003.bin", selected);
	static_cast<void>(Call(vm, segment0, "setFileId", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{3}}));
	static_cast<void>(Call(vm, segment0, "autoload"));
	RequireError([&] {
		static_cast<void>(Call(vm, segment0, "setFileId", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{2}}));
	},
	    "pending worker");
	fixture.Drain(manager);
	Require(first_storage.Serialize() == selected && std::get<std::int32_t>(Call(vm, segment0, "getFileId")) == 3 && std::get<bool>(Call(vm, segment0, "getSuccess")), "segment load must retain its actual selected file identity");
	Require(std::get<bool>(Call(vm, segment1, "getNofile")) && !std::get<bool>(Call(vm, segment1, "getSuccess")) && !second_storage.HasBacking(), "segment autoload must not apply the manager batch's all-segment result reset");
	fixture.Close();
}

static void TestActualAdventureBatchSave() {
	BackupFileFixture files;
	BackupVmFixture fixture;
	auto& vm = fixture.Vm;
	std::atomic<std::size_t> timestamps = 0;
	sgre::save::BackupFileServices services;
	services.Directory = files.Directory();
	services.Decrypt = sgre::platform::DecryptBackupBytes;
	services.Encrypt = sgre::platform::EncryptBackupBytes;
	services.ReadTimestamp = [&] {
		Require(std::this_thread::get_id() != fixture.OwnerThread, "batch timestamps must be read by the genuine background task");
		++timestamps;

		return std::int64_t{0x123456789};
	};
	fixture.Backups.ConfigureFiles(services);
	const auto manager = vm.CreateInstance(fixture.Backups.AdventureManagerClass());
	static_cast<void>(Call(vm, manager, "constructor"));
	const auto manager_pin = vm.Retain(manager);
	fixture.Layers.AttachBaseFrame(manager, fixture.Renderer);
	const auto schema = fixture.Psb.WrapDocument(Schema());
	std::array<sgre::script::SquirrelValue, 3> documents;
	std::array<sgre::script::SquirrelValue, 3> segments;
	for (std::size_t index = 0; index != documents.size(); ++index) {
		documents[index] = vm.CreateInstance(fixture.Structs.StructClass());
		static_cast<void>(Call(vm, documents[index], "constructor", std::array<sgre::script::SquirrelValue, 1>{schema}));
		fixture.Structs.BorrowStructStorage(documents[index]).Unserialize(std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(index + 1), 2, 3, 4});
		segments[index] = Call(vm, manager, "addSegment", std::array<sgre::script::SquirrelValue, 2>{documents[index], std::int32_t{32}});
	}
	static_cast<void>(Call(vm, manager, "init"));
	fixture.Drain(manager);
	static_cast<void>(Call(vm, manager, "setEnableMetaData", std::array<sgre::script::SquirrelValue, 1>{true}));
	static_cast<void>(Call(vm, manager, "autosave"));
	Require(timestamps == 0 && !std::filesystem::exists(files.Directory() / "data_000_0000.bin"), "queueing a batch must defer all physical writes to its actual camera/background task");
	fixture.Drain(manager);
	Require(timestamps == 3 && std::get<bool>(Call(vm, manager, "getSuccess")), "empty native FileData input must save every actual segment in one manager job");
	for (std::uint32_t ordinal = 0; ordinal != documents.size(); ++ordinal) {
		sgre::save::BackupFileReadRequest request;
		request.SegmentId = ordinal;
		request.RootBytes = 4;
		request.EnableMetaData = true;
		const auto read = sgre::save::ReadAdventureBackupFiles(files.Directory(), request, services.Decrypt);
		Require(read.Success && read.Bytes == std::vector<std::uint8_t>{static_cast<std::uint8_t>(ordinal + 1), 2, 3, 4}, "each actual batch file and metadata must independently load and verify its original Struct bytes");
	}
	static_cast<void>(Call(vm, segments[2], "setFileId", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{31}}));
	static_cast<void>(Call(vm, segments[2], "autoload"));
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, segments[2], "getNofile")), "unselected segment failure must precede batch result reset");
	const auto entry = [&](std::int32_t ordinal, std::int32_t file) {
		const auto value = vm.CreateTable();
		vm.NewSlot(value, std::string("segmentId"), ordinal);
		vm.NewSlot(value, std::string("fileId"), file);

		return value;
	};
	const auto selections = vm.CreateArray(std::array<sgre::script::SquirrelValue, 4>{entry(0, 7), entry(0, -1), entry(1, 9), entry(1000, 0)});
	static_cast<void>(Call(vm, manager, "autosave", std::array<sgre::script::SquirrelValue, 1>{selections}));
	fixture.Drain(manager);
	Require(timestamps == 6 && std::get<std::int32_t>(Call(vm, segments[0], "getFileId")) == 7 && std::get<std::int32_t>(Call(vm, segments[1], "getFileId")) == 9, "batch must retain request order, reuse a previous duplicate file ID and skip out-of-range segment ordinals");
	Require(std::get<bool>(Call(vm, segments[2], "getSuccess")) && !std::get<bool>(Call(vm, segments[2], "getNofile")) && std::get<std::int32_t>(Call(vm, segments[2], "getFileId")) == 31, "native batch reset must clear even unselected result flags while retaining their selected file IDs");
	Require(std::filesystem::exists(files.Directory() / "data_000_0007.bin") && std::filesystem::exists(files.Directory() / "data_001_0009.bin"), "normal save selections must produce the requested physical slot files");
	fixture.Close();
}

static void TestActualAdventureSegmentSave() {
	BackupFileFixture files;
	BackupVmFixture fixture;
	auto& vm = fixture.Vm;
	std::atomic<std::size_t> timestamps = 0;
	std::atomic<bool> fail_timestamp = false;
	sgre::save::BackupFileServices services;
	services.Directory = files.Directory();
	services.Decrypt = sgre::platform::DecryptBackupBytes;
	services.Encrypt = sgre::platform::EncryptBackupBytes;
	services.ReadTimestamp = [&] {
		Require(std::this_thread::get_id() != fixture.OwnerThread, "autosave must read its real timestamp in the background worker");
		++timestamps;
		if (fail_timestamp) {
			throw std::runtime_error("actual timestamp worker failure");
		}

		return std::int64_t{0x102030405};
	};
	fixture.Backups.ConfigureFiles(services);
	const auto manager = vm.CreateInstance(fixture.Backups.AdventureManagerClass());
	static_cast<void>(Call(vm, manager, "constructor"));
	const auto manager_pin = vm.Retain(manager);
	fixture.Layers.AttachBaseFrame(manager, fixture.Renderer);
	const auto schema = fixture.Psb.WrapDocument(Schema());
	const auto first = vm.CreateInstance(fixture.Structs.StructClass());
	const auto second = vm.CreateInstance(fixture.Structs.StructClass());
	static_cast<void>(Call(vm, first, "constructor", std::array<sgre::script::SquirrelValue, 1>{schema}));
	static_cast<void>(Call(vm, second, "constructor", std::array<sgre::script::SquirrelValue, 1>{schema}));
	const auto first_pin = vm.Retain(first);
	const auto second_pin = vm.Retain(second);
	const auto segment0 = Call(vm, manager, "addSegment", std::array<sgre::script::SquirrelValue, 2>{first, std::int32_t{4}});
	const auto segment1 = Call(vm, manager, "addSegment", std::array<sgre::script::SquirrelValue, 2>{second, std::int32_t{4}});
	const auto segment0_pin = vm.Retain(segment0);
	const auto segment1_pin = vm.Retain(segment1);
	static_cast<void>(Call(vm, manager, "init"));
	fixture.Drain(manager);
	static_cast<void>(Call(vm, segment0, "autoload"));
	fixture.Drain(manager);
	static_cast<void>(Call(vm, segment1, "autoload"));
	fixture.Drain(manager);
	Require(std::get<bool>(Call(vm, segment0, "getNofile")) && std::get<bool>(Call(vm, segment1, "getNofile")), "both actual segment failures must precede selected save publication");

	auto& storage = fixture.Structs.BorrowStructStorage(first);
	const std::vector<std::uint8_t> snapshot{1, 9, 2, 8};
	storage.Unserialize(snapshot);
	static_cast<void>(Call(vm, manager, "setSecureFileId", std::array<sgre::script::SquirrelValue, 1>{std::string("independent-save-owner-key")}));
	static_cast<void>(Call(vm, manager, "setEnableMetaData", std::array<sgre::script::SquirrelValue, 1>{true}));
	static_cast<void>(Call(vm, segment0, "autosave", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{99}}));
	Require(std::get<bool>(Call(vm, segment0, "getRunning")) && std::get<bool>(Call(vm, segment0, "getNofile")) && !std::filesystem::exists(files.Directory() / "data_000_0000.bin") && timestamps == 0, "queued autosave must not perform I/O or publish results before its genuine task is updated");
	RequireError([&] {
		static_cast<void>(Call(vm, segment0, "setFileId", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{2}}));
	},
	    "pending worker");
	// The authored worker owns its queued snapshot; concurrent native Struct mutation timing is not claimed.
	storage.Unserialize(std::vector<std::uint8_t>{0, 0, 0, 0});
	fixture.Drain(manager);
	Require(timestamps == 1 && std::get<bool>(Call(vm, segment0, "getSuccess")) && !std::get<bool>(Call(vm, segment0, "getNofile")) && !std::get<bool>(Call(vm, segment0, "getCrypted")), "genuine selected save completion must publish only its native write flags; Crypted is a read result");
	Require(std::get<bool>(Call(vm, segment1, "getNofile")) && !std::get<bool>(Call(vm, segment1, "getSuccess")), "selected autosave must preserve the other segment's failure result");
	sgre::save::BackupFileReadRequest read;
	read.RootBytes = snapshot.size();
	read.EnableMetaData = true;
	read.SecureFileId = "independent-save-owner-key";
	const auto loaded = sgre::save::ReadAdventureBackupFiles(files.Directory(), read, sgre::platform::DecryptBackupBytes);
	Require(loaded.Success && loaded.Bytes == snapshot && loaded.Crypted, "real manager-owned encrypted data and metadata must contain the exact submitted Struct snapshot");
	static_cast<void>(Call(vm, segment0, "autoload"));
	fixture.Drain(manager);
	Require(storage.Serialize() == snapshot && std::get<bool>(Call(vm, segment0, "getCrypted")), "real selected load must restore the new save through the same manager and Struct owner");

	static_cast<void>(Call(vm, segment0, "setFileId", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{2}}));
	Require(std::filesystem::create_directory(files.Directory() / "meta_000_0002.bin"), "metadata failure fixture must own the output obstruction");
	static_cast<void>(Call(vm, segment0, "autosave"));
	fixture.Drain(manager);
	Require(timestamps == 2 && !std::get<bool>(Call(vm, segment0, "getSuccess")) && std::get<bool>(Call(vm, segment0, "getNospace")) && !std::get<bool>(Call(vm, segment0, "getCrypted")) && !std::filesystem::exists(files.Directory() / "data_000_0002.bin"), "actual metadata open failure must remove written data and publish native NoSpace");
	Require(std::get<bool>(Call(vm, segment1, "getNofile")) && std::get<bool>(Call(vm, manager, "getInitSuccess")), "actual write failure must preserve unrelated segment and initialization results");
	fail_timestamp = true;
	static_cast<void>(Call(vm, segment0, "autosave"));
	RequireError([&] {
		fixture.Drain(manager);
	},
	    "actual timestamp worker failure");
	Require(timestamps == 3 && !std::get<bool>(Call(vm, segment0, "getRunning")) && !std::get<bool>(Call(vm, segment0, "getSuccess")) && !std::get<bool>(Call(vm, segment0, "getNospace")), "actual service exception must retire and publish a selected failure before rethrowing; NoSpace requires a physical write failure");
	fail_timestamp = false;
	static_cast<void>(Call(vm, segment0, "setFileId", std::array<sgre::script::SquirrelValue, 1>{std::int32_t{3}}));
	static_cast<void>(Call(vm, segment0, "autosave"));
	fixture.Layers.ReleaseInstance(manager);
	Require(timestamps == 3 && !std::filesystem::exists(files.Directory() / "data_000_0003.bin"), "retiring the genuine owner before its queued worker starts must cancel I/O without late callbacks");
	fixture.Close();
}

#endif

int main() {
	try {
		TestActualVmBaseAndWorker();
		TestActualAdventureProfile();
		TestRealCountByteAndOwnBridgeRetirement();
		TestWorkerExceptionRetirement();
		TestPortableBackupFiles();
#if defined(__APPLE__)
		TestActualAdventureFileLoad();
		TestActualAdventureSegmentLoad();
		TestActualAdventureSegmentSave();
		TestActualAdventureBatchSave();
#endif
		std::cout << "PASS genuine ordinary and Adventure Backup VM/Base/Struct/job metadata, ordinary Init success and failed-retry reset, low-byte count, own-camera extraction, actual file/metadata/MD5/AES load, missing/corrupt results, cancelled publication and exception retirement\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
