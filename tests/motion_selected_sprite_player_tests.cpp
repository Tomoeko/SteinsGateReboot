#include "sgre/scene/motion_selected_sprite_player.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected selected sprite failure was not reported");
}

static sgre::resource::PsbValue Integer(std::int64_t value) {
	sgre::resource::PsbValue result;
	result.Data = value;

	return result;
}

static sgre::resource::PsbValue Number(float value) {
	sgre::resource::PsbValue result;
	result.Data = static_cast<double>(value);

	return result;
}

static sgre::resource::PsbValue Text(std::string text) {
	sgre::resource::PsbValue result;
	result.Data = std::move(text);

	return result;
}

static sgre::resource::PsbValue Array(sgre::resource::PsbArray values) {
	sgre::resource::PsbValue result;
	result.Data = std::move(values);

	return result;
}

static sgre::resource::PsbValue Object(sgre::resource::PsbObject values) {
	sgre::resource::PsbValue result;
	result.Data = std::move(values);

	return result;
}

struct Inputs {
	bool Missing = false;
	bool Reverse = false;
	std::uint32_t Opacity = 255;
	std::uint32_t Color = 0x808080ff;
	std::uint32_t Blend = 0x10;
	float Priority = 0;
	float X = 0;
	float Y = 0;
};

static sgre::resource::MotionLayer Layer(const Inputs& inputs, std::uint32_t ordinal, std::uint32_t kind, std::uint32_t inheritance) {
	sgre::resource::MotionLayer result;
	result.Label = "n" + std::to_string(ordinal);
	result.Type = kind;
	result.InheritMask = inheritance;
	result.TransformOrder = {0, 3, 2, 1};
	result.Properties = {{"stencilType", Integer(0)}, {"objTriPriority", Integer(ordinal == 2 ? -1 : 7)}, {"meshTransform", Integer(0)}, {"parameterize", sgre::resource::PsbValue{}}, {"joinTarget", Integer(0)}, {"groundCorrection", Integer(0)}};
	for (std::uint32_t index = 0; index < 3; ++index) {
		sgre::resource::MotionFrame frame;
		frame.Time = static_cast<float>(index * 4);
		frame.Type = index == 2 ? 0 : 2;
		frame.Content = Object({{"mask", Integer(2 | 1 | 0x400 | 0x200 | 0x20000 | 0x40000)}, {"src", Text(inputs.Missing && kind == 0 && index == 1 ? "missing" : "atlas")}, {"icon", Text("tile")}, {"coord", Array({Number(static_cast<float>(5 + ordinal + index * 2)), Number(static_cast<float>(7 + ordinal)), Number(static_cast<float>(ordinal * 2))})}, {"ox", Number(static_cast<float>(ordinal))}, {"oy", Number(static_cast<float>(index))}, {"opa", Integer(inputs.Opacity)}, {"color", Integer(inputs.Color)}, {"bm", Integer(inputs.Blend)}, {"act", Text("act" + std::to_string(index))}});
		result.Frames.push_back(std::move(frame));
	}

	return result;
}

static std::shared_ptr<sgre::resource::MotionResource> Model(const Inputs& inputs) {
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionSource source;
	source.Name = "atlas";
	source.Texture.SerializedValueOffset = 100;
	source.Texture.Format = sgre::resource::PixelFormat::Alpha8;
	source.Texture.Width = 256;
	source.Texture.Height = 256;
	source.Texture.TruncatedWidth = 256;
	source.Texture.TruncatedHeight = 256;
	source.Texture.PixelRange.Size = 65536;
	sgre::resource::MotionIcon icon;
	icon.Name = "tile";
	icon.Left = 8;
	icon.Top = 16;
	icon.Width = 32;
	icon.Height = 64;
	icon.OriginX = 3;
	icon.OriginY = 5;
	icon.AttributeFlags = 2;
	source.Icons.push_back(std::move(icon));
	model->Sources.push_back(std::move(source));
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "main";
	timeline.LastTime = 8;
	auto folder = Layer(inputs, 1, 2, 0);
	folder.Children.push_back(Layer(inputs, 2, 0, 4));
	timeline.Layers.push_back(std::move(folder));
	timeline.Layers.push_back(Layer(inputs, 3, 0, 4));
	const sgre::resource::PsbArray order = inputs.Reverse ? sgre::resource::PsbArray{Integer(2), Integer(0), Integer(1)} : sgre::resource::PsbArray{Integer(0), Integer(1), Integer(2)};
	for (std::uint32_t index = 0; index < 2; ++index) {
		sgre::resource::MotionFrame priority;
		priority.Time = static_cast<float>(index * 8);
		priority.Type = 1;
		priority.Content = index == 0 ? Array(order) : sgre::resource::PsbValue{};
		timeline.Priority.push_back(std::move(priority));
	}
	model->Objects.push_back(sgre::resource::MotionObject{});
	model->Objects[0].Name = "character";
	model->Objects[0].Timelines.push_back(std::move(timeline));

	return model;
}

