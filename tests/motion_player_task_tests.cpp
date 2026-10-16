#include "sgre/scene/motion_player_task.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
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

struct RuntimeFixture {
	std::uint32_t Uploads = 0;
	std::uint32_t Releases = 0;
	std::size_t AliasReads = 0;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	std::vector<std::uint8_t> Bytes;
	std::shared_ptr<sgre::resource::MotionResource> Resource;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionProjectIdentity Identity;
	sgre::scene::MotionProjectId Project;
	sgre::core::FrameClock Clock;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	sgre::scene::MotionLayerTasks Tasks;
	std::vector<std::pair<std::string, std::string>> Events;
	std::function<void()> OnEvent;
	std::function<void()> OnAlias;

	explicit RuntimeFixture(std::uint32_t metadata = 0, std::size_t extra_timelines = 0)
	    : Bytes(65536, 128), Resource(Model(Inputs{})), Cache([this](auto, const auto&) {
		      return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	      },
	                                                        [this](auto) {
		                                                        ++Releases;
	                                                        }),
	      Manager(Cache), Clock(0), Tasks(8) {
		for (std::size_t index = 0; index < Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
		}
		if ((metadata & 1) != 0) {
			Resource->Objects[0].Timelines[0].Parameters = Array({});
		}
		if ((metadata & 2) != 0) {
			Resource->Objects[0].Timelines[0].Variables = Array({});
		}
		if ((metadata & 4) != 0) {
			Resource->Objects[0].Timelines[0].Parameters = Array({Object({{"id", Text("param")}, {"discretization", Integer(0)}, {"rangeBegin", Number(-1)}, {"rangeEnd", Number(1)}, {"division", Number(8)}})});
		}
		if ((metadata & 16) != 0) {
			Resource->Objects[0].Timelines[0].Parameters = Array({Integer(1)});
		}
		if ((metadata & 8) != 0) {
			Resource->Objects[0].Timelines[0].Variables = Array({Integer(1)});
		}

		const auto original = Resource->Objects.front().Timelines.front();
		for (std::size_t index = 0; index != extra_timelines; ++index) {
			auto timeline = original;
			timeline.Name = "variant" + std::to_string(index);
			Resource->Objects.front().Timelines.push_back(std::move(timeline));
		}

		Identity.DocumentInstance = 7;
		Identity.RootOffset = 100;
		Project = Manager.Register(Identity, Resource, Bytes);
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project, "alias metadata must borrow the actual registration");
			++AliasReads;
			if (OnAlias) {
				OnAlias();
			}

			return std::span<const sgre::scene::MotionSourceAlias>{};
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, std::move(services), 32);
	}

	~RuntimeFixture() {
		Runtime->Context().Clear();
		Tasks.Clear();
	}

	sgre::scene::MotionPlayerTask Attach(sgre::scene::MotionTaskInsertion insertion = sgre::scene::MotionTaskInsertion::Append) {
		return sgre::scene::MotionPlayerTask::Attach(
		    *Runtime, Tasks, false, "", [this](const auto& event) {
			    Require(event.Kind == sgre::scene::MotionPlayerEventKind::Action, "fixture contains real layer actions only");
			    Events.emplace_back(event.Layer, event.Action);
			    if (OnEvent) {
				    OnEvent();
			    }
		    },
		    insertion);
	}

	sgre::scene::ParallelTaskSet& Second() {
		return Tasks.Group(sgre::scene::MotionTaskGroup::Second);
	}
};

static void TestLegacyCurrentPreparationHistory() {
	RuntimeFixture fixture(0, 300);
	auto task = fixture.Attach();
	task.SetCharacter("character");
	task.SetVisible(true);
	for (std::size_t index = 0; index != 300; ++index) {
		task.Play("variant" + std::to_string(index), sgre::scene::MotionPlayFlags::Force);
		task.Progress(0);
		Require(task.Player().PublishedRows().size() == 2 && fixture.Runtime->Identifiers().LiveCount() == 3, "legacy runtime Prepare caller must retain its real current frame/source owner beyond256 historical timelines");
		fixture.Second().Draw(0);
		Require(fixture.Runtime->Context().Size() == 2, "every legacy replacement must lend its two actual persistent sprite rows");
		fixture.Runtime->Context().Clear();
	}
	Require(fixture.Events.size() == 900, "current tag/source lifetime must preserve all actual legacy frame actions");
	fixture.Tasks.Clear();
	Require(fixture.Runtime->CanClose(), "legacy current and pending preparation leases must retire with their actual task");
}

