#include "sgre/script/capture_bindings.hpp"

#include "sgre/scene/layer_base_frame.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"

#include "capture_native_vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sgre::script::SquirrelValue;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected Capture rejection: " + std::string(expected));
}

class CaptureBackend;

class CaptureResource final : public sgre::scene::MutableTextureResource {
public:
	CaptureResource(CaptureBackend& backend, sgre::scene::MutableTextureDescription description);
	~CaptureResource() override;
	sgre::scene::MutableTextureDescription Description() const noexcept override {
		return m_Description;
	}

private:
	CaptureBackend& m_Backend;
	sgre::scene::MutableTextureDescription m_Description;
};

class CaptureView final : public sgre::scene::MutableTextureView {
public:
	explicit CaptureView(sgre::scene::MutableTextureViewDescription description)
	    : m_Description(description) {
	}
	sgre::scene::MutableTextureViewDescription Description() const noexcept override {
		return m_Description;
	}

private:
	sgre::scene::MutableTextureViewDescription m_Description;
};

class CaptureBackend final : public sgre::scene::MutableTextureBackend {
public:
	std::size_t CreatedTextures = 0;
	std::size_t LiveTextures = 0;
	std::size_t FailCreation = 0;

	std::unique_ptr<sgre::scene::MutableTextureResource> CreateTexture(const sgre::scene::MutableTextureDescription& description, const sgre::scene::MutableTextureInitialData& initial) override {
		Require(initial.Bytes.empty(), "Capture creates an actual empty staging allocation, not substitute initial pixels");
		++CreatedTextures;
		if (CreatedTextures == FailCreation) {
			throw std::runtime_error("actual allocation failure");
		}
		return std::make_unique<CaptureResource>(*this, description);
	}
	std::unique_ptr<sgre::scene::MutableTextureView> CreateView(sgre::scene::MutableTextureResource&, const sgre::scene::MutableTextureViewDescription& description) override {
		return std::make_unique<CaptureView>(description);
	}
	std::unique_ptr<sgre::scene::MutableTextureSampler> CreateSampler(const sgre::scene::MutableTextureSamplerDescription&) override {
		throw std::runtime_error("Capture copy has no sampler producer");
	}
	sgre::scene::MutableTextureMapping Map(sgre::scene::MutableTextureResource&, sgre::scene::MutableTextureMapMode) override {
		throw std::runtime_error("Capture ordinary draw must not map GPU memory");
	}
	void Unmap(sgre::scene::MutableTextureResource&) override {
		throw std::runtime_error("Capture ordinary draw must not unmap GPU memory");
	}
	void Upload(sgre::scene::MutableTextureResource&, std::span<const std::byte>, std::uint32_t) override {
		throw std::runtime_error("Capture ordinary draw must not upload CPU pixels");
	}
	void Copy(sgre::scene::MutableTextureResource&, sgre::scene::MutableTextureResource&) override {
		throw std::runtime_error("Capture main copy is the typed current-target encoder service");
	}
	void Bind(sgre::scene::MutableTextureView*, sgre::scene::MutableTextureSampler*, std::uint32_t, sgre::scene::MutableTextureStage) override {
		throw std::runtime_error("MCapture itself has no materialized quad or sampled texture draw");
	}
};

CaptureResource::CaptureResource(CaptureBackend& backend, sgre::scene::MutableTextureDescription description)
    : m_Backend(backend), m_Description(description) {
	++m_Backend.LiveTextures;
}

CaptureResource::~CaptureResource() {
	--m_Backend.LiveTextures;
}

static sgre::scene::CaptureFrameContext PixelFrame() {
	sgre::scene::CaptureFrameContext frame;
	frame.Viewport = {100, 100};
	frame.View.Coefficients = {0.02f, 0, 0, -1, 0, -0.02f, 0, 1, 0, 0, 1, 0};

	return frame;
}

