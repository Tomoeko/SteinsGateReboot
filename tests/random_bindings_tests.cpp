#include "sgre/script/random_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}
	throw std::runtime_error("Random fixture did not reject: " + std::string(expected));
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::initializer_list<sgre::script::SquirrelValue> tail = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{vm.RootTable()};
	arguments.insert(arguments.end(), tail);

	return vm.Invoke(class_value, arguments).Value;
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::string_view name, std::initializer_list<sgre::script::SquirrelValue> tail = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), tail);

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

struct NativeStep {
	std::uint32_t Method;
	std::uint32_t Maximum;
	std::uint32_t Result;
};

static std::uint32_t InvokeStep(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, const NativeStep& step) {
	Require(step.Method <= 1, "Random native fixture method is unsupported");
	const auto output = Call(vm, receiver, step.Method == 0 ? "random" : "intrandom", {std::bit_cast<std::int32_t>(step.Maximum), std::int32_t{777}});

	return step.Method == 0 ? std::bit_cast<std::uint32_t>(std::get<float>(output)) : std::bit_cast<std::uint32_t>(std::get<std::int32_t>(output));
}

struct NativeCase {
	std::uint32_t Seed;
	std::array<NativeStep, 8> Steps;
};

// Whole original seeded-generator/method captures, script-random-native-stage a03ffcfc.
static constexpr std::array<NativeCase, 8> kSeeded{{
    {0x00000000u, {{
                      {0u, 0x00000000u, 0x3f7e1558u},
                      {1u, 0x00000000u, 0x00000000u},
                      {0u, 0x00000001u, 0x3e3533a8u},
                      {1u, 0x00000001u, 0x00000000u},
                      {0u, 0x00000003u, 0x3f1d6686u},
                      {1u, 0x00000003u, 0x00000003u},
                      {0u, 0x00000010u, 0x3f7aed86u},
                      {1u, 0x00000010u, 0x00000001u},
                  }}},
    {0x00000001u, {{
                      {0u, 0x00000000u, 0x3f03e84au},
                      {1u, 0x00000000u, 0x00000000u},
                      {0u, 0x00000001u, 0x3f4e5f18u},
                      {1u, 0x00000001u, 0x00000000u},
                      {0u, 0x00000003u, 0x3d6fdfe0u},
                      {1u, 0x00000003u, 0x00000001u},
                      {0u, 0x00000010u, 0x3f4b5596u},
                      {1u, 0x00000010u, 0x00000007u},
                  }}},
    {0x00001571u, {{
                      {0u, 0x00000000u, 0x3e0ddae0u},
                      {1u, 0x00000000u, 0x00000000u},
                      {0u, 0x00000001u, 0x3f43f5dcu},
                      {1u, 0x00000001u, 0x00000001u},
                      {0u, 0x00000003u, 0x3c8d4b00u},
                      {1u, 0x00000003u, 0x00000003u},
                      {0u, 0x00000010u, 0x3f26000au},
                      {1u, 0x00000010u, 0x0000000au},
                  }}},
    {0xffffffffu, {{
                      {0u, 0x00000000u, 0x3f7cd346u},
                      {1u, 0x00000000u, 0x00000000u},
                      {0u, 0x00000001u, 0x3e0f6060u},
                      {1u, 0x00000001u, 0x00000001u},
                      {0u, 0x00000003u, 0x3f7c6f68u},
                      {1u, 0x00000003u, 0x00000000u},
                      {0u, 0x00000010u, 0x3eb94358u},
                      {1u, 0x00000010u, 0x00000000u},
                  }}},
    {0x80000000u, {{
                      {0u, 0x00000000u, 0x3f535234u},
                      {1u, 0x00000000u, 0x00000000u},
                      {0u, 0x00000001u, 0x3df17110u},
                      {1u, 0x00000001u, 0x00000001u},
                      {0u, 0x00000003u, 0x3d61c8a0u},
                      {1u, 0x00000003u, 0x00000001u},
                      {0u, 0x00000010u, 0x3ced2d00u},
                      {1u, 0x00000010u, 0x00000010u},
                  }}},
    {0xfffffffeu, {{
                      {0u, 0x00000000u, 0x3f14db34u},
                      {1u, 0x00000000u, 0x00000000u},
                      {0u, 0x00000001u, 0x3ede0044u},
                      {1u, 0x00000001u, 0x00000000u},
                      {0u, 0x00000003u, 0x3f635c6eu},
                      {1u, 0x00000003u, 0x00000001u},
                      {0u, 0x00000010u, 0x3e2412d0u},
                      {1u, 0x00000010u, 0x00000010u},
                  }}},
    {0x075bcd15u, {{
                      {0u, 0x00000000u, 0x3f4f7d70u},
                      {1u, 0x00000000u, 0x00000000u},
                      {0u, 0x00000001u, 0x3ef4b4c8u},
                      {1u, 0x00000001u, 0x00000001u},
                      {0u, 0x00000003u, 0x3f6422b4u},
                      {1u, 0x00000003u, 0x00000001u},
                      {0u, 0x00000010u, 0x3eb087b0u},
                      {1u, 0x00000010u, 0x0000000cu},
                  }}},
    {0x7fffffffu, {{
                      {0u, 0x00000000u, 0x3ed51fbcu},
                      {1u, 0x00000000u, 0x00000000u},
                      {0u, 0x00000001u, 0x3e57ece8u},
                      {1u, 0x00000001u, 0x00000000u},
                      {0u, 0x00000003u, 0x3e393ac8u},
                      {1u, 0x00000003u, 0x00000000u},
                      {0u, 0x00000010u, 0x3f43b5e0u},
                      {1u, 0x00000010u, 0x00000003u},
                  }}},
}};