static void TestRealInitialAndSharedOwners() {
	RuntimeFixture fixture;
	sgre::scene::MotionSharedRandom expected(fixture.Seed);
	const auto sample0 = expected.NextUnitFloat();
	auto first = fixture.Attach();
	const auto sample1 = expected.NextUnitFloat();
	const auto second = fixture.Attach(sgre::scene::MotionTaskInsertion::Prepend);
	Require(first.Random().State() == sgre::scene::MotionPlayerRandom(sample0).State() && second.Random().State() == sgre::scene::MotionPlayerRandom(sample1).State(), "each full-new consumes exactly one consecutive shared actual seed sample");
	Require(fixture.Runtime->PlayerCount() == 2 && fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->DepthProjection() == 0 && !first.Root().Overrides().Visible && !first.Root().Frame().Composed, "actual full-new shares the manager while preserving initial root-only state");
	std::array<sgre::scene::ParallelTaskHandle, 2> order{};
	Require(fixture.Second().WriteOrder(order) == 2 && order[0] == second.Handle() && order[1] == first.Handle(), "actual Second prepend/append owns the new tasks in native order");
	fixture.Second().Draw(0x12345678);
	Require(fixture.Runtime->Context().Size() == 0, "native initial Draw appends its real empty queue");
	fixture.Second().Update();
	first.PostUpdate();
	Require(first.Root().Frame().Composed && first.Root().Frame().Hierarchy.Transform.XX == 1 && first.Root().Frame().Hierarchy.Attributes.Hidden && fixture.Events.empty(), "initial Update composes the genuine hidden root and independent PostUpdate has no fabricated events");
	Require(fixture.Runtime->ReadFrameDelta() == 1 && fixture.Clock.VariableStep() == 1, "Motion reads the already prepared fixed clock without advancing it");
	fixture.Clock.SetVariableFrame(true);
	static_cast<void>(fixture.Clock.Advance(50));
	Require(fixture.Runtime->ReadFrameDelta() == 3 && fixture.Clock.VariableStep() == 3, "actual variable frame consumes cached looper ticks without a second Advance");
	RequireFailure([&] {
		fixture.Runtime->Close();
	},
	    "concrete players");
	fixture.Tasks.Clear();
	Require(!first.IsLive() && !second.IsLive() && fixture.Runtime->PlayerCount() == 0, "group retirement releases real task payloads and expires weak views");
	RequireFailure([&] {
		static_cast<void>(first.Commands());
	},
	    "expired");
	Require(fixture.Manager.ReferenceCount(fixture.Project) == 1, "runtime/tasks add no resource retain");
}

