#include "sgre/render/motion_sprite_frame.hpp"

#include "sprite_presentation_bridge.hpp"
#include "sprite_presentation_fixture.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <sys/types.h>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected rejected frame: " + std::string(message));
}

static sgre::resource::PsbValue Integer(std::int64_t value) {
	sgre::resource::PsbValue result;
	result.Data = value;

	return result;
}

static sgre::resource::PsbValue Text(std::string value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Coordinate() {
	sgre::resource::PsbValue result;
	result.Data = sgre::resource::PsbArray{Integer(2), Integer(2), Integer(0)};

	return result;
}

static std::shared_ptr<sgre::resource::MotionResource> Model(std::uint32_t original_icon_attributes = 0, std::optional<std::uint32_t> explicit_color = std::nullopt, std::uint32_t blend_mode = 0x10) {
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionSource source;
	source.Name = "atlas";
	source.Texture.SerializedValueOffset = 100;
	source.Texture.Format = sgre::resource::PixelFormat::Alpha8;
	source.Texture.Width = 4;
	source.Texture.Height = 4;
	source.Texture.TruncatedWidth = 4;
	source.Texture.TruncatedHeight = 4;
	source.Texture.PixelRange.Size = 16;
	sgre::resource::MotionIcon icon;
	icon.Name = "tile";
	icon.Width = 4;
	icon.Height = 4;
	icon.AttributeFlags = original_icon_attributes;
	source.Icons.push_back(std::move(icon));
	model->Sources.push_back(std::move(source));
	sgre::resource::MotionLayer layer;
	layer.Label = "sprite";
	layer.Type = 0;
	layer.InheritMask = 4;
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Properties = {{"stencilType", Integer(0)}, {"objTriPriority", Integer(0)}, {"meshTransform", Integer(0)}, {"parameterize", sgre::resource::PsbValue{}}, {"joinTarget", Integer(0)}, {"groundCorrection", Integer(0)}};
	for (std::uint32_t index = 0; index < 3; ++index) {
		sgre::resource::MotionFrame frame;
		frame.Time = static_cast<float>(index * 4);
		frame.Type = index == 2 ? 0 : 2;
		frame.Content.Data = sgre::resource::PsbObject{{"mask", Integer(2 | 1 | 0x400 | 0x20000 | (explicit_color ? 0x200 : 0))}, {"src", Text("atlas")}, {"icon", Text("tile")}, {"coord", Coordinate()}, {"ox", Integer(0)}, {"oy", Integer(0)}, {"opa", Integer(255)}, {"bm", Integer(blend_mode)}};
		if (explicit_color) {
			std::get<sgre::resource::PsbObject>(frame.Content.Data).emplace_back("color", Integer(*explicit_color));
		}

		layer.Frames.push_back(std::move(frame));
	}
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "main";
	timeline.LastTime = 8;
	timeline.Layers.push_back(std::move(layer));
	for (std::uint32_t index = 0; index < 2; ++index) {
		sgre::resource::MotionFrame priority;
		priority.Time = static_cast<float>(index * 8);
		priority.Type = 1;
		if (index == 0) {
			priority.Content.Data = sgre::resource::PsbArray{Integer(0)};
		}
		timeline.Priority.push_back(std::move(priority));
	}
	sgre::resource::MotionObject object;
	object.Name = "character";
	object.Timelines.push_back(std::move(timeline));
	model->Objects.push_back(std::move(object));

	return model;
}

static sgre::render::SpritePassDescription Description(std::size_t vertices = 64) {
	sgre::render::SpritePassDescription result;
	result.Width = 8;
	result.Height = 8;
	result.MaxVertices = vertices;
	result.MaxIndices = 128;
	result.MaxDraws = 16;
	result.MaxTextures = 8;
	result.MaxPipelines = 48;

	return result;
}

static constexpr std::array<float, 16> kPhysicalProjection = {0.25f, 0, 0, -1, 0, -0.25f, 0, 1, 0, 0, 0, 0.5f, 0, 0, 0, 1};

struct SceneFixture {
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionResourceManager Manager;
	std::shared_ptr<sgre::resource::MotionResource> Resource;
	std::vector<std::uint8_t> Bytes;
	sgre::scene::MotionProjectIdentity Identity;
	sgre::scene::MotionProjectId Project;
	sgre::scene::MotionSourceRegistration Registration;
	std::unique_ptr<sgre::scene::PreparedMotionSourceRegistry> Registry;
	sgre::scene::PreparedMotionTimeline Prepared;
	sgre::scene::MotionPlayerCommands Commands;
	sgre::scene::MotionRootScene Root;
	sgre::scene::MotionPlayerRenderControls Render;
	sgre::scene::MotionLayerIdentifierPool Identifiers;
	sgre::scene::MotionSpriteDrawContext Context;
	std::unique_ptr<sgre::scene::MotionSelectedSpritePlayer> Player;
	std::unique_ptr<sgre::render::MotionSpriteFrameRenderer> Frame;

	explicit SceneFixture(sgre::render::SpritePassRenderer& renderer, std::uint32_t original_icon_attributes = 0, std::optional<std::uint32_t> explicit_color = std::nullopt, std::uint32_t blend_mode = 0x10) : Cache([&renderer](auto format, const auto& image) {
		                                                                                                                                                                                                             return renderer.UploadTexture(format, image);
	                                                                                                                                                                                                             },
	                                                                                                                                                                                                                 [&renderer](auto texture) {
		                                                                                                                                                                                                                 renderer.ReleaseTexture(texture);
	                                                                                                                                                                                                                 }),
	                                                                                                                                                                                                             Manager(Cache), Resource(Model(original_icon_attributes, explicit_color, blend_mode)), Bytes(256, 255), Commands(Manager), Context(16) {
		Identity.DocumentInstance = Cache.AllocateDocumentIdentity();
		Identity.RootOffset = 100;
		Project = Manager.Register(Identity, Resource, Bytes);
		Registration.Project = Identity;
		Registration.Sources = Resource->Sources;
		Registry = std::make_unique<sgre::scene::PreparedMotionSourceRegistry>(std::span<const sgre::scene::MotionSourceRegistration>(&Registration, 1));
		Prepared = sgre::scene::PrepareMotionTimeline(Resource->Objects[0].Timelines[0], *Resource);
		Commands.SetCharacter("character");
		Commands.Play("main");
		Root.SetVisible(true);
		sgre::scene::MotionSelectedSpriteServices services;
		services.ReadFrameDelta = [] {
			return 0.0f;
		};
		services.ReadDepthProjection = [] {
			return 0.0f;
		};
		services.Construction.ReadManagerRandom = [this](const auto& manager) {
			Require(&manager == &Manager, "selected construction lost its actual manager");

			return 0.5f;
		};
		services.Textures.FindTexture = [this](const auto& source) {
			Require(source.Project == Identity && source.Source == &Resource->Sources[0], "selected texture lookup lost original resource identity");

			return Manager.FindSource(Project, source.Source->Name);
		};
		services.DeliverEvent = [](const auto&) {
			throw std::runtime_error("fixture has no authored action/event producer");
		};
		Player = std::make_unique<sgre::scene::MotionSelectedSpritePlayer>(Commands, Root, Render, *Resource, Resource->Objects[0].Timelines[0], Prepared, *Registry, Identifiers, Context, std::move(services));
		Frame = std::make_unique<sgre::render::MotionSpriteFrameRenderer>(renderer, Cache, sgre::scene::SpritePipelineOptions{}, 1);
		Player->Update();
		Require(Player->Rows().size() == 1 && Player->PublishedRows().size() == 1, "actual selected sprite did not publish its persistent row");
		Frame->BindTexture(0, Player->Rows()[0].Texture);
	}

	~SceneFixture() {
		Context.Clear();
		Player.reset();
		Frame.reset();
		Manager.Close();
		Cache.Close();
	}
};

struct FrameInputs {
	std::array<sgre::resource::MotionPoint, 1> Sizes{{{4, 4}}};
	std::array<sgre::resource::MotionPoint, 64> Positions;
	std::array<sgre::resource::MotionPoint, 64> Uvs;
	std::array<std::uint32_t, 64> Colors;
	std::array<std::uint32_t, 128> Indices;
	std::array<sgre::scene::MotionMaskOperation, 8> Masks;
	std::array<sgre::scene::MotionMaskedBlendPass, 8> Blends;
	sgre::scene::MotionPreparedFrame Globals;
	sgre::scene::MotionIntegerScissor Device{0, 0, 8, 8};
	sgre::scene::MotionFrameScratch Scratch;

	FrameInputs() {
		Globals.TextureSizes = Sizes;
		Globals.Viewport = {8, 8};
		Globals.InitialScissor = {0, 0, 8, 8};
		Globals.AppliedRoundedScissor = Globals.InitialScissor;
		Globals.TargetBounds = {-4, -4, 4, 4};
		Globals.AxisScale = {1, 1};
		Scratch.Batch.Positions = Positions;
		Scratch.Batch.Uvs = Uvs;
		Scratch.Batch.Colors = Colors;
		Scratch.Batch.Indices = Indices;
		Scratch.MaskOperations = Masks;
		Scratch.BlendPasses = Blends;
	}
};

enum class HiddenFixtureProfile {
	Lifetime,
	Pool,
	Slots
};

struct HiddenFixtureResults {
	std::size_t Queued = 0;
	std::size_t Unavailable = 0;
	std::size_t Busy = 0;
	std::size_t SourceBusy = 0;
	std::size_t DrainedCommands = 0;
	std::size_t BorrowedPoolDrawables = 0;
	std::string_view FirstStatus = "unobserved";
	std::string_view DestinationMismatch = "unobserved";
	std::string_view PoolExhaustion = "unobserved";
	std::string_view SoftwareSlots = "NOT_ATTEMPTED_OTHER_PROFILE";
	std::string_view GateUnwind = "NOT_ATTEMPTED_OTHER_PROFILE";
};

class ForegroundWitness {
public:
	ForegroundWitness() : m_ForegroundPid(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier) {
		Require(m_ForegroundPid > 0, "fixture has no observable foreground process");
		Require(NSApp == nil || (!NSApp.active && NSApp.windows.count == 0), "fixture requires an initially inactive application without existing windows");
	}

	void RequirePreserved() const {
		Require(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == m_ForegroundPid, "hidden fixture changed the observed foreground process");
		Require(NSApp == nil || !NSApp.active, "hidden fixture application became active");
		for (NSWindow* window in NSApp.windows) {
			Require(!window.visible && !window.keyWindow && !window.mainWindow, "hidden fixture window became visible, key or main");
		}
	}

private:
	pid_t m_ForegroundPid = 0;
};

template <typename Operation>
static void RequireWorkerRejection(Operation operation) {
	std::exception_ptr error;
	std::thread worker([&] {
		try {
			operation();
		} catch (...) {
			error = std::current_exception();
		}
	});
	worker.join();
	RequireError([&] {
		if (error) {
			std::rethrow_exception(error);
		}
	},
	    "main thread");
}

static void RequireNoRequestOrRetentionChange(const sgre::render::SpritePresentationFixtureObservation& before, const sgre::render::SpritePresentationFixtureObservation& after) {
	Require(after.DrawableRequests == before.DrawableRequests && after.RetainedTransfers == before.RetainedTransfers && after.RetainedDrawables == before.RetainedDrawables, "rejected presentation requested a drawable or changed retained transfers");
}

static void RequireHiddenOwner(const sgre::render::SpritePassPresentation& presentation) {
	const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(!observation.ApplicationActive && !observation.WindowVisible && !observation.WindowKey && !observation.WindowMain && observation.ViewAttached && observation.LayerAttached, "actual hidden owner state or attachments differ");
	Require(observation.RetainedTransfers <= 3 && observation.RetainedDrawables == observation.RetainedTransfers, "actual hidden transfer retention exceeds its three slots or lost its drawable");
}

static sgre::render::MotionSpriteFrameSubmission SubmitActualFixture(SceneFixture& fixture) {
	FrameInputs input;
	fixture.Player->Draw();
	const auto submission = fixture.Frame->Submit(fixture.Context, input.Globals, input.Scratch, input.Device, kPhysicalProjection, {}, sgre::render::SpritePassLoadAction::Clear);
	Require(submission.Identity != 0 && fixture.Context.Size() == 0 && fixture.Player->PublishedRows().size() == 1, "actual selected prepared frame did not consume its row into a nonzero renderer submission");

	return submission;
}

static sgre::render::SpritePresentationSubmission Tagged(sgre::render::SpritePassRenderer& renderer, std::uint64_t identity) {
	sgre::render::SpritePresentationSubmission result;
	result.Owner = &renderer;
	result.Identity = identity;

	return result;
}

class ActualGeometryMutation {
public:
	explicit ActualGeometryMutation(const sgre::render::SpritePassPresentation& presentation) {
		const auto objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(presentation);
		m_View = objects.View;
		m_Layer = objects.Layer;
		Require(m_View != nil && m_Layer != nil, "actual owned geometry objects expired before fixture scope");
		m_Bounds = m_View.bounds;
		m_LayerFrame = m_Layer.frame;
		m_LayerBounds = m_Layer.bounds;
		m_LayerScale = m_Layer.contentsScale;
		m_DrawableSize = m_Layer.drawableSize;
	}

	~ActualGeometryMutation() {
		// This scope always ends while the main-thread presentation owner still lives.
		m_View.bounds = m_Bounds;
		m_Layer.frame = m_LayerFrame;
		m_Layer.bounds = m_LayerBounds;
		m_Layer.contentsScale = m_LayerScale;
		m_Layer.drawableSize = m_DrawableSize;
	}

	ActualGeometryMutation(const ActualGeometryMutation&) = delete;
	ActualGeometryMutation& operator=(const ActualGeometryMutation&) = delete;

	void ChangeViewWidth(double scale) {
		m_View.bounds = NSMakeRect(0, 0, m_Bounds.size.width + 1 / scale, m_Bounds.size.height);
	}

	void ChangeDrawableWidth() {
		m_Layer.drawableSize = CGSizeMake(m_DrawableSize.width + 1, m_DrawableSize.height);
	}

private:
	NSView* m_View = nil;
	CAMetalLayer* m_Layer = nil;
	NSRect m_Bounds{};
	CGRect m_LayerFrame{};
	CGRect m_LayerBounds{};
	double m_LayerScale = 0;
	CGSize m_DrawableSize{};
};

static void TestActualGeometry(sgre::render::SpritePassPresentation& presentation, sgre::render::SpritePresentationSubmission latest) {
	const auto geometry = presentation.Geometry();
	Require(geometry.WidthPixels == 8 && geometry.HeightPixels == 8 && geometry.BackingScale > 0 && geometry.WidthPoints * geometry.BackingScale == 8 && geometry.HeightPoints * geometry.BackingScale == 8, "actual backing scale conversion differs from the real eight-pixel renderer target");
	const auto before = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	{
		ActualGeometryMutation mutation(presentation);
		mutation.ChangeViewWidth(geometry.BackingScale);
		RequireError([&] {
			static_cast<void>(presentation.TryPresentLatest(latest));
		},
		    "actual backing dimensions");
	}
	{
		ActualGeometryMutation mutation(presentation);
		mutation.ChangeDrawableWidth();
		RequireError([&] {
			static_cast<void>(presentation.TryPresentLatest(latest));
		},
		    "layer device, format or physical geometry changed");
	}
	const auto restored = presentation.Geometry();
	Require(restored.WidthPixels == geometry.WidthPixels && restored.HeightPixels == geometry.HeightPixels && restored.WidthPoints == geometry.WidthPoints && restored.HeightPoints == geometry.HeightPoints && restored.BackingScale == geometry.BackingScale, "actual geometry RAII scope failed to restore the owned view and layer");
	RequireNoRequestOrRetentionChange(before, sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation));
}

