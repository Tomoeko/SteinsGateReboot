#include "sgre/script/layer_picture_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"

#include "image_fixture.hpp"

#include <optional>
#include <string_view>

using sgre::script::SquirrelValue;

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected genuine LayerPicture failure: " + std::string(expected));
}

static sgre::script::SquirrelVmLimits Limits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

struct PictureFixture {
	ImageFixture Resources;
	sgre::script::SquirrelVm Vm{Limits()};
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::script::ScriptImageBindings Images{Vm, Objects, Psb, Resources.Catalog, Resources.Cache};
	sgre::scene::MotionAssetCatalog MotionAssets{Resources.Cache};
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};
	sgre::script::ScriptLayerPictureBindings Pictures{Vm, Objects, Layers, Images, Tree};

	PictureFixture() {
		Vm.InstallRecoveredBaseLibrary();
		Layers.ConfigureMotionOwners(Psb, MotionAssets, Resources.Cache);
	}

	~PictureFixture() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	SquirrelValue Parent() {
		return Vm.Invoke(Layers.LayerClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
	}

	SquirrelValue Image() {
		const auto document = Psb.WrapDocument(Document({Entry("one", 16, 8, {Tile(0, 0, 8, 8, 0), Tile(8, 0, 8, 8, 1)})}), Resources.Backing);

		return Vm.Invoke(Images.ImageClass(), std::array<SquirrelValue, 2>{Vm.RootTable(), document}).Value;
	}

	SquirrelValue Picture(const SquirrelValue& parent, const SquirrelValue& image, std::optional<std::array<float, 4>> rectangle = {}) {
		std::vector<SquirrelValue> arguments{Vm.RootTable(), parent, image};
		if (rectangle) {
			for (const auto value : *rectangle) {
				arguments.emplace_back(value);
			}
		}

		return Vm.Invoke(Pictures.LayerPictureClass(), arguments).Value;
	}

	void Update(const SquirrelValue& parent) {
		Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Update();
	}

	void Draw(const SquirrelValue& parent) {
		Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Draw(17);
	}
};

