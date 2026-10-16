#include "sgre/script/resource_bindings.hpp"

#include "sgre/resource/resource_cache.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected Resource rejection: " + std::string(message));
}

static std::shared_ptr<const sgre::resource::PsbDocument> Document() {
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	sgre::resource::PsbValue number;
	number.SerializedTag = 5;
	number.Data = std::int64_t(42);
	document->Root.SerializedTag = 33;
	document->Root.Data = sgre::resource::PsbObject{{"answer", number}};
	document->Header.DataOffset = 64;
	document->SerializedSize = 128;

	return document;
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, std::string name, const sgre::script::SquirrelValue& argument) {
	const std::array<sgre::script::SquirrelValue, 2> arguments{instance, argument};

	return vm.Invoke(vm.GetSlot(instance, name), arguments).Value;
}

static void TestNativeLifecycle() {
	sgre::script::SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	const auto document = Document();
	const auto backing = std::make_shared<const std::vector<std::uint8_t>>(128);
	std::size_t requests = 0;
	std::size_t polls = 0;
	std::size_t errors = 0;
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, [&](std::string_view name) {
		Require(name == "config/Exact.psb", "Resource binding normalized an original lookup identity");
		++requests;

		return [&, document, backing]() -> std::optional<sgre::script::ScriptResourceAsset> {
			if (++polls == 1) {
				return std::nullopt;
			}

			sgre::script::ScriptResourceAsset asset;
			asset.Document = document;
			asset.Backing = backing;

			return asset;
		};
	},
	    [&](std::string_view, std::string_view) {
		    ++errors;
	    });
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	const auto instance = vm.Invoke(resources.ResourceClass(), root_receiver).Value;
	Require(objects.HasInstance(instance) && vm.IsInstanceOf(instance, objects.ObjectClass()), "Resource constructor did not initialize actual Object inheritance");
	static_cast<void>(Call(vm, instance, "load", std::string("config/Exact.psb")));
	Require(std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "pending Resource request did not report loading");
	RequireError([&] {
		static_cast<void>(Call(vm, instance, "load", std::string("config/Exact.psb")));
	},
	    "cannot load before unload");
	Require(!std::get<bool>(vm.ReadSlot(instance, std::string("waiting"))), "completed Resource request remained waiting");
	const auto data = Call(vm, instance, "find", std::string("config/Exact.psb"));
	Require(vm.IsInstanceOf(data, psb.ObjectClass()), "Resource.find flattened its PSB owner into a table");
	const auto value = vm.ReadSlot(data, std::string("root"));
	Require(std::get<std::int32_t>(vm.ReadSlot(value, std::string("answer"))) == 42, "Resource result did not expose its actual typed PSB root");
	const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
	static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("unload")), receiver));
	Require(std::holds_alternative<std::monostate>(Call(vm, instance, "find", std::string("config/Exact.psb"))), "unload retained the Resource's target ownership");
	Require(std::get<std::int32_t>(vm.ReadSlot(value, std::string("answer"))) == 42, "unloading Resource invalidated the returned strongly owned PSB value");
	Require(requests == 1 && polls == 2 && errors == 0, "Resource repeated a completed poll or silently reported a success error");
	resources.ReleaseInstance(instance);
	Require(!vm.HasSlot(instance, std::string("_get")), "Resource must inherit native Object meta storage without exposing an ordinary _get method");
	RequireError([&] {
		static_cast<void>(Call(vm, instance, "get", std::string("loading")));
	},
	    "retained base state");
	RequireError([&] {
		static_cast<void>(vm.ReadSlot(instance, std::string("loading")));
	},
	    "the index doesn't exist");
}

