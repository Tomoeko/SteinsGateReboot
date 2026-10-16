#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported stencil owner operation was accepted");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue atom;
	atom.Data = std::move(value);

	return atom;
}

class Reader {
public:
	explicit Reader(std::istream& stream) : m_Stream(stream) {
	}
	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		Require(value != std::char_traits<char>::eof(), "short native stencil fixture");

		return static_cast<std::uint8_t>(value);
	}
	std::uint32_t Word() {
		std::uint32_t word = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			word |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return word;
	}

private:
	std::istream& m_Stream;
};

struct InputControl {
	std::uint32_t Kind = 0;
	bool Enabled = false;
	bool Reverse = false;
	float Threshold = 0;
	float Softness = 0;
	std::optional<std::uint32_t> EasingIndex;
};

static sgre::resource::MotionLayer Layer(std::uint32_t kind, std::string label) {
	sgre::resource::MotionLayer layer;
	layer.Type = kind;
	layer.Label = std::move(label);
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Properties.emplace_back("parameterize", Atom(std::monostate{}));
	layer.Properties.emplace_back("meshTransform", Atom(std::int64_t{0}));
	layer.Properties.emplace_back("joinTarget", Atom(false));
	layer.Properties.emplace_back("groundCorrection", Atom(false));
	layer.Properties.emplace_back("objTriPriority", Atom(std::int64_t{-1}));
	layer.Properties.emplace_back("motionIndependentLayerInherit", Atom(false));
	for (const auto time : {0.0f, 12.0f}) {
		sgre::resource::MotionFrame frame;
		frame.Time = time;
		layer.Frames.push_back(frame);
	}

	return layer;
}

static sgre::resource::MotionTimeline Timeline(std::uint32_t stencil_type, bool map_empty, const std::vector<std::string>& mask_names, const std::array<InputControl, 4>& inputs) {
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "main";
	timeline.LastTime = 12;
	sgre::resource::PsbObject map;
	if (!map_empty) {
		map = {{"n1", Atom(std::int64_t{0})}, {"n2", Atom(std::int64_t{1})}, {"n3", Atom(std::int64_t{2})}, {"mask", Atom(std::int64_t{3})}};
	}
	timeline.LayerIndexMap = Atom(std::move(map));
	timeline.Layers = {Layer(0, "n1"), Layer(3, "n2"), Layer(2, "n3"), Layer(12, "mask")};
	auto& stencil = timeline.Layers.back();
	stencil.Properties.emplace_back("stencilType", Atom(static_cast<std::int64_t>(stencil_type)));
	sgre::resource::PsbArray names;
	for (const auto& name : mask_names) {
		names.push_back(Atom(name));
	}
	stencil.Properties.emplace_back("stencilCompositeMaskLayerList", Atom(std::move(names)));
	stencil.Frames.clear();
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		const auto& control = inputs[index];
		sgre::resource::MotionFrame frame;
		frame.Time = static_cast<float>(index) * 4;
		frame.Type = control.Kind;
		sgre::resource::PsbObject stc = {{"wrv", Atom(control.Reverse)}, {"wcc", control.EasingIndex ? Atom(static_cast<std::int64_t>(*control.EasingIndex)) : Atom(std::monostate{})}, {"wrt", Atom(static_cast<double>(control.Threshold))}, {"wsf", Atom(static_cast<double>(control.Softness))}};
		const auto mask = control.Enabled ? std::int64_t{0x20000000} : 0;
		frame.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(mask)}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"stc", Atom(std::move(stc))}});
		stencil.Frames.push_back(std::move(frame));
	}

	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2}), Atom(std::int64_t{3})});
	timeline.Priority.push_back(priority);
	priority.Time = 12;
	priority.Content = Atom(std::monostate{});
	timeline.Priority.push_back(priority);

	return timeline;
}

static std::shared_ptr<sgre::resource::MotionResource> Resource(sgre::resource::MotionTimeline timeline, bool with_easing = false) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject object;
	object.Name = "a";
	object.Timelines.push_back(std::move(timeline));
	resource->Objects.push_back(std::move(object));
	if (with_easing) {
		sgre::resource::EasingCurve first;
		first.Segments = {{{0, 0, 0}, {0.5f, 0.25f, 2}, {1, 1, 0}}};
		sgre::resource::EasingCurve second;
		second.Segments = {{{0, 0, 0}, {0.5f, 0.75f, -2}, {1, 1, 0}}};
		resource->Easing = {std::move(first), std::move(second)};
	}

	return resource;
}

