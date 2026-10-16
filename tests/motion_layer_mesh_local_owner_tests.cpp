#include "sgre/scene/motion_layer_mesh_local_owner.hpp"

#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_static_source.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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

	throw std::runtime_error("unbound mesh owner operation was accepted");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

class Reader {
public:
	explicit Reader(std::istream& stream) : m_Stream(stream) {
	}
	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		Require(value != std::char_traits<char>::eof(), "short native mesh owner fixture");

		return static_cast<std::uint8_t>(value);
	}
	std::uint32_t Word() {
		std::uint32_t word = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			word |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return word;
	}
	float Real() {
		return std::bit_cast<float>(Word());
	}
	std::optional<sgre::resource::MotionBezierPatch> Patch() {
		const auto degree = Word();
		Require(degree == 0 || degree == 0x33, "native mesh degree exceeds the recovered bicubic scope");
		if (degree == 0) {
			return std::nullopt;
		}

		sgre::resource::MotionBezierPatch patch;
		for (auto& point : patch.Controls) {
			point.X = Real();
			point.Y = Real();
		}

		return patch;
	}

private:
	std::istream& m_Stream;
};

static sgre::resource::MotionFrame Frame(float time, std::uint32_t kind, bool masked, const std::optional<sgre::resource::MotionBezierPatch>& patch) {
	sgre::resource::MotionFrame frame;
	frame.Time = time;
	frame.Type = kind;
	sgre::resource::PsbObject content = {{"mask", Atom(masked ? std::int64_t{0x02000000} : std::int64_t{0})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}};
	if (masked) {
		sgre::resource::PsbValue points;
		if (patch) {
			sgre::resource::PsbArray coordinates;
			for (const auto& point : patch->Controls) {
				coordinates.push_back(Atom(static_cast<double>(point.X)));
				coordinates.push_back(Atom(static_cast<double>(point.Y)));
			}
			points = Atom(std::move(coordinates));
		}
		content.emplace_back("mesh", Atom(sgre::resource::PsbObject{{"cc", Atom(std::monostate{})}, {"bp", std::move(points)}}));
	}
	frame.Content = Atom(std::move(content));

	return frame;
}

static std::shared_ptr<sgre::resource::MotionResource> Resource(std::uint32_t sync, bool combine, const sgre::resource::MotionBezierPatch& first, const sgre::resource::MotionBezierPatch& last) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionLayer layer;
	layer.Label = "n1";
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Properties = {{"parameterize", Atom(std::monostate{})}, {"meshTransform", Atom(std::int64_t{1})}, {"meshSyncChildMask", Atom(static_cast<std::int64_t>(sync))}, {"meshDivision", Atom(std::int64_t{20})}, {"meshCombine", Atom(combine)}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"objTriPriority", Atom(std::int64_t{0})}, {"stencilType", Atom(std::int64_t{0})}};
	layer.Frames = {Frame(0, 3, true, first), Frame(2, 2, false, std::nullopt), Frame(4, 2, true, std::nullopt), Frame(6, 3, true, last), Frame(8, 0, false, std::nullopt), Frame(10, 0, false, std::nullopt)};
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "main";
	timeline.LastTime = 10;
	timeline.Layers.push_back(std::move(layer));
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});
	timeline.Priority.push_back(priority);
	priority.Time = 10;
	priority.Content = Atom(std::monostate{});
	timeline.Priority.push_back(priority);
	sgre::resource::MotionObject object;
	object.Name = "a";
	object.Timelines.push_back(std::move(timeline));
	resource->Objects.push_back(std::move(object));
	sgre::resource::MotionSource source;
	source.Name = "atlas";
	source.Texture.SerializedValueOffset = 128;
	source.Texture.Width = 64;
	source.Texture.Height = 128;
	source.Texture.TruncatedWidth = 64;
	source.Texture.TruncatedHeight = 128;
	source.Texture.PixelRange.Size = 64 * 128;
	sgre::resource::MotionIcon icon;
	icon.Name = "tile";
	icon.Left = 8;
	icon.Top = 16;
	icon.AttributeFlags = 2;
	icon.Width = 32;
	icon.Height = 64;
	icon.OriginX = 3;
	icon.OriginY = 5;
	source.Icons.push_back(icon);
	resource->Sources.push_back(std::move(source));

	return resource;
}

static void RequirePatch(const std::optional<sgre::resource::MotionBezierPatch>& actual, const std::optional<sgre::resource::MotionBezierPatch>& expected, std::string_view context) {
	Require(actual.has_value() == expected.has_value(), context);
	if (!actual) {
		return;
	}

	for (std::size_t index = 0; index < actual->Controls.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual->Controls[index].X) == std::bit_cast<std::uint32_t>(expected->Controls[index].X) && std::bit_cast<std::uint32_t>(actual->Controls[index].Y) == std::bit_cast<std::uint32_t>(expected->Controls[index].Y), std::string(context) + " point " + std::to_string(index) + " actual " + std::to_string(actual->Controls[index].X) + "," + std::to_string(actual->Controls[index].Y) + " expected " + std::to_string(expected->Controls[index].X) + "," + std::to_string(expected->Controls[index].Y));
	}
}

