#include "sgre/input/function_keys.hpp"
#include "sgre/script/game_bindings.hpp"
#include "sgre/script/input_bindings.hpp"
#include "sgre/script/source_compiler.hpp"

#include "bootstrap_alt_enter_vectors.hpp"
#include "bootstrap_pitch_vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("missing expected bootstrap input rejection");
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, std::string name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());

	return vm.Invoke(vm.GetSlot(vm.RootTable(), name), arguments).Value;
}

static void TestOriginalPitchAndLooseWrapper() {
	sgre::script::SquirrelVm vm;
	sgre::script::InstallGameResourceProfile(vm);
	for (std::size_t index = 0; index != kNativePitchWords.size(); ++index) {
		const auto& words = kNativePitchWords[index];
		const auto expected = std::bit_cast<float>(words[1]);
		const auto actual = std::get<float>(Call(vm, "calcPitch", {std::bit_cast<float>(words[0])}));
		const auto message = "native calcPitch case " + std::to_string(index);
		if (std::isnan(expected)) {
			Require(std::isnan(actual), message + " must preserve unordered input rather than replace it with100");
		} else if (std::isinf(expected)) {
			Require(actual == expected, message + " infinity classification differs");
		} else if (expected == 0) {
			Require(std::bit_cast<std::uint32_t>(actual) == words[1], message + " zero sign differs");
		} else {
			// Preselected before vector execution; cross-CRT log fidelity is measured, not bit-identity claimed.
			const auto bound = 8.0f * std::numeric_limits<float>::epsilon() * std::max(1.0f, std::abs(expected));
			Require(std::isfinite(actual) && std::abs(actual - expected) <= bound, message + " exceeds preselected float bound");
		}
	}
	Require(std::get<float>(Call(vm, "calcPitch")) == 0, "missing pitch operand uses initialized native scalar zero");
	Require(std::get<float>(Call(vm, "calcPitch", {std::string("200")})) == 0, "native pitch must not parse a string or invoke ToString");
	Require(std::get<float>(Call(vm, "calcPitch", {std::int32_t{200}, std::string("ignored")})) == 1, "native wrapper accepts integer pitch and ignores extra operands");
	Require(std::get<float>(Call(vm, "calcPitch", {false})) == 0, "native sq_getfloat accepts boolean zero");
	Require(std::get<float>(Call(vm, "calcPitch", {true})) < -6, "native sq_getfloat converts true to1 rather than rejecting it");
	Require(std::isinf(std::get<float>(Call(vm, "calcPitch", {std::numeric_limits<float>::denorm_min()}))), "positive ratio underflow retains log0 rather than nonpositive-input fallback");
}

static void TestOriginalLineHelpers() {
	sgre::script::SquirrelVm vm;
	sgre::script::InstallGameResourceProfile(vm);
	const std::array<std::string, 6> inputs{"", "alpha", "\r", "a\r\nb\nc", "日本語\ntext\r\n", std::string("a\nb\0c\n", 7)};
	const std::array<std::int32_t, 6> counts{0, 0, 0, 2, 2, 1};
	const std::array<std::string, 6> removed{"", "alpha", "\r", "a\rbc", "日本語text\r", "ab"};
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		Require(std::get<std::int32_t>(Call(vm, "countCR", {inputs[index], std::int32_t{17}})) == counts[index], "countCR changed native LF-only/C-string semantics");
		Require(sgre::script::StringText(Call(vm, "removeCR", {inputs[index]})) == removed[index], "removeCR changed carriage returns or bytes beyond its C-string prefix");
	}
	std::size_t stringifications = 0;
	const auto type = vm.CreateClass();
	vm.NewSlot(type, std::string("_tostring"), vm.CreateNativeClosure("line fixture", [&](auto&, auto) {
		++stringifications;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("a\nb\0c\n", 7));
	}));
	const auto instance = vm.CreateInstance(type);
	Require(std::get<std::int32_t>(Call(vm, "countCR", {instance})) == 1 && stringifications == 1, "countCR did not stringify its actual operand exactly once");
	Require(sgre::script::StringText(Call(vm, "removeCR", {instance})) == "ab" && stringifications == 2, "removeCR repeated conversion or lost its C-string boundary");
	RequireFailure([&] {
		static_cast<void>(Call(vm, "countCR"));
	},
	    "original text operand");
}

