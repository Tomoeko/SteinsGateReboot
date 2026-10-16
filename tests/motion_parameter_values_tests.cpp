#include "sgre/scene/motion_parameter_values.hpp"

#include "sgre/scene/motion_player_commands.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_copy_constructible_v<sgre::scene::MotionParameterCell> && !std::is_move_constructible_v<sgre::scene::MotionParameterCell>);

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("invalid parameter registration or unsupported controller was accepted");
}

static sgre::resource::MotionTimeline Timeline(std::string name, float begin, float end, float division) {
	sgre::resource::MotionTimeline timeline;
	timeline.Name = std::move(name);
	timeline.LastTime = 201;
	timeline.Parameters = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("param"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(static_cast<double>(begin))}, {"rangeEnd", Atom(static_cast<double>(end))}, {"division", Atom(static_cast<double>(division))}})});

	return timeline;
}

static std::uint32_t Word(std::istream& input) {
	std::uint32_t word = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto value = input.get();
		Require(value != std::char_traits<char>::eof(), "short native variable-owner fixture");
		word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(value)) << shift;
	}

	return word;
}

static void TestActualLocalAndRegisteredViews() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("parameter view fixture has no texture input");
	},
	    [](auto) {
		    throw std::runtime_error("parameter view fixture has no texture owner");
	    });
	sgre::scene::MotionResourceManager manager(cache);
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "character";
	auto parent_timeline = Timeline("parent", -10, 10, 20);
	auto& definitions = std::get<sgre::resource::PsbArray>(parent_timeline.Parameters.Data);
	definitions.push_back(definitions[0]);
	auto first = definitions[0];
	for (auto& [name, value] : std::get<sgre::resource::PsbObject>(first.Data)) {
		if (name == "id") {
			value = Atom(std::string("alpha"));
		}
	}
	definitions.push_back(std::move(first));
	character.Timelines.push_back(std::move(parent_timeline));
	character.Timelines.push_back(Timeline("child", -10, 10, 20));
	resource->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = cache.AllocateDocumentIdentity();
	identity.RootOffset = 44;
	static_cast<void>(manager.Register(identity, resource, {}));
	sgre::scene::MotionPlayerCommands parent(manager);
	parent.SetCharacter("character");
	parent.Play("parent");
	auto parent_parameters = std::make_unique<sgre::scene::MotionTimelineParameters>(parent.ParameterValues(), *parent.CachedTimeline());
	parent.ParameterValues().WriteLocal("param", 5, 7);
	parent.ParameterValues().WriteLocal("not_registered", 42, 0);
	sgre::scene::MotionPlayerCommands child(manager, parent);
	child.SetCharacter("character");
	child.Play("child");
	auto child_parameters = std::make_unique<sgre::scene::MotionTimelineParameters>(child.ParameterValues(), *child.CachedTimeline());
	Require(child.ParameterValues().Read("param") == 5 && child.ParameterValues().ReadLocal("param") == 0 && child_parameters->Entries()[0].Position == 15, "native script local lookup differs from the genuine inherited parameter initializer");
	Require(parent.ParameterValues().RegisteredKeys() == std::vector<std::string>{"alpha", "param", "param", "param"} && child.ParameterValues().RegisteredKeys() == std::vector<std::string>{"param"}, "native key catalogue preserves sorted duplicate own/descendant registrations, excluding unrelated local map keys");
	child.ParameterValues().WriteLocal("param", -2, 3);
	parent.ParameterValues().WriteLocal("param", 8, 0x80000001u);
	Require(parent.ParameterValues().ReadLocal("param") == 8 && child.ParameterValues().ReadLocal("param") == -2 && child.ParameterValues().Read("param") == -2, "parent fanout must not overwrite a child's own script-local value");
	Require(child_parameters->Entries()[0].Position == 18 && child_parameters->Entries()[0].ControlFlags == 0x80000001u && parent_parameters->Entries()[0].ControlFlags == 0x80000001u && parent_parameters->Entries()[1].ControlFlags == 0x80000001u, "the exact immediate setter control word reaches every matching real parameter record");
	child_parameters.reset();
	Require(parent.ParameterValues().RegisteredKeys() == std::vector<std::string>{"alpha", "param", "param"} && child.ParameterValues().RegisteredKeys().empty(), "exact child retirement removes only its own duplicate catalogue entry");
	parent_parameters.reset();
	Require(parent.ParameterValues().RegisteredKeys().empty() && parent.ParameterValues().ReadLocal("not_registered") == 42, "parameter retirement preserves actual stored local values without advertising false keys");
	std::cout << "actual script-local lookup and duplicate registered parameter key views PASS\n";
}