static void Replay(const char* path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.is_open(), "cannot open native mesh owner fixture");
	Reader reader(stream);
	Require(reader.Byte() == 'M' && reader.Byte() == 'M' && reader.Byte() == 'L' && reader.Byte() == '1', "wrong native mesh owner fixture");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases == 4 && steps == 14, "native mesh owner fixture exceeds its fixed bounded scope");
	std::size_t comparisons = 0;
	for (std::size_t case_index = 0; case_index < cases; ++case_index) {
		const auto sync = reader.Word();
		const bool combine = reader.Word() != 0;
		sgre::resource::MotionBezierPatch first;
		sgre::resource::MotionBezierPatch last;
		for (auto* patch : {&first, &last}) {
			for (auto& point : patch->Controls) {
				point.X = reader.Real();
				point.Y = reader.Real();
			}
		}
		const auto resource = Resource(sync, combine, first, last);
		std::array<std::uint8_t, 64 * 128> pixels{};
		pixels.fill(255);
		std::size_t backend_owner = 0;
		sgre::scene::TextureCache cache([&](auto, const auto&) {
			sgre::render::SpriteTextureHandle handle;
			handle.Owner = &backend_owner;
			handle.Generation = 1;

			return handle;
		},
		    [](auto) {
		    });
		sgre::scene::MotionResourceManager manager(cache);
		sgre::scene::MotionProjectIdentity project;
		project.DocumentInstance = 1;
		project.RootOffset = 128;
		const auto registration = manager.Register(project, resource, pixels);
		sgre::scene::MotionPlayerCommands commands(manager);
		commands.SetCharacter("a");
		commands.Play("main");
		const auto& timeline = *commands.CachedTimeline();
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
		sgre::scene::MotionRootScene root;
		const sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionLayerIdentifierPool identifiers;
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, {}, {});
		RequireFailure([&] {
			static_cast<void>(selected.MeshLocal(0).OwnPatch());
		},
		    "genuine selected numeric evaluation");
		sgre::scene::MotionLayerMeshLocalOwner unbound(selected, 0);
		RequireFailure([&] {
			static_cast<void>(unbound.OwnPatch());
		},
		    "actual live exclusive selected owner");
		std::array<sgre::scene::MotionSourceRegistration, 1> registrations{};
		registrations[0].Project = project;
		registrations[0].Sources = resource->Sources;
		sgre::scene::PreparedMotionSourceRegistry registry(registrations);
		sgre::scene::MotionStaticSourceServices source_services;
		source_services.FindTexture = [&](const auto& icon) {
			return manager.FindSource(registration, icon.Source->Name);
		};
		sgre::scene::MotionStaticSourceBinding source(registry, project, source_services);
		sgre::scene::MotionLocalFrameServices services;
		services.BindSource = [&](auto, const auto& current, const auto&) {
			static_cast<void>(source.Bind(current));
		};
		services.EnqueueAction = [](auto, auto) {
			throw std::runtime_error("action-free mesh owner fixture queued an action");
		};
		sgre::scene::MotionLocalFrames local(selected, *resource, services);
		for (std::size_t step = 0; step < steps; ++step) {
			const auto position = reader.Real();
			const auto traversal = reader.Word();
			const auto current_slot = reader.Word();
			const auto current = reader.Word();
			const auto following = reader.Word();
			const bool dirty = reader.Word() != 0;
			const bool force = reader.Word() != 0;
			const bool expected_changed = reader.Word() != 0;
			const auto preparations = reader.Word();
			Require(traversal <= 2 && preparations <= 8, "native mesh traversal/preparation exceeds scope");
			for (std::size_t index = 0; index < preparations; ++index) {
				static_cast<void>(reader.Word());
				static_cast<void>(reader.Word());
			}
			std::array<std::optional<sgre::resource::MotionBezierPatch>, 2> expected_slots = {reader.Patch(), reader.Patch()};
			const auto expected_own = reader.Patch();
			const std::array<sgre::scene::MotionFrameTraversal, 3> modes = {sgre::scene::MotionFrameTraversal::Seek, sgre::scene::MotionFrameTraversal::Forward, sgre::scene::MotionFrameTraversal::Reverse};
			local.Select(modes[traversal], position);
			const auto& state = local.State(0);
			Require(state.CurrentSlot == current_slot && state.CurrentIndex == current && following == current + 1 && state.FrameDirty == dirty, "actual mesh cursor differs from native physical-slot selection");
			Require(local.Evaluate(0, force) == expected_changed, "actual common mesh numeric changed gate differs from native");
			const auto context = "mesh control words differ case " + std::to_string(case_index) + " step " + std::to_string(step);
			for (std::size_t slot = 0; slot < expected_slots.size(); ++slot) {
				RequirePatch(selected.MeshLocal(0).Slot(slot).Deformation.Patch, expected_slots[slot], context + " physical slot " + std::to_string(slot));
				++comparisons;
			}
			RequirePatch(selected.MeshLocal(0).OwnPatch(), expected_own, context + " own patch");
			RequirePatch(local.Evaluation(0).Sample->Deformation, expected_own, context + " own patch");
			Require(selected.MeshLocal(0).EvaluatedFrameIndex() == current && selected.MeshLocal(0).EvaluatedPosition() == position, "actual mesh numeric owner has stale selected frame identity");
			Require(local.Samples().empty(), "local mesh numeric output must not pretend the remaining geometry/composition phases completed");
			++comparisons;
		}
		selected.Retire();
		RequireFailure([&] {
			static_cast<void>(local.Evaluate(0, true));
		},
		    "actual live selected raw/prepared owner");
		Require(identifiers.LiveCount() == 0 && manager.ReferenceCount(registration) == 1, "actual mesh retirement must preserve enclosing registration lifetime");
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "native mesh owner fixture has trailing bytes");
	std::cout << comparisons << " original retained mesh slot/own-patch records matched\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_layer_mesh_local_owner_tests native_fixture");
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