static void TestCacheReferenceRetention() {
	sgre::resource::ResourceCache cache(0);
	const auto handle = cache.Acquire("config/owned.psb");
	cache.Complete(handle, std::vector<std::uint8_t>(128), 128);
	cache.Advance();
	{
		sgre::script::SquirrelVm vm;
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptPsbBindings psb(vm, objects);
		const auto document = Document();
		auto backing = cache.PinBytes(handle);
		cache.Release(handle);
		sgre::script::ScriptResourceBindings resources(vm, objects, psb, [&, document, backing](std::string_view) {
			return [&, document, backing]() -> std::optional<sgre::script::ScriptResourceAsset> {
				sgre::script::ScriptResourceAsset asset;
				asset.Document = document;
				asset.Backing = backing;
				asset.RetainBacking = [&cache, handle] {
					return cache.PinBytes(handle);
				};

				return asset;
			};
		},
		    [](std::string_view, std::string_view) {
			    throw std::runtime_error("successful retained fixture reported a load error");
		    });
		const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
		const auto instance = vm.Invoke(resources.ResourceClass(), root_receiver).Value;
		static_cast<void>(Call(vm, instance, "load", std::string("config/owned.psb")));
		Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "retained fixture did not complete its prepared resource");
		const auto first = Call(vm, instance, "find", std::string("config/owned.psb"));
		const auto second = Call(vm, instance, "find", std::string("config/owned.psb"));
		Require(first != second && cache.Info(handle).References == 3, "distinct native PSBObjects did not each retain a manager reference");
		const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("unload")), receiver));
		cache.ClearCache();
		cache.Advance();
		const auto value = vm.ReadSlot(first, std::string("root"));
		Require(std::get<std::int32_t>(vm.ReadSlot(value, std::string("answer"))) == 42 && cache.State().TotalActiveSize == 128, "unload/cache clearing invalidated a strongly owned native PSBObject");
	}
	Require(!cache.Contains("config/owned.psb"), "destroyed PSB adapter owners retained their native cache references");
}

static void TestResourceVariants() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	std::size_t requests = 0;
	const auto document = Document();
	const auto backing = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{0x7f, 0x80, 0xff});
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, binary, [&, document, backing](std::string_view name, sgre::script::ScriptResourceRequestMode mode) {
		Require(name == "Folder/Exact.payload", "Resource mode changed its cache lookup identity");
		++requests;
		const bool wrapped = mode == sgre::script::ScriptResourceRequestMode::PsbWrapped || mode == sgre::script::ScriptResourceRequestMode::BinaryWrapped;
		Require(sgre::script::ResourceReadFilename(name, mode) == (wrapped ? "Folder/Exact.payload.m" : "Folder/Exact.payload"), "native filter flag did not select its exact file identity");

		return [document, backing, mode]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			if (mode == sgre::script::ScriptResourceRequestMode::BinaryWrapped || mode == sgre::script::ScriptResourceRequestMode::BinaryRaw) {
				asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
				asset.Backing = backing;
			} else {
				asset.Document = document;
			}

			return asset;
		};
	},
	    [](std::string_view, std::string_view) {
		    throw std::runtime_error("valid Resource mode reported a load error");
	    });
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	const auto instance = vm.Invoke(resources.ResourceClass(), root_receiver).Value;
	const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
	for (const auto method : {"load", "loadBinary", "loadRaw", "loadRawBinary"}) {
		static_cast<void>(Call(vm, instance, method, std::string("Folder/Exact.payload")));
		Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "completed typed request remained pending");
		const auto data = Call(vm, instance, "find", std::string("Folder/Exact.payload"));
		const bool is_binary = std::string_view(method).find("Binary") != std::string_view::npos;
		Require(vm.IsInstanceOf(data, is_binary ? binary.BinaryClass() : psb.ObjectClass()), "Resource.find returned a wrapper for the wrong actual data kind");
		if (is_binary) {
			const std::array<sgre::script::SquirrelValue, 2> arguments{data, std::int32_t(2)};
			Require(std::get<std::int32_t>(vm.Invoke(vm.GetSlot(data, std::string("at")), arguments).Value) == 255, "Resource Binary did not retain its unsigned source bytes");
		}
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("unload")), receiver));
	}
	Require(requests == 4, "Resource variants duplicated or omitted an owned request");
	RequireError([] {
		static_cast<void>(sgre::script::ResourceReadFilename("", sgre::script::ScriptResourceRequestMode::PsbRaw));
	},
	    "empty");
}

static void AppendWord(std::vector<std::uint8_t>& bytes, std::uint32_t word) {
	for (std::size_t index = 0; index < 4; ++index) {
		bytes.push_back(static_cast<std::uint8_t>(word >> (8 * index)));
	}
}

static void AppendString(std::vector<std::uint8_t>& bytes, std::string_view text) {
	AppendWord(bytes, 0x08000010);
	AppendWord(bytes, static_cast<std::uint32_t>(text.size()));
	bytes.insert(bytes.end(), text.begin(), text.end());
}