static sgre::scene::TextureCache SourceFreeCache() {
	return sgre::scene::TextureCache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("stencil fixture unexpectedly uploaded a texture");
	},
	    [](auto) {
		    throw std::runtime_error("stencil fixture unexpectedly released a texture");
	    });
}

static sgre::scene::MotionProjectId Register(sgre::scene::MotionResourceManager& manager, const std::shared_ptr<const sgre::resource::MotionResource>& resource) {
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	identity.RootOffset = 128;

	return manager.Register(identity, resource, {});
}

static sgre::scene::MotionChildConstructionServices Construction() {
	sgre::scene::MotionChildConstructionServices services;
	services.ReadManagerRandom = [](const auto&) {
		return 0.5f;
	};

	return services;
}

static sgre::scene::MotionChildFrameSlotServices Slots() {
	sgre::scene::MotionChildFrameSlotServices services;
	services.EnqueueAction = [](auto, auto) {
		throw std::runtime_error("action-free stencil fixture queued an action");
	};

	return services;
}

static void Replay(const char* path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.is_open(), "cannot read stencil native fixture");
	Reader reader(stream);
	const std::array<std::uint8_t, 4> magic{reader.Byte(), reader.Byte(), reader.Byte(), reader.Byte()};
	const bool with_easing = magic == std::array<std::uint8_t, 4>{'M', 'S', 'W', '1'};
	Require(with_easing || magic == std::array<std::uint8_t, 4>{'M', 'S', 'M', '2'}, "wrong stencil native fixture magic");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases > 0 && cases <= 64 && steps <= 32, "native stencil fixture exceeds bounds");
	const std::array<std::string, 12> strings = {"", "n1", "n2", "n3", "atlas", "tile", "missing", "act0", "act1", "act2", "mask", std::string("n1\0ignored", 10)};
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		const auto stencil_type = reader.Word();
		const bool map_empty = reader.Word() != 0;
		const auto name_count = reader.Word();
		const auto member_count = reader.Word();
		Require(name_count <= 16 && member_count <= 16, "native mask fixture list exceeds bounds");
		std::vector<std::string> names;
		for (std::uint32_t index = 0; index < name_count; ++index) {
			const auto name = reader.Word();
			Require(name < strings.size(), "native stencil name index exceeds strings");
			names.push_back(strings[name]);
		}
		std::vector<std::uint32_t> members;
		for (std::uint32_t index = 0; index < member_count; ++index) {
			members.push_back(reader.Word());
		}
		std::array<bool, 4> markers{};
		for (auto& marker : markers) {
			marker = reader.Byte() != 0;
		}
		std::array<std::uint32_t, 4> row_ordinals{};
		for (auto& ordinal : row_ordinals) {
			ordinal = reader.Word();
		}
		std::array<InputControl, 4> controls{};
		for (auto& control : controls) {
			control.Kind = reader.Word();
			control.Enabled = reader.Word() != 0;
			control.Reverse = reader.Word() != 0;
			control.Threshold = std::bit_cast<float>(reader.Word());
			control.Softness = std::bit_cast<float>(reader.Word());
			if (with_easing) {
				const auto easing = reader.Word();
				if (easing != std::numeric_limits<std::uint32_t>::max()) {
					control.EasingIndex = easing;
				}
			}
		}
		const auto resource = Resource(Timeline(stencil_type, map_empty, names, controls), with_easing);
		auto cache = SourceFreeCache();
		sgre::scene::MotionResourceManager manager(cache);
		const auto registration = Register(manager, resource);
		sgre::scene::MotionPlayerCommands commands(manager);
		commands.SetCharacter("a");
		commands.Play("main");
		const auto& timeline = *commands.CachedTimeline();
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
		sgre::scene::MotionRootScene root;
		const sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionLayerIdentifierPool identifiers;
		{
			sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, Construction(), Slots());
			auto& mask = selected.StencilMask(3);
			Require(mask.MaskLayersBound() && mask.MaskLayers().size() == members.size(), "native stencil list owner/count differs");
			Require(selected.DrawRowCount() == 3 && selected.SpriteCount() == 1 && selected.KindMask() == 4109 && selected.Nodes()[3].DrawRow == 2, "native kind12 actual drawing-row allocation differs");
			for (std::size_t index = 0; index < members.size(); ++index) {
				Require(members[index] < selected.Nodes().size() && mask.MaskLayers()[index] == &selected.Nodes()[members[index]], "native stencil mask node identity/order/duplicates differ");
			}
			for (std::size_t index = 0; index < markers.size(); ++index) {
				Require(selected.Nodes()[index].CompositeMask == markers[index], "native mask membership marker differs");
				Require(selected.Nodes()[index].DrawRow.value_or(0xffffffffu) == row_ordinals[index], "native selected drawing row identity differs");
			}
			RequireFailure([&] {
				static_cast<void>(mask.Summary());
			},
			    "initial numeric");
			for (std::uint32_t step = 0; step < steps; ++step) {
				const auto position = std::bit_cast<float>(reader.Word());
				const auto traversal = reader.Word();
				const auto slot = reader.Word();
				const bool dirty = reader.Word() != 0;
				const bool force = reader.Word() != 0;
				const bool expected_changed = reader.Word() != 0;
				const auto current = reader.Word();
				const auto following = reader.Word();
				Require(traversal <= 2 && current + 1 == following, "native fixture lost its actual adjacent decoded pair");
				const auto preparations = reader.Word();
				Require(preparations <= 8, "native frame decoder trace exceeds bounded frames");
				for (std::uint32_t decode = 0; decode < preparations; ++decode) {
					const auto decoded_slot = reader.Word();
					const auto decoded_frame = reader.Word();
					mask.PrepareFrame(decoded_slot, decoded_frame);
				}
				for (std::size_t decoded_slot = 0; decoded_slot < 2; ++decoded_slot) {
					const auto enabled = reader.Word();
					const auto reverse = reader.Word();
					const auto threshold = reader.Word();
					const auto softness = reader.Word();
					const auto easing = with_easing ? reader.Word() : std::numeric_limits<std::uint32_t>::max();
					const auto& actual = mask.Control(decoded_slot);
					Require(actual.Enabled == (enabled != 0) && actual.ReverseWrite == (reverse != 0) && std::bit_cast<std::uint32_t>(actual.Threshold) == threshold && std::bit_cast<std::uint32_t>(actual.Softness) == softness && actual.EasingIndex.value_or(std::numeric_limits<std::uint32_t>::max()) == easing, "native retained control-slot decode differs");
				}
				const auto enabled = reader.Word();
				const auto reverse = reader.Word();
				const auto scale = reader.Word();
				const auto offset = reader.Word();
				try {
					Require(mask.Evaluate(*resource, position, slot, force, dirty) == expected_changed, "native stencil numeric changed gate differs");
					const auto& summary = mask.Summary();
					Require(summary.Enabled == (enabled != 0) && summary.ReverseWrite == (reverse != 0) && std::bit_cast<std::uint32_t>(summary.Scale) == scale && std::bit_cast<std::uint32_t>(summary.Offset) == offset, "native retained stencil summary differs");
				} catch (const std::exception& error) {
					throw std::runtime_error("stencil native case " + std::to_string(case_index) + " step " + std::to_string(step) + ": " + error.what());
				}
			}
		}
		Require(identifiers.LiveCount() == 0 && manager.ReferenceCount(registration) == 1, "selected stencil retirement must release real IDs and preserve enclosing resource ownership");
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "stencil native fixture has unexpected trailing bytes");
	std::cout << cases * steps << " original selected stencil summaries and " << cases << " real mask owners matched\n";
}