struct Fixture {
	std::uint32_t Uploads = 0;
	std::uint32_t Releases = 0;
	std::uint32_t TextureLookups = 0;
	std::uint32_t Samples = 0;
	float Delta = 0;
	bool FailProjection = false;
	std::function<void()> OnDelivery;
	std::vector<std::pair<std::string, std::string>> Events;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionResourceManager Manager;
	std::shared_ptr<sgre::resource::MotionResource> Resource;
	sgre::scene::MotionProjectIdentity Identity;
	sgre::scene::MotionProjectId Project;
	std::vector<std::uint8_t> Bytes;
	sgre::scene::MotionSourceRegistration Registration;
	std::unique_ptr<sgre::scene::PreparedMotionSourceRegistry> Registry;
	sgre::scene::MotionPlayerCommands Commands;
	sgre::scene::MotionRootScene Root;
	sgre::scene::MotionPlayerRenderControls Render;
	sgre::scene::MotionLayerIdentifierPool Identifiers;
	sgre::scene::PreparedMotionTimeline Prepared;
	sgre::scene::MotionSpriteDrawContext Context;
	std::unique_ptr<sgre::scene::MotionSelectedSpritePlayer> Player;

	explicit Fixture(const Inputs& inputs = {}) : Cache([this](auto, const auto& image) {
		                                              Require(image.Width == 256 && image.Height == 256, "actual atlas import dimensions differ");

		                                              return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	                                              },
	                                                  [this](auto) {
		                                                  ++Releases;
	                                                  }),
	                                              Manager(Cache), Resource(Model(inputs)), Bytes(65536, 128), Commands(Manager), Context(32) {
		Identity.DocumentInstance = 7;
		Identity.RootOffset = 100;
		Project = Manager.Register(Identity, Resource, Bytes);
		Registration.Project = Identity;
		Registration.Sources = Resource->Sources;
		Registry = std::make_unique<sgre::scene::PreparedMotionSourceRegistry>(std::span<const sgre::scene::MotionSourceRegistration>(&Registration, 1));
		Prepared = sgre::scene::PrepareMotionTimeline(Resource->Objects[0].Timelines[0], *Resource);
		Commands.SetCharacter("character");
		Commands.Play("main");
		Root.SetVisible(true);
		Root.SetCoordinate({inputs.X, inputs.Y});
		Root.SetPriority(inputs.Priority);
		RebuildPlayer();
	}

	~Fixture() {
		Context.Clear();
		Player.reset();
	}

	sgre::scene::MotionSelectedSpriteServices Services() {
		sgre::scene::MotionSelectedSpriteServices result;
		result.ReadFrameDelta = [this] {
			return Delta;
		};
		result.ReadDepthProjection = [this] {
			if (FailProjection) {
				throw std::runtime_error("actual projection fixture failure");
			}

			return 0.25f;
		};
		result.Construction.ReadManagerRandom = [this](const auto& manager) {
			Require(&manager == &Manager, "selected child constructor must use the actual manager");
			++Samples;

			return 0.5f;
		};
		result.Textures.FindTexture = [this](const auto& binding) {
			++TextureLookups;
			Require(binding.Project == Identity && binding.Source == &Resource->Sources[0], "source lookup must use actual registered provenance");

			return Manager.FindSource(Project, binding.Source->Name);
		};
		result.DeliverEvent = [this](const auto& event) {
			Require(event.Kind == sgre::scene::MotionPlayerEventKind::Action, "fixture timeline has only actual layer actions");
			Events.emplace_back(event.Layer, event.Action);
			if (OnDelivery) {
				OnDelivery();
			}
		};

		return result;
	}

	void RebuildPlayer() {
		Player = std::make_unique<sgre::scene::MotionSelectedSpritePlayer>(Commands, Root, Render, *Resource, Resource->Objects[0].Timelines[0], Prepared, *Registry, Identifiers, Context, Services());
	}