static std::shared_ptr<const std::vector<std::uint8_t>> ExecutableFixture(bool throws) {
	std::vector<std::uint8_t> bytes{0xfa, 0xfa, 0x52, 0x49, 0x51, 0x53, 1, 0, 0, 0};
	const auto part = [&] {
		AppendWord(bytes, 0x50415254);
	};
	part();
	AppendString(bytes, "independent-exec.nut");
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
	bytes.insert(bytes.end(), {static_cast<std::uint8_t>(throws ? 58 : 19), 1, 0, 0});
	part();
	AppendWord(bytes, 2);
	bytes.insert(bytes.end(), {0, 0});
	AppendWord(bytes, 0x5441494c);

	return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
}

static void TestCompiledExecution() {
	sgre::script::SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	std::size_t failures = 0;
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, binary, [](std::string_view name, sgre::script::ScriptResourceRequestMode mode) {
		Require(mode == sgre::script::ScriptResourceRequestMode::BinaryWrapped, "executable fixture was not requested as a binary resource");
		const auto backing = ExecutableFixture(name == "failure.nut");

		return [backing]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = backing;

			return asset;
		};
	},
	    [&](std::string_view name, std::string_view error) {
		    Require(name == "failure.nut" && !error.empty(), "Resource execution lost its failure identity/diagnostic");
		    ++failures;
	    });
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	const auto instance = vm.Invoke(resources.ResourceClass(), root_receiver).Value;
	static_cast<void>(Call(vm, instance, "loadBinary", std::string("success.nut")));
	Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "binary execution fixture remained pending");
	Require(std::get<std::int32_t>(Call(vm, instance, "exec", std::string("success.nut"))) == 42, "Resource.exec failed compiled root invocation");
	const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
	static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("unload")), receiver));
	static_cast<void>(Call(vm, instance, "loadBinary", std::string("failure.nut")));
	Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "throwing execution fixture remained pending");
	Require(sgre::script::StringText(Call(vm, instance, "exec", std::string("failure.nut"))) == "failure.nut", "native failed-call stack-top return was changed into a rethrow");
	const std::array<sgre::script::SquirrelValue, 3> quiet_arguments{instance, std::string("failure.nut"), false};
	Require(!std::get<bool>(vm.Invoke(vm.GetSlot(instance, std::string("exec")), quiet_arguments).Value) && failures == 2, "explicit error flag or required execution diagnostics were lost");

	std::size_t handled = 0;
	const auto handler = vm.CreateNativeClosure("exec_flag_policy", [&](auto&, auto arguments) {
		Require(arguments.size() == 2 && std::get<std::int32_t>(arguments[1]) == 42, "Resource execution handler must receive the original script error");
		++handled;

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const std::array<sgre::script::SquirrelValue, 2> install{vm.RootTable(), handler};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), install));
	const std::array<std::pair<sgre::script::SquirrelValue, bool>, 17> flags{{{256, true}, {-256, true}, {258, true}, {0, false}, {-1, true}, {0.75f, false}, {-0.75f, false}, {1.25f, true}, {-1.25f, true}, {std::bit_cast<float>(0x7fc00001u), true}, {std::bit_cast<float>(0x7f800000u), true}, {2147483648.0f, true}, {-2147483904.0f, true}, {false, false}, {true, true}, {sgre::script::SquirrelValue{}, true}, {std::string("failed-conversion"), true}}};
	for (const auto& [flag, raises] : flags) {
		const auto before = handled;
		const std::array<sgre::script::SquirrelValue, 3> arguments{instance, std::string("failure.nut"), flag};
		const auto result = vm.Invoke(vm.GetSlot(instance, std::string("exec")), arguments).Value;
		Require(handled == before + static_cast<std::size_t>(raises), "Resource.exec numeric boolean policy differs from normalized native conversion");
		if (const auto* number = std::get_if<float>(&flag)) {
			Require(std::bit_cast<std::uint32_t>(std::get<float>(result)) == std::bit_cast<std::uint32_t>(*number), "failed Resource.exec must return the exact previous floating argument word");
		} else {
			Require(result == flag, "failed Resource.exec must retain previous nonnumeric argument identity");
		}

		Require(std::get<std::int32_t>(vm.LastError()) == 42, "Resource flag policy must not erase the original script last error");
	}

	const auto before = handled;
	Require(sgre::script::StringText(Call(vm, instance, "exec", std::string("failure.nut"))) == "failure.nut" && handled == before + 1 && failures == 20, "omitted Resource.exec flag must remain true and preserve name return plus diagnostics");
}