// Whole original constructor plus typed wrappers, script-random-wrapper-native-stage ad89b974.
static constexpr std::array<std::array<NativeStep, 32>, 2> kNoArgument{{
    {{
        {0u, 0x00000000u, 0x3e79bd00u},
        {1u, 0x00000000u, 0x00000000u},
        {0u, 0x00000001u, 0x3f69b7cau},
        {1u, 0x00000001u, 0x00000001u},
        {0u, 0x000000ffu, 0x3f27ed38u},
        {1u, 0x000000ffu, 0x000000edu},
        {0u, 0x7fffffffu, 0x3f60cd5au},
        {1u, 0x7fffffffu, 0x6f5f5511u},
        {0u, 0xfffffffeu, 0x3dec5850u},
        {1u, 0xfffffffeu, 0x05e8c1d1u},
        {0u, 0x80000000u, 0x3f5d457au},
        {1u, 0x80000000u, 0x745dae3au},
        {0u, 0x00000000u, 0x3f2eeb80u},
        {1u, 0x00000000u, 0x00000000u},
        {0u, 0x00000001u, 0x3f713b2eu},
        {1u, 0x00000001u, 0x00000000u},
        {0u, 0x000000ffu, 0x3f4558e0u},
        {1u, 0x000000ffu, 0x000000e9u},
        {0u, 0x7fffffffu, 0x3e15a8e8u},
        {1u, 0x7fffffffu, 0x2afac4e4u},
        {0u, 0xfffffffeu, 0x3ec24750u},
        {1u, 0xfffffffeu, 0x0dc22d05u},
        {0u, 0x80000000u, 0x3eb72788u},
        {1u, 0x80000000u, 0x5d79e478u},
        {0u, 0x00000000u, 0x3e89dfd0u},
        {1u, 0x00000000u, 0x00000000u},
        {0u, 0x00000001u, 0x3f05a7a2u},
        {1u, 0x00000001u, 0x00000000u},
        {0u, 0x000000ffu, 0x3e1e7998u},
        {1u, 0x000000ffu, 0x000000a9u},
        {0u, 0x7fffffffu, 0x3f57ac68u},
        {1u, 0x7fffffffu, 0x2abaf911u},
    }},
    {{
        {0u, 0x00000000u, 0x3e6c62e0u},
        {1u, 0x00000000u, 0x00000000u},
        {0u, 0x00000001u, 0x3f6a6e94u},
        {1u, 0x00000001u, 0x00000000u},
        {0u, 0x000000ffu, 0x3e9d033cu},
        {1u, 0x000000ffu, 0x0000000fu},
        {0u, 0x7fffffffu, 0x3ee27ddcu},
        {1u, 0x7fffffffu, 0x2b75e762u},
        {0u, 0xfffffffeu, 0x3eb2bf84u},
        {1u, 0xfffffffeu, 0x6d2b6da5u},
        {0u, 0x80000000u, 0x3ef518acu},
        {1u, 0x80000000u, 0x69e65449u},
        {0u, 0x00000000u, 0x3f05c974u},
        {1u, 0x00000000u, 0x00000000u},
        {0u, 0x00000001u, 0x3f1c756eu},
        {1u, 0x00000001u, 0x00000001u},
        {0u, 0x000000ffu, 0x3f26870cu},
        {1u, 0x000000ffu, 0x00000044u},
        {0u, 0x7fffffffu, 0x3f23d514u},
        {1u, 0x7fffffffu, 0x37903360u},
        {0u, 0xfffffffeu, 0x3cad0800u},
        {1u, 0xfffffffeu, 0x11d5e76du},
        {0u, 0x80000000u, 0x3f24ce82u},
        {1u, 0x80000000u, 0x6430afb0u},
        {0u, 0x00000000u, 0x3f32dc16u},
        {1u, 0x00000000u, 0x00000000u},
        {0u, 0x00000001u, 0x3e7462e8u},
        {1u, 0x00000001u, 0x00000001u},
        {0u, 0x000000ffu, 0x3f14be42u},
        {1u, 0x000000ffu, 0x000000c2u},
        {0u, 0x7fffffffu, 0x3f3a5f82u},
        {1u, 0x7fffffffu, 0x70dee2fdu},
    }},
}};

