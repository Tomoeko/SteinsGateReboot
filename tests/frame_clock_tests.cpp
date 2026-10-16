#include "sgre/core/frame_clock.hpp"
#include "sgre/script/system_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestClock() {
	sgre::core::FrameClock clock;
	Require(!clock.VariableFrame() && clock.Advance(100) == 1 && clock.VariableStep() == 6, "fixed mode changed its original one-tick step");
	clock.SetVariableFrame(true);
	Require(clock.Advance(200) == 6 && clock.ResidualTicks() == 0, "six logical ticks per hundred milliseconds changed");
	Require(clock.Advance(200) == 1 && clock.ResidualTicks() == -1, "minimum one-tick update changed");
	Require(clock.Advance(200) == 1 && clock.ResidualTicks() == -2, "negative residual bound changed");
	Require(clock.Advance(200) == 1 && clock.ResidualTicks() == -2, "residual underflow was not clamped");
	Require(clock.Advance(300) == 4 && clock.ResidualTicks() == 0, "residual did not repay prior minimum-tick updates");

	sgre::core::FrameClock wrap(0xfffffff0);
	wrap.SetVariableFrame(true);
	Require(wrap.Advance(0x100000010) == 2 && std::bit_cast<std::uint32_t>(wrap.ResidualTicks()) == 0xbda3d710, "unsigned millisecond wrap or independent float32 residual changed");
}

static void TestBindings() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::resource::ResourceCache resources(123);
	sgre::script::ScriptSystemBindings bindings(vm, clock, [] {
		sgre::platform::HostLocalDateTime time;
		time.Second = 12;
		time.Minute = 34;
		time.Hour = 15;
		time.MonthDay = 29;
		time.Month = 2;
		time.Year = 2024;
		time.WeekDay = 4;
		time.YearDay = 59;

		return time;
	});
	const auto system = bindings.SystemClass();
	bindings.ConfigureResourceCache(resources);
	const std::array<sgre::script::SquirrelValue, 1> receiver{system};
	Require(vm.Type(system) == sgre::script::SquirrelValueType::Class, "System is not the original static-method class form");
	Require(sgre::script::StringText(vm.Invoke(vm.GetSlot(system, std::string("getSpec")), receiver).Value) == "win", "game resource profile identity changed with the host operating system");
	Require(!std::get<bool>(vm.Invoke(vm.GetSlot(system, std::string("getDebugBuild")), receiver).Value), "provided release-game debug identity changed with the C++ build");
	const std::array<sgre::script::SquirrelValue, 2> enabled{system, true};
	static_cast<void>(vm.Invoke(vm.GetSlot(system, std::string("setVariableFrame")), enabled));
	Require(clock.VariableFrame() && std::get<bool>(vm.Invoke(vm.GetSlot(system, std::string("getVariableFrame")), receiver).Value), "script frame mode did not update the actual portable clock");
	bindings.ConfigureRenderTarget(3, 5);
	const auto bounds = vm.Invoke(vm.GetSlot(system, std::string("getScreenBounds")), receiver).Value;
	Require(vm.Type(bounds) == sgre::script::SquirrelValueType::Table && vm.Length(bounds) == 6, "bounds are not the original six-field table");
	Require(std::get<float>(vm.GetSlot(bounds, std::string("left"))) == -1.5f && std::get<float>(vm.GetSlot(bounds, std::string("bottom"))) == 2.5f, "bounds lost centered float32 half-pixel extents");
	Require(std::get<float>(vm.GetSlot(bounds, std::string("width"))) == 3 && std::get<float>(vm.GetSlot(bounds, std::string("height"))) == 5, "bounds do not describe the configured actual target");
	const auto date = vm.Invoke(vm.GetSlot(system, std::string("getLocalDateTime")), receiver).Value;
	Require(vm.Type(date) == sgre::script::SquirrelValueType::Table && vm.Length(date) == 8 && std::get<std::int32_t>(vm.GetSlot(date, std::string("mon"))) == 2 && std::get<std::int32_t>(vm.GetSlot(date, std::string("year"))) == 2024, "local date fields or integer types changed");
	Require(!vm.HasSlot(date, std::string("isdst")), "local date exposed an unregistered field");
	const std::array<sgre::script::SquirrelValue, 2> limit{system, std::int32_t(524288000)};
	static_cast<void>(vm.Invoke(vm.GetSlot(system, std::string("setResourceCacheLimitSize")), limit));
	Require(resources.Limit() == 524288000 && std::get<std::int32_t>(vm.Invoke(vm.GetSlot(system, std::string("getResourceCacheLimitSize")), receiver).Value) == 524288000, "cache configuration did not affect the actual resource owner");
	const auto retained = resources.Prefetch("raw");
	resources.Complete(retained, {1, 2}, 2);
	resources.Advance();
	const auto state = vm.Invoke(vm.GetSlot(system, std::string("getResourceState")), receiver).Value;
	Require(vm.Length(state) == 6 && std::get<std::int32_t>(vm.GetSlot(state, std::string("totalCacheSize"))) == 2 && std::get<std::int32_t>(vm.GetSlot(state, std::string("cacheEntryNum"))) == 1, "native resource-state table did not report retained data");
	static_cast<void>(vm.Invoke(vm.GetSlot(system, std::string("clearResourceCache")), receiver));
	Require(!resources.Contains("raw"), "script cache clearing did not release real retained data");
}

static void TestVariableFrameNativeAdapters() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	auto bindings = std::make_unique<sgre::script::ScriptSystemBindings>(vm, clock);
	const auto system = bindings->SystemClass();
	const auto setter = vm.GetSlot(system, "setVariableFrame");
	const auto getter = vm.GetSlot(system, "getVariableFrame");
	const std::array<std::pair<sgre::script::SquirrelValue, bool>, 12> vectors{{
	    {{}, false},
	    {false, false},
	    {true, true},
	    {std::int32_t{0}, false},
	    {std::int32_t{256}, true},
	    {std::int32_t{-1}, true},
	    {0.0f, false},
	    {-0.0f, false},
	    {0.25f, true},
	    {std::numeric_limits<float>::quiet_NaN(), true},
	    {vm.CreateString(""), true},
	    {vm.CreateTable(), true},
	}};
	for (const auto& [value, expected] : vectors) {
		const auto result = vm.Invoke(setter, std::array<sgre::script::SquirrelValue, 3>{system, value, false});
		Require(std::holds_alternative<std::monostate>(result.Value), "native System frame setter returns zero values and ignores extra operands");
		Require(clock.VariableFrame() == expected && std::get<bool>(vm.Invoke(getter, std::array<sgre::script::SquirrelValue, 2>{system, false}).Value) == expected, "native truth-test adapters read and write the actual same frame clock");
	}
	static_cast<void>(vm.Invoke(setter, std::array<sgre::script::SquirrelValue, 1>{system}));
	Require(clock.VariableFrame(), "without an explicit operand, original stack2 is the actual nonnull captured descriptor");
	bindings.reset();
	bool rejected = false;
	try {
		static_cast<void>(vm.Invoke(setter, std::array<sgre::script::SquirrelValue, 2>{system, false}));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("bindings have expired") != std::string_view::npos;
	}
	Require(rejected && clock.VariableFrame(), "expired native adapter cannot mutate its former borrowed clock");
}

int main() {
	try {
		TestClock();
		TestBindings();
		TestVariableFrameNativeAdapters();
		std::cout << "frame clock fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