static void TestClonedParameterRecordsOutliveSource() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("cloned parameter fixture has no texture input");
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "character";
	character.Timelines.push_back(Timeline("parent", 0, 10, 10));
	character.Timelines.push_back(Timeline("child", 0, 10, 10));
	resource->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = cache.AllocateDocumentIdentity();
	identity.RootOffset = 44;
	static_cast<void>(manager.Register(identity, resource, {}));
	std::unique_ptr<sgre::scene::MotionPlayerCommands> clone;
	std::unique_ptr<sgre::scene::MotionTimelineParameters> clone_parameters;
	std::uint64_t value_serial = 0;
	{
		sgre::scene::MotionPlayerCommands parent(manager);
		parent.SetCharacter("character");
		parent.Play("parent");
		auto parent_parameters = std::make_unique<sgre::scene::MotionTimelineParameters>(parent.ParameterValues(), *parent.CachedTimeline());
		sgre::scene::MotionPlayerCommands source(manager, parent);
		source.SetCharacter("character");
		source.Play("child");
		source.ParameterValues().WriteLocal("param", 1.5f, 1);
		auto source_parameters = std::make_unique<sgre::scene::MotionTimelineParameters>(source.ParameterValues(), *source.CachedTimeline());
		parent.ParameterValues().WriteLocal("param", 8, 0x80000007u);
		Require(source.ParameterValues().ReadLocal("param") == 1.5f && source_parameters->Entries()[0].Position == 8, "real parent fanout must distinguish current registered cell from stored local map before cloning");
		value_serial = source_parameters->ValueSerial();
		clone = std::make_unique<sgre::scene::MotionPlayerCommands>(manager);
		clone->CopyForClone(source);
		clone_parameters = std::make_unique<sgre::scene::MotionTimelineParameters>(clone->ParameterValues(), *source_parameters);
		Require(&clone_parameters->Entries()[0] != &source_parameters->Entries()[0] && clone_parameters->Entries()[0].Position == 8 && clone_parameters->Entries()[0].ControlFlags == 0x80000007u && clone_parameters->ValueSerial() == value_serial, "native parameter clone must copy real cell position/flags/serial into independent registered storage");
	}
	Require(clone->ParameterValues().ReadLocal("param") == 1.5f && clone_parameters->Entries()[0].Position == 8 && clone->ParameterValues().RegisteredParameterCount() == 1, "source-first retirement must preserve cloned scalar and independent parameter registration");
	clone->ParameterValues().WriteLocal("param", 3, 0x11);
	Require(clone_parameters->Entries()[0].Position == 3 && clone_parameters->Entries()[0].ControlFlags == 0x11 && clone_parameters->ValueSerial() > value_serial, "cloned cell must remain connected to its actual new command writer");
	clone_parameters.reset();
	Require(clone->ParameterValues().RegisteredKeys().empty(), "clone retirement must remove exactly its own stable parameter cells");
	clone.reset();
}