static void TestActualSnapshotMappingAndFocus() {
	sgre::input::FunctionKeyState owner;
	Require(owner.Snapshot().Current == 0 && owner.Snapshot().Previous == 0, "native globals start zero before the first actual VSync sample");
	for (std::uint32_t key = 0; key != 256; ++key) {
		sgre::input::KeyboardSample sample;
		sample.Available = true;
		owner.Advance(sample);
		sample.VirtualKeys[key] = 0x80;
		owner.Advance(sample);
		const auto found = std::find_if(kNativeFunctionKeyWords.begin(), kNativeFunctionKeyWords.end(), [key](const auto& row) {
			return row[0] == key;
		});
		const auto expected = found == kNativeFunctionKeyWords.end() ? 0 : (*found)[1];
		Require(owner.Snapshot().Pressed(0xffffffffu) == expected, "actual high-bit one-hot sample differs from original VK mapping at " + std::to_string(key));
		owner.Advance(sample);
		Require(owner.Snapshot().Pressed(0xffffffffu) == 0, "held key is not a repeated rising edge");
		sample.VirtualKeys[key] = 1;
		owner.Advance(sample);
		Require(owner.Snapshot().Current == 0, "native sampler ignores OS toggle low bit");
	}
	sgre::input::KeyboardSample sample;
	sample.Available = true;
	sample.VirtualKeys[0x70] = 0x80;
	owner.Advance(sample);
	sample.Available = false;
	owner.Advance(sample);
	Require(owner.Snapshot().Current == 0 && owner.Snapshot().Previous == 1 && owner.Snapshot().Pressed(0xffffffffu) == 0, "unfocused frame still advances previous and clears current");
	owner.Advance(sample);
	sample.Available = true;
	owner.Advance(sample);
	Require(owner.Snapshot().Pressed(1) == 1, "actual focused key after an unfocused frame produces a fresh edge");
	std::exception_ptr failure;
	std::thread other([&] {
		try {
			static_cast<void>(owner.Snapshot());
		} catch (...) {
			failure = std::current_exception();
		}
	});
	other.join();
	Require(static_cast<bool>(failure), "snapshot must preserve actual serialized input-owner thread");
}

static void TestConnectedVmProviderAndRetirement() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::InstallGameResourceProfile(vm);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::input::KeyMappings mappings;
	sgre::input::FunctionKeyState owner;
	auto bindings = std::make_unique<sgre::script::ScriptInputBindings>(vm, objects, mappings);
	RequireFailure([&] {
		static_cast<void>(Call(vm, "funcKeyPressed", {std::int32_t{-1}}));
	},
	    "actual input snapshot provider");
	std::size_t reads = 0;
	bool reenter = false;
	bindings->ConfigureFunctionKeys([&] {
		++reads;
		if (reenter) {
			static_cast<void>(Call(vm, "funcKeyPressed", {std::int32_t{-1}}));
		}
		return owner.Snapshot();
	});
	RequireFailure([&] {
		bindings->ConfigureFunctionKeys([&] {
			return owner.Snapshot();
		});
	},
	    "one actual immutable");
	sgre::input::KeyboardSample sample;
	sample.Available = true;
	sample.VirtualKeys[0x70] = 0x80;
	sample.VirtualKeys[4] = 0x80;
	owner.Advance(sample);
	Require(std::get<std::int32_t>(Call(vm, "funcKeyPressed", {std::int32_t{-1}})) == 0x08000001, "real VM global reads actual function/middle-button snapshot");
	Require(std::get<std::int32_t>(Call(vm, "funcKeyPressed", {std::int32_t{1}})) == 1 && std::get<std::int32_t>(Call(vm, "funcKeyPressed", {true, std::string("ignored")})) == 1, "getter mask uses actual loose native conversion and ignores extras");
	Require(std::get<std::int32_t>(Call(vm, "funcKeyPressed")) == 0 && std::get<std::int32_t>(Call(vm, "funcKeyPressed", {std::string("1")})) == 0, "missing or nonnumeric mask defaults0 without string coercion");
	Require(reads == 5 && owner.Snapshot().Previous == 0, "getter reads never consume or advance current/previous state");
	reenter = true;
	RequireFailure([&] {
		static_cast<void>(Call(vm, "funcKeyPressed", {std::int32_t{-1}}));
	},
	    "nonrecursive");
	reenter = false;
	Require(std::get<std::int32_t>(Call(vm, "funcKeyPressed", {1.9f})) == 1, "failed provider call releases its reentry fence and native integer conversion truncates");
	owner.Advance(sample);
	Require(std::get<std::int32_t>(Call(vm, "funcKeyPressed", {std::int32_t{-1}})) == 0, "actual next input phase holds key without false press");
	bindings.reset();
	static_cast<void>(vm.CollectGarbage());
	RequireFailure([&] {
		static_cast<void>(Call(vm, "funcKeyPressed", {std::int32_t{-1}}));
	},
	    "live nonrecursive");
}