class CaptureObserver final : public sgre::scene::CaptureTaskObserver {
public:
	explicit CaptureObserver(CaptureBackend& backend)
	    : Backend(backend) {
	}
	void CaptureResized(sgre::resource::MotionPoint size) noexcept override {
		++Resized;
		LastSize = size;
	}
	void CaptureInvalidated() noexcept override {
		++Invalidated;
	}
	void CaptureRetired() noexcept override {
		++Retired;
		LiveAtRetirement = Backend.LiveTextures;
	}
	CaptureBackend& Backend;
	sgre::resource::MotionPoint LastSize;
	std::size_t Resized = 0;
	std::size_t Invalidated = 0;
	std::size_t Retired = 0;
	std::size_t LiveAtRetirement = 0;
};

static void TestActualStagingLifetime() {
	CaptureBackend backend;
	sgre::scene::CaptureTask task(20, 10);
	CaptureObserver consumer(backend);
	task.AddConsumer(consumer);
	Require(backend.CreatedTextures == 0 && !task.State().Captured && consumer.Resized == 0, "native MCapture starts without a texture or synthetic consumer-ready notification");
	const auto allocate = [&](const auto& spec) {
		return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
	};
	std::size_t copies = 0;
	const auto copy = [&](auto& destination, const auto& bounds) {
		const auto actual = destination.State();
		Require(actual.Storage == sgre::scene::MutableTextureStorage::StagingCompanion && actual.HasCompanion && !actual.HasView && actual.Description.Usage == sgre::scene::MutableTextureUsage::Staging && actual.Description.CpuRead && actual.Description.CpuWrite, "actual main resource is nonsampled Storage4 staging with its genuine GPU companion");
		Require(bounds.MaximumX - bounds.MinimumX == 20 && bounds.MaximumY - bounds.MinimumY == 10 && consumer.Resized != 0, "native resize notification precedes the actual current-target copy");
		Require(!task.CanClose(), "physical task and resources remain borrowed through the actual copy and outer frame");
		RequireError([&] {
			task.Close();
		},
		    "live idle");
		RequireError([&] {
			task.SetOffset({1, 2});
		},
		    "live idle");
		++copies;
	};
	task.Draw(backend, PixelFrame(), allocate, copy);
	Require(task.State().Captured && task.State().FramePending && backend.LiveTextures == 2 && copies == 1 && consumer.Resized == 1, "first real copy creates exactly one staging/companion pair");
	RequireError([&] {
		task.Close();
	},
	    "End or Abort");
	task.ReleaseFrame(true);
	task.Draw(backend, PixelFrame(), allocate, copy);
	Require(backend.CreatedTextures == 2 && consumer.Resized == 1 && copies == 2, "ordinary identical bounds reuse the exact owner without allocation");
	task.ReleaseFrame(true);
	task.SetOffset({-3, -4});
	task.Draw(backend, PixelFrame(), allocate, copy);
	Require(task.State().Coordinate.X == 3 && task.State().Coordinate.Y == 4 && backend.LiveTextures == 4 && consumer.Resized == 2, "native full-bounds comparison replaces even identical dimensions at a new position, retaining old frame resources");
	task.ReleaseFrame(true);
	Require(backend.LiveTextures == 2 && task.Texture().State().Description.Width == 20, "only outer frame completion retires displaced owners");
	task.Draw(backend, PixelFrame(), allocate, copy);
	task.ReleaseFrame(false);
	Require(!task.State().Captured && consumer.Invalidated == 1, "aborted GPU work cannot leave a logically ready capture");
	RequireError([&] {
		static_cast<void>(task.Texture());
	},
	    "preceding framebuffer copy");
	task.Draw(backend, PixelFrame(), allocate, copy);
	task.ReleaseFrame(true);
	Require(task.State().Captured && backend.CreatedTextures == 4, "a fresh actual copy restores readiness without a substitute allocation");
	RequireError([&] {
		task.Draw(backend, PixelFrame(), allocate, [](auto&, const auto&) {
			throw std::runtime_error("actual encoder copy failed");
		});
	},
	    "actual encoder copy failed");
	Require(!task.State().Captured && task.State().FramePending, "failed copy remains enclosed by outer abort retirement");
	task.ReleaseFrame(false);
	task.Draw(backend, PixelFrame(), allocate, copy);
	task.ReleaseFrame(true);
	task.SetOffset({-5, -6});
	backend.FailCreation = backend.CreatedTextures + 1;
	RequireError([&] {
		task.Draw(backend, PixelFrame(), allocate, copy);
	},
	    "actual allocation failure");
	Require(task.State().Captured && !task.State().FramePending && task.State().PhysicalBounds.MinimumX == 3 && backend.LiveTextures == 2, "failed typed allocation preserves the previous actual retained capture and bounds");
	backend.FailCreation = 0;
	CaptureBackend foreign;
	RequireError([&] {
		task.Draw(foreign, PixelFrame(), allocate, copy);
	},
	    "another actual graphics backend");
	task.Close();
	Require(consumer.Retired == 1 && consumer.LiveAtRetirement == 2 && backend.LiveTextures == 0, "actual consumer backlinks retire before the staging pair");
}