static void PollActualTransfers(sgre::render::SpritePassPresentation& presentation) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!presentation.PollTransfers()) {
		Require(std::chrono::steady_clock::now() < deadline, "actual hidden transfers exceeded the bounded fixture poll deadline");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(observation.RetainedTransfers == 0 && observation.RetainedDrawables == 0, "completed actual transfers retained drawable references");
}

static sgre::render::SpritePresentationStatus ObserveActualPresentation(sgre::render::SpritePassPresentation& presentation, sgre::render::SpritePassRenderer& renderer, std::uint64_t identity, HiddenFixtureResults& results) {
	const auto before = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	const auto status = presentation.TryPresentLatest(Tagged(renderer, identity));
	const auto after = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	RequireHiddenOwner(presentation);
	switch (status) {
	case sgre::render::SpritePresentationStatus::QueuedHidden:
		++results.Queued;
		Require(after.DrawableRequests == before.DrawableRequests + 1 && after.RetainedTransfers != 0, "queued actual transfer did not request and retain its genuine drawable");
		break;
	case sgre::render::SpritePresentationStatus::DrawableUnavailable:
		++results.Unavailable;
		Require(after.DrawableRequests == before.DrawableRequests + 1 && after.RetainedTransfers <= before.RetainedTransfers && after.RetainedDrawables <= before.RetainedDrawables, "nil drawable created a transfer or retried the actual request");
		break;
	case sgre::render::SpritePresentationStatus::Busy:
		++results.Busy;
		Require(after.DrawableRequests == before.DrawableRequests && after.RetainedTransfers == 3 && after.RetainedDrawables == 3, "Busy was not an actual full three-slot condition before drawable acquisition");
		break;
	}
	// The request cannot consume or advance the renderer identity, even when nil or Busy.
	const auto source = sgre::render::SpritePassPresentationBridge::CreateSource(renderer);
	static_cast<void>(source.RequireLatest(identity));

	return status;
}

