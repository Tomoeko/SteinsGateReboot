#include "sgre/core/frame_clock.hpp"
#include "sgre/script/capture_bindings.hpp"
#include "sgre/script/captured_bindings.hpp"

#include "sgre/scene/layer_base_frame.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"

#include "captured_native_vectors.hpp"

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

static void TestNativeCapturedPackets() {
	std::size_t draws = 0;
	for (const auto& sample : kCapturedNativeCases) {
		auto source = std::make_shared<sgre::scene::CaptureTask>(10, 10);
		sgre::scene::CapturedTask task(source);
		task.SetOpacity(sample.Opacity);
		Require(task.State().Opacity == sample.StoredOpacity, "Captured opacity clamp must match original4fb950");
		if (!sample.Ready || sample.Retired) {
			continue;
		}
		sgre::scene::CapturedTaskState state;
		state.Coordinate = {sample.Coordinate[0], sample.Coordinate[1]};
		state.FixedSize = {sample.Size[0], sample.Size[1]};
		state.Opacity = task.State().Opacity;
		const auto packet = sgre::scene::PrepareCapturedDrawPacket(state);
		std::array<std::uint32_t, 16> words;
		for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
			const auto& vertex = packet.Vertices[index];
			words[index * 4] = std::bit_cast<std::uint32_t>(vertex.Position.X);
			words[index * 4 + 1] = std::bit_cast<std::uint32_t>(vertex.Position.Y);
			words[index * 4 + 2] = std::bit_cast<std::uint32_t>(vertex.Uv.X);
			words[index * 4 + 3] = std::bit_cast<std::uint32_t>(vertex.Uv.Y);
		}
		Require(words == sample.Vertices && packet.Color == sample.Color && packet.TextureAttributes == sample.Attributes, "Captured ordinary geometry/UV/color/attributes must match original4fb2d0 words");
		++draws;
	}
	Require(draws == 52, "all52 actual ordinary native draw cases must compare");
}

static void TestActualCaptureObserverAndAbort() {
	CaptureBackend backend;
	auto source = std::make_shared<sgre::scene::CaptureTask>(20, 10);
	sgre::scene::CapturedTask consumer(source);
	Require(consumer.State().FixedSize.X == 20 && consumer.State().FixedSize.Y == 10 && !consumer.State().Ready && backend.LiveTextures == 0, "native consumer reads cached initial extent without allocating or forcing Capture ready");
	std::size_t draws = 0;
	const auto drawing = [&](auto& texture, const auto& packet) {
		Require(texture.UsesBackend(backend) && texture.State().Storage == sgre::scene::MutableTextureStorage::StagingCompanion, "Captured samples its actual source's staging/companion owner");
		Require(packet.Vertices[3].Position.X == consumer.State().FixedSize.X && packet.Vertices[3].Position.Y == consumer.State().FixedSize.Y, "Captured uses actual last physical extent and normalized full-source UV");
		Require(!consumer.CanClose(), "physical Captured draw owns its enclosing source packet borrow");
		RequireError([&] {
			consumer.Close();
		},
		    "live idle");
		++draws;
	};
	const auto capture = [&] {
		source->Draw(backend, PixelFrame(), [&](const auto& spec) {
			return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
		},
		    [&](auto&, const auto&) {
			    Require(consumer.State().Ready, "native resize updates readiness before actual copy");
		    });
	};
	consumer.Draw(drawing);
	Require(draws == 0, "not-ready native consumer must skip all drawing");
	capture();
	consumer.Draw(drawing);
	Require(draws == 1 && consumer.State().Ready, "same containing frame can consume the actual completed capture");
	RequireError([&] {
		consumer.Close();
	},
	    "source frame End or Abort");
	source->ReleaseFrame(false);
	consumer.Draw(drawing);
	Require(draws == 1 && !consumer.State().Ready, "aborted framebuffer copy invalidates actual consumer readiness");
	RequireError([&] {
		source->Draw(backend, PixelFrame(), [&](const auto& spec) {
			return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
		},
		    [](auto&, const auto&) {
			    throw std::runtime_error("actual copy failed");
		    });
	},
	    "actual copy failed");
	consumer.Draw(drawing);
	Require(draws == 1 && !consumer.State().Ready, "failed same-bounds copy must not publish completion readiness");
	source->ReleaseFrame(false);
	// Same-bounds retry must recover readiness only after a fresh real copy, without inventing a resize.
	source->Draw(backend, PixelFrame(), [&](const auto& spec) {
		return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
	},
	    [](auto&, const auto&) {
	    });
	consumer.Draw(drawing);
	Require(draws == 2, "successful same-bounds retry must publish actual consumer readiness after Abort");
	source->ReleaseFrame(true);
	source->SetOffset({5, 0});
	capture();
	consumer.Draw(drawing);
	Require(consumer.State().FixedSize.X == 15 && draws == 3 && backend.LiveTextures == 4, "actual clipped bounds replacement updates consumer dimensions and retains displaced source until End");
	source->ReleaseFrame(true);
	Require(backend.LiveTextures == 2, "successful containing frame releases only displaced source pair");
	source->Close();
	Require(consumer.State().SourceRetired && backend.LiveTextures == 0, "native source retirement clears consumer backlink before texture pair deletion");
	consumer.Draw(drawing);
	Require(draws == 3, "retired native source is an honest draw no-op");
	consumer.Close();
}

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, const char* name, std::initializer_list<SquirrelValue> operands = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), operands);

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