static std::uint32_t ReadNativeFlagWord(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	input.read(static_cast<char*>(static_cast<void*>(bytes.data())), bytes.size());
	Require(static_cast<bool>(input), "native Resource flag projection is truncated");

	return bytes[0] | (std::uint32_t{bytes[1]} << 8) | (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
}

static void TestNativeFlagProjection(const char* path) {
	std::ifstream input(path, std::ios::binary);
	const auto count = ReadNativeFlagWord(input);
	Require(count == 549, "native Resource flag projection must contain the verified bounded input extent");
	for (std::uint32_t index = 0; index != count; ++index) {
		const auto tag = ReadNativeFlagWord(input);
		const auto payload = ReadNativeFlagWord(input);
		const auto status = ReadNativeFlagWord(input);
		const auto output = ReadNativeFlagWord(input);
		const auto flag = ReadNativeFlagWord(input);
		sgre::script::SquirrelValue value;
		if (tag == 0x5000002u) {
			value = std::bit_cast<std::int32_t>(payload);
		} else if (tag == 0x5000004u) {
			value = std::bit_cast<float>(payload);
		} else if (tag == 0x1000008u) {
			value = payload != 0;
		}

		const auto converted = sgre::script::TryNativeInteger(value);
		Require(converted.has_value() == (status == 0) && output == static_cast<std::uint32_t>(converted.value_or(1) != 0) && flag == output, "native sq_getbool/default Resource flag differs in case " + std::to_string(index));
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native Resource flag projection has unchecked trailing bytes");
}

static void TestFailureDiagnostic() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::string diagnostic;
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, [](std::string_view) {
		return []() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset result;
			result.Error = "original entry is missing";

			return result;
		};
	},
	    [&](std::string_view name, std::string_view error) {
		    diagnostic = std::string(name) + ": " + std::string(error);
	    });
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	const auto instance = vm.Invoke(resources.ResourceClass(), root_receiver).Value;
	static_cast<void>(Call(vm, instance, "load", std::string("missing.psb")));
	Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && diagnostic == "missing.psb: original entry is missing", "failed load became a pending request or lost its diagnostic");
	Require(std::holds_alternative<std::monostate>(Call(vm, instance, "find", std::string("missing.psb"))), "failed Resource.find manufactured decoded data");
}

struct PendingResourceProbe {
	explicit PendingResourceProbe(std::vector<std::int32_t>& events, std::int32_t event = 2) : Events(events), Event(event) {
	}

	~PendingResourceProbe() {
		Events.push_back(Event);
	}

	std::vector<std::int32_t>& Events;
	std::int32_t Event = 2;
};

static void TestReclaimedPendingAndFailure() {
	sgre::script::SquirrelVmLimits vm_limits;
	vm_limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(vm_limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptResourceBindingLimits limits;
	limits.MaxInstances = 1;
	std::weak_ptr<PendingResourceProbe> pending;
	std::vector<std::int32_t> events;
	events.reserve(8);
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, [&](std::string_view) {
		auto job = std::make_shared<PendingResourceProbe>(events);
		pending = job;

		return [job]() -> std::optional<sgre::script::ScriptResourceAsset> {
			return std::nullopt;
		};
	},
	    [](std::string_view, std::string_view) {
	    },
	    limits);
	const auto derived = vm.CreateClass(resources.ResourceClass());
	vm.NewSlot(derived, std::string("destructor"), vm.CreateNativeClosure("fixture.resource.destructor", [&](sgre::script::SquirrelVm&, const auto arguments) -> sgre::script::SquirrelNativeResult {
		Require(objects.HasInstance(arguments[0]) && !pending.expired(), "Resource script destructor ran after pending target release");
		events.push_back(1);
		throw std::runtime_error("resource script release failure");
	}));
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	auto instance = vm.Invoke(derived, receiver).Value;
	const auto identity = vm.BorrowObjectIdentity(instance);
	static_cast<void>(Call(vm, instance, "load", std::string("pending.psb")));
	Require(std::get<bool>(vm.ReadSlot(instance, std::string("loading"))) && !pending.expired(), "pending Resource job did not retain its host observer");
	vm.NewSlot(vm.RootTable(), std::string("fixture-resource"), instance);
	static_cast<void>(vm.CollectGarbage());
	Require(!pending.expired(), "collection released a reachable Resource job");
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(listener);
	objects.Subscribe(instance, listener, [&](const auto&) -> bool {
		Require(pending.expired(), "Resource Object notification ran before target ownership ended");
		events.push_back(3);
		throw std::runtime_error("resource base notification failure");
	});
	vm.SetSlot(vm.RootTable(), std::string("fixture-resource"), {});
	instance = {};
	RequireError([&] {
		vm.RethrowReleaseFailure();
	},
	    "resource base notification failure");
	Require(sgre::script::StringText(vm.LastError()).find("resource script release failure") != std::string::npos, "ignored Resource script-destructor status lost its VM last-error diagnostic");
	Require(events == std::vector<std::int32_t>{1, 2, 3} && pending.expired() && !vm.ResolveObjectIdentity(identity), "Resource diagnostic failure stranded native targets or registry identity");
	const auto replacement = vm.Invoke(resources.ResourceClass(), receiver).Value;
	Require(objects.HasInstance(replacement) && vm.BorrowObjectIdentity(replacement) != identity, "Resource registry did not release its bounded slot after failure");
}