static void TestReplacementBudgetAndGeometry() {
	CaptureBackend backend;
	sgre::scene::CaptureTaskLimits limits;
	limits.MaximumFrameReplacements = 1;
	sgre::scene::CaptureTask task(20, 10, limits);
	const auto allocate = [&](const auto& spec) {
		return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
	};
	const auto copy = [](auto&, const auto&) {
	};
	task.Draw(backend, PixelFrame(), allocate, copy);
	task.SetOffset({-1, -2});
	task.Draw(backend, PixelFrame(), allocate, copy);
	task.SetOffset({-2, -3});
	RequireError([&] {
		task.Draw(backend, PixelFrame(), allocate, copy);
	},
	    "resource retention");
	Require(backend.LiveTextures == 4, "rejected third replacement leaves actual referenced owners alive");
	task.ReleaseFrame(false);
	task.Close();
	Require(backend.LiveTextures == 0, "abort releases displaced and current owners in their containing scopes");

	auto frame = PixelFrame();
	const auto clamped = sgre::scene::PrepareCapturePhysicalBounds({-5, -3, 8, 7}, frame);
	Require(clamped.MinimumX == 0 && clamped.MinimumY == 0 && clamped.MaximumX == 8 && clamped.MaximumY == 7, "native camera AABB is clamped against the actual physical target");
	RequireError([&] {
		static_cast<void>(sgre::scene::PrepareCapturePhysicalBounds({200, 3, 210, 7}, frame));
	},
	    "positive physical");
	frame.NativeTargetType = 2;
	RequireError([&] {
		static_cast<void>(sgre::scene::PrepareCapturePhysicalBounds({0, 0, 8, 7}, frame));
	},
	    "split-target");
	RequireError([] {
		sgre::scene::CaptureTask invalid(0, 10);
	},
	    "positive actual fixed");
}

static void TestOriginalCaptureGeometry() {
	std::size_t copied = 0;
	std::size_t excluded = 0;
	for (const auto& native : kNativeCaptureGeometry) {
		sgre::scene::CaptureFrameContext frame;
		frame.Viewport = {native.Viewport[0], native.Viewport[1]};
		for (std::size_t index = 0; index < 12; ++index) {
			frame.View.Coefficients[index] = std::bit_cast<float>(native.View[index]);
			frame.Model.Coefficients[index] = std::bit_cast<float>(native.Model[index]);
		}
		const auto x = std::bit_cast<float>(native.Coordinate[0]);
		const auto y = std::bit_cast<float>(native.Coordinate[1]);
		const sgre::scene::MotionBounds logical{x, y, x + static_cast<float>(native.Width), y + static_cast<float>(native.Height)};
		const auto left = std::bit_cast<float>(native.Rounded[0]);
		const auto top = std::bit_cast<float>(native.Rounded[1]);
		const auto right = std::bit_cast<float>(native.Rounded[2]);
		const auto bottom = std::bit_cast<float>(native.Rounded[3]);
		if (!(right > left) || !(bottom > top)) {
			RequireError([&] {
				static_cast<void>(sgre::scene::PrepareCapturePhysicalBounds(logical, frame));
			},
			    "positive physical copy extent");
			++excluded;
			continue;
		}
		const auto actual = sgre::scene::PrepareCapturePhysicalBounds(logical, frame);
		const std::array<std::uint32_t, 4> words{std::bit_cast<std::uint32_t>(actual.MinimumX), std::bit_cast<std::uint32_t>(actual.MinimumY), std::bit_cast<std::uint32_t>(actual.MaximumX), std::bit_cast<std::uint32_t>(actual.MaximumY)};
		Require(words == native.Rounded, "actual MCapture4fafe0 camera/clipping/5823e0 rounded bounds differ from unchanged original instructions");
		++copied;
	}
	Require(copied != 0 && excluded != 0 && copied + excluded == 64, "native Capture geometry includes both actual positive copies and precise unsupported nonpositive extents");
}

