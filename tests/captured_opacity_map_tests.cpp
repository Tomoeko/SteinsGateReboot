#include "sgre/scene/captured_opacity_map.hpp"
#include "sgre/scene/layer_base_frame.hpp"
#include "sgre/script/capture_bindings.hpp"
#include "sgre/script/captured_bindings.hpp"
#include "sgre/script/image_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"

#include "captured_map_native_vectors.hpp"
#include "image_fixture.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

using sgre::script::SquirrelValue;

template <typename Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("expected Captured opacity map rejection: " + std::string(expected));
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

static void TestNativeMapPackets() {
	std::size_t draws = 0;
	for (const auto& sample : kCapturedMapNativeCases) {
		sgre::scene::CapturedTaskState state;
		state.FixedSize = {sample.Size[0], sample.Size[1]};
		state.Coordinate = {sample.Coordinate[0], sample.Coordinate[1]};
		state.Opacity = sample.Opacity;
		sgre::scene::CapturedOpacityMapState map;
		map.ImageRatio = {static_cast<float>(sample.Logical[0]) / static_cast<float>(sample.Physical[0]), static_cast<float>(sample.Logical[1]) / static_cast<float>(sample.Physical[1])};
		map.Repeat = {sample.Repeat[0], sample.Repeat[1]};
		map.Width = std::clamp(std::bit_cast<std::uint32_t>(sample.Width), 1u, 765u);
		map.AddressU = sample.Address[0];
		map.AddressV = sample.Address[1];
		Require(map.Width == sample.ClampedWidth && std::bit_cast<std::uint32_t>(map.ImageRatio.X) == sample.Ratios[0] && std::bit_cast<std::uint32_t>(map.ImageRatio.Y) == sample.Ratios[1], "actual first-image texture ratio and unsigned physical width clamp must match original4fb990");
		if (!sample.Draw) {
			continue;
		}
		const auto packet = sgre::scene::PrepareCapturedOpacityMapDrawPacket(state, map);
		std::array<std::uint32_t, 28> words{};
		for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
			const auto& vertex = packet.Vertices[index];
			words[index * 7] = std::bit_cast<std::uint32_t>(vertex.Position.X);
			words[index * 7 + 1] = std::bit_cast<std::uint32_t>(vertex.Position.Y);
			words[index * 7 + 2] = vertex.Color;
			words[index * 7 + 3] = std::bit_cast<std::uint32_t>(vertex.SourceUv.X);
			words[index * 7 + 4] = std::bit_cast<std::uint32_t>(vertex.SourceUv.Y);
			words[index * 7 + 5] = std::bit_cast<std::uint32_t>(vertex.MapUv.X);
			words[index * 7 + 6] = std::bit_cast<std::uint32_t>(vertex.MapUv.Y);
		}
		Require(words == sample.Vertices && packet.Indices == std::array<std::uint16_t, 4>{0, 1, 2, 3}, "format5 position/ARGB/source UV/map UV/index order must match every original word");
		Require(std::bit_cast<std::uint32_t>(packet.Phase) == sample.Phase && std::bit_cast<std::uint32_t>(packet.InverseVague) == sample.InverseVague && packet.AddressU == map.AddressU && packet.AddressV == map.AddressV, "native opacity phase/inverseVague arithmetic and sampler selectors must match bit-exact");
		++draws;
	}
	Require(draws == 52, "all52 original map draws must be compared");
	sgre::scene::CapturedTaskState state;
	state.FixedSize = {2, 3};
	sgre::scene::CapturedOpacityMapState map;
	map.Width = 0;
	RequireError([&] {
		static_cast<void>(sgre::scene::PrepareCapturedOpacityMapDrawPacket(state, map));
	},
	    "bounded width");
	map.Width = 1;
	map.Repeat.X = std::numeric_limits<float>::infinity();
	RequireError([&] {
		static_cast<void>(sgre::scene::PrepareCapturedOpacityMapDrawPacket(state, map));
	},
	    "bounded width");
}

