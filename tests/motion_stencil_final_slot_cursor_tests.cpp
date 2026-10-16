#include "sgre/scene/motion_local_frames.hpp"

#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_static_source.hpp"
#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
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
		Require(value != std::char_traits<char>::eof(), "short final-slot native fixture");

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

private:
	std::istream& m_Stream;
};

struct InputControl {
	std::uint32_t Kind = 0;
	bool Enabled = false;
	bool Reverse = false;
	float Threshold = 0;
	float Softness = 0;
};

static std::shared_ptr<sgre::resource::MotionResource> Resource(const std::array<InputControl, 6>& controls) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionLayer layer;
	layer.Type = 12;
	layer.Label = "mask";
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Properties = {{"parameterize", Atom(std::monostate{})}, {"meshTransform", Atom(std::int64_t{0})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"objTriPriority", Atom(std::int64_t{0})}, {"stencilType", Atom(std::int64_t{5})}, {"stencilCompositeMaskLayerList", Atom(sgre::resource::PsbArray{})}};
	for (std::size_t index = 0; index < controls.size(); ++index) {
		const auto& input = controls[index];
		sgre::resource::MotionFrame frame;
		frame.Time = static_cast<float>(index * 2);
		frame.Type = input.Kind;
		const sgre::resource::PsbObject stc = {{"wrv", Atom(input.Reverse)}, {"wcc", Atom(std::monostate{})}, {"wrt", Atom(static_cast<double>(input.Threshold))}, {"wsf", Atom(static_cast<double>(input.Softness))}};
		frame.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(input.Enabled ? std::int64_t{0x20000000} : std::int64_t{0})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"stc", Atom(stc)}});
		layer.Frames.push_back(std::move(frame));
	}
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

static void Replay(const char* path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.is_open(), "cannot open native final-slot fixture");
	Reader reader(stream);
	Require(reader.Byte() == 'M' && reader.Byte() == 'S' && reader.Byte() == 'F' && reader.Byte() == '1', "wrong native final-slot fixture");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases == 4 && steps == 14, "native final-slot fixture exceeds fixed bounds");
	for (std::size_t case_index = 0; case_index < cases; ++case_index) {
		std::array<InputControl, 6> inputs{};
		for (auto& input : inputs) {
			input.Kind = reader.Word();
			input.Enabled = reader.Word() != 0;
			input.Reverse = reader.Word() != 0;
			input.Threshold = reader.Real();
			input.Softness = reader.Real();
		}
		const auto resource = Resource(inputs);
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
		auto& mask = selected.StencilMask(0);
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
			throw std::runtime_error("action-free final-slot fixture queued an action");
		};
		sgre::scene::MotionLocalFrames local(selected, *resource, services);
		for (std::size_t step = 0; step < steps; ++step) {
			const auto position = reader.Real();
			const auto traversal = reader.Word();
			const auto current_slot = reader.Word();
			const bool dirty = reader.Word() != 0;
			const bool force = reader.Word() != 0;
			const bool changed = reader.Word() != 0;
			const auto current = reader.Word();
			const auto following = reader.Word();
			const auto preparations = reader.Word();
			Require(traversal <= 2 && preparations <= 8 && current + 1 == following, "native final-slot cursor exceeds scope");
			// Assigned native indices are observations; the actual portable cursor selects and decodes its own slots.
			for (std::size_t index = 0; index < preparations; ++index) {
				static_cast<void>(reader.Word());
				static_cast<void>(reader.Word());
			}
			std::array<std::array<std::uint32_t, 4>, 3> expected{};
			for (auto& record : expected) {
				for (auto& word : record) {
					word = reader.Word();
				}
			}
			const std::array<sgre::scene::MotionFrameTraversal, 3> modes = {sgre::scene::MotionFrameTraversal::Seek, sgre::scene::MotionFrameTraversal::Forward, sgre::scene::MotionFrameTraversal::Reverse};
			local.Select(modes[traversal], position);
			const auto& state = local.State(0);
			Require(state.CurrentSlot == current_slot && state.CurrentIndex == current && state.FrameDirty == dirty && state.Initialized, "actual final-slot cursor/dirty state differs");
			Require(local.Evaluate(0, force) == changed, "actual final-slot numeric changed gate differs");
			const auto context = "native final-slot case " + std::to_string(case_index) + " step " + std::to_string(step);
			const auto& summary = mask.Summary();
			const std::array<std::uint32_t, 4> actual = {summary.Enabled ? 1u : 0u, summary.ReverseWrite ? 1u : 0u, std::bit_cast<std::uint32_t>(summary.Scale), std::bit_cast<std::uint32_t>(summary.Offset)};
			Require(actual == expected[2], context + " retained summary coefficients");
			for (std::size_t slot = 0; slot < 2; ++slot) {
				const auto& control = mask.Control(slot);
				const std::array<std::uint32_t, 4> actual = {control.Enabled ? 1u : 0u, control.ReverseWrite ? 1u : 0u, std::bit_cast<std::uint32_t>(control.Threshold), std::bit_cast<std::uint32_t>(control.Softness)};
				Require(actual == expected[slot], context + " retained control slot " + std::to_string(slot));
			}
			Require(mask.EvaluatedFrameIndex() == current && mask.EvaluatedPosition() == position && local.Samples().empty(), "actual selected mask evaluation lost identity or acknowledged missing geometry");
		}
		selected.Retire();
		Require(identifiers.LiveCount() == 0 && manager.ReferenceCount(registration) == 1, "actual final-slot retirement must preserve enclosing resource");
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "native final-slot fixture has trailing bytes");
	std::cout << cases * steps << " actual final-slot stencil updates matched; " << cases * steps * 3 << " retained records\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_stencil_final_slot_cursor_tests native_fixture");
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