static void FillSeed(std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords>& seed, std::uint32_t variant) {
	for (std::size_t index = 0; index < seed.size(); ++index) {
		const auto word = static_cast<std::uint32_t>(index);
		seed[index] = variant == 0 ? word : (word * 2654435761u) ^ 0x87654321u;
	}
}

static void TestNativeMixedOutputsAndContemporarySeed() {
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	FillSeed(seed, 0);
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptRandomBindings random(vm, objects, seed);
	Require(vm.GetSlot(vm.RootTable(), std::string("Random")) == random.RandomClass(), "Random must register its actual class in the root table");
	for (const auto& native : kSeeded) {
		const auto instance = Construct(vm, random.RandomClass(), {std::bit_cast<std::int32_t>(native.Seed), std::string("ignored tail")});
		Require(vm.IsInstanceOf(instance, objects.ObjectClass()) && objects.HasInstance(instance) && std::holds_alternative<std::monostate>(objects.Delegate(instance)), "Random must attach a real Object with an empty delegate, independent of seed arguments");
		for (const auto& step : native.Steps) {
			Require(InvokeStep(vm, instance, step) == step.Result, "Random seeded mixed native result differs");
		}
	}

	const auto first = Construct(vm, random.RandomClass());
	const auto sibling = Construct(vm, random.RandomClass());
	for (const auto& step : kNoArgument[0]) {
		Require(InvokeStep(vm, first, step) == step.Result && InvokeStep(vm, sibling, step) == step.Result, "unchanged actual seed buffer must initialize two independent equal Random streams");
	}
	const auto explicit_seed = Construct(vm, random.RandomClass(), {std::int32_t{1}});
	FillSeed(seed, 1);
	const auto second = Construct(vm, random.RandomClass());
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> original_seed{};
	FillSeed(original_seed, 0);
	sgre::scene::MotionSharedRandom previous(original_seed);
	for (std::size_t step = 0; step < kNoArgument[0].size(); ++step) {
		static_cast<void>(previous.NextWord());
	}
	const auto continued = previous.NextUnitFloat();
	Require(std::get<float>(Call(vm, first, "random")) == continued && std::get<float>(Call(vm, sibling, "random")) == continued, "changing the actual seed buffer must not change existing Random instance state");
	for (const auto& step : kNoArgument[1]) {
		Require(InvokeStep(vm, second, step) == step.Result, "no-argument construction must read the contemporary actual seed buffer rather than a binding-time copy");
	}
	Require(std::bit_cast<std::uint32_t>(std::get<float>(Call(vm, explicit_seed, "random"))) == kSeeded[1].Steps[0].Result, "explicit seed initialization must not depend on the contemporary 624-word buffer");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestTypedArgumentsAndWeakReference() {
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptRandomBindings random(vm, objects, seed);
	std::size_t conversions = 0;
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::string("_tostring"), vm.CreateNativeClosure("Random must not stringify", [&](auto&, auto) {
		++conversions;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("1"));
	}));
	const std::array<sgre::script::SquirrelValue, 11> values{std::int32_t{0}, std::int32_t{-2}, 2.7f, -2.7f, true, false, sgre::script::SquirrelValue{}, std::string("1"), std::bit_cast<float>(0x7fc00001u), std::numeric_limits<float>::infinity(), table};
	const std::array<std::uint32_t, 11> seeds{0u, 0xfffffffeu, 2u, 0xfffffffeu, 1u, 0u, 0u, 0u, 0x80000000u, 0x80000000u, 0u};
	for (std::size_t index = 0; index < values.size(); ++index) {
		const auto instance = Construct(vm, random.RandomClass(), {values[index], std::int32_t{12345}});
		sgre::scene::MotionSharedRandom expected(seeds[index]);
		for (std::size_t step = 0; step != 3; ++step) {
			Require(std::get<float>(Call(vm, instance, "random", {table, false})) == expected.NextUnitFloat(), "Random constructor native integer conversion or ignored random tail differs");
		}
	}

	const auto instance = Construct(vm, random.RandomClass(), {std::int32_t{1}});
	Require(std::get<std::int32_t>(Call(vm, instance, "intrandom")) == 0, "missing intrandom argument must initialize its native integer to zero and consume a sample");
	const std::array<std::uint32_t, 10> expected_integer{0u, 946286476u, 0u, 491263u, 1u, 0u, 0u, 0u, 1013994432u, 396591248u};
	for (std::size_t index = 0; index < expected_integer.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(std::get<std::int32_t>(Call(vm, instance, "intrandom", {values[index], std::int32_t{99}}))) == expected_integer[index], "whole native intrandom typed/default conversion result differs");
	}
	Require(conversions == 0, "Random native scalar conversion must not invoke _tostring");
	Require(vm.ResolveWeakReference(Call(vm, instance, "weakref")) == instance && std::get<std::int32_t>(Call(vm, instance, "weakref", {std::int32_t{37}})) == 37 && vm.ResolveWeakReference(Call(vm, instance, "weakref", {table})) == table, "Random weakref must observe the original final ordinary argument");
	const auto subclass = vm.CreateClass(random.RandomClass());
	const auto derived = Construct(vm, subclass, {std::int32_t{1}});
	Require(std::bit_cast<std::uint32_t>(std::get<float>(Call(vm, derived, "random"))) == kSeeded[1].Steps[0].Result, "Random derived class must inherit the genuine constructor and native methods");
	const auto integer = vm.GetSlot(instance, std::string("intrandom"));
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(integer, std::array<sgre::script::SquirrelValue, 2>{instance, std::int32_t{-1}}));
	},
	    "zero-divisor");
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(integer, std::array<sgre::script::SquirrelValue, 2>{table, std::int32_t{3}}));
	},
	    "genuine native instance");
	const auto uninitialized = vm.CreateInstance(random.RandomClass());
	RequireFailure([&] {
		static_cast<void>(Call(vm, uninitialized, "random"));
	},
	    "live native identity");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestObjectLifetimeCapacityAndRollback() {
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	std::vector<std::uint32_t> events;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptRandomBindings random(vm, objects, seed, 1);
	for (const auto maximum : {std::size_t{0}, std::size_t{65537}}) {
		RequireFailure([&] {
			sgre::script::ScriptRandomBindings invalid(vm, objects, seed, maximum);
		},
		    "bounded positive");
	}
	const auto subclass = vm.CreateClass(random.RandomClass());
	vm.NewSlot(subclass, std::string("destructor"), vm.CreateNativeClosure("Random script destructor", [&](auto&, auto arguments) {
		events.push_back(1);
		Require(std::get<float>(Call(vm, arguments[0], "random")) == std::bit_cast<float>(kSeeded[1].Steps[0].Result), "script destructor must run while its real native Random generator is live");

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	auto first = Construct(vm, subclass, {std::int32_t{1}});
	const auto identity = vm.BorrowObjectIdentity(first);
	Require(vm.RetainedReferenceCount(first) == 0, "Random native records must borrow instance identities rather than root them");
	auto first_root = vm.Retain(first);
	const auto listener = Construct(vm, objects.ObjectClass());
	const auto callback = vm.GetSlot(first, std::string("random"));
	objects.Subscribe(first, listener, [&, callback](const auto& receiver) {
		events.push_back(2);
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 1>{receiver}));
		},
		    "live native identity");

		return true;
	});
	RequireFailure([&] {
		static_cast<void>(Construct(vm, random.RandomClass(), {std::int32_t{0}}));
	},
	    "capacity");
	vm.ResetContext(vm.RootContext());
	first = {};
	first_root = {};
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::uint32_t>{1, 2} && !vm.ResolveObjectIdentity(identity), "actual Random destruction must free native state before Object notification and release its borrowed identity");
	const auto replacement = Construct(vm, random.RandomClass(), {std::int32_t{0}});
	const auto replacement_root = vm.Retain(replacement);
	Require(std::bit_cast<std::uint32_t>(std::get<float>(Call(vm, replacement, "random"))) == kSeeded[0].Steps[0].Result, "actual finalization must return Random capacity to an independent replacement");
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.GetSlot(random.RandomClass(), std::string("constructor")), std::array<sgre::script::SquirrelValue, 2>{replacement, std::int32_t{1}}));
	},
	    "already initialized");
	vm.Close();
	vm.RethrowReleaseFailure();

	sgre::script::SquirrelVm second_vm;
	sgre::script::ScriptObjectBindingLimits object_limits;
	object_limits.MaxInstances = 1;
	sgre::script::ScriptObjectBindings limited_objects(second_vm, object_limits);
	sgre::script::ScriptRandomBindings limited_random(second_vm, limited_objects, seed, 1);
	const auto occupied = Construct(second_vm, limited_objects.ObjectClass());
	RequireFailure([&] {
		static_cast<void>(Construct(second_vm, limited_random.RandomClass(), {std::int32_t{0}}));
	},
	    "Object instance limit");
	limited_objects.ReleaseInstance(occupied);
	const auto after_failure = Construct(second_vm, limited_random.RandomClass(), {std::int32_t{0}});
	Require(limited_objects.HasInstance(after_failure), "failed Object-base attachment must leave Random registry capacity available");
	second_vm.Close();
	second_vm.RethrowReleaseFailure();
}

