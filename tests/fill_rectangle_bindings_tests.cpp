#include "sgre/script/fill_rectangle_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include "fill_rectangle_native_vectors.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::script::SquirrelValue;

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected FillRect failure: " + std::string(expected));
}

static void TestOriginalDrawVectorsAndTaskBorrow() {
	std::size_t observations = 0;
	for (const auto& native : kFillRectangleNativeVectors) {
		sgre::scene::FillRectangleTask task;
		const auto initial = task.State();
		Require(initial.Color == 0 && initial.Opacity == 255 && initial.BlendMode == 0 && initial.Coordinate.X == 0 && initial.Size.Y == 0, "real MFillRect constructor state");
		task.SetCoordinate({std::bit_cast<float>(native.CoordinateSize[0]), std::bit_cast<float>(native.CoordinateSize[1])});
		task.SetSize({std::bit_cast<float>(native.CoordinateSize[2]), std::bit_cast<float>(native.CoordinateSize[3])});
		task.SetColor(native.Color);
		task.SetOpacity(native.Opacity);
		task.SetBlendMode(native.BlendMode);
		std::size_t draws = 0;
		task.Draw([&](const auto& packet) {
			++draws;
			Require(!task.CanClose(), "packet borrow prevents retirement");
			RequireFailure([&] {
				task.Close();
			},
			    "current draw packet");
			RequireFailure([&] {
				task.SetColor(0);
			},
			    "live idle");
			const std::array<std::uint32_t, 8> actual{
			    std::bit_cast<std::uint32_t>(packet.Vertices[0].X), std::bit_cast<std::uint32_t>(packet.Vertices[0].Y),
			    std::bit_cast<std::uint32_t>(packet.Vertices[1].X), std::bit_cast<std::uint32_t>(packet.Vertices[1].Y),
			    std::bit_cast<std::uint32_t>(packet.Vertices[2].X), std::bit_cast<std::uint32_t>(packet.Vertices[2].Y),
			    std::bit_cast<std::uint32_t>(packet.Vertices[3].X), std::bit_cast<std::uint32_t>(packet.Vertices[3].Y)};
			const std::array<std::uint32_t, 8> expected{native.Rectangle[0], native.Rectangle[1], native.Rectangle[0], native.Rectangle[3], native.Rectangle[2], native.Rectangle[1], native.Rectangle[2], native.Rectangle[3]};
			const auto argb = (native.OutputRgba >> 8) | (native.OutputRgba << 24);
			Require(actual == expected && packet.Color == argb, "native rectangle/byte opacity and 4f5740 strip/color conversion must match bitwise");
			++observations;
		});
		Require(draws == static_cast<std::size_t>(native.Drawn) && task.CanClose(), "full color zero skips; valid draw ends packet borrow");
		Require(task.State().BlendMode == native.BlendMode && task.State().Opacity == (native.Opacity & 255u), "ignored blend retainsDWORD while native opacity setter zero-extends its low byte");
		task.Close();
		Require(task.State().Closed, "physical close marks its actual owner");
	}
	Require(observations == 56, "all independently observed native backend calls were compared");

	sgre::scene::FillRectangleTask task;
	task.Draw({});
	task.SetColor(0x123456ff);
	RequireFailure([&] {
		task.Draw({});
	},
	    "drawing service");
	RequireFailure([&] {
		task.Draw([](const auto&) {
			throw std::runtime_error("actual backend rejected");
		});
	},
	    "actual backend rejected");
	Require(task.CanClose(), "backend exception ends packet borrow for whole-frame abort/replay");
	std::size_t replay = 0;
	task.Draw([&](const auto&) {
		++replay;
	});
	Require(replay == 1, "same task replays after actual backend abort");
	RequireFailure([&] {
		task.SetLeft(std::numeric_limits<float>::infinity());
	},
	    "finite");
	task.SetLeft(std::numeric_limits<float>::max());
	task.SetWidth(std::numeric_limits<float>::max());
	RequireFailure([&] {
		task.Draw([](const auto&) {
		});
	},
	    "finite");
	task.Close();
	RequireFailure([&] {
		task.SetOpacity(1);
	},
	    "live idle");
}

static sgre::script::SquirrelVmLimits VmLimits() {
	sgre::script::SquirrelVmLimits result;
	result.ReclaimObjects = true;

	return result;
}