static void TestActualWrongDestination(sgre::render::SpritePassPresentation& presentation, sgre::render::SpritePassRenderer& renderer, HiddenFixtureResults& results) {
	const auto source = sgre::render::SpritePassPresentationBridge::CreateSource(renderer);
	CAMetalLayer* other = [CAMetalLayer layer];
	other.device = source.Device;
	other.pixelFormat = MTLPixelFormatBGRA8Unorm;
	other.framebufferOnly = YES;
	other.allowsNextDrawableTimeout = YES;
	other.maximumDrawableCount = 3;
	other.drawableSize = CGSizeMake(source.Width + 1, source.Height);
	id<CAMetalDrawable> actual = [other nextDrawable];
	if (actual == nil) {
		results.DestinationMismatch = "DRAWABLE_UNAVAILABLE_NO_DESTINATION_CLAIM";

		return;
	}

	RequireError([&] {
		sgre::render::SpritePassPresentationFixtureBridge::RequireDestination(presentation, actual);
	},
	    "drawable device, format or physical dimensions differ");
	results.DestinationMismatch = "PASS_ACTUAL_DIFFERENT_EXTENT_DRAWABLE";
}

class ActualTransferGate {
public:
	explicit ActualTransferGate(const sgre::render::MetalSpritePresentationSource& source) {
		Require(source.Device != nil && source.Queue != nil && source.Queue.device == source.Device, "event gate requires the actual same-device source queue");
		m_Event = [source.Device newSharedEvent];
		Require(m_Event != nil && m_Event.signaledValue == 0, "event gate requires a fresh actual zero-valued shared event");
		m_Commands = [source.Queue commandBuffer];
		Require(m_Commands != nil && m_Commands.commandQueue == source.Queue, "event gate requires an actual command buffer on the borrowed source queue");
		[m_Commands encodeWaitForEvent:m_Event value:1];
		[m_Commands commit];
		m_Committed = true;
	}