static void TestReclaimedCachePins() {
	sgre::resource::ResourceCache cache(0);
	const auto handle = cache.Acquire("owned.psb");
	cache.Complete(handle, std::vector<std::uint8_t>(128), 128);
	cache.Advance();
	auto request_pin = cache.PinBytes(handle);
	cache.Release(handle);
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, [&](std::string_view) {
		auto pin = std::move(request_pin);

		return [&, pin]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			asset.Document = Document();
			asset.Backing = pin;
			asset.RetainBacking = [&] {
				return cache.PinBytes(handle);
			};

			return asset;
		};
	},
	    [](std::string_view, std::string_view) {
	    });
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	auto instance = vm.Invoke(resources.ResourceClass(), receiver).Value;
	static_cast<void>(Call(vm, instance, "load", std::string("owned.psb")));
	Require(!std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "prepared cache target remained pending");
	auto wrapper = Call(vm, instance, "find", std::string("owned.psb"));
	auto proxy = vm.ReadSlot(wrapper, std::string("root"));
	vm.NewSlot(vm.RootTable(), std::string("fixture-proxy"), proxy);
	Require(cache.Info(handle).References == 2, "native Resource and returned PSBObject pins did not retain distinct manager references");
	instance = {};
	vm.RethrowReleaseFailure();
	Require(cache.Info(handle).References == 1, "reclaimed Resource target did not release its own manager pin");
	wrapper = {};
	cache.ClearCache();
	cache.Advance();
	static_cast<void>(vm.CollectGarbage());
	Require(cache.Info(handle).References == 1 && std::get<std::int32_t>(vm.ReadSlot(proxy, std::string("answer"))) == 42, "PSB proxy failed to mark its native document/cache owner during collection");
	vm.SetSlot(vm.RootTable(), std::string("fixture-proxy"), {});
	proxy = {};
	vm.RethrowReleaseFailure();
	Require(!cache.Contains("owned.psb"), "last reclaimed PSBValue left the native manager pin alive");
}

static void TestForwardTargetRelease() {
	sgre::script::SquirrelVmLimits vm_limits;
	vm_limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(vm_limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::vector<std::int32_t> events;
	events.reserve(16);
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, [&](std::string_view name) {
		auto job = std::make_shared<PendingResourceProbe>(events, name == "first.psb" ? 4 : 5);

		return [job]() -> std::optional<sgre::script::ScriptResourceAsset> {
			return std::nullopt;
		};
	},
	    [](std::string_view, std::string_view) {
	    });
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	auto instance = vm.Invoke(resources.ResourceClass(), root_receiver).Value;
	const std::array<sgre::script::SquirrelValue, 2> names{std::string("first.psb"), std::string("second.psb")};
	const auto list = vm.CreateArray(names);
	static_cast<void>(Call(vm, instance, "load", list));
	{
		const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("unload")), receiver));
	}

	Require(events == std::vector<std::int32_t>{4, 5}, "Resource unload released native targets in reverse order");
	static_cast<void>(Call(vm, instance, "load", list));
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(listener);
	objects.Subscribe(instance, listener, [&](const auto&) {
		events.push_back(6);

		return false;
	});
	instance = {};
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::int32_t>{4, 5, 4, 5, 6}, "Resource finalization lost forward target release before Object notification");
}

