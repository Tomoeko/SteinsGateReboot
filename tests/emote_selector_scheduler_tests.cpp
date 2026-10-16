#include "sgre/scene/emote_selector_control.hpp"

#include "fixtures/emote_selector_scheduler_native.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool value, std::string_view message) {
	if (!value) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("selector accepted an invalid value or reentrant mutation");
}

class VariableIo final : public sgre::scene::EmoteVariableIo {
public:
	mutable std::vector<selector_fixture::Event> Events;
	std::map<std::string, float, std::less<>> Values;
	sgre::scene::EmoteSelectorControl* Reenter = nullptr;

	static std::uint32_t Name(std::string_view value) {
		Require(value == "value" || value == "now", "native option order/name changed");

		return value == "value" ? 0u : 1u;
	}

	float Read(std::string_view identifier) const override {
		const auto found = Values.find(identifier);
		const auto current = found == Values.end() ? 0.0f : found->second;
		Events.push_back({0, Name(identifier), std::bit_cast<std::uint32_t>(current), 0, 0});

		return current;
	}

	void Set(std::string_view identifier, float value, float duration, float easing) override {
		if (Reenter != nullptr) {
			RequireFailure([&] {
				Reenter->Set(1, 2, 0, true);
			});
			RequireFailure([&] {
				Reenter->Update(1);
			});
		}
		Events.push_back({1, Name(identifier), std::bit_cast<std::uint32_t>(value), std::bit_cast<std::uint32_t>(duration), std::bit_cast<std::uint32_t>(easing)});
		Values.insert_or_assign(std::string(identifier), value);
	}
};

static sgre::resource::EmoteSelectorDefinition Definition() {
	sgre::resource::EmoteSelectorDefinition definition;
	definition.Label = "choice";
	definition.Options.push_back({"value", -3.25f, 7.5f, false, {{1, 2}}});
	definition.Options.push_back({"now", 2.75f, -1.5f, true, {{-1, 0}}});
	definition.Options.push_back({"", 0, 100, false, {{0, 99}}});

	return definition;
}

static void TestNativeScheduler() {
	VariableIo io;
	std::unique_ptr<sgre::scene::EmoteSelectorControl> selector;
	std::size_t compared_events = 0;
	for (const auto& phase : selector_fixture::kPhases) {
		io.Events.clear();
		if (phase.Operation == 3) {
			selector.reset();
			io.Values.clear();
			selector = std::make_unique<sgre::scene::EmoteSelectorControl>(Definition(), io);
		} else if (phase.Operation == 0) {
			selector->Set(std::bit_cast<float>(phase.Value), std::bit_cast<float>(phase.Duration), std::bit_cast<float>(phase.Easing), phase.Append != 0);
		} else if (phase.Operation == 1) {
			selector->Update(std::bit_cast<float>(phase.Value));
		} else {
			selector->FinishTransitions();
		}
		const auto state = selector->TransitionState();
		Require(selector->Selection() == phase.Selection && state.Queued == phase.Queued && state.Active == (phase.Active != 0), "native selection/FIFO/transition state differs");
		Require(std::bit_cast<std::uint32_t>(state.ReciprocalDuration) == phase.Reciprocal && std::bit_cast<std::uint32_t>(state.Progress) == phase.Progress, "native reciprocal/progress arithmetic differs");
		Require(io.Events.size() == phase.EventCount, "actual read/set callback count differs from native");
		for (std::size_t index = 0; index < io.Events.size(); ++index) {
			const auto& actual = io.Events[index];
			const auto& expected = selector_fixture::kEvents[phase.EventBegin + index];
			Require(actual.Write == expected.Write && actual.Name == expected.Name && actual.Value == expected.Value && actual.Duration == expected.Duration && actual.Easing == expected.Easing, "native callback order/float selection/on-off/immediate duration/easing differs");
			++compared_events;
		}
	}
	std::cout << "native selector8 constructors/34 queued phases and " << compared_events << " ordered IO events passed\n";
}

static void TestFiniteAndReentrantBoundaries() {
	VariableIo io;
	sgre::scene::EmoteSelectorControl selector(Definition(), io);
	selector.Set(1, 3, 0);
	const auto pending = selector.TransitionState();
	RequireFailure([&] {
		selector.Set(std::numeric_limits<float>::quiet_NaN(), 1, 0);
	});
	RequireFailure([&] {
		selector.Set(1, std::numeric_limits<float>::denorm_min(), 0);
	});
	RequireFailure([&] {
		selector.Set(2147483648.0f, 1, 0);
	});
	Require(selector.TransitionState().Queued == pending.Queued && selector.Selection() == 0, "invalid preflight changed the actual queued selection");
	io.Reenter = &selector;
	selector.Update(0);
	Require(selector.Selection() == 1 && selector.TransitionState().Queued == 0 && selector.TransitionState().Progress == 0, "reentrant callback published another queued target or advanced zero time");
	io.Reenter = nullptr;
	selector.Set(-1, 0, 9, true);
	Require(selector.Selection() == -1 && !selector.TransitionState().Active && selector.TransitionState().Queued == 0, "immediate setter failed to cancel active and appended work");
}

int main() {
	try {
		TestNativeScheduler();
		TestFiniteAndReentrantBoundaries();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