	~ActualTransferGate() noexcept {
		Retire();
	}
	ActualTransferGate(const ActualTransferGate&) = delete;
	ActualTransferGate& operator=(const ActualTransferGate&) = delete;

	[[nodiscard]] id<MTLCommandBuffer> Commands() const {
		return m_Commands;
	}

	void RequireBlocked() const {
		Require(m_Committed && m_Event.signaledValue == 0 && m_Commands.status != MTLCommandBufferStatusCompleted && m_Commands.status != MTLCommandBufferStatusError, "actual event gate unexpectedly retired before its CPU signal");
	}

	void SignalAndWait() {
		Retire();
		Require(m_Commands.status == MTLCommandBufferStatusCompleted, "actual event gate did not complete after its CPU signal");
	}

private:
	void Retire() noexcept {
		if (!m_Committed) {
			return;
		}

		// Open the real gate before waiting, including every C++ exception exit.
		m_Event.signaledValue = 1;
		[m_Commands waitUntilCompleted];
		m_Committed = false;
	}

	id<MTLSharedEvent> m_Event = nil;
	id<MTLCommandBuffer> m_Commands = nil;
	bool m_Committed = false;
};

static void TestActualGateUnwind(sgre::render::SpritePassPresentation& presentation, sgre::render::SpritePassRenderer& renderer, std::uint64_t identity, const sgre::render::MetalSpritePresentationSource& source, HiddenFixtureResults& results) {
	id<MTLCommandBuffer> actual_commands = nil;
	RequireError([&] {
		ActualTransferGate gate(source);
		actual_commands = gate.Commands();
		gate.RequireBlocked();
		Require(ObserveActualPresentation(presentation, renderer, identity, results) == sgre::render::SpritePresentationStatus::QueuedHidden, "event unwind fixture requires one actual hidden transfer");
		const auto retained = sgre::render::SpritePassPresentationFixtureBridge::RetainCommands(presentation);
		Require(retained[0].Transfer != nil && retained[0].Transfer.status != MTLCommandBufferStatusCompleted && retained[0].Transfer.status != MTLCommandBufferStatusError, "event unwind fixture requires its actual incomplete transfer");
		throw std::runtime_error("actual event gate unwind sentinel");
	},
	    "actual event gate unwind sentinel");
	Require(actual_commands != nil && actual_commands.status == MTLCommandBufferStatusCompleted, "exception exit did not signal and wait for its actual event gate");
	PollActualTransfers(presentation);
	results.GateUnwind = "PASS_ACTUAL_EVENT_SIGNAL_AND_WAIT_THEN_TRANSFER_RETIREMENT";
}