static void TestResourceOwnerExpiry() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::vector<std::int32_t> events;
	events.reserve(4);
	sgre::script::SquirrelValue instance;
	sgre::script::SquirrelValue callable;
	{
		sgre::script::ScriptResourceBindings resources(vm, objects, psb, [&](std::string_view) {
			auto job = std::make_shared<PendingResourceProbe>(events);

			return [job]() -> std::optional<sgre::script::ScriptResourceAsset> {
				return std::nullopt;
			};
		},
		    [](std::string_view, std::string_view) {
		    });
		const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
		instance = vm.Invoke(resources.ResourceClass(), receiver).Value;
		static_cast<void>(Call(vm, instance, "load", std::string("pending.psb")));
		callable = vm.GetSlot(instance, std::string("getLoading"));
	}

	Require(events == std::vector<std::int32_t>{2}, "expired Resource adapter retained its pending native observer");
	const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
	RequireError([&] {
		static_cast<void>(vm.Invoke(callable, receiver));
	},
	    "Resource callbacks have expired");
}

static void TestNotificationReleasePreflight() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::vector<std::int32_t> events;
	events.reserve(4);
	std::weak_ptr<PendingResourceProbe> pending;
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, [&](std::string_view) {
		auto job = std::make_shared<PendingResourceProbe>(events);
		pending = job;

		return [job]() -> std::optional<sgre::script::ScriptResourceAsset> {
			return std::nullopt;
		};
	},
	    [](std::string_view, std::string_view) {
	    });
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	auto instance = vm.Invoke(resources.ResourceClass(), receiver).Value;
	static_cast<void>(Call(vm, instance, "load", std::string("pending.psb")));
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(listener);
	std::size_t notifications = 0;
	objects.Subscribe(instance, listener, [&](const auto&) {
		RequireError([&] {
			resources.ReleaseInstance(instance);
		},
		    "release during completion notification");
		Require(!pending.expired() && events.empty() && objects.HasInstance(instance) && std::get<bool>(vm.ReadSlot(instance, std::string("loading"))), "rejected nested Resource release altered native targets or base state");
		++notifications;

		return false;
	});
	objects.Notify(instance, true);
	Require(notifications == 1 && !pending.expired(), "Resource release preflight did not preserve its live pending payload");
	instance = {};
	vm.RethrowReleaseFailure();
	Require(pending.expired() && events == std::vector<std::int32_t>{2}, "Resource failed to release normally after the notification guard cleared");
}

static void TestDerivedResourceClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::vector<std::int32_t> events;
	std::weak_ptr<PendingResourceProbe> pending;
	sgre::script::ScriptResourceBindings resources(vm, objects, psb, [&](std::string_view) {
		auto job = std::make_shared<PendingResourceProbe>(events);
		pending = job;

		return [job]() -> std::optional<sgre::script::ScriptResourceAsset> {
			return std::nullopt;
		};
	},
	    [](std::string_view, std::string_view) {
	    });
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	const auto derived = vm.CreateClass(resources.ResourceClass());
	const auto instance = vm.Invoke(derived, receiver).Value;
	const auto identity = vm.BorrowObjectIdentity(instance);
	static_cast<void>(Call(vm, instance, "load", std::string("pending.psb")));
	vm.NewSlot(vm.RootTable(), std::string("fixture-derived-resource"), instance);
	Require(objects.HasInstance(instance) && !pending.expired(), "derived Resource setup lost its pending native state");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(pending.expired() && events == std::vector<std::int32_t>{2} && !vm.ResolveObjectIdentity(identity), "Class finalization prevented derived Resource native/base/job cleanup");
}

int main(int argc, char** argv) {
	try {
		TestNativeLifecycle();
		TestCacheReferenceRetention();
		TestResourceVariants();
		TestCompiledExecution();
		TestFailureDiagnostic();
		TestReclaimedPendingAndFailure();
		TestReclaimedCachePins();
		TestForwardTargetRelease();
		TestResourceOwnerExpiry();
		TestNotificationReleasePreflight();
		TestDerivedResourceClose();
		if (argc == 2) {
			TestNativeFlagProjection(argv[1]);
		}
		std::cout << "Resource construction, loading, PSB ownership, unload, and failure diagnostics passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