static sgre::script::SquirrelVmLimits CaptureVmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static sgre::scene::LayerCameraFrameState CameraState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {100, 100};
	state.RawScissor = {0, 0, 100, 100};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, const char* name, std::initializer_list<SquirrelValue> operands = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), operands);

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

static void TestActualBaseCommonFirstAndVmLifetime() {
	CaptureBackend backend;
	sgre::script::SquirrelVm vm(CaptureVmLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptRawImageBindings images(vm, objects, binary);
	sgre::scene::LayerTree tree;
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	std::array<sgre::scene::MotionScissorMatrix, 1> models;
	sgre::scene::LayerCameraDrawScope camera(CameraState(), models, [](const auto&) {
	});
	sgre::script::ScriptCaptureBindings captures(vm, objects, layers);
	std::size_t thumbnail_reads = 0;
	captures.ConfigureThumbnailReadback(images, [&](auto& texture, auto width, auto height) {
		Require(texture.UsesBackend(backend) && !texture.State().Locked, "thumbnail service must receive the actual unlocked captured texture");
		++thumbnail_reads;
		std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
		for (std::uint32_t y = 0; y < height; ++y) {
			for (std::uint32_t x = 0; x < width; ++x) {
				const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
				pixels[offset] = static_cast<std::uint8_t>(x);
				pixels[offset + 1] = static_cast<std::uint8_t>(y);
				pixels[offset + 2] = static_cast<std::uint8_t>(x + y);
				pixels[offset + 3] = static_cast<std::uint8_t>(100 + x + y * 10);
			}
		}
		return pixels;
	});
	CaptureObserver consumer(backend);
	std::vector<std::shared_ptr<sgre::scene::CaptureTask>> pending;
	std::size_t copies = 0;
	captures.ConfigureFrameOwners(backend, [&](const auto& task) {
		Require(camera.Depth() == 1, "MCapture actual CommonFirst draw runs before this Base's own clip/camera");
		pending.push_back(task);
		task->Draw(
		    backend, PixelFrame(), [&](const auto& spec) {
			    return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
		    },
		    [&](auto&, const auto&) {
			    ++copies;
		    });
	});
	const auto release_frame = [&](bool submitted) {
		for (const auto& task : pending) {
			task->ReleaseFrame(submitted);
		}
		pending.clear();
	};
	try {
		const auto parent = vm.Invoke(layers.LayerFolderClass(), std::array<SquirrelValue, 1>{vm.RootTable()}).Value;
		auto parent_root = vm.Retain(parent);
		const auto capture = vm.Invoke(captures.CaptureClass(), std::array<SquirrelValue, 4>{vm.RootTable(), parent, std::int32_t(20), std::int32_t(10)}).Value;
		Require(vm.IsInstanceOf(capture, layers.LayerBaseClass()) && vm.IsInstanceOf(capture, objects.ObjectClass()), "Capture is the actual SQLayerBase descendant");
		Require(vm.RetainedReferenceCount(capture) == 0 && backend.LiveTextures == 0, "wrapper does not add a synthetic host root or eager GPU texture");
		auto capture_root = vm.Retain(capture);
		const auto handle = layers.Handle(capture);
		Require(tree.State(handle).Owner == layers.Handle(parent) && layers.BaseTasks(capture).CommonFirst().Count() == 1 && layers.BaseTasks(capture).Children().Count() == 0, "actual optional owner retains a CommonFirst physical Capture, not a camera child quad");
		layers.AttachBaseFrame(capture, camera);
		Require(Call(vm, capture, "getWidth") == SquirrelValue{20.0f} && Call(vm, capture, "getHeight") == SquirrelValue{10.0f}, "native getters read fixed constructor dimensions");
		Call(vm, capture, "setOffset", {std::int32_t(-3), -4.0f});
		Require(captures.State(capture).Coordinate.X == 3 && captures.State(capture).Coordinate.Y == 4, "actual member fixed slots preserve native negated offset conversion");
		Call(vm, capture, "setOffset");
		Require(captures.State(capture).Coordinate.X == 0, "missing final operand sees actual appended userdata and native primitive fallback");
		Call(vm, capture, "setOffset", {false, false});
		vm.ResetExecution();
		static_cast<void>(vm.CollectGarbage());
		Require(tree.Contains(handle), "real explicit application root retains Capture across actual collection");
		layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
			frame.Update();
			frame.Draw(0xfedcba98);
		});
		Require(copies == 1 && pending.size() == 1 && captures.State(capture).FramePending, "one real Base draw invokes the native copy exactly once");
		RequireError([&] {
			captures.ReleaseInstance(capture);
		},
		    "retained resource inputs");
		Require(tree.Contains(handle) && objects.HasInstance(capture), "borrow rejection occurs before actual wrapper/native owner mutation");
		release_frame(true);
		Call(vm, capture, "setOffset", {5.0f, 0.0f});
		layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
			frame.Draw(0);
		});
		Require(pending.front()->Texture().State().Description.Width == 15 && Call(vm, capture, "getWidth") == SquirrelValue{20.0f}, "native clipping changes actual physical owner extent while fixed constructor width stays unchanged");
		release_frame(true);
		RequireError([&] {
			Call(vm, capture, "storeThumbnail");
		},
		    "requires a RawImage");
		const std::array<SquirrelValue, 3> image_arguments{vm.RootTable(), std::int32_t(2), std::int32_t(2)};
		const auto image = vm.Invoke(images.RawImageClass(), image_arguments).Value;
		const auto image_root = vm.Retain(image);
		Call(vm, image, "clearUpdate");
		Require(std::holds_alternative<std::monostate>(Call(vm, capture, "storeThumbnail", {image, 2.0f})), "raw-VM thumbnail callback removes its descriptor and returns no values");
		Require(std::ranges::equal(images.Image(image).BgraPixels(), std::vector<std::uint8_t>{0, 0, 1, 105, 2, 0, 3, 107, 0, 2, 3, 125, 2, 2, 5, 127}) && !images.Image(image).IsUpdated() && thumbnail_reads == 1, "thumbnail area reduction must use actual BGRA and truncated arithmetic without changing the native update flag");
		Call(vm, capture, "storeThumbnail", {image});
		Require(std::ranges::equal(images.Image(image).BgraPixels(), std::vector<std::uint8_t>{0, 0, 0, 100, 1, 0, 1, 101, 0, 1, 1, 110, 1, 1, 2, 111}) && thumbnail_reads == 2, "default scale-one thumbnail copies the real tight image without reduction");
		RequireError([&] {
			Call(vm, capture, "storeThumbnail", {image, 0.0f});
		},
		    "bounded nonshrinking");
		layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
			frame.Draw(0);
		});
		RequireError([&] {
			Call(vm, capture, "storeThumbnail", {image});
		},
		    "preceding frame to be submitted");
		const auto physical = pending.front();
		physical->AddConsumer(consumer);
		release_frame(true);
		captures.ReleaseInstance(capture);
		Require(!tree.Contains(handle) && !objects.HasInstance(capture) && physical->State().Closed && consumer.Retired == 1 && backend.LiveTextures == 0, "actual derived cleanup and CommonFirst physical retirement clear backlinks/resources before Object destruction");
		RequireError([&] {
			Call(vm, capture, "getWidth");
		},
		    "actual live native Layer");
		capture_root = {};
		layers.ReleaseInstance(parent);
		parent_root = {};
		vm.Close();
		vm.RethrowReleaseFailure();
	} catch (...) {
		release_frame(false);
		try {
			vm.Close();
		} catch (...) {
		}
		throw;
	}
}

int main() {
	try {
		TestActualStagingLifetime();
		TestReplacementBudgetAndGeometry();
		TestOriginalCaptureGeometry();
		TestActualBaseCommonFirstAndVmLifetime();
		std::cout << "PASS actual Capture staging/companion bounds replacement, frame abort/retention, consumer retirement and SQLayerBase CommonFirst/VM ownership\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