static void RequireActualIncompleteTransfers(sgre::render::SpritePassPresentation& presentation, const sgre::render::MetalSpritePresentationSource& source) {
	const auto retained = sgre::render::SpritePassPresentationFixtureBridge::RetainCommands(presentation);
	for (const auto& commands : retained) {
		Require(commands.Transfer != nil && commands.Producer != nil && commands.Transfer.commandQueue == source.Queue && commands.Producer.commandQueue == source.Queue, "full software slots did not retain three actual same-queue commands");
		Require(commands.Producer.status == MTLCommandBufferStatusCompleted && commands.Transfer.status != MTLCommandBufferStatusCompleted && commands.Transfer.status != MTLCommandBufferStatusError, "slot fixture requires the actual completed producer and incomplete transfers");
	}
}

static void TestActualSoftwareSlots(sgre::render::SpritePassPresentation& presentation, sgre::render::SpritePassRenderer& renderer, std::uint64_t identity, HiddenFixtureResults& results) {
	const auto source = sgre::render::SpritePassPresentationBridge::CreateSource(renderer);
	const auto initialized = source.RequireLatest(identity);
	Require(initialized.Producer != nil && initialized.Producer.commandQueue == source.Queue, "slot fixture requires the actual latest same-queue producer");
	[initialized.Producer waitUntilCompleted];
	Require(initialized.Producer.status == MTLCommandBufferStatusCompleted, "slot fixture producer did not complete before the event gate");
	const auto before_unwind = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(before_unwind.RetainedTransfers == 0 && before_unwind.RetainedDrawables == 0, "slot fixture requires actual completed presentation retirement before its gate");
	TestActualGateUnwind(presentation, renderer, identity, source, results);
	const auto empty = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(empty.RetainedTransfers == 0 && empty.RetainedDrawables == 0, "exception gate fixture did not retire its actual transfer before slot admission");

	// This owner unwinds before the caller can drain or destroy presentation.
	ActualTransferGate gate(source);
	gate.RequireBlocked();
	for (std::size_t index = 0; index < 3; ++index) {
		Require(ObserveActualPresentation(presentation, renderer, identity, results) == sgre::render::SpritePresentationStatus::QueuedHidden, "slot fixture requires three genuine available hidden drawables");
		gate.RequireBlocked();
	}
	const auto full = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(full.RetainedTransfers == 3 && full.RetainedDrawables == 3 && full.DrawableRequests == empty.DrawableRequests + 3, "slot fixture did not fill three actual software transfer and drawable owners");
	RequireActualIncompleteTransfers(presentation, source);
	Require(ObserveActualPresentation(presentation, renderer, identity, results) == sgre::render::SpritePresentationStatus::Busy, "actual fourth software-slot request did not return Busy");
	RequireNoRequestOrRetentionChange(full, sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation));
	RequireActualIncompleteTransfers(presentation, source);
	gate.RequireBlocked();
	gate.SignalAndWait();
	PollActualTransfers(presentation);
	results.SoftwareSlots = "PASS_THREE_ACTUAL_INCOMPLETE_TRANSFERS_AND_BUSY_NO_REQUEST";
}

