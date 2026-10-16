#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/script/system_bindings.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireError(const auto& operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual mask-mode rejection: " + std::string(fragment));
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription description;
	description.Width = 8;
	description.Height = 8;
	description.MaxVertices = 4;
	description.MaxIndices = 4;
	description.MaxDraws = 1;
	description.MaxTextures = 4;
	description.MaxPipelines = 2;
	description.MaxTextureBytes = 4096;

	return description;
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t operand = 0, std::uint8_t second = 0, std::uint8_t third = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = operand;
	instruction.Argument2 = second;
	instruction.Argument3 = third;

	return instruction;
}

static sgre::script::BytecodeProgram AuthoredStaticCall() {
	sgre::script::BytecodeProgram program;
	auto& function = program.MainFunction;
	function.SourceName = std::string("authored-mask-mode-fixture.nut");
	function.Name = std::string("main");
	function.Parameters.push_back(std::string("this"));
	function.StackSize = 6;
	function.Literals = {std::string("System"), std::string("setMaskMode"), std::string("getMaskMode")};
	function.Instructions = {Instruction(9, 1, 0, 0), Instruction(8, 2, 1, 1, 3), Instruction(2, 4, 1), Instruction(6, 5, 2, 3, 2), Instruction(8, 2, 2, 1, 3), Instruction(6, 5, 2, 3, 1), Instruction(19, 1, 5)};

	return program;
}

static void TestActualGraphicsOwner(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	sgre::core::FrameClock clock;
	sgre::script::SquirrelVm vm;
	auto bindings = std::make_unique<sgre::script::ScriptSystemBindings>(vm, clock);
	const auto system = bindings->SystemClass();
	const auto setter = vm.GetSlot(system, "setMaskMode");
	const auto getter = vm.GetSlot(system, "getMaskMode");
	Require(vm.GetSlot(vm.RootTable(), "System") == system && vm.Type(system) == sgre::script::SquirrelValueType::Class, "mask methods must belong to the actual native System class");
	Require(!vm.HasSlot(vm.RootTable(), "setMaskMode") && !vm.HasSlot(vm.RootTable(), "getMaskMode"), "mask methods were incorrectly registered as replacement globals");
	Require(graphics.MaskMode() == 0, "native4c4662 initial graphics mask-mode word differs");
	RequireError([&] {
		static_cast<void>(vm.Invoke(setter, std::array<sgre::script::SquirrelValue, 2>{system, std::int32_t{1}}));
	},
	    "actual shared graphics owner");
	RequireError([&] {
		static_cast<void>(vm.Invoke(getter, std::array<sgre::script::SquirrelValue, 1>{system}));
	},
	    "actual shared graphics owner");
	Require(graphics.MaskMode() == 0, "unconfigured binding mutated or invented graphics state");
	bindings->ConfigureMaskMode([&graphics](auto value) {
		graphics.SetMaskMode(value);
	},
	    [&graphics] {
		    return graphics.MaskMode();
	    });
	RequireError([&] {
		bindings->ConfigureMaskMode([](auto) {}, [] {
			return 99u;
		});
	},
	    "one complete actual graphics owner");
	const std::array<std::pair<sgre::script::SquirrelValue, std::uint32_t>, 13> values{{
	    {std::int32_t{0}, 0},
	    {std::int32_t{1}, 1},
	    {std::int32_t{7}, 7},
	    {std::int32_t{-1}, 0xffffffffu},
	    {std::numeric_limits<std::int32_t>::min(), 0x80000000u},
	    {false, 0},
	    {true, 1},
	    {3.75f, 3},
	    {-3.75f, 0xfffffffdu},
	    {std::numeric_limits<float>::quiet_NaN(), 0x80000000u},
	    {vm.CreateString("1"), 0},
	    {vm.CreateTable(), 0},
	    {{}, 0},
	}};
	for (const auto& [value, expected] : values) {
		const auto result = vm.Invoke(setter, std::array<sgre::script::SquirrelValue, 3>{system, value, std::int32_t{123}});
		Require(std::holds_alternative<std::monostate>(result.Value), "native mask setter must return no values");
		const auto observed = vm.Invoke(getter, std::array<sgre::script::SquirrelValue, 2>{system, false}).Value;
		Require(graphics.MaskMode() == expected && std::bit_cast<std::uint32_t>(std::get<std::int32_t>(observed)) == expected, "static methods do not address the same actual graphics word or original integer conversion");
	}
	graphics.SetMaskMode(99);
	static_cast<void>(vm.Invoke(setter, std::array<sgre::script::SquirrelValue, 1>{system}));
	Require(graphics.MaskMode() == 0, "missing operand must read the real captured userdata at stack2 and retain native zero conversion fallback");
	const auto program = vm.CreateClosure(AuthoredStaticCall());
	const auto executed = vm.Invoke(program, std::array<sgre::script::SquirrelValue, 1>{vm.RootTable()});
	Require(executed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(executed.Value) == 1 && graphics.MaskMode() == 1, "authored script static lookup/call did not reach the actual graphics owner");
	const auto primary = graphics.PrimaryTarget();
	const auto secondary = graphics.AllocateTarget(Description());
	static_cast<void>(graphics.SelectTarget(primary));
	static_cast<void>(graphics.SelectTarget(secondary));
	Require(graphics.MaskMode() == 1, "target selection reset shared graphics mask-mode state");
	graphics.ConfigureOrdinaryMaskControl(-0.0f, 0.25f);
	Require(graphics.MaskMode() == 1, "independent operation/threshold control overwrote the distinct mode word");
	bool rejected_write = false;
	bool rejected_read = false;
	std::thread foreign([&] {
		try {
			graphics.SetMaskMode(17);
		} catch (const std::runtime_error&) {
			rejected_write = true;
		}
		try {
			static_cast<void>(graphics.MaskMode());
		} catch (const std::runtime_error&) {
			rejected_read = true;
		}
	});
	foreign.join();
	Require(rejected_write && rejected_read && graphics.MaskMode() == 1, "foreign thread mutated/read the actual serialized graphics owner");
	bindings.reset();
	RequireError([&] {
		static_cast<void>(vm.Invoke(setter, std::array<sgre::script::SquirrelValue, 2>{system, std::int32_t{0}}));
	},
	    "bindings have expired");
	Require(graphics.MaskMode() == 1, "expired retained static method mutated its former graphics borrower");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "system_mask_mode_tests SPRITE_SHADER");
		TestActualGraphicsOwner(argv[1]);
		std::cout << "PASS actual static System mask mode, native numeric/failure/missing conversions, shared graphics state and lifetimes; no stencil/ping-pong GPU execution claim\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