static void TestInstallationAndQueuedLifetime() {
	RuntimeFixture fixture;
	auto first = fixture.Attach();
	auto second = fixture.Attach(sgre::scene::MotionTaskInsertion::Prepend);
	first.SetCharacter("character");
	first.SetVisible(true);
	first.Play("main");
	fixture.Second().Update();
	Require(first.Player().Nodes().Nodes().size() == 3 && first.Player().PublishedRows().size() == 2 && fixture.Events.size() == 3 && fixture.AliasReads == 1, "real successful selection installs populated nodes, source slots, action queues and persistent rows");
	const auto first_identifier = first.Player().Nodes().Nodes()[0].Identifier;
	first.Play("main");
	Require(first.Player().Nodes().Nodes()[0].Identifier == first_identifier, "unchanged-name Play does not reinstall selected IDs");
	first.Play("main", sgre::scene::MotionPlayFlags::Force);
	Require(first.Player().Nodes().Nodes()[0].Identifier != first_identifier && fixture.AliasReads == 1, "Force reinstalls the actual tree even with identical raw timeline and immutable prepared owner");
	fixture.Second().Update();
	fixture.Second().Draw(0xfedcba98);
	Require(fixture.Runtime->Context().Size() == 2 && !fixture.Second().CanClear(), "actual borrowed rows block concrete task retirement");
	std::size_t sibling_releases = 0;
	sgre::scene::ParallelTaskCallbacks sibling;
	sibling.Release = [&](auto) {
		++sibling_releases;
	};
	static_cast<void>(fixture.Tasks.Group(sgre::scene::MotionTaskGroup::CommonFirst).Append(std::move(sibling)));
	RequireFailure([&] {
		fixture.Tasks.Clear();
	},
	    "all actual group borrows");
	Require(sibling_releases == 0 && fixture.Second().Count() == 2 && fixture.Runtime->PlayerCount() == 2, "all-group preflight prevents partial sibling cleanup");
	RequireFailure([&] {
		first.Play("main", sgre::scene::MotionPlayFlags::Force);
	},
	    "queued row borrows");
	first.RequestRetire();
	second.RequestRetire();
	RequireFailure([&] {
		first.PostUpdate();
	},
	    "pending retirement");
	RequireFailure([&] {
		second.PostUpdate();
	},
	    "pending retirement");
	RequireFailure([&] {
		fixture.Second().Update();
	},
	    "every actual dependent borrow");
	Require(fixture.Second().Count() == 2 && second.IsLive() && first.IsLive(), "status2 preflight rejects before unlinking the unborrowed leading sibling");
	fixture.Runtime->Context().Clear();
	fixture.Second().Update();
	fixture.Tasks.Clear();
	Require(sibling_releases == 1 && fixture.Second().Count() == 0 && fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->PlayerCount() == 0, "consumption permits exact status2 and group cleanup retry with all node IDs returned");
}

static void TestFailuresAndSeparatePostPhase() {
	RuntimeFixture fixture;
	auto task = fixture.Attach();
	task.SetCharacter("character");
	task.SetVisible(true);
	task.Play("main");
	std::size_t nested = 0;
	fixture.OnEvent = [&] {
		RequireFailure([&] {
			fixture.Tasks.Clear();
		},
		    "all actual group borrows");
		RequireFailure([&] {
			task.PostUpdate();
		},
		    "idle owner");
		RequireFailure([&] {
			task.Play("main", sgre::scene::MotionPlayFlags::Force);
		},
		    "live idle player");
		++nested;
	};
	fixture.Second().Update();
	Require(nested == 3 && task.IsLive(), "actual detached callback receiver cannot clear active task ownership or reenter its selected phases");
	fixture.OnEvent = [] {
		throw std::runtime_error("listener failed");
	};
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	RequireFailure([&] {
		fixture.Second().Update();
	},
	    "listener failed");
	Require(task.Player().PublishedRows().empty() && fixture.Second().Count() == 1, "failed event delivery invalidates publication without dropping its containing task");
	fixture.OnEvent = {};
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	fixture.Second().Update();
	const auto delivered = fixture.Events.size();
	task.PostUpdate();
	Require(fixture.Events.size() == delivered && task.Player().PublishedRows().size() == 2, "successful synchronous Update drains once; separately requested PostUpdate remains empty");
	RequireFailure([&] {
		task.Play("main", sgre::scene::MotionPlayFlags::Delay1);
	},
	    "delayed selection");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, true, "", [](const auto&) {
		}));
	},
	    "synchronous mode");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "", {}));
	},
	    "real event listener");
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
};

static void TestGenericReleasePreflight() {
	sgre::scene::ParallelTaskSet tasks(4);
	bool consumed = false;
	std::size_t releases = 0;
	sgre::scene::ParallelTaskCallbacks first;
	first.Release = [&](auto) {
		++releases;
	};
	sgre::scene::ParallelTaskCallbacks second = first;
	second.CanRelease = [&](auto) {
		return consumed;
	};
	const auto head = tasks.Append(std::move(first));
	const auto tail = tasks.Append(std::move(second));
	tasks.SetStatus(head, sgre::scene::TaskStatus::Retire);
	RequireFailure([&] {
		tasks.Update();
	},
	    "every actual dependent borrow");
	Require(releases == 0 && tasks.Count() == 2 && tasks.Contains(head) && tasks.Contains(tail), "all-owner preflight preserves both linked records before status2 removal");
	consumed = true;
	tasks.Update();
	auto lease = tasks.Extract(tail);
	consumed = false;
	RequireFailure([&] {
		lease.Release();
	},
	    "dependent borrows");
	Require(!lease.IsEmpty() && releases == 1, "detached concrete ownership survives a failed borrow-release preflight");
	consumed = true;
	lease.Release();
	Require(releases == 2 && lease.IsEmpty(), "detached borrow release retry drops the real payload once");
}