static void TestOriginalAltEnterFrames() {
	sgre::input::FunctionKeyState owner;
	for (const auto& words : kNativeAltEnterFrames) {
		sgre::input::KeyboardSample sample;
		sample.Available = words[0] != 0;
		sample.VirtualKeys[0x12] = static_cast<std::uint8_t>(words[1]);
		sample.VirtualKeys[0xa4] = static_cast<std::uint8_t>(words[2]);
		sample.VirtualKeys[0xa5] = static_cast<std::uint8_t>(words[3]);
		sample.VirtualKeys[0x0d] = static_cast<std::uint8_t>(words[4]);
		owner.Advance(sample);
		const auto actual = owner.Snapshot();
		Require(static_cast<std::uint32_t>(actual.AltEnterCurrent) == words[5] && static_cast<std::uint32_t>(actual.AltEnterPrevious) == words[6] && static_cast<std::uint32_t>(actual.AltEnterPressed()) == words[7], "actual AltEnter current/previous/rising edge differs from original instruction sequence");
	}
}

static void TestActualAltEnterOwnerAndRootBinding() {
	sgre::input::FunctionKeyState owner;
	Require(owner.Snapshot().AltEnterPressed() == 0, "native AltEnter globals initialize zero before first VSync");
	for (const auto alt : {std::uint8_t{0x12}, std::uint8_t{0xa4}, std::uint8_t{0xa5}}) {
		sgre::input::KeyboardSample sample;
		sample.Available = true;
		owner.Advance(sample);
		sample.VirtualKeys[alt] = 0x80;
		owner.Advance(sample);
		Require(owner.Snapshot().AltEnterPressed() == 0, "Alt alone must not become native AltEnter");
		sample.VirtualKeys[0x0d] = 0x80;
		owner.Advance(sample);
		Require(owner.Snapshot().AltEnterPressed() == 1 && owner.Snapshot().AltEnterPressed() == 1, "actual Enter with generic/left/right Alt gives one nonconsuming edge");
		owner.Advance(sample);
		Require(owner.Snapshot().AltEnterPressed() == 0, "holding AltEnter does not repeatedly toggle fullscreen");
		sample.Available = false;
		owner.Advance(sample);
		Require(owner.Snapshot().AltEnterPressed() == 0 && !owner.Snapshot().AltEnterCurrent, "unfocused VSync clears native combination while retaining its previous sample");
		sample.Available = true;
		owner.Advance(sample);
		Require(owner.Snapshot().AltEnterPressed() == 1, "real refocus samples a new AltEnter edge");
		sample.VirtualKeys[alt] = 1;
		owner.Advance(sample);
		Require(owner.Snapshot().AltEnterPressed() == 0, "native high-bit test ignores modifier toggle low bit");
	}
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::input::KeyMappings mappings;
	auto bindings = std::make_unique<sgre::script::ScriptInputBindings>(vm, objects, mappings);
	RequireFailure([&] {
		static_cast<void>(Call(vm, "altEnterPressed"));
	},
	    "actual input snapshot provider");
	std::size_t reads = 0;
	bool reenter = false;
	bindings->ConfigureFunctionKeys([&] {
		++reads;
		if (reenter) {
			static_cast<void>(Call(vm, "altEnterPressed"));
		}
		return owner.Snapshot();
	});
	sgre::input::KeyboardSample sample;
	sample.Available = true;
	sample.VirtualKeys[0x0d] = 0x80;
	sample.VirtualKeys[0xa5] = 0x80;
	owner.Advance(sample);
	Require(std::get<std::int32_t>(Call(vm, "altEnterPressed", {std::string("ignored"), vm.CreateTable()})) == 1, "native no-arg integer wrapper ignores explicit operands without coercion");
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	const auto hub = vm.Invoke(bindings->InputHubClass(), root_receiver).Value;
	const auto hub_pin = vm.Retain(hub);
	const auto program = sgre::script::CompileSquirrelSource(vm, "return altEnterPressed();", "actual-implicit-input-receiver");
	const auto closure = vm.CreateClosure(program);
	const std::array<sgre::script::SquirrelValue, 1> receiver{hub};
	Require(std::get<std::int32_t>(vm.Invoke(closure, receiver).Value) == 1 && reads == 2, "unqualified root global must resolve from actual InputHub this without adding a fake Hub property");
	reenter = true;
	RequireFailure([&] {
		static_cast<void>(Call(vm, "altEnterPressed"));
	},
	    "nonrecursive");
	reenter = false;
	Require(std::get<std::int32_t>(Call(vm, "altEnterPressed")) == 1, "failed provider read must release its reentry fence without consuming edge");
	bindings.reset();
	RequireFailure([&] {
		static_cast<void>(Call(vm, "altEnterPressed"));
	},
	    "live nonrecursive");
}