	void Step(float position) {
		Delta = position - Commands.ClockState().TickCount;
		Events.clear();
		Player->Update();
	}
};

static std::array<std::uint32_t, 40> PacketWords(const sgre::scene::MotionSpriteDrawRow& row) {
	std::array<std::uint32_t, 40> words{};
	words[0] = row.Order.Ordinal;
	words[1] = row.Clip.Hidden;
	words[2] = row.NodeIdentifier;
	words[3] = std::bit_cast<std::uint32_t>(row.Order.Secondary);
	words[4] = row.Command.BlendMode;
	words[5] = std::bit_cast<std::uint32_t>(row.Command.BlendParameter);
	words[6] = row.Command.Filter;
	words[7] = row.Command.FilterColor;
	words[8] = row.EmissiveColor;
	words[9] = std::bit_cast<std::uint32_t>(row.AmbientFactor);
	words[10] = std::bit_cast<std::uint32_t>(row.Order.Primary);
	words[11] = static_cast<std::uint32_t>(row.Command.Texture);
	words[12] = row.Command.Key;
	words[13] = std::bit_cast<std::uint32_t>(row.Command.PixelUvs.X);
	words[14] = std::bit_cast<std::uint32_t>(row.Command.PixelUvs.Y);
	words[15] = std::bit_cast<std::uint32_t>(row.Command.PixelUvs.Width);
	words[16] = std::bit_cast<std::uint32_t>(row.Command.PixelUvs.Height);
	words[17] = row.Clip.GeometryCount;
	for (std::size_t index = 0; index < 4; ++index) {
		words[index + 18] = row.Command.Colors[index];
	}
	words[22] = row.TrianglePriority;
	words[23] = row.CoordinatePlane;
	words[24] = row.Command.IndividualClip != nullptr;
	words[26] = row.Clip.Geometry != nullptr;

	return words;
}

class Reader {
public:
	explicit Reader(std::string_view path) : m_Input(std::string(path), std::ios::binary) {
		if (!m_Input) {
			throw std::runtime_error("cannot read selected sprite native fixture");
		}
	}

	std::uint32_t Word() {
		std::array<char, 4> bytes{};
		m_Input.read(bytes.data(), 4);
		Require(static_cast<bool>(m_Input), "selected sprite native fixture truncated");

		return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0])) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8 | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16 | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24;
	}

	void End() {
		Require(m_Input.peek() == std::char_traits<char>::eof(), "selected sprite native fixture has trailing bytes");
	}

private:
	std::ifstream m_Input;
};