static sgre::scene::LayerCameraFrameState FrameState() {
	sgre::scene::LayerCameraFrameState result;
	result.Viewport = {1280, 720};
	result.RawScissor = {0, 0, 1280, 720};
	result.RoundedScissor = result.RawScissor;
	result.AppliedRoundedScissor = result.RawScissor;

	return result;
}

struct FillResources {
	std::size_t Uploads = 0;
	sgre::scene::TextureCache Cache;

	FillResources() : Cache([this](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		                  ++Uploads;
		                  throw std::runtime_error("FillRect must not create a substitute texture");
	                  },
	                      [](auto) {
		                      throw std::runtime_error("FillRect has no texture owner to retire");
	                      }) {
	}
};

struct FillFixture {
	FillResources Resources;
	sgre::script::SquirrelVm Vm{VmLimits()};
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::scene::MotionAssetCatalog Assets{Resources.Cache};
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};
	std::array<sgre::scene::MotionScissorMatrix, 1> Models{};
	sgre::scene::LayerCameraDrawScope Camera{FrameState(), Models, [](const auto&) {
	                                         }};
	sgre::script::ScriptFillRectangleBindings Rectangles;
	std::vector<sgre::scene::FillRectangleDrawPacket> Packets;
	std::function<void(const sgre::scene::FillRectangleDrawPacket&)> DuringDraw;

	explicit FillFixture(sgre::script::ScriptFillRectangleLimits limits = {}) : Rectangles(Vm, Objects, Layers, limits) {
		Vm.InstallRecoveredBaseLibrary();
		Layers.ConfigureMotionOwners(Psb, Assets, Resources.Cache);
		Rectangles.ConfigureFrameOwners(Camera, [&](const auto& packet) {
			if (DuringDraw) {
				DuringDraw(packet);
			}
			Packets.push_back(packet);
		});
	}

	~FillFixture() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	SquirrelValue Parent() {
		return Vm.Invoke(Layers.LayerClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
	}

	SquirrelValue Rectangle(const SquirrelValue& parent) {
		return Vm.Invoke(Rectangles.FillRectangleClass(), std::array<SquirrelValue, 3>{Vm.RootTable(), parent, std::string("ignored extra")}).Value;
	}

	SquirrelValue Call(const SquirrelValue& receiver, const char* method, std::initializer_list<SquirrelValue> parameters = {}) {
		std::vector<SquirrelValue> arguments{receiver};
		arguments.insert(arguments.end(), parameters);

		return Vm.Invoke(Vm.GetSlot(receiver, std::string(method)), arguments).Value;
	}

	void Update(const SquirrelValue& parent) {
		Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Update();
	}

	void Draw(const SquirrelValue& parent) {
		Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Draw(0x80000001u);
	}

	void Close() {
		Vm.Close();
		Vm.RethrowReleaseFailure();
		Require(Resources.Uploads == 0, "FillRect never allocates a white texture or source atlas");
	}
};