static void TestActualImageBorrowAndPhysicalSource() {
	ImageFixture image;
	const auto document = Document({Entry("map", 3, 5, {Tile(0, 0, 1, 1, 0)})});
	const auto asset = image.Catalog.Prepare(document, image.Backing);
	sgre::scene::ImageTextureOwner map(image.Cache, asset);
	CaptureBackend backend;
	auto source = std::make_shared<sgre::scene::CaptureTask>(20, 10);
	sgre::scene::CapturedTask consumer(source);
	consumer.SetOpacityMap(*asset, map.Source(0), -10);
	consumer.SetOpacityMapRepeat({2, -.5f});
	consumer.SetOpacityMapSmoothing(true);
	consumer.SetOpacity(128);
	Require(consumer.OpacityMapState().Width == 765 && consumer.OpacityMapState().ImageRatio.X == 3 && consumer.OpacityMapState().ImageRatio.Y == 5, "actual Image source0 ratio retained without substituting tile logical extent");
	source->Draw(backend, PixelFrame(), [&](const auto& spec) {
		return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
	},
	    [](auto&, const auto&) {
	    });
	std::size_t ordinary = 0;
	std::size_t mapped = 0;
	const auto draw = [&](auto&, const auto&) {
		++ordinary;
	};
	const auto draw_map = [&](auto& actual_capture, auto actual_map, const auto& packet) {
		Require(actual_capture.UsesBackend(backend) && actual_map.SameIdentity(map.Source(0)), "map draw borrows the exact Capture owner and first Image source wrapper");
		const auto cached = image.Cache.Get(actual_map.Texture());
		Require(cached.Minification == 1 && cached.Magnification == 1 && image.Cache.Address(actual_map.Texture()).Horizontal == 1 && image.Cache.Address(actual_map.Texture()).Vertical == 1, "actual shared Image sampler receives linear filter and retained native address flags");
		Require(packet.Vertices[3].MapUv.X == 6 && packet.Vertices[3].MapUv.Y == -2.5f, "native actual repeat times first-source ratio reaches the dual-UV recipient");
		RequireError([&] {
			consumer.ResetOpacityMap();
		},
		    "live idle");
		++mapped;
	};
	consumer.Draw(draw, draw_map);
	source->ReleaseFrame(true);
	Require(mapped == 1 && ordinary == 0, "selected opacity map must not enter the ordinary opaque recipient");
	map.Close();
	RequireError([&] {
		consumer.Draw(draw, draw_map);
	},
	    "Image source retired");
	consumer.ResetOpacityMap();
	consumer.Draw(draw, draw_map);
	Require(mapped == 1 && ordinary == 1, "reset map restores actual ordinary Capture sampling without changing opacity");
	source->Close();
	consumer.Close();
}

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, const char* name, std::initializer_list<SquirrelValue> tail = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), tail);
	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

static void TestActualVmMapBinding() {
	ImageFixture resources;
	CaptureBackend backend;
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptImageBindings images(vm, objects, psb, resources.Catalog, resources.Cache);
	sgre::scene::LayerTree tree;
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	sgre::script::ScriptCaptureBindings captures(vm, objects, layers);
	captures.ConfigureFrameOwners(backend, [&](const auto& actual_source) {
		actual_source->Draw(backend, PixelFrame(), [&](const auto& spec) {
			return std::make_unique<sgre::scene::MutableTextureOwner>(backend, spec);
		},
		    [](auto&, const auto&) {
		    });
	});
	sgre::script::ScriptCapturedBindings consumers(vm, objects, layers, captures, [](auto&, const auto&) {
	});
	consumers.ConfigureOpacityMaps(images, [](auto&, auto, const auto&) {
	});
	const auto source = vm.Invoke(captures.CaptureClass(), std::array<SquirrelValue, 3>{vm.RootTable(), std::int32_t(20), std::int32_t(10)}).Value;
	const auto captured = vm.Invoke(consumers.CapturedClass(), std::array<SquirrelValue, 2>{vm.RootTable(), source}).Value;
	const auto document = psb.WrapDocument(Document({Entry("map", 3, 5, {Tile(0, 0, 1, 1, 0)})}), resources.Backing);
	const auto image = vm.Invoke(images.ImageClass(), std::array<SquirrelValue, 2>{vm.RootTable(), document}).Value;
	const auto roots = vm.RetainedReferenceCount(image);
	Call(vm, captured, "setOpacityMap", {image, std::int32_t(765)});
	Require(vm.RetainedReferenceCount(image) == roots, "native opacity map borrows Image source0 without inventing another Image ObjectInfo root");
	Call(vm, captured, "setOpacityMapRepeat", {2.0f, -.5f});
	Call(vm, captured, "setOpacityMapSmoothing", {false});
	Require(Call(vm, captured, "getOpacityMapSmoothing") == SquirrelValue{false}, "actual false flag preserves native clamp address pair");
	Call(vm, captured, "setOpacityMapSmoothing");
	Require(Call(vm, captured, "getOpacityMapSmoothing") == SquirrelValue{true}, "short typed bool call sees its actual appended method descriptor as truthy");
	Call(vm, captured, "resetOpacityMap");
	images.ReleaseInstance(image);
	consumers.ReleaseInstance(captured);
	captures.ReleaseInstance(source);
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestNativeMapPackets();
		TestActualImageBorrowAndPhysicalSource();
		TestActualVmMapBinding();
		std::cout << "PASS Captured opacity map native64/52 format5 packets, real Image/Capture source and VM borrow/sampler bindings\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