static void Replay(std::string_view path) {
	Reader reader(path);
	Require(reader.Word() == 0x314f534d, "selected sprite native fixture magic differs");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases == 32 && steps == 5, "selected sprite native fixture count differs");
	const std::array<std::string_view, 10> strings{"", "n1", "n2", "n3", "atlas", "tile", "missing", "act0", "act1", "act2"};
	std::size_t compared = 0;
	for (std::uint32_t index = 0; index < cases; ++index) {
		Inputs inputs;
		inputs.Missing = reader.Word() != 0;
		inputs.Reverse = reader.Word() != 0;
		inputs.Opacity = reader.Word();
		inputs.Color = reader.Word();
		inputs.Blend = reader.Word();
		inputs.Priority = std::bit_cast<float>(reader.Word());
		inputs.X = std::bit_cast<float>(reader.Word());
		inputs.Y = std::bit_cast<float>(reader.Word());
		Fixture fixture(inputs);
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto position = std::bit_cast<float>(reader.Word());
			const auto native_lookups = reader.Word();
			const auto native_geometry_preparations = reader.Word();
			Require(native_lookups <= 2 && native_geometry_preparations <= 2, "native source/geometry phase count exceeds actual nodes");
			const auto event_count = reader.Word();
			Require(event_count <= 3, "native sprite event count exceeds selected layers");
			std::vector<std::pair<std::string, std::string>> expected_events;
			for (std::uint32_t event = 0; event < event_count; ++event) {
				const auto label = reader.Word();
				const auto action = reader.Word();
				Require(label < strings.size() && action < strings.size(), "native sprite event string identity exceeds fixture");
				expected_events.emplace_back(strings[label], strings[action]);
			}
			const auto count = reader.Word();
			Require(count <= 2, "native published row count exceeds selected sprites");
			std::array<std::uint32_t, 2> order{};
			for (std::uint32_t entry = 0; entry < count; ++entry) {
				order[entry] = reader.Word();
				Require(order[entry] < 2, "native published row ordinal exceeds persistent storage");
			}

			fixture.Step(position);
			Require(fixture.Events == expected_events, "native complete layer-action delivery order differs");
			const auto rows = fixture.Player->Rows();
			const auto published = fixture.Player->PublishedRows();
			Require(rows.size() == 2 && published.size() == count, "native persistent/published row counts differ");
			for (std::uint32_t entry = 0; entry < count; ++entry) {
				Require(published[entry] == &rows[order[entry]], "native priority publication must borrow exact persistent rows");
			}
			for (const auto& row : rows) {
				std::array<std::uint32_t, 40> expected{};
				for (auto& word : expected) {
					word = reader.Word();
				}
				Require(PacketWords(row) == expected, "native selected sprite complete row fields differ");
				const auto geometry = reader.Word() != 0;
				std::array<std::uint32_t, 8> expected_positions{};
				for (auto& word : expected_positions) {
					word = reader.Word();
				}
				Require(geometry == (row.Clip.Geometry != nullptr), "native retained geometry owner differs");
				if (geometry) {
					Require(row.Clip.Geometry->Positions.size() == 4, "native actual one-cell geometry extent differs");
					for (std::size_t corner = 0; corner < 4; ++corner) {
						Require(std::bit_cast<std::uint32_t>(row.Clip.Geometry->Positions[corner].X) == expected_positions[corner * 2] && std::bit_cast<std::uint32_t>(row.Clip.Geometry->Positions[corner].Y) == expected_positions[corner * 2 + 1], "native composed/source-bound geometry words differ");
					}
				}
				++compared;
			}
			fixture.Player->Draw();
			Require(fixture.Context.Size() == count, "actual draw must append current persistent row pointers into its real context");
			fixture.Context.Clear();
		}
		Require(fixture.Manager.ReferenceCount(fixture.Project) == 1 && fixture.Uploads == 1 && fixture.Samples == 0, "selected scene must not add resource pins, frame uploads or child sampler calls");
	}
	reader.End();
	std::cout << "native selected scene/source/geometry/persistent-row/action/draw replay: " << compared << " rows PASS\n";
}

struct FrameOutput {
	std::array<sgre::resource::MotionPoint, 1> TextureSizes{};
	std::array<sgre::resource::MotionPoint, 64> Positions{};
	std::array<sgre::resource::MotionPoint, 64> Uvs{};
	std::array<std::uint32_t, 64> Colors{};
	std::array<std::uint32_t, 64> Indices{};
	std::array<sgre::scene::MotionMaskOperation, 8> Masks{};
	std::array<sgre::scene::MotionMaskedBlendPass, 8> Passes{};
	sgre::scene::MotionPreparedFrame Globals;
	sgre::scene::MotionFrameScratch Scratch;
	std::size_t Packets = 0;
	std::size_t Vertices = 0;
	std::size_t IndexCount = 0;
	std::size_t Begins = 0;
	std::size_t Ends = 0;

	FrameOutput() {
		TextureSizes[0].X = 256;
		TextureSizes[0].Y = 256;
		Globals.TextureSizes = TextureSizes;
		Globals.InitialScissor.MinimumX = 0;
		Globals.InitialScissor.MinimumY = 0;
		Globals.InitialScissor.MaximumX = 256;
		Globals.InitialScissor.MaximumY = 256;
		Globals.AppliedRoundedScissor = Globals.InitialScissor;
		Globals.View.Coefficients = {1.0f / 128, 0, 0, 0, 0, -1.0f / 128, 0, 0, 0, 0, 1, 0};
		Globals.Viewport.Width = 256;
		Globals.Viewport.Height = 256;
		Globals.TargetBounds.MinimumX = -128;
		Globals.TargetBounds.MinimumY = -128;
		Globals.TargetBounds.MaximumX = 128;
		Globals.TargetBounds.MaximumY = 128;
		Globals.AxisScale.X = 1;
		Globals.AxisScale.Y = 1;
		Scratch.Batch.Positions = Positions;
		Scratch.Batch.Uvs = Uvs;
		Scratch.Batch.Colors = Colors;
		Scratch.Batch.Indices = Indices;
		Scratch.MaskOperations = Masks;
		Scratch.BlendPasses = Passes;
	}