static void TestSourcePreparationBorrowAndFlushRetry() {
	RuntimeFixture fixture;
	auto task = fixture.Attach();
	task.SetCharacter("character");
	task.SetVisible(true);
	std::size_t checks = 0;
	fixture.OnAlias = [&] {
		RequireFailure([&] {
			fixture.Tasks.Clear();
		},
		    "all actual group borrows");
		RequireFailure([&] {
			task.Play("main", sgre::scene::MotionPlayFlags::Force);
		},
		    "live idle player");
		++checks;
	};
	task.Play("main");
	Require(checks == 1 && fixture.Second().Count() == 1, "actual selection preparation cannot retire or reenter its containing task");
	fixture.Second().Update();
	fixture.Second().Draw(7);
	FrameOutput output;
	RequireFailure([&] {
		fixture.Runtime->Context().Flush(output.Globals, output.Scratch, [](const auto&) {
			throw std::runtime_error("sink failed");
		});
	},
	    "sink failed");
	Require(fixture.Runtime->Context().Size() == 2 && !fixture.Second().CanClear(), "failed actual sink preserves queued owner borrows for retry");
	fixture.Runtime->Context().Flush(output.Globals, output.Scratch, [&](const auto& event) {
		RequireFailure([&] {
			fixture.Tasks.Clear();
		},
		    "all actual group borrows");
		if (const auto* packet = std::get_if<sgre::scene::MotionBatchSubmission>(&event)) {
			output.Vertices += packet->Positions.size();
			output.IndexCount += packet->Indices.size();
		}
	});
	Require(output.Vertices == 8 && output.IndexCount == 10 && fixture.Runtime->Context().Size() == 0 && fixture.Second().CanClear(), "real selected texture/geometry packet counts: " + std::to_string(output.Vertices) + "/" + std::to_string(output.IndexCount));
	fixture.Tasks.Clear();
	fixture.Runtime->Context().Flush(output.Globals, output.Scratch, [&](const auto&) {
		RequireFailure([&] {
			fixture.Runtime->Close();
		},
		    "row borrows");
	});
}

static void TestRegistrationExpiryAndExactNotification() {
	std::weak_ptr<const sgre::resource::MotionResource> backing;
	{
		RuntimeFixture fixture;
		std::size_t notifications = 0;
		sgre::scene::MotionPlayerCommands commands(fixture.Manager, [&](const auto& selected, auto) {
			Require(selected.Project == fixture.Project && selected.Timeline == &fixture.Resource->Objects[0].Timelines[0], "installation callback names the actual selector result");
			++notifications;
		});
		commands.SetCharacter("character");
		commands.Play("main");
		commands.Play("main");
		commands.Play("main", sgre::scene::MotionPlayFlags::AsCan);
		commands.Play("main", sgre::scene::MotionPlayFlags::Force);
		commands.Play("missing", sgre::scene::MotionPlayFlags::Force);
		Require(notifications == 2 && commands.CachedTimeline() != nullptr && !commands.SelectedProject(), "only real successful preparation notifies, including identical-pointer Force and excluding failed/unchanged/as-can lookup");
		auto task = fixture.Attach();
		task.SetCharacter("character");
		task.SetVisible(true);
		task.Play("main");
		fixture.Second().Update();
		fixture.Second().Draw(0);
		backing = fixture.Resource;
		fixture.Resource.reset();
		Require(fixture.Manager.Unregister(fixture.Project), "actual final unregistration must release its model/texture owner");
		Require(!backing.expired() && !task.Player().ResourcesLive(), "native current atoms retain immutable CPU metadata while active selected resources become unavailable");
		Require(!fixture.Manager.Contains(fixture.Project) && fixture.Manager.Count() == 0 && fixture.Cache.Count() == 0 && fixture.Uploads == fixture.Releases, "retained CPU metadata must not retain a manager registration or texture owner");
		FrameOutput output;
		RequireFailure([&] {
			fixture.Runtime->Context().Flush(output.Globals, output.Scratch, [](const auto&) {
			});
		},
		    "expired");
		fixture.Runtime->Context().Clear();
		RequireFailure([&] {
			fixture.Second().Update();
		},
		    "live selected resource");
		fixture.Tasks.Clear();
		Require(fixture.Runtime->PlayerCount() == 0 && fixture.Runtime->Identifiers().LiveCount() == 0, "unregistered selected metadata remains untouched while actual selected IDs and task borrows retire");
		Require(!backing.expired(), "the independent current command owner still retains its immutable CPU metadata after task retirement");
	}

	Require(backing.expired(), "immutable CPU metadata expires after the actual task, current commands and runtime owners retire");
}