static void TestActualClassControlsAndLazyCamera() {
	FillFixture fixture;
	const auto parent = fixture.Parent();
	const auto rectangle = fixture.Rectangle(parent);
	Require(fixture.Vm.IsInstanceOf(rectangle, fixture.Objects.ObjectClass()) && !fixture.Vm.IsInstanceOf(rectangle, fixture.Layers.LayerBaseClass()), "FillRect is its genuine direct Object class, not a Layer alias");
	Require(fixture.Vm.RetainedReferenceCount(rectangle) == 0 && fixture.Vm.RetainedReferenceCount(parent) == 0, "native wrapper/task registration adds no host root to this or Layer");
	Require(fixture.Call(rectangle, "getVisible") == SquirrelValue{true} && fixture.Call(rectangle, "getOpacity") == SquirrelValue{std::int32_t(255)} && fixture.Call(rectangle, "getColor") == SquirrelValue{std::int32_t(0)}, "native constructor starts unpaused with transparent zero and full opacity");
	fixture.Draw(parent);
	Require(fixture.Packets.empty(), "full zero color skips untextured record creation");
	fixture.Call(rectangle, "setCoord", {3.0f, -2.0f});
	fixture.Call(rectangle, "setSize", {-8.0f, 4.0f});
	fixture.Call(rectangle, "setLeft", {5.0f});
	fixture.Call(rectangle, "setTop", {7.0f});
	fixture.Call(rectangle, "setWidth", {10.0f});
	fixture.Call(rectangle, "setHeight", {20.0f});
	fixture.Call(rectangle, "setColor", {std::bit_cast<std::int32_t>(0x80402080u)});
	fixture.Call(rectangle, "setOpacity", {std::int32_t(-1)});
	fixture.Call(rectangle, "setBlendMode", {std::int32_t(-17)});
	Require(fixture.Call(rectangle, "getLeft") == SquirrelValue{5.0f} && fixture.Call(rectangle, "getTop") == SquirrelValue{7.0f} && fixture.Call(rectangle, "getWidth") == SquirrelValue{10.0f} && fixture.Call(rectangle, "getHeight") == SquirrelValue{20.0f}, "all geometry setters/getters operate on the same actual task fields");
	Require(fixture.Call(rectangle, "getColor") == SquirrelValue{std::bit_cast<std::int32_t>(0x80402080u)} && fixture.Call(rectangle, "getOpacity") == SquirrelValue{std::int32_t(255)} && fixture.Call(rectangle, "getBlendMode") == SquirrelValue{std::int32_t(-17)}, "color/blend retainDWORD and opacity truncates the actual setter low byte");
	fixture.Update(parent);
	fixture.Draw(parent);
	Require(fixture.Packets.size() == 1 && fixture.Packets.back().Vertices[3].X == 15 && fixture.Packets.back().Vertices[3].Y == 27, "native update is empty and draw publishes actual coordinates");
	fixture.Call(rectangle, "setVisible", {false});
	fixture.Draw(parent);
	Require(fixture.Packets.size() == 1, "visibility pauses the actual registered First task");
	fixture.Call(rectangle, "setVisible");
	Require(fixture.Call(rectangle, "getVisible") == SquirrelValue{true}, "missing bool operand sees actual appended capture userdata truth");
	fixture.Call(rectangle, "setOffset", {0.0f, 0.0f});
	fixture.Call(rectangle, "setScale", {1.0f, 1.0f});
	fixture.Call(rectangle, "setRot", {0.0f});
	Require(!fixture.Rectangles.ScaleView(rectangle).State().HasCamera, "identity controls retain initial task ownership");
	fixture.Call(rectangle, "setOffset", {2.0f, -3.0f});
	fixture.Call(rectangle, "setScale", {2.0f, 3.0f});
	fixture.Call(rectangle, "setPrimaryScale", {4.0f});
	fixture.Call(rectangle, "setRot", {90.0f});
	const auto camera = fixture.Rectangles.ScaleView(rectangle).CameraState();
	Require(camera.Scale.X == 4 && camera.Scale.Y == 4 && std::abs(camera.RotationRadians - 1.57079637f) < 0.000001f && camera.Offset.X == 2, "actual shared scale camera retains primary scale and degree-to-radian rotation");
	fixture.DuringDraw = [&](const auto&) {
		Require(fixture.Camera.Depth() == fixture.Models.size() + 1 && fixture.Camera.IsDrawing() && fixture.Camera.CurrentModel().Coefficients != fixture.Models[0].Coefficients, "actual FillRect draw observes genuine pushed camera model");
		RequireFailure([&] {
			fixture.Rectangles.ReleaseInstance(rectangle);
		},
		    "current actual draw packet");
		RequireFailure([&] {
			fixture.Call(rectangle, "setWidth", {3.0f});
		},
		    "live idle");
	};
	fixture.Draw(parent);
	Require(fixture.Camera.Depth() == fixture.Models.size() && !fixture.Camera.IsDrawing() && fixture.Camera.CurrentModel().Coefficients == fixture.Models[0].Coefficients, "same camera model owner restores after drawing");
	fixture.DuringDraw = {};
	fixture.Close();
}