	void Flush(Fixture& fixture) {
		fixture.Context.Flush(Globals, Scratch, [&](const auto& event) {
			if (std::holds_alternative<sgre::scene::MotionFrameBegin>(event)) {
				++Begins;
			} else if (std::holds_alternative<sgre::scene::MotionFrameEnd>(event)) {
				++Ends;
			} else if (const auto* packet = std::get_if<sgre::scene::MotionBatchSubmission>(&event)) {
				++Packets;
				Vertices += packet->Positions.size();
				IndexCount += packet->Indices.size();
				Require(packet->Positions[0].X == 2 && packet->Positions[0].Y == 6.5f && packet->Uvs[0].X == 0.03125f && packet->Uvs[0].Y == 0.0625f && packet->Colors[0] == 0xff808080, "actual source/geometry/render submission must retain first root-sprite position, pixel UV normalization and color conversion");
			}
		});
	}
};

static void TestActualOwnerAndContext() {
	Fixture fixture;
	fixture.Step(0);
	Require(fixture.Player->Nodes().Nodes().size() == 3 && fixture.Player->Nodes().DrawRowCount() == 2 && fixture.Events == std::vector<std::pair<std::string, std::string>>{{"n1", "act0"}, {"n2", "act0"}, {"n3", "act0"}}, "actual selected hierarchy/IDs/source/action owner must publish real prepared nodes");
	Require(!fixture.Player->LocalState(1).FrameDirty && fixture.Player->Rows()[0].NodeIdentifier == 2 && fixture.Player->Rows()[0].TrianglePriority == 0xffffffff && fixture.Player->Rows()[1].NodeIdentifier == 3, "complete bounded sprite geometry must acknowledge dirty state and preserve real node/triangle identities");
	auto* geometry = fixture.Player->Rows()[0].Clip.Geometry;
	static_cast<void>(sgre::scene::MeasureMotionGeometryBounds(*geometry));
	Require(geometry->Calculated, "real geometry cache must support lazy bounds");
	fixture.Step(0);
	Require(geometry->Calculated && fixture.Events.empty() && fixture.TextureLookups == 2, "force-false repeated frame must retain geometry bounds/source bindings and avoid repeated actions");
	fixture.Player->Draw();
	fixture.Player->Draw();
	Require(fixture.Context.Size() == 4, "repeated native Draw appends duplicate actual row pointers");
	RequireFailure([&] {
		fixture.Step(4);
	},
	    "queued draw rows");
	FrameOutput output;
	output.Flush(fixture);
	Require(output.Begins == 1 && output.Ends == 1 && output.Packets == 1 && output.Vertices == 16 && output.IndexCount == 22 && fixture.Context.Size() == 0, "actual sorted duplicated rows must reach populated native quad submission and release context borrows after success");
	const auto ordinal = fixture.Player->Rows()[0].Order.Ordinal;
	Require(ordinal == 3, "aliased row ordinal must come from its final queued position");
	fixture.Commands.Play("main", sgre::scene::MotionPlayFlags::Force);
	fixture.Player->Rebuild(*fixture.Resource, fixture.Resource->Objects[0].Timelines[0], fixture.Prepared, *fixture.Registry);
	fixture.Step(0);
	Require(fixture.Player->Rows()[0].Order.Ordinal == ordinal && fixture.Player->Rows()[0].NodeIdentifier == 2 && fixture.Player->Rows()[1].NodeIdentifier == 1, "same-size rebuild retains persistent row words while native LIFO IDs and real geometry owners rebuild");
	fixture.FailProjection = true;
	RequireFailure([&] {
		fixture.Step(0);
	},
	    "projection fixture");
	Require(fixture.Player->Rows().empty() && fixture.Player->PublishedRows().empty(), "failed real geometry phase must invalidate publication");
	fixture.FailProjection = false;
	fixture.Step(0);
	fixture.Player->Draw();
	RequireFailure([&] {
		fixture.Context.Flush(output.Globals, output.Scratch, [](const auto&) {
			throw std::runtime_error("actual output sink fixture failure");
		});
	},
	    "output sink");
	Require(fixture.Context.Size() == 2, "failed backend submission retains its real borrowed queue for retry/clear");
	RequireFailure([&] {
		fixture.Player->Retire();
	},
	    "queued draw rows");
	fixture.Context.Clear();
	fixture.Player->Retire();
	Require(fixture.Player->Rows().empty() && fixture.Manager.ReferenceCount(fixture.Project) == 1, "selected retirement leaves resource registration ownership with its actual manager");
}