static void TestActualDrawablePool(sgre::render::SpritePassPresentation& presentation, sgre::render::SpritePassRenderer& renderer, SceneFixture& fixture, HiddenFixtureResults& results) {
	PollActualTransfers(presentation);
	const auto objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(presentation);
	CAMetalLayer* layer = objects.Layer;
	Require(layer != nil && layer.maximumDrawableCount == 3, "actual hidden drawable pool owner or bound changed");
	std::array<id<CAMetalDrawable>, 3> held{};
	for (auto& drawable : held) {
		drawable = [layer nextDrawable];
		if (drawable == nil) {
			results.PoolExhaustion = "EARLIER_ACTUAL_POOL_REQUEST_UNAVAILABLE";

			return;
		}
		++results.BorrowedPoolDrawables;
	}

	const auto latest = SubmitActualFixture(fixture);
	Require(ObserveActualPresentation(presentation, renderer, latest.Identity, results) == sgre::render::SpritePresentationStatus::DrawableUnavailable, "three held genuine pool drawables did not produce one bounded unavailable request");
	results.PoolExhaustion = "PASS_THREE_ACTUAL_DRAWABLE_BORROWS";
}

static void RequireClosedObjects(const sgre::render::SpritePresentationFixtureObjects& objects) {
	NSWindow* window = objects.Window;
	NSView* view = objects.View;
	Require(window == nil || (!window.visible && !window.keyWindow && !window.mainWindow && window.contentView == nil && ![NSApp.windows containsObject:window]), "destroyed presentation retained an attached, visible or application-owned window");
	Require(view == nil || (view.layer == nil && view.window == nil), "destroyed presentation retained its view layer or window attachment");
	Require(NSApp.windows.count == 0, "presentation teardown did not close its only actual hidden window");
}