static void Replay(const char* path) {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("parameter fixture has no texture sources");
	},
	    [](auto) {
		    throw std::runtime_error("parameter fixture has no texture releases");
	    });
	sgre::scene::MotionResourceManager manager(cache);
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "character";
	character.Timelines.push_back(Timeline("parent", -100, 100, 200));
	character.Timelines.push_back(Timeline("child", -10, 10, 20));
	resource->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = cache.AllocateDocumentIdentity();
	identity.RootOffset = 44;
	const auto project = manager.Register(identity, resource, {});
	Require(manager.Contains(project) && cache.Count() == 0, "actual parameter fixture registration differs");
	sgre::scene::MotionPlayerCommands parent(manager);
	parent.SetCharacter("character");
	parent.Play("parent");
	Require(parent.ParameterValues().Read("param") == 0 && parent.ParameterValues().LocalValueCount() == 0, "actual root constructor did not own empty initial local values");
	auto parent_parameters = std::make_unique<sgre::scene::MotionTimelineParameters>(parent.ParameterValues(), *parent.CachedTimeline());
	Require(parent_parameters->Entries()[0].Position == 100 && parent_parameters->Entries()[0].ControlFlags == 0, "actual native parameter initializer differs");
	parent.ParameterValues().WriteLocal("param", 25, 2);
	Require(parent.ParameterValues().Read("param") == 25 && parent_parameters->Entries()[0].Position == 125 && parent_parameters->Entries()[0].ControlFlags == 2, "actual plain setter registration differs");
	sgre::scene::MotionPlayerCommands child(manager, parent);
	child.SetCharacter("character");
	child.Play("child");
	Require(child.ParameterValues().Read("param") == 25 && child.ParameterValues().LocalValueCount() == 0, "actual containing constructor or ancestor lookup differs");
	auto child_parameters = std::make_unique<sgre::scene::MotionTimelineParameters>(child.ParameterValues(), *child.CachedTimeline());
	Require(child_parameters->Entries()[0].Position == 20 && parent.ParameterValues().RegisteredParameterCount() == 2 && child.ParameterValues().RegisteredParameterCount() == 1, "actual child parameter references were not registered in self and parent");
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open() && Word(input) == 0x3156504d && Word(input) == 5, "wrong native variable-owner fixture");
	for (std::uint32_t operation = 0; operation < 5; ++operation) {
		std::array<std::uint32_t, 10> expected{};
		for (auto& word : expected) {
			word = Word(input);
		}
		auto& writer = expected[0] == 0 ? parent.ParameterValues() : child.ParameterValues();
		writer.WriteLocal("param", std::bit_cast<float>(expected[1]), expected[2]);
		Require(std::bit_cast<std::uint32_t>(parent.ParameterValues().Read("param")) == expected[3] && std::bit_cast<std::uint32_t>(child.ParameterValues().Read("param")) == expected[4], "native parent/local variable precedence differs");
		const auto& parent_cell = parent_parameters->Entries()[0];
		const auto& child_cell = child_parameters->Entries()[0];
		Require(std::bit_cast<std::uint32_t>(parent_cell.Position) == expected[5] && parent_cell.ControlFlags == expected[6] && std::bit_cast<std::uint32_t>(child_cell.Position) == expected[7] && child_cell.ControlFlags == expected[8], "native parameter fanout position or control flags differ");
		Require(parent.ParameterValues().RegisteredParameterCount() == expected[9], "native parameter reference count differs");
	}
	Require(input.get() == std::char_traits<char>::eof(), "native variable-owner fixture has trailing data");
	child.ParameterValues().WriteLocal("node::param", 1, 0);
	Require(child.ParameterValues().ReadLocal("node/param") == 1, "descriptor values retain their canonical map before a genuine target selection exists");
	RequireFailure([&] {
		sgre::scene::MotionTimelineParameters duplicate(child.ParameterValues(), *child.CachedTimeline());
	});
	const auto serial = child.SelectionSerial();
	child.Play("child", sgre::scene::MotionPlayFlags::Force);
	Require(child.SelectionSerial() == serial + 1, "same-pointer FORCE did not advance the actual preparation occurrence");
	RequireFailure([&] {
		static_cast<void>(child_parameters->Entries());
	});
	RequireFailure([&] {
		parent.ParameterValues().WriteLocal("param", 7, 0);
	});
	child_parameters.reset();
	Require(parent.ParameterValues().RegisteredParameterCount() == 1 && child.ParameterValues().RegisteredParameterCount() == 0 && child.ParameterValues().Read("param") == -3.5f, "exact-pointer child retirement changed the wrong registrations or local values");
	parent_parameters.reset();
	Require(parent.ParameterValues().RegisteredParameterCount() == 0 && parent.ParameterValues().Read("param") == 0, "native parameter retirement did not preserve the local-value map");
	child_parameters = std::make_unique<sgre::scene::MotionTimelineParameters>(child.ParameterValues(), *child.CachedTimeline());
	Require(child_parameters->Entries()[0].Range.Identifier == "param" && parent.ParameterValues().RegisteredParameterCount() == 1, "native record identifier ownership or refreshed registration differs");
	Require(manager.Unregister(project), "actual project could not retire for the metadata lifetime fixture");
	resource.reset();
	RequireFailure([&] {
		static_cast<void>(child_parameters->Entries());
	});
	RequireFailure([&] {
		static_cast<void>(child_parameters->Timeline());
	});
	RequireFailure([&] {
		parent.ParameterValues().WriteLocal("param", 7, 0);
	});
	child_parameters.reset();
	Require(parent.ParameterValues().RegisteredParameterCount() == 0 && child.ParameterValues().RegisteredParameterCount() == 0 && child.ParameterValues().Read("param") == -3.5f, "copied native identifiers did not retire safely after raw metadata release");
	std::size_t accepted_keys = 0;
	bool key_budget_rejected = false;
	const std::string key_prefix(65500, 'x');
	for (std::size_t index = 0; index < 256; ++index) {
		auto& values = index < 96 ? parent.ParameterValues() : child.ParameterValues();
		const auto count_before = values.LocalValueCount();
		try {
			values.WriteLocal(key_prefix + std::to_string(index), 1, 0);
			++accepted_keys;
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()).find("bounded actual command-root ownership") != std::string_view::npos && values.LocalValueCount() == count_before, "shared actual-root key limit did not reject before map mutation");
			key_budget_rejected = true;
			break;
		}
	}
	Require(key_budget_rejected && accepted_keys > 96 && accepted_keys < 256, "parameter key budget was not shared by the genuine parent/child owners");
	std::cout << "5 native setter/parent lookup/registration fanout records and actual typed retirement/metadata/key-budget guards PASS\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "motion_parameter_values_tests NATIVE_FIXTURE");
		TestActualLocalAndRegisteredViews();
		TestClonedParameterRecordsOutliveSource();
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