static void TestRequiredServicesAndLifetimes() {
	Fixture fixture;
	fixture.Player.reset();
	auto services = fixture.Services();
	services.ReadDepthProjection = {};
	RequireFailure([&] {
		sgre::scene::MotionSelectedSpritePlayer rejected(fixture.Commands, fixture.Root, fixture.Render, *fixture.Resource, fixture.Resource->Objects[0].Timelines[0], fixture.Prepared, *fixture.Registry, fixture.Identifiers, fixture.Context, services);
	},
	    "requires actual manager");
	fixture.RebuildPlayer();
	fixture.OnDelivery = [&] {
		RequireFailure([&] {
			fixture.Player->Retire();
		},
		    "idle owner");
	};
	fixture.Step(0);
	Require(fixture.Events.size() == 3, "real listeners may observe the owner but cannot destructively reenter its scoped delivery phase");
	fixture.Commands.Play("main", sgre::scene::MotionPlayFlags::Force);
	fixture.Player->Rebuild(*fixture.Resource, fixture.Resource->Objects[0].Timelines[0], fixture.Prepared, *fixture.Registry);
	fixture.OnDelivery = [&] {
		Require(fixture.Manager.Unregister(fixture.Project), "fixture final unregister must remove actual registration");
	};
	RequireFailure([&] {
		fixture.Step(0);
	},
	    "unregister during");
	Require(fixture.Events.size() == 1 && fixture.Player->Rows().empty(), "callback unregister must reject before visiting any following borrowed event and publishing invalid rows");
	RequireFailure([&] {
		fixture.Player->Draw();
	},
	    "live selected");
}

static void TestProducerBoundaries() {
	Fixture fixture;
	fixture.Player.reset();
	sgre::scene::MotionRootScene inherited(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
	RequireFailure([&] {
		sgre::scene::MotionSelectedSpritePlayer rejected(fixture.Commands, inherited, fixture.Render, *fixture.Resource, fixture.Resource->Objects[0].Timelines[0], fixture.Prepared, *fixture.Registry, fixture.Identifiers, fixture.Context, fixture.Services());
	},
	    "standalone root");
	fixture.Resource->Objects[0].Timelines[0].Layers[0].Children[0].Type = 3;
	fixture.Prepared.Layers[1].Type = 3;
	RequireFailure([&] {
		fixture.RebuildPlayer();
	},
	    "kind3 admission remains closed");
	Require(fixture.Samples == 0, "rejected child selection cannot fabricate a child owner or consume its manager seed");
	fixture.Resource->Objects[0].Timelines[0].Layers[0].Children[0].Type = 0;
	fixture.Prepared.Layers[1].Type = 0;
	fixture.RebuildPlayer();
	fixture.Step(0);
	fixture.Render.EmissiveColor = 1;
	fixture.Step(0);
	RequireFailure([&] {
		fixture.Player->Draw();
	},
	    "auxiliary lighting");
	Require(fixture.Context.Size() == 0, "unrecovered lighting preflight cannot partially append rows");
	fixture.Render.EmissiveColor = 0;
	fixture.Step(0);
	fixture.Player->Draw();
	FrameOutput output;
	RequireFailure([&] {
		fixture.Context.Flush(output.Globals, output.Scratch, {});
	},
	    "driver/sink");
	Require(fixture.Context.Size() == 2, "missing actual output sink retains all original context borrows");
	fixture.Context.Clear();
	auto command = fixture.Player->Rows()[0].Command;
	command.BlendMode = 0x20;
	const std::array<sgre::scene::MotionPreparedFrameCommand, 1> commands = {command};
	const std::array<sgre::scene::MotionClipCommand, 1> clips = {fixture.Player->Rows()[0].Clip};
	output.Globals.Queue = commands;
	output.Globals.ClipQueue = clips;
	bool emitted = false;
	RequireFailure([&] {
		sgre::scene::SubmitPreparedMotionFrame(output.Globals, output.Scratch, [&](const auto&) {
			emitted = true;
		});
	},
	    "unsupported blend");
	Require(!emitted, "unproved blend flags reject before any frame output rather than aliasing a supported mode");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_selected_sprite_player_tests [native-fixture]");
		if (argc == 2) {
			Replay(argv[1]);
		}
		TestActualOwnerAndContext();
		TestRequiredServicesAndLifetimes();
		TestProducerBoundaries();
		std::cout << "independent selected sprite owner/context/lifetime fixtures PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