static void TestRealVmStrongSourceAndCameraChild() {
	CaptureBackend backend;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::scene::LayerTree tree;
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	sgre::scene::LayerCameraFrameState frame_state;
	frame_state.Viewport = {100, 100};
	frame_state.RawScissor = {0, 0, 100, 100};
	frame_state.RoundedScissor = frame_state.RawScissor;
	frame_state.AppliedRoundedScissor = frame_state.RawScissor;
	std::array<sgre::scene::MotionScissorMatrix, 1> models;
	sgre::scene::LayerCameraDrawScope camera(frame_state, models, [](const auto&) {
	});
	sgre::script::ScriptCaptureBindings captures(vm, objects, layers);
	std::vector<std::shared_ptr<sgre::scene::CaptureTask>> pending;
	captures.ConfigureFrameOwners(backend, [&](const auto& source) {
		pending.push_back(source);
		source->Draw(backend, PixelFrame(), [&](const auto& spec) {
			return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
		},
		    [](auto&, const auto&) {
		    });
	});
	std::size_t draws = 0;
	sgre::script::ScriptCapturedBindings consumers(vm, objects, layers, captures, [&](auto& actual_source, const auto& packet) {
		Require(camera.Depth() == 2 && actual_source.UsesBackend(backend), "native MCaptured is a genuine camera child using actual source texture");
		Require(packet.Color == 0x7fffffffu && packet.TextureAttributes == 0, "actual uint32 setter feeds clamped opacity and white draw alpha");
		++draws;
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
		const auto source = vm.Invoke(captures.CaptureClass(), std::array<SquirrelValue, 3>{vm.RootTable(), std::int32_t(20), std::int32_t(10)}).Value;
		const auto source_handle = layers.Handle(source);
		layers.AttachBaseFrame(source, camera);
		const auto consumer = vm.Invoke(consumers.CapturedClass(), std::array<SquirrelValue, 3>{vm.RootTable(), parent, source}).Value;
		auto consumer_root = vm.Retain(consumer);
		const auto consumer_handle = layers.Handle(consumer);
		layers.AttachBaseFrame(consumer, camera);
		Require(vm.IsInstanceOf(consumer, layers.LayerBaseClass()) && layers.BaseTasks(consumer).CommonFirst().Count() == 0 && layers.BaseTasks(consumer).Children().Count() == 1, "Captured is real SQLayerBase with one camera child, not another Capture CommonFirst producer");
		Require(vm.RetainedReferenceCount(source) == 1, "SQCaptured retains actual source VMObjectInfo, independently of its optional parent");
		const auto unparented = vm.Invoke(consumers.CapturedClass(), std::array<SquirrelValue, 2>{vm.RootTable(), source}).Value;
		Require(!tree.State(layers.Handle(unparented)).Owner && vm.RetainedReferenceCount(source) == 2, "one-argument Captured constructor consumes Capture as source rather than misclassifying it as optional Layer owner");
		consumers.ReleaseInstance(unparented);
		Require(vm.RetainedReferenceCount(source) == 1, "physical consumer cleanup releases only its actual source root");
		vm.ResetExecution();
		static_cast<void>(vm.CollectGarbage());
		Require(tree.Contains(source_handle) && captures.State(source).FixedSize.X == 20, "real consumer VM root keeps Capture alive through collection");
		Call(vm, consumer, "setOpacity", {std::int32_t(-1)});
		Require(Call(vm, consumer, "getOpacity") == SquirrelValue{255.0f}, "native unsigned opacity conversion occurs before float clamp");
		Call(vm, consumer, "setOpacity");
		Require(Call(vm, consumer, "getOpacity") == SquirrelValue{0.0f}, "missing primitive opacity reads the actual appended descriptor and numeric fallback zero");
		Call(vm, consumer, "setOpacity", {std::int32_t(127)});
		Require(Call(vm, consumer, "getOpacity") == SquirrelValue{127.0f}, "native getter returns actual float opacity");
		layers.DispatchBaseFrame(source_handle, [](const auto&, auto& frame) {
			frame.Draw(0);
		});
		layers.DispatchBaseFrame(consumer_handle, [](const auto&, auto& frame) {
			frame.Draw(0);
		});
		Require(draws == 1 && Call(vm, consumer, "getWidth") == SquirrelValue{20.0f}, "actual original producer and camera child execute in real containing phases");
		release_frame(true);
		RequireError([&] {
			Call(vm, consumer, "setOpacityMap");
		},
		    "opacity-map");
		Call(vm, consumer, "resetOpacityMap");

		RequireError([&] {
			Call(vm, consumer, "animateOpacity", {std::int32_t(0), std::int32_t(1), std::int32_t(0)});
		},
		    "shared Looper clock");
		sgre::core::FrameClock clock;
		consumers.ConfigureAnimationClock(clock);
		Call(vm, consumer, "animateOpacity", {std::int32_t(0), std::int32_t(1), std::int32_t(0)});
		Require(Call(vm, consumer, "getOpacity") == SquirrelValue{127.0f} && layers.BaseTasks(consumer).CommonFirst().Count() == 1, "real VM animateOpacity owns a deferred actual CommonFirst handler");
		layers.DispatchBaseFrame(consumer_handle, [](const auto&, auto& frame) {
			frame.Update();
		});
		Require(Call(vm, consumer, "getOpacity") == SquirrelValue{0.0f} && layers.BaseTasks(consumer).CommonFirst().Count() == 0, "actual containing update publishes and retires the native terminal opacity sample");
		captures.ReleaseInstance(source);
		Require(consumers.State(consumer).SourceRetired, "explicit source wrapper retirement clears physical backlink even while its VM object stays rooted");
		layers.DispatchBaseFrame(consumer_handle, [](const auto&, auto& frame) {
			frame.Draw(0);
		});
		Require(draws == 1, "source retirement keeps surviving Captured wrapper as native draw no-op");
		consumers.ReleaseInstance(consumer);
		Require(vm.RetainedReferenceCount(source) == 0, "actual consumer wrapper release drops source VMObjectInfo");
		consumer_root = {};
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
		TestNativeCapturedPackets();
		TestActualCaptureObserverAndAbort();
		TestRealVmStrongSourceAndCameraChild();
		std::cout << "PASS Captured native52draw/64opacity cases, real source notifications/Abort retry and SQLayerBase camera-child/VM-root ownership\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