static void TestRetainedWcc() {
	// Expected coefficients are original50bc40 outputs from cases1/3, step1 of the MSW1 witness.
	static constexpr std::array<std::array<std::uint32_t, 2>, 2> kExpected{{{1104298072u, 3225627752u}, {1071083356u, 3175411712u}}};
	for (std::size_t test = 0; test < kExpected.size(); ++test) {
		std::array<InputControl, 4> inputs{};
		inputs[0].Kind = 3;
		inputs[0].Enabled = true;
		inputs[0].Reverse = true;
		inputs[0].Threshold = test == 0 ? 0.25f : 1.0f;
		inputs[0].Softness = test == 0 ? 0.0625f : 1.0f;
		inputs[0].EasingIndex = 1;
		inputs[1].Kind = 3;
		inputs[1].Enabled = test != 0;
		inputs[1].Threshold = test == 0 ? 0.75f : -0.5f;
		inputs[1].Softness = test == 0 ? 0.5f : 0.0f;
		inputs[1].EasingIndex = 0;
		inputs[2].Kind = 2;
		inputs[2].Enabled = true;
		inputs[2].Threshold = test == 0 ? 1.0f : 1.5f;
		inputs[2].Softness = test == 0 ? 1.0f : 0.0625f;
		const auto resource = Resource(Timeline(5, false, {}, inputs), true);
		const auto& timeline = resource->Objects[0].Timelines[0];
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
		sgre::scene::MotionStencilMaskOwner owner(timeline.Layers[3], prepared.Layers[3], *resource);
		owner.PrepareFrame(0, 0);
		owner.PrepareFrame(1, 1);
		Require(owner.Control(0).EasingIndex == 1 && owner.Control(1).EasingIndex.has_value() == (test != 0), "visible stencil slots must retain their actual curve while disabled slots clear it");
		Require(owner.Evaluate(*resource, 1, 0, false, true), "actual eased stencil interval must publish");
		const auto summary = owner.Summary();
		Require(summary.Enabled && summary.ReverseWrite && std::bit_cast<std::uint32_t>(summary.Scale) == kExpected[test][0] && std::bit_cast<std::uint32_t>(summary.Offset) == kExpected[test][1], "original eased stencil coefficient words differ");
		const auto retained = owner.Control(1);
		owner.PrepareFrame(1, 3);
		Require(owner.Control(1).EasingIndex == retained.EasingIndex && owner.Control(1).Enabled == retained.Enabled && owner.Control(1).Threshold == retained.Threshold, "hidden stencil slot must preserve its actual retained easing/control");
		owner.PrepareFrame(0, 2);
		Require(!owner.Control(0).EasingIndex, "visible null wcc must release the previous physical-slot curve");
		auto foreign = *resource;
		RequireFailure([&] {
			static_cast<void>(owner.Evaluate(foreign, 1, 0, true, true));
		},
		    "actual retained resource");
	}

	std::array<InputControl, 4> inputs{};
	inputs[0].Kind = 3;
	inputs[0].Enabled = true;
	inputs[0].Reverse = true;
	inputs[0].Threshold = 0.25f;
	inputs[0].Softness = 0.5f;
	inputs[0].EasingIndex = 0;
	inputs[1] = inputs[0];
	inputs[1].EasingIndex = 1;
	inputs[2].Kind = 2;
	const auto resource = Resource(Timeline(5, false, {}, inputs), true);
	const auto& timeline = resource->Objects[0].Timelines[0];
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	sgre::scene::MotionStencilMaskOwner owner(timeline.Layers[3], prepared.Layers[3], *resource);
	owner.PrepareFrame(0, 0);
	owner.PrepareFrame(1, 1);
	Require(owner.Evaluate(*resource, 20, 0, true, true), "native equal endpoints must bypass out-of-range curve evaluation independently");
	Require(owner.Summary().Scale == 1024.0f / 512.5f, "equal endpoints must retain exact coefficient input");
	owner.PrepareFrame(0, 2);
	const auto disabled = owner.Control(0);
	Require(!disabled.Enabled && !disabled.EasingIndex && disabled.ReverseWrite && disabled.Threshold == 0.25f && disabled.Softness == 0.5f, "disabled slot releases only the retained curve while preserving native physical scalars");
	for (const auto invalid : {std::int64_t{-1}, std::int64_t{2}}) {
		auto malformed = *resource;
		auto& content = std::get<sgre::resource::PsbObject>(malformed.Objects[0].Timelines[0].Layers[3].Frames[0].Content.Data);
		auto& stc = std::get<sgre::resource::PsbObject>(content.back().second.Data);
		stc[1].second = Atom(invalid);
		RequireFailure([&] {
			sgre::scene::MotionStencilMaskOwner rejected(malformed.Objects[0].Timelines[0].Layers[3], prepared.Layers[3], malformed);
		},
		    "actual resource easing table");
	}
}