static void TestActualReceiverExpiry(const std::filesystem::path& sprite_shader) {
	auto renderer = std::make_unique<sgre::render::SpritePassRenderer>(Description(), sprite_shader);
	const auto source = sgre::render::SpritePassPresentationBridge::CreateSource(*renderer);
	std::weak_ptr<sgre::resource::MotionResource> resource;
	std::uint64_t identity = 0;
	{
		SceneFixture fixture(*renderer);
		resource = fixture.Resource;
		identity = SubmitActualFixture(fixture).Identity;
		const auto frame = source.RequireLatest(identity);
		Require(frame.Target != nil && frame.Producer != nil && frame.Producer.commandQueue == source.Queue && frame.Target.device == source.Device, "actual bridge lost its same-device same-queue submitted target");
	}
	Require(resource.expired(), "private presentation source pinned the actual model owner");
	renderer.reset();
	RequireError([&] {
		static_cast<void>(source.RequireLatest(identity));
	},
	    "renderer has closed");
}

static void TestHiddenBoundary(const std::filesystem::path& sprite_shader, const std::filesystem::path& transfer_shader, const ForegroundWitness& foreground, HiddenFixtureResults& results, HiddenFixtureProfile profile) {
	sgre::render::SpritePassRenderer renderer(Description(), sprite_shader);
	sgre::render::SpritePassRenderer other(Description(), sprite_shader);
	NSApplication* application_before = NSApp;
	RequireWorkerRejection([&] {
		sgre::render::SpritePassPresentation rejected(renderer, transfer_shader);
	});
	Require(NSApp == application_before, "worker-thread constructor initialized an application before rejection");
	foreground.RequirePreserved();
	sgre::render::SpritePresentationFixtureObjects closed_objects;
	std::array<sgre::render::SpritePresentationFixtureCommands, 3> retained_commands;
	std::weak_ptr<sgre::resource::MotionResource> resource;
	@autoreleasepool {
		auto presentation = std::make_unique<sgre::render::SpritePassPresentation>(renderer, transfer_shader);
		closed_objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(*presentation);
		RequireHiddenOwner(*presentation);
		const auto fresh = sgre::render::SpritePassPresentationFixtureBridge::Observe(*presentation);
		RequireError([&] {
			static_cast<void>(presentation->TryPresentLatest(Tagged(renderer, 0)));
		},
		    "exact latest initialized submission");
		RequireNoRequestOrRetentionChange(fresh, sgre::render::SpritePassPresentationFixtureBridge::Observe(*presentation));
		{
			SceneFixture fixture(renderer);
			SceneFixture other_fixture(other);
			resource = fixture.Resource;
			const auto first = SubmitActualFixture(fixture);
			const auto collision = SubmitActualFixture(other_fixture);
			Require(first.Identity == collision.Identity, "real renderer owners did not expose the intended colliding initial numeric identities");
			const auto before_rejection = sgre::render::SpritePassPresentationFixtureBridge::Observe(*presentation);
			RequireError([&] {
				static_cast<void>(presentation->TryPresentLatest(Tagged(other, collision.Identity)));
			},
			    "belongs to another renderer");
			RequireWorkerRejection([&] {
				static_cast<void>(presentation->TryPresentLatest(Tagged(renderer, first.Identity)));
			});
			RequireWorkerRejection([&] {
				static_cast<void>(presentation->PollTransfers());
			});
			RequireWorkerRejection([&] {
				static_cast<void>(presentation->Geometry());
			});
			const auto second = SubmitActualFixture(fixture);
			Require(second.Identity != first.Identity, "second actual prepared frame did not advance its renderer identity");
			RequireError([&] {
				static_cast<void>(presentation->TryPresentLatest(Tagged(renderer, first.Identity)));
			},
			    "exact latest initialized submission");
			RequireNoRequestOrRetentionChange(before_rejection, sgre::render::SpritePassPresentationFixtureBridge::Observe(*presentation));
			TestActualGeometry(*presentation, Tagged(renderer, second.Identity));
			foreground.RequirePreserved();
			const auto first_status = ObserveActualPresentation(*presentation, renderer, second.Identity, results);
			if (first_status == sgre::render::SpritePresentationStatus::DrawableUnavailable) {
				results.FirstStatus = "DRAWABLE_UNAVAILABLE";
				results.DestinationMismatch = "NOT_ATTEMPTED_HIDDEN_UNAVAILABLE";
				results.PoolExhaustion = "NOT_ATTEMPTED_HIDDEN_UNAVAILABLE";
				Require(presentation->PollTransfers(), "initial nil drawable retained a command");
				Require(profile != HiddenFixtureProfile::Slots, "software-slot profile cannot certify Busy without actual hidden drawable availability");
			} else {
				Require(first_status == sgre::render::SpritePresentationStatus::QueuedHidden, "fresh hidden presentation unexpectedly reported Busy");
				results.FirstStatus = "QUEUED_HIDDEN";
				PollActualTransfers(*presentation);
				TestActualWrongDestination(*presentation, renderer, results);
				if (profile == HiddenFixtureProfile::Pool) {
					TestActualDrawablePool(*presentation, renderer, fixture, results);
				} else if (profile == HiddenFixtureProfile::Slots) {
					results.PoolExhaustion = "NOT_ATTEMPTED_SOFTWARE_SLOTS_PROFILE";
					TestActualSoftwareSlots(*presentation, renderer, second.Identity, results);
				} else {
					results.PoolExhaustion = "NOT_ATTEMPTED_LIFETIME_PROFILE";
					for (std::size_t index = 0; index < 3; ++index) {
						if (!renderer.CanSubmit()) {
							++results.SourceBusy;

							break;
						}
						const auto latest = SubmitActualFixture(fixture);
						if (ObserveActualPresentation(*presentation, renderer, latest.Identity, results) != sgre::render::SpritePresentationStatus::QueuedHidden) {
							break;
						}
					}
				}
			}
			retained_commands = sgre::render::SpritePassPresentationFixtureBridge::RetainCommands(*presentation);
		}
		Require(resource.expired(), "hidden presentation transfer owner pinned the actual registered model");
		foreground.RequirePreserved();
		presentation.reset();
	}
	for (const auto& commands : retained_commands) {
		if (commands.Transfer == nil) {
			continue;
		}
		Require(commands.Transfer.status == MTLCommandBufferStatusCompleted && commands.Producer.status == MTLCommandBufferStatusCompleted, "hidden presentation destructor did not drain its actual retained same-queue commands successfully");
		++results.DrainedCommands;
	}
	retained_commands = {};
	RequireClosedObjects(closed_objects);
	foreground.RequirePreserved();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 4, "usage: hidden-presentation-fixture SPRITE_SHADER TRANSFER_SHADER lifetime|pool|slots");
		const std::string_view profile(argv[3]);
		Require(profile == "lifetime" || profile == "pool" || profile == "slots", "hidden fixture requires an explicit lifetime, pool or slots profile");
		const auto selected_profile = profile == "pool" ? HiddenFixtureProfile::Pool : (profile == "slots" ? HiddenFixtureProfile::Slots : HiddenFixtureProfile::Lifetime);
		@autoreleasepool {
			Require([NSThread isMainThread], "hidden fixture requires a main-thread CLI entry");
			const ForegroundWitness foreground;
			HiddenFixtureResults results;
			TestHiddenBoundary(argv[1], argv[2], foreground, results, selected_profile);
			TestActualReceiverExpiry(argv[1]);
			foreground.RequirePreserved();
			std::cout << "{\"status\":\"PASS_MANDATORY_HIDDEN_GUARDS_AND_LIFETIMES\",\"first_hidden_status\":\"" << results.FirstStatus << "\",\"queued_transfers\":" << results.Queued << ",\"unavailable_requests\":" << results.Unavailable << ",\"busy_requests\":" << results.Busy << ",\"source_upload_busy\":" << results.SourceBusy << ",\"drained_actual_commands\":" << results.DrainedCommands << ",\"gpu_drain_scope\":\"" << (results.DrainedCommands == 0 ? "NO_QUEUED_TRANSFERS_AT_TEARDOWN" : "ACTUAL_RETAINED_COMMANDS_COMPLETED") << "\",\"borrowed_actual_pool_drawables\":" << results.BorrowedPoolDrawables << ",\"destination_mismatch\":\"" << results.DestinationMismatch << "\",\"pool_exhaustion\":\"" << results.PoolExhaustion << "\",\"software_slot_bound\":\"" << results.SoftwareSlots << "\",\"event_gate_unwind\":\"" << results.GateUnwind << "\",\"foreground_unchanged\":true,\"window_remained_hidden\":true,\"display_fidelity\":\"UNVERIFIED\"}\n";
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