static std::uint32_t ReadLineVectorWord(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	input.read(reinterpret_cast<char*>(bytes.data()), 4);
	Require(static_cast<bool>(input), "native line fixture word is truncated");
	std::uint32_t result = 0;
	for (std::uint32_t index = 0; index < 4; ++index) {
		result |= static_cast<std::uint32_t>(bytes[index]) << (index * 8);
	}

	return result;
}

static std::string ReadLineVectorText(std::istream& input, std::uint32_t size) {
	Require(size <= 4096, "native line fixture text exceeds its bound");
	std::string result(size, '\0');
	input.read(result.data(), static_cast<std::streamsize>(size));
	Require(static_cast<bool>(input), "native line fixture text is truncated");

	return result;
}

static void TestNativeLineVectors(const char* filename) {
	std::ifstream input(filename, std::ios::binary);
	std::array<char, 8> signature{};
	input.read(signature.data(), 8);
	Require(input && std::string_view(signature.data(), signature.size()) == "SGRELF01", "native line fixture signature is invalid");
	const auto count = ReadLineVectorWord(input);
	Require(count > 0 && count <= 4096, "native line fixture count exceeds its bound");
	sgre::script::SquirrelVm vm;
	sgre::script::InstallGameResourceProfile(vm);
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto input_size = ReadLineVectorWord(input);
		const auto output_size = ReadLineVectorWord(input);
		const auto line_count = ReadLineVectorWord(input);
		Require(line_count <= input_size, "native line fixture count exceeds input length");
		const auto text = ReadLineVectorText(input, input_size);
		const auto expected = ReadLineVectorText(input, output_size);
		Require(std::get<std::int32_t>(Call(vm, "countCR", {text})) == static_cast<std::int32_t>(line_count), "countCR differs from unchanged original instructions");
		const auto actual = Call(vm, "removeCR", {text});
		Require(sgre::script::StringText(actual) == expected, "removeCR differs from unchanged original instructions");
	}
	Require(input.get() == std::char_traits<char>::eof(), "native line fixture contains trailing bytes");
	std::cout << count << " original line-helper vectors passed\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "bootstrap fixture accepts one optional native line vector file");
		TestOriginalPitchAndLooseWrapper();
		TestOriginalLineHelpers();
		if (argc == 2) {
			TestNativeLineVectors(argv[1]);
		}
		TestActualSnapshotMappingAndFocus();
		TestConnectedVmProviderAndRetirement();
		TestOriginalAltEnterFrames();
		TestActualAltEnterOwnerAndRootBinding();
		std::cout << "Native pitch vectors, actual function-key mapping/edges and connected VM/provider lifetime passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