class NativeFixtureReader {
public:
	explicit NativeFixtureReader(const char* path) {
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		Require(static_cast<bool>(input), "cannot open native Random fixture");
		const auto length = input.tellg();
		Require(length >= 12 && length <= 128 * 1024, "native Random fixture length is outside its bounded domain");
		Bytes.resize(static_cast<std::size_t>(length));
		input.seekg(0);
		Require(static_cast<bool>(input.read(Bytes.data(), static_cast<std::streamsize>(Bytes.size()))), "native Random fixture read is incomplete");
	}

	[[nodiscard]] std::uint32_t Word() {
		Require(Offset <= Bytes.size() && 4 <= Bytes.size() - Offset, "native Random fixture word is truncated");
		std::uint32_t word = 0;
		for (std::uint32_t byte = 0; byte < 4; ++byte) {
			word |= static_cast<std::uint32_t>(static_cast<unsigned char>(Bytes[Offset++])) << (byte * 8);
		}

		return word;
	}

	[[nodiscard]] bool Finished() const {
		return Offset == Bytes.size();
	}

private:
	std::vector<char> Bytes;
	std::size_t Offset = 0;
};

static void ReplayNativeFixture(const char* path) {
	NativeFixtureReader reader(path);
	Require(reader.Word() == 0x314e5253u, "native Random fixture magic differs from SRN1");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases > 0 && cases <= 16 && steps > 0 && steps <= 4096, "native Random fixture dimensions exceed their bounded domain");
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptRandomBindings random(vm, objects, seed);
	for (std::uint32_t index = 0; index < cases; ++index) {
		const auto word = reader.Word();
		sgre::scene::MotionSharedRandom expected(word);
		for (const auto state : expected.State()) {
			Require(reader.Word() == state, "seeded Random initialization differs from all624 native state words");
		}
		const auto instance = Construct(vm, random.RandomClass(), {std::bit_cast<std::int32_t>(word)});
		for (std::uint32_t step = 0; step < steps; ++step) {
			const NativeStep native{reader.Word(), reader.Word(), reader.Word()};
			Require(InvokeStep(vm, instance, native) == native.Result, "Random full native mixed-method result differs");
		}
	}
	Require(reader.Finished(), "native Random fixture contains an unconsumed tail");
	vm.Close();
	vm.RethrowReleaseFailure();
	std::cout << "RANDOM_NATIVE_REPLAY=PASS cases=" << cases << " calls=" << cases * steps << '\n';
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "Random fixture accepts at most one optional native SRN1 path");
		TestNativeMixedOutputsAndContemporarySeed();
		TestTypedArgumentsAndWeakReference();
		TestObjectLifetimeCapacityAndRollback();
		if (argc == 2) {
			ReplayNativeFixture(argv[1]);
		}
		std::cout << "Random native class, independent generators, typed wrappers and Object lifetime tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