static SquirrelValue Call(PictureFixture& fixture, const SquirrelValue& receiver, const char* name, std::initializer_list<SquirrelValue> parameters = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), parameters);

	return fixture.Vm.Invoke(fixture.Vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

static void TestGenuineClassControlsAndRangeStack() {
	PictureFixture fixture;
	const auto parent = fixture.Parent();
	const auto image = fixture.Image();
	const auto picture = fixture.Picture(parent, image);
	Require(fixture.Vm.IsInstanceOf(picture, fixture.Objects.ObjectClass()) && !fixture.Vm.IsInstanceOf(picture, fixture.Layers.LayerBaseClass()), "LayerPicture is its genuine direct Object class, not a LayerBase alias");
	Require(fixture.Vm.RetainedReferenceCount(picture) == 0 && fixture.Vm.RetainedReferenceCount(image) == 1, "Picture registry borrows wrapper and retains exactly one actual Image root");
	Require(Call(fixture, picture, "getWidth") == SquirrelValue{std::int32_t(16)} && Call(fixture, picture, "getHeight") == SquirrelValue{std::int32_t(8)} && Call(fixture, picture, "getVisible") == SquirrelValue{true}, "Picture starts full-image, Active and unpaused");
	const auto before = fixture.Pictures.PreparedState(picture);
	fixture.Update(parent);
	Require(fixture.Pictures.PreparedState(picture).Width == before.Width, "empty-modulator update does not invent onProcess or geometry mutation");
	Call(fixture, picture, "clearImageRange");
	Call(fixture, picture, "assignImageRange", {4.0f, 0.0f, 12.0f, 4.0f, 2.0f, 3.0f});
	Require(Call(fixture, picture, "getWidth") == SquirrelValue{std::int32_t(10)} && Call(fixture, picture, "getHeight") == SquirrelValue{std::int32_t(7)}, "VM-taking range thunk pops capture before original six-parameter range selection");
	Call(fixture, picture, "setCoord", {3.0f, -2.0f});
	Require(Call(fixture, picture, "contains", {5.0f, 1.0f}) == SquirrelValue{true} && Call(fixture, picture, "contains", {13.0f, 1.0f}) == SquirrelValue{false}, "Picture contains uses translated source-order half-open crop");
	Call(fixture, picture, "setOpacity", {std::int32_t(-256)});
	Call(fixture, picture, "setBlendMode", {std::int32_t(-1)});
	Call(fixture, picture, "setMeshSize", {std::bit_cast<float>(0x7fc00042u)});
	Require(Call(fixture, picture, "getOpacity") == SquirrelValue{std::int32_t(-256)} && Call(fixture, picture, "getBlendMode") == SquirrelValue{std::int32_t(-1)} && std::bit_cast<std::uint32_t>(fixture.Pictures.PreparedState(picture).MeshSize) == 0x7fc00042u, "Picture setters preserve full DWORD and raw float fields");
	Call(fixture, picture, "setVisible", {false});
	fixture.Draw(parent);
	Call(fixture, picture, "setVisible");
	Require(Call(fixture, picture, "getVisible") == SquirrelValue{true}, "missing visibility argument observes actual truthy appended userdata");
	RequireFailure([&] {
		fixture.Draw(parent);
	},
	    "configured frame receiver");
	RequireFailure([&] {
		Call(fixture, picture, "registerVertexModulator", {image});
	},
	    "genuine class");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestConfiguredReceivingAndCameraLifetime() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {1280, 720};
	state.View.Coefficients = {2.0f / 1280, 0, 0, -1, 0, -2.0f / 720, 0, 1, 0, 0, 1, 0};
	state.RawScissor = {0, 0, 1280, 720};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;
	sgre::scene::LayerCameraDrawScope camera(state, {}, {});
	PictureFixture fixture;
	SquirrelValue picture;
	std::size_t draws = 0;
	std::size_t region_count = 0;
	bool camera_expected = false;
	bool fail_draw = false;
	fixture.Pictures.ConfigureFrameOwners(camera, [&](const sgre::scene::PictureDrawFrame& frame) {
		++draws;
		region_count = frame.Regions.size();
		Require(frame.Sources.size() == 2 && frame.Sources[0].IsLive() && frame.Sources[1].IsLive(), "configured Picture must lend exact live Image wrappers during its real First Draw");
		Require(frame.State.Coordinate.X == -8 && frame.State.Coordinate.Y == -4 && frame.State.Minification == 1 && frame.State.Magnification == 1, "configured Picture preserves task coordinates and actual parent smoothing words");
		Require(camera.Depth() == (camera_expected ? 1u : 0u), "Picture Draw must use its genuine lazily created ScaleView camera");
		if (camera_expected) {
			Require(camera.CurrentModel().Coefficients[3] == -3 && camera.CurrentModel().Coefficients[7] == -4, "Picture receiver must observe native camera offset composition");
		}
		RequireFailure([&] {
			fixture.Pictures.ReleaseInstance(picture);
		},
		    "actual receiving call");
		RequireFailure([&] {
			Call(fixture, picture, "setLeft", {99.0f});
		},
		    "actual receiving call");
		if (fail_draw) {
			throw std::runtime_error("actual receiving failure");
		}
	});
	RequireFailure([&] {
		fixture.Pictures.ConfigureFrameOwners(camera, [](const auto&) {
		});
	},
	    "once before construction");
	const auto parent = fixture.Parent();
	auto image = fixture.Image();
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	picture = fixture.Picture(parent, image);
	Call(fixture, picture, "setCoord", {-8.0f, -4.0f});
	Call(fixture, parent, "setSmoothing", {true});
	fixture.Draw(parent);
	Require(draws == 1 && region_count == 2, "genuine selected Image tiles reach one synchronous Picture frame in source order");
	Call(fixture, picture, "setVisible", {false});
	fixture.Draw(parent);
	Require(draws == 1, "hidden Picture must preserve native paused Draw suppression");
	Call(fixture, picture, "setVisible", {true});
	Call(fixture, picture, "clearImageRange");
	fixture.Draw(parent);
	Require(draws == 2 && region_count == 0, "empty Picture still enters its native begin/end receiving scope");
	Call(fixture, picture, "assignImageRange", {8.0f, 0.0f, 16.0f, 8.0f});
	Call(fixture, picture, "setOffset", {3.0f, 4.0f});
	camera_expected = true;
	fixture.Draw(parent);
	Require(draws == 3 && region_count == 1 && camera.Depth() == 0, "cropped Picture task and camera must restore their containing frame after receiving");
	fail_draw = true;
	RequireFailure([&] {
		fixture.Draw(parent);
	},
	    "actual receiving failure");
	Require(camera.Depth() == 0, "receiving failure must restore the real camera scope");
	fail_draw = false;
	fixture.Draw(parent);
	Require(draws == 5, "Picture receiving failure must release its reentry fence for a genuine retry");
	image = {};
	fixture.Vm.ResetContext(fixture.Vm.RootContext());
	fixture.Pictures.ReleaseInstance(picture);
	Require(!borrowed.IsLive() && fixture.Resources.Cache.Count() == 0, "configured Picture still drops Image roots before deferred native task retirement");
	fixture.Update(parent);
	fixture.Draw(parent);
	Require(draws == 5, "retired Picture never revisits expired source wrappers");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestConstructorArityPreservesNativeError() {
	PictureFixture fixture;
	const auto parent = fixture.Parent();
	const auto image = fixture.Image();
	fixture.Vm.SetLastError(std::string("retained native factory error"));
	bool failed = false;
	try {
		static_cast<void>(fixture.Vm.Invoke(fixture.Pictures.LayerPictureClass(), std::array<SquirrelValue, 4>{fixture.Vm.RootTable(), parent, image, 4.0f}));
	} catch (const sgre::script::SquirrelVmException&) {
		failed = true;
	}
	Require(failed && fixture.Vm.LastError() == SquirrelValue{std::string("retained native factory error")} && fixture.Vm.RetainedReferenceCount(image) == 0 && fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 0, "unsupported positive factory arity preserves last error and creates no native root/task");
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(fixture.Pictures.LayerPictureClass(), std::array<SquirrelValue, 2>{fixture.Vm.RootTable(), parent}));
	},
	    "invalid argument");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestWrapperFirstImageThenDeferredTaskRetirement() {
	PictureFixture fixture;
	const auto parent = fixture.Parent();
	auto image = fixture.Image();
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	const auto picture = fixture.Picture(parent, image);
	const auto weak_image = fixture.Vm.CreateWeakReference(image);
	image = {};
	fixture.Vm.ResetContext(fixture.Vm.RootContext());
	std::vector<std::uint32_t> events;
	fixture.Resources.OnDrop = [&] {
		Require(fixture.Pictures.ScaleView(picture).TaskState().Status == sgre::scene::TaskStatus::Active, "native Image/GPU release must precede Picture task Retire marking");
		events.push_back(1);
	};
	fixture.Pictures.ReleaseInstance(picture);
	Require(events == std::vector<std::uint32_t>{1, 1} && !borrowed.IsLive() && fixture.Resources.Cache.Count() == 0 && std::holds_alternative<std::monostate>(fixture.Vm.ResolveWeakReference(weak_image)), "wrapper-first retirement drops real Image/source ownership before borrowed task marks");
	Require(fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 1, "wrapper release defers actual Picture task deletion in First");
	fixture.Update(parent);
	Require(fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 0, "First update deletes retired task without dereferencing expired Image");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestParentFirstDetachedDefaultsAndRetainedImage() {
	PictureFixture fixture;
	const auto parent = fixture.Parent();
	auto image = fixture.Image();
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	const auto picture = fixture.Picture(parent, image);
	image = {};
	fixture.Vm.ResetContext(fixture.Vm.RootContext());
	fixture.Layers.ReleaseInstance(parent);
	Require(fixture.Pictures.ScaleView(picture).State().Detached && borrowed.IsLive() && fixture.Resources.Cache.Count() == 2, "parent-first task disposal and view detach preserve wrapper-owned Image root");
	Require(Call(fixture, picture, "getWidth") == SquirrelValue{std::int32_t(0)} && Call(fixture, picture, "getHeight") == SquirrelValue{std::int32_t(0)} && Call(fixture, picture, "getOpacity") == SquirrelValue{std::int32_t(0)} && Call(fixture, picture, "getLeft") == SquirrelValue{0.0f} && Call(fixture, picture, "contains", {0.0f, 0.0f}) == SquirrelValue{false}, "detached original getters do not access freed task storage");
	Call(fixture, picture, "assignImageRange");
	Call(fixture, picture, "setCoord", {8.0f, 9.0f});
	fixture.Pictures.ReleaseInstance(picture);
	Require(!borrowed.IsLive() && fixture.Resources.Cache.Count() == 0, "retained detached Picture drops actual Image only on wrapper release");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestScalarCameraCouplingAndSmoothing() {
	PictureFixture fixture;
	const auto parent = fixture.Parent();
	const auto image = fixture.Image();
	const auto picture = fixture.Picture(parent, image, std::array<float, 4>{4, 0, 8, 4});
	Require(Call(fixture, picture, "getWidth") == SquirrelValue{std::int32_t(8)}, "constructor rectangle arguments are x/y/width/height");
	Call(fixture, picture, "setOffset", {2.0f, 3.0f});
	Call(fixture, picture, "setScale", {2.0f, 0.0f});
	Call(fixture, picture, "setRot", {90.0f});
	Require(fixture.Pictures.ScaleView(picture).State().HasCamera && fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 1, "same native Picture task transfers into one lazily created camera");
	Require(Call(fixture, picture, "contains", {0.0f, -3.0f}) == SquirrelValue{true}, "contains uses offset/axis scale with zero-scale bypass and ignores rotation");
	Call(fixture, parent, "setSmoothing", {true});
	const auto state = fixture.Pictures.PreparedState(picture);
	Require(state.Minification == 1 && state.Magnification == 1, "actual borrowed-view smoothing forwards to Picture task filter fields");
	fixture.Pictures.ReleaseInstance(picture);
	fixture.Update(parent);
	Require(fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 0, "wrapper retirement marks both Picture and genuine camera tasks");
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestReleasePreflightAndDiagnosticCleanup() {
	PictureFixture fixture;
	const auto parent = fixture.Parent();
	auto image = fixture.Image();
	const auto picture = fixture.Picture(parent, image);
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	image = {};
	fixture.Vm.ResetContext(fixture.Vm.RootContext());
	const auto listener = fixture.Vm.Invoke(fixture.Objects.ObjectClass(), std::array<SquirrelValue, 1>{fixture.Vm.RootTable()}).Value;
	fixture.Objects.Subscribe(picture, listener, [&](const auto&) {
		RequireFailure([&] {
			fixture.Pictures.ReleaseInstance(picture);
		},
		    "completion notification");
		Require(borrowed.IsLive() && fixture.Resources.Cache.Count() == 2 && fixture.Pictures.ScaleView(picture).TaskState().Status == sgre::scene::TaskStatus::Active, "rejected notification release preserves payload/root state");

		return true;
	});
	fixture.Objects.Notify(picture);
	fixture.Resources.FailRelease = true;
	fixture.Pictures.ReleaseInstance(picture);
	Require(!borrowed.IsLive() && fixture.Resources.Cache.Count() == 0 && !fixture.Objects.HasInstance(picture), "native source failure still removes Picture/Image records and retires every source");
	RequireFailure([&] {
		fixture.Vm.RethrowReleaseFailure();
	},
	    "retirement failure");
	fixture.Update(parent);
	fixture.Vm.Close();
}

static void TestActiveTaskPreflightPreservesImageRoot() {
	PictureFixture fixture;
	const auto parent = fixture.Parent();
	const auto image = fixture.Image();
	const auto picture = fixture.Picture(parent, image);
	const auto borrowed = fixture.Images.BorrowNativeImage(image);
	std::size_t attempts = 0;
	sgre::scene::ParallelTaskCallbacks other;
	other.Update = [&](auto) {
		++attempts;
		RequireFailure([&] {
			fixture.Pictures.ReleaseInstance(picture);
		},
		    "task groups is active");
		Require(borrowed.IsLive() && fixture.Vm.RetainedReferenceCount(image) == 1 && fixture.Pictures.ScaleView(picture).TaskState().Status == sgre::scene::TaskStatus::Active, "active-task rejection preserves Image root and original task state");
	};
	auto& first = fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First);
	const auto other_task = first.Append(std::move(other));
	fixture.Update(parent);
	Require(attempts == 1, "independent native task callback exercises preflight once");
	first.SetStatus(other_task, sgre::scene::TaskStatus::Complete);
	fixture.Pictures.ReleaseInstance(picture);
	fixture.Update(parent);
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

class PictureLifetimeReader {
public:
	explicit PictureLifetimeReader(const char* path)
	    : m_Input(path, std::ios::binary) {
		Require(static_cast<bool>(m_Input), "Picture lifetime projection cannot be opened");
	}

	std::uint32_t Word() {
		std::array<char, 4> bytes{};
		m_Input.read(bytes.data(), bytes.size());
		Require(static_cast<bool>(m_Input), "Picture lifetime projection is truncated");

		return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24;
	}

	void Compare(const std::array<std::uint32_t, 6>& values) {
		for (const auto value : values) {
			Require(Word() == value, "genuine Picture root/task lifetime projection differs");
		}
	}

	void Finish() {
		Require(m_Input.peek() == std::char_traits<char>::eof(), "Picture lifetime projection has trailing bytes");
	}

private:
	std::ifstream m_Input;
};

static std::array<std::uint32_t, 6> LifetimeState(PictureFixture& fixture, const SquirrelValue& parent, const SquirrelValue& weak_image) {
	const auto image = fixture.Vm.ResolveWeakReference(weak_image);
	const auto roots = fixture.Vm.Type(image) == sgre::script::SquirrelValueType::Null ? 0 : fixture.Vm.RetainedReferenceCount(image);
	auto& first = fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First);
	const auto alive = first.Count() != 0;
	sgre::scene::ParallelTaskState task;
	if (alive) {
		std::array<sgre::scene::ParallelTaskHandle, 1> order;
		Require(first.WriteOrder(order) == 1, "Picture lifetime fixture unexpectedly contains another First task");
		task = first.State(order[0]);
	}

	return {static_cast<std::uint32_t>(roots), static_cast<std::uint32_t>(alive), alive ? static_cast<std::uint32_t>(task.Status) : 0, alive ? static_cast<std::uint32_t>(task.DrawPaused) : 0, static_cast<std::uint32_t>(first.Status()), static_cast<std::uint32_t>(alive)};
}

static void TestNativeLifetimeProjection(const char* path) {
	PictureLifetimeReader input(path);
	Require(input.Word() == 1000, "Picture lifetime projection case count differs");
	for (std::size_t case_id = 0; case_id < 1000; ++case_id) {
		const auto parent_first = input.Word();
		const auto external_image = input.Word();
		PictureFixture fixture;
		const auto parent = fixture.Parent();
		auto image = fixture.Image();
		const auto weak_image = fixture.Vm.CreateWeakReference(image);
		const auto borrowed = fixture.Images.BorrowNativeImage(image);
		sgre::script::SquirrelHostRoot external;
		if (external_image) {
			external = fixture.Vm.Retain(image);
		}
		const auto picture = fixture.Picture(parent, image);
		image = {};
		fixture.Vm.ResetContext(fixture.Vm.RootContext());
		input.Compare(LifetimeState(fixture, parent, weak_image));
		if (parent_first) {
			// Compare the actual First owner before full Layer teardown removes its public native record.
			fixture.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Clear();
			input.Compare(LifetimeState(fixture, parent, weak_image));
			fixture.Layers.ReleaseInstance(parent);
			Require(fixture.Pictures.ScaleView(picture).State().Detached, "native parent-first retirement must detach retained Picture wrapper");
			fixture.Pictures.ReleaseInstance(picture);
			const auto target = fixture.Vm.ResolveWeakReference(weak_image);
			const auto roots = fixture.Vm.Type(target) == sgre::script::SquirrelValueType::Null ? 0 : fixture.Vm.RetainedReferenceCount(target);
			input.Compare({static_cast<std::uint32_t>(roots), 0, 0, 0, 0, 0});
		} else {
			fixture.Pictures.ReleaseInstance(picture);
			input.Compare(LifetimeState(fixture, parent, weak_image));
			fixture.Update(parent);
			input.Compare(LifetimeState(fixture, parent, weak_image));
		}
		Require(borrowed.IsLive() == static_cast<bool>(external_image), "only the separately retained real Image root survives Picture wrapper retirement");
		external = {};
		fixture.Vm.Close();
		fixture.Vm.RethrowReleaseFailure();
		Require(!borrowed.IsLive() && fixture.Resources.Cache.Count() == 0, "all native Picture/Image source owners retire after final external root release");
	}
	input.Finish();
}

int main(int argc, char** argv) {
	try {
		TestGenuineClassControlsAndRangeStack();
		TestConfiguredReceivingAndCameraLifetime();
		TestConstructorArityPreservesNativeError();
		TestWrapperFirstImageThenDeferredTaskRetirement();
		TestParentFirstDetachedDefaultsAndRetainedImage();
		TestScalarCameraCouplingAndSmoothing();
		TestReleasePreflightAndDiagnosticCleanup();
		TestActiveTaskPreflightPreservesImageRoot();
		if (argc == 2) {
			TestNativeLifetimeProjection(argv[1]);
		}
		std::cout << "LayerPicture binding tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