static void TestDeferredRetirementDetachAndGc() {
	{
		FillFixture fixture;
		const auto parent = fixture.Parent();
		const auto rectangle = fixture.Rectangle(parent);
		auto& first = fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First);
		fixture.Rectangles.ReleaseInstance(rectangle);
		Require(first.Count() == 1 && !fixture.Objects.HasInstance(rectangle), "wrapper retirement unlinks script identity but leaves real deferred task owned by First");
		fixture.Update(parent);
		Require(first.Count() == 0, "actual First traversal performs physical task deletion");
		fixture.Close();
	}
	{
		FillFixture fixture;
		const auto parent = fixture.Parent();
		const auto rectangle = fixture.Rectangle(parent);
		fixture.Call(rectangle, "setColor", {std::int32_t(255)});
		fixture.Layers.ReleaseInstance(parent);
		Require(fixture.Rectangles.ScaleView(rectangle).State().Detached, "parent physically retires its tasks before detaching surviving weak wrappers");
		Require(fixture.Call(rectangle, "getLeft") == SquirrelValue{0.0f} && fixture.Call(rectangle, "getTop") == SquirrelValue{0.0f} && fixture.Call(rectangle, "getWidth") == SquirrelValue{0.0f} && fixture.Call(rectangle, "getHeight") == SquirrelValue{0.0f}, "detached geometry getters return native zero defaults");
		Require(fixture.Call(rectangle, "getColor") == SquirrelValue{std::int32_t(-1)} && fixture.Call(rectangle, "getOpacity") == SquirrelValue{std::int32_t(255)} && fixture.Call(rectangle, "getBlendMode") == SquirrelValue{std::int32_t(255)} && fixture.Call(rectangle, "getVisible") == SquirrelValue{false}, "detached native packed/color/opacity/blend defaults remain distinct");
		fixture.Call(rectangle, "setCoord", {3.0f, 4.0f});
		fixture.Call(rectangle, "setOffset", {9.0f, 9.0f});
		fixture.Call(rectangle, "setVisible", {true});
		fixture.Rectangles.ReleaseInstance(rectangle);
		fixture.Close();
	}
	{
		FillFixture fixture;
		const auto parent = fixture.Parent();
		const auto parent_root = fixture.Vm.Retain(parent);
		auto rectangle = fixture.Rectangle(parent);
		const auto weak = fixture.Call(rectangle, "weakref");
		const auto weak_root = fixture.Vm.Retain(weak);
		rectangle = {};
		fixture.Vm.ResetContext(fixture.Vm.RootContext());
		static_cast<void>(fixture.Vm.CollectGarbage());
		Require(std::holds_alternative<std::monostate>(fixture.Vm.ResolveWeakReference(weak)), "wrapper registry/task payload do not root the actual VM instance");
		Require(fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 1, "GC release marks actual task for deferred deletion");
		fixture.Update(parent);
		Require(fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 0, "actual First owner completes GC-triggered physical retirement");
		fixture.Close();
	}
}

static void TestFailureReplayAndConstructorBounds() {
	sgre::script::ScriptFillRectangleLimits limits;
	limits.MaxInstances = 1;
	FillFixture fixture(limits);
	const auto parent = fixture.Parent();
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(fixture.Rectangles.FillRectangleClass(), std::array<SquirrelValue, 1>{fixture.Vm.RootTable()}));
	},
	    "invalid argument");
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(fixture.Rectangles.FillRectangleClass(), std::array<SquirrelValue, 2>{fixture.Vm.RootTable(), std::int32_t(4)}));
	},
	    "invalid argument");
	const auto rectangle = fixture.Rectangle(parent);
	RequireFailure([&] {
		static_cast<void>(fixture.Rectangle(parent));
	},
	    "capacity");
	Require(fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 1, "constructor limit failure adds no second actual task");
	fixture.Call(rectangle, "setColor", {std::int32_t(255)});
	fixture.DuringDraw = [](const auto&) {
		throw std::runtime_error("actual frame rejected");
	};
	RequireFailure([&] {
		fixture.Draw(parent);
	},
	    "actual frame rejected");
	fixture.DuringDraw = {};
	fixture.Draw(parent);
	Require(fixture.Packets.size() == 1, "whole-frame draw failure does not strand payload borrow or prevent replay");
	const auto setter = fixture.Vm.GetSlot(rectangle, std::string("setLeft"));
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(setter, std::array<SquirrelValue, 2>{parent, 3.0f}));
	},
	    "live native wrapper");
	const auto table = fixture.Vm.CreateTable();
	const auto table_root = fixture.Vm.Retain(table);
	const auto weak_extra = fixture.Call(rectangle, "weakref", {table});
	Require(fixture.Vm.ResolveWeakReference(weak_extra) == table, "generic native weakref wraps final original operand instead of hardcoding receiver");
	fixture.Close();
}

int main() {
	try {
		TestOriginalDrawVectorsAndTaskBorrow();
		TestActualClassControlsAndLazyCamera();
		TestDeferredRetirementDetachAndGc();
		TestFailureReplayAndConstructorBounds();
		std::cout << "FillRect task/binding tests passed:64 native vectors and genuine Layer/VM ownership\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