static void TestClearLegacySelectedOwner() {
	RuntimeFixture fixture(4);
	auto task = fixture.Attach();
	task.SetCharacter("character");
	task.SetVisible(true);
	task.Play("main");
	task.Progress(2);
	task.SetVariable("retained", 4);
	const auto serial = task.Commands().SelectionSerial();
	const auto tick = task.Commands().ClockState().TickCount;
	fixture.Second().Draw(0);
	RequireFailure([&] {
		task.Clear();
	},
	    "queued row borrows");
	fixture.Runtime->Context().Clear();
	task.Clear();
	task.Clear();
	Require(task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0 && task.Player().PublishedRows().empty() && task.Commands().ParameterValues().RegisteredParameterCount() == 1 && task.GetVariable("retained") == 4, "legacy clear must retire actual selected nodes/source borrows while retaining containing parameter registrations and local values/task");
	fixture.Second().Draw(0);
	Require(fixture.Runtime->Context().Size() == 0, "legacy clear immediately publishes its real empty row queue");
	task.Progress(1);
	Require(task.Commands().ClockState().TickCount == tick + 1 && task.Commands().ClockState().Playing && task.Commands().SelectionSerial() == serial, "legacy clear must preserve and advance its actual clock with its retained containing parameter records");
	task.Play("main");
	Require(fixture.Runtime->Identifiers().LiveCount() == 0, "legacy unchanged-name Play after clear does not fabricate a selection");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	task.Progress(0);
	Require(fixture.Runtime->Identifiers().LiveCount() == 3 && task.Player().PublishedRows().size() == 2 && task.Commands().ParameterValues().RegisteredParameterCount() == 1, "legacy genuine Force must reinstall its exact ordinary selected producers");
}

static void TestEmptyArraysAndNonemptyOwners() {
	for (std::uint32_t variant = 0; variant < 4; ++variant) {
		RuntimeFixture fixture(variant);
		auto task = fixture.Attach();
		task.SetCharacter("character");
		task.SetVisible(true);
		task.Play("main");
		fixture.Second().Update();
		Require(task.Player().Nodes().Nodes().size() == 3 && task.Player().PublishedRows().size() == 2, "native zero-count parameter/variable arrays preserve real ordinary preparation and publication");
	}
	RuntimeFixture parameter_fixture(4);
	auto parameter_task = parameter_fixture.Attach();
	parameter_task.SetCharacter("character");
	parameter_task.SetVisible(true);
	parameter_task.Play("main");
	parameter_fixture.Second().Update();
	Require(parameter_task.Player().Nodes().Nodes().size() == 3 && parameter_task.Player().PublishedRows().size() == 2 && parameter_task.Commands().ParameterValues().RegisteredParameterCount() == 1, "valid parameter metadata must register in its actual commands before ordinary row publication");

	for (const auto variant : {16u, 8u}) {
		RuntimeFixture fixture(variant);
		auto task = fixture.Attach();
		task.SetCharacter("character");
		RequireFailure([&] {
			task.Play("main");
		},
		    variant == 16 ? "parameter definition requires field id" : "motion timeline variable requires field label");
		Require(fixture.Runtime->Identifiers().LiveCount() == 0 && task.Player().PublishedRows().empty(), "malformed parameter and variable metadata rejects before actual selected IDs or rows");
	}
}