static void TestBoundedFailures() {
	std::array<InputControl, 4> inputs{};
	inputs[0].Kind = 3;
	inputs[0].Enabled = true;
	inputs[0].Threshold = 0.25f;
	inputs[0].Softness = 0.5f;
	inputs[1] = inputs[0];
	inputs[1].Threshold = 0.75f;
	inputs[1].Softness = 1;
	inputs[2].Kind = 2;
	const auto resource = Resource(Timeline(5, false, {"n1", "n2", "n3", "missing", "n1"}, inputs));
	auto& timeline = resource->Objects[0].Timelines[0];
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	sgre::scene::MotionStencilMaskOwner detached(timeline.Layers[3], prepared.Layers[3]);
	RequireFailure([&] {
		static_cast<void>(detached.MaskLayers());
	},
	    "binding phase");
	RequireFailure([&] {
		static_cast<void>(detached.Control(0));
	},
	    "decoded");
	RequireFailure([&] {
		detached.PrepareFrame(2, 0);
	},
	    "exceeds");
	RequireFailure([&] {
		static_cast<void>(detached.Evaluate(*resource, 0, 0, true, true));
	},
	    "adjacent");
	detached.PrepareFrame(0, 0);
	detached.PrepareFrame(1, 1);
	Require(detached.Evaluate(*resource, 1, 0, true, true), "actual stencil numeric initial state must publish");
	Require(detached.Evaluate(*resource, 20, 0, true, true), "actual selected stencil interval must preserve finite extrapolation beyond its following key");
	const auto extrapolated_scale = 1024.0f / 3070.0f;
	const auto extrapolated_offset = 1.0f - (extrapolated_scale + 1.0f) * 2.75f;
	Require(detached.EvaluatedPosition() == 20 && std::bit_cast<std::uint32_t>(detached.Summary().Scale) == std::bit_cast<std::uint32_t>(extrapolated_scale) && std::bit_cast<std::uint32_t>(detached.Summary().Offset) == std::bit_cast<std::uint32_t>(extrapolated_offset), "stencil threshold/softness must extrapolate at actual ratio5 without clamping or changing selected slots");
	RequireFailure([&] {
		static_cast<void>(detached.Evaluate(*resource, std::numeric_limits<float>::quiet_NaN(), 0, true, true));
	},
	    "outside");
	RequireFailure([&] {
		static_cast<void>(detached.Summary());
	},
	    "initial numeric");
	Require(detached.Evaluate(*resource, 1, 0, true, true), "failed stencil numeric mutation must allow an actual valid retry");
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	static_cast<void>(Register(manager, resource));
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("main");
	sgre::scene::MotionRootScene root;
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool ids;
	{
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, ids, Construction(), Slots());
		RequireFailure([&] {
			detached.BindMaskLayers(selected);
		},
		    "exclusive");
		RequireFailure([&] {
			selected.StencilMask(3).BindMaskLayers(selected);
		},
		    "exclusive");
		RequireFailure([&] {
			static_cast<void>(selected.StencilMask(0));
		},
		    "kind12");
	}
	Require(ids.LiveCount() == 0, "stencil exclusive failures must preserve retirement");
	timeline.LayerIndexMap = Atom(sgre::resource::PsbObject{{"n1", Atom(std::int64_t{999})}});
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, ids, Construction(), Slots());
	},
	    "map index");
	Require(ids.LiveCount() == 0, "failed actual stencil binding must unwind every selected ID");
	auto& stc = std::get<sgre::resource::PsbObject>(std::get<sgre::resource::PsbObject>(timeline.Layers[3].Frames[0].Content.Data).back().second.Data);
	stc[1].second = Atom(std::int64_t{0});
	RequireFailure([&] {
		sgre::scene::MotionStencilMaskOwner unverified_curve(timeline.Layers[3], prepared.Layers[3]);
	},
	    "wcc");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: stencil_mask_owner_tests [native fixture]");
		TestBoundedFailures();
		TestRetainedWcc();
		if (argc == 2) {
			Replay(argv[1]);
		}
		std::cout << "selected stencil mask identity, retained controls and bounded failures PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