class Reader {
public:
	explicit Reader(std::string_view path)
	    : m_Input(std::string(path), std::ios::binary) {
		Require(static_cast<bool>(m_Input), "cannot read full preparation native fixture");
	}

	std::uint32_t Word() {
		std::array<char, 4> bytes{};
		m_Input.read(bytes.data(), 4);
		Require(static_cast<bool>(m_Input), "full preparation native fixture truncated");

		return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0])) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8 | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16 | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24;
	}

	void End() {
		Require(m_Input.peek() == std::char_traits<char>::eof(), "full preparation native fixture has trailing bytes");
	}

private:
	std::ifstream m_Input;
};

static void ReplayNative(std::string_view path) {
	Reader reader(path);
	Require(reader.Word() == 0x4d4d4f54 && reader.Word() == 112, "full preparation fixture header differs");
	std::size_t preparations = 0;
	for (std::uint32_t group = 0; group < 16; ++group) {
		RuntimeFixture fixture(group % 4);
		for (auto& seed : fixture.Seed) {
			seed = reader.Word();
		}
		auto task = fixture.Attach();
		task.SetCharacter("character");
		task.SetVisible(true);
		for (std::uint32_t step = 0; step < 7; ++step) {
			std::array<std::uint32_t, 20> expected{};
			for (auto& value : expected) {
				value = reader.Word();
			}
			Require(expected[0] == group, "native case sequence differs");
			task.Play("main", static_cast<sgre::scene::MotionPlayFlags>(expected[1]));
			preparations += expected[2];
			fixture.Second().Update();
			task.PostUpdate();
			fixture.Second().Draw(0xfedcba98);
			const auto nodes = task.Player().Nodes().Nodes();
			for (std::size_t node = 0; node < 3; ++node) {
				Require(nodes[node].Identifier == expected[node + 3], "native full preparation/Force selected node ID differs");
			}
			Require(task.Player().PublishedRows().size() == expected[6], "native containing persistent row count differs");
			const auto& clock = task.Commands().ClockState();
			const std::array<std::uint32_t, 4> words = {std::bit_cast<std::uint32_t>(clock.TickCount), std::bit_cast<std::uint32_t>(clock.EvaluationTime), std::bit_cast<std::uint32_t>(clock.LoopTime), std::bit_cast<std::uint32_t>(clock.LastTime)};
			for (std::size_t index = 0; index < words.size(); ++index) {
				Require(words[index] == expected[index + 7], "native full preparation/CHAIN/update float word differs");
			}
			Require(clock.Playing == (expected[11] != 0) && clock.TickAssigned == (expected[12] != 0) && clock.InitialCompositionPending == (expected[13] != 0) && clock.InitialTagDispatchPending == (expected[14] != 0), "native selected control flags differ");
			Require(fixture.Runtime->Context().Size() == expected[15], "native task Draw/shared context append count differs");
			for (std::size_t index = 0; index < 4; ++index) {
				Require(task.Random().State()[index] == expected[index + 16], "native full-new real manager seed/player random coupling differs");
			}
			fixture.Runtime->Context().Clear();
		}
	}
	reader.End();
	Require(preparations == 64, "native successful preparation count differs");
	std::cout << "native full-new/Play/install/Force/CHAIN/selected Update/PostUpdate/Draw: 112 calls, 64 real preparations, 16 actual seeds PASS\n";
}

int main(int argc, char** argv) {
	try {
		TestLegacyCurrentPreparationHistory();
		TestRealInitialAndSharedOwners();
		TestInstallationAndQueuedLifetime();
		TestFailuresAndSeparatePostPhase();
		TestGenericReleasePreflight();
		TestSourcePreparationBorrowAndFlushRetry();
		TestRegistrationExpiryAndExactNotification();
		TestClearLegacySelectedOwner();
		TestEmptyArraysAndNonemptyOwners();
		if (argc == 2) {
			ReplayNative(argv[1]);
		} else {
			Require(argc == 1, "usage: motion_player_task_tests [native-full-preparation.bin]");
		}
		std::cout << "motion containing manager/new task/selection/row lifetime independent fixtures PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
