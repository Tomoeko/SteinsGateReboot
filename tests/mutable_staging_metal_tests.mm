#include "metal_support.hpp"
#include "mutable_texture_backend.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view text) {
	if (!condition) {
		throw std::runtime_error(std::string(text));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view text) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(text) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("staging Metal backend accepted an invalid actual resource or frame phase");
}

struct MetalRecord {
	id<MTLTexture> View = nil;
	std::size_t Bytes = 0;
	std::uint64_t Generation = 1;
	bool Live = false;
};

class MetalFixture {
public:
	explicit MetalFixture(std::size_t maximum = 8192, bool copy_callback = true) : Device(sgre::render::FindMetalDevice()), Queue([Device newCommandQueue]), Maximum(maximum) {
		Require(Queue != nil, "staging fixture has no real Metal queue");
		sgre::render::SpriteMutableTextureContext context;
		context.ResourceDomain = this;
		context.Device = Device;
		context.Queue = Queue;
		context.MaximumBytes = maximum;
		context.Available = [this] {
			return Live;
		};
		context.BindingAvailable = [this] {
			return Live && Binding;
		};
		context.IsPendingFrameUpload = [this](id<MTLCommandBuffer> commands) {
			return commands != nil && commands == Pending;
		};
		context.EncodeUpload = [](id<MTLBuffer>, std::size_t, id<MTLTexture>, std::uint32_t, std::uint32_t) -> id<MTLCommandBuffer> {
			throw std::runtime_error("GPU staging path attempted a CPU upload");
		};
		if (copy_callback) {
			context.EncodeTextureCopy = [this](id<MTLTexture> destination, id<MTLTexture> source, std::uint32_t width, std::uint32_t height) {
				return Encode(destination, source, width, height);
			};
		}
		context.Register = [this](std::size_t bytes, std::uint32_t) {
			Require(bytes <= Maximum - Allocated, "fixture actual registration budget exceeded");
			const auto index = Records.size();
			Require(index < 32, "fixture actual registration count exceeded");
			Records.push_back({nil, bytes, 1, true});
			Allocated += bytes;
			return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(index), 1, this};
		};
		context.Retire = [this](sgre::render::SpriteTextureHandle handle) noexcept {
			if (handle.Owner != this || handle.Index >= Records.size() || !Records[handle.Index].Live || Records[handle.Index].Generation != handle.Generation) {
				std::terminate();
			}
			auto& record = Records[handle.Index];
			Allocated -= record.Bytes;
			record.View = nil;
			record.Live = false;
			++record.Generation;
		};
		context.SetView = [this](sgre::render::SpriteTextureHandle handle, id<MTLTexture> view) {
			Require(handle.Owner == this && Records.at(handle.Index).Live && Records[handle.Index].Generation == handle.Generation, "fixture view belongs to a retired actual descriptor");
			Records[handle.Index].View = view;
		};
		context.Bind = [this](sgre::render::SpriteTextureHandle handle, id<MTLSamplerState> sampler, std::uint32_t slot, sgre::scene::MutableTextureStage stage) {
			Require(handle.Owner == this && Records.at(handle.Index).Live && Records[handle.Index].Generation == handle.Generation && Records[handle.Index].View != nil && sampler != nil && slot == 0 && stage == sgre::scene::MutableTextureStage::Pixel, "fixture binding lost its actual sampled companion/sampler");
			++Bindings;
		};
		Backend = sgre::render::CreateSpriteMutableTextureBackend(std::move(context));
	}

	id<MTLCommandBuffer> Encode(id<MTLTexture> destination, id<MTLTexture> source, std::uint32_t width, std::uint32_t height) {
		Require(Live && Binding && destination.device == Device && source.device == Device, "fixture copy lost its actual live device/binding phase");
		id<MTLCommandBuffer> commands = Pending == nil ? [Queue commandBuffer] : Pending;
		Require(commands != nil && commands.retainedReferences, "fixture copy has no real retained commands");
		id<MTLBlitCommandEncoder> encoder = [commands blitCommandEncoder];
		Require(encoder != nil, "fixture copy has no actual blit encoder");
		[encoder copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1) toTexture:destination destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
		[encoder endEncoding];
		++Copies;
		if (Pending == nil && !DropSubmission) {
			[commands commit];
		}
		return commands;
	}

	void Capture(sgre::scene::MutableTextureOwner& target, const std::array<std::uint8_t, 16>& pixels) {
		const auto description = target.State().Description;
		MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:description.OriginalFormat == 28 ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatBGRA8Unorm width:2 height:2 mipmapped:NO];
		descriptor.storageMode = MTLStorageModeShared;
		id<MTLTexture> source = [Device newTextureWithDescriptor:descriptor];
		Require(source != nil, "fixture actual capture source texture allocation failed");
		[source replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:8];
		id<MTLTexture> staging = sgre::render::SpriteMutableStagingTexture(target.Resource(), this);
		static_cast<void>(Encode(staging, source, 2, 2));
		target.NotifyGpuWrite();
	}

	std::array<std::uint8_t, 16> Read(const sgre::scene::MutableTextureOwner& target) {
		Require(Pending == nil && target.Companion() != nullptr, "fixture readback requires a completed sampled companion");
		const auto handle = sgre::render::SpriteMutableTextureHandle(target.Companion()->Resource());
		id<MTLTexture> texture = Records.at(handle.Index).View;
		id<MTLBuffer> buffer = [Device newBufferWithLength:512 options:MTLResourceStorageModeShared];
		id<MTLCommandBuffer> commands = [Queue commandBuffer];
		id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
		Require(texture != nil && buffer != nil && commands != nil && blit != nil, "fixture readback lost its actual GPU resource");
		[blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(2, 2, 1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:512];
		[blit endEncoding];
		[commands commit];
		sgre::render::WaitForMetalCommand(commands, "staging fixture readback");
		std::array<std::uint8_t, 16> pixels{};
		const auto* bytes = static_cast<const std::uint8_t*>(buffer.contents);
		std::copy_n(bytes, 8, pixels.begin());
		std::copy_n(bytes + 256, 8, pixels.begin() + 8);
		return pixels;
	}

	void Begin() {
		Require(Pending == nil, "fixture nested pending frame");
		Pending = [Queue commandBuffer];
		Require(Pending != nil && Pending.retainedReferences, "fixture pending frame must retain its actual resources");
	}
	void Complete() {
		id<MTLCommandBuffer> commands = Pending;
		Require(commands != nil, "fixture has no actual pending frame");
		[commands commit];
		sgre::render::WaitForMetalCommand(commands, "staging fixture pending frame");
		Pending = nil;
	}

	id<MTLDevice> Device = nil;
	id<MTLCommandQueue> Queue = nil;
	id<MTLCommandBuffer> Pending = nil;
	std::size_t Maximum = 0;
	std::size_t Allocated = 0;
	std::size_t Copies = 0;
	std::size_t Bindings = 0;
	bool Live = true;
	bool Binding = true;
	bool DropSubmission = false;
	std::vector<MetalRecord> Records;
	std::unique_ptr<sgre::scene::MutableTextureBackend> Backend;
};

static sgre::scene::MutableTextureSpec Spec(std::uint32_t format = 28) {
	sgre::scene::MutableTextureSpec spec;
	spec.Storage = sgre::scene::MutableTextureStorage::StagingCompanion;
	spec.Description.Width = 2;
	spec.Description.Height = 2;
	spec.Description.OriginalFormat = format;
	return spec;
}

static constexpr std::array<std::uint8_t, 16> kInitial{13, 27, 41, 203, 47, 59, 73, 201, 83, 97, 109, 199, 127, 139, 151, 197};
static constexpr std::array<std::uint8_t, 16> kAborted{161, 151, 141, 191, 131, 121, 111, 189, 101, 91, 81, 187, 71, 61, 51, 185};
static constexpr std::array<std::uint8_t, 16> kFresh{211, 197, 181, 241, 167, 151, 137, 239, 127, 113, 103, 237, 89, 73, 59, 235};

static void TestFormats(std::uint32_t format) {
	MetalFixture fixture;
	{
		sgre::scene::MutableTextureOwner target(*fixture.Backend, Spec(format));
		Require(fixture.Records.size() == 2 && fixture.Records[0].View == nil && fixture.Records[1].View != nil && fixture.Allocated == 1568, "staging main must have no sampled view and both actual allocations stay budgeted");
		const auto state = target.State();
		Require(state.Description.Usage == sgre::scene::MutableTextureUsage::Staging && state.Description.CpuRead && state.Description.CpuWrite && !state.Description.ShaderResource && target.Companion()->State().Description.Usage == sgre::scene::MutableTextureUsage::Default, "portable native staging/main/companion descriptions changed");
		int foreign_domain = 0;
		RequireFailure([&] {
			static_cast<void>(sgre::render::SpriteMutableStagingTexture(target.Resource(), &foreign_domain));
		},
		    "resource domain");
		RequireFailure([&] {
			static_cast<void>(sgre::render::SpriteMutableStagingTexture(target.Companion()->Resource(), &fixture));
		},
		    "nonsampled");
		RequireFailure([&] {
			static_cast<void>(target.Map());
		},
		    "hardware maps");
		Require(!target.State().Locked && !target.State().Dirty, "unsupported hardware map cannot manufacture a lock or captured pixels");
		fixture.Capture(target, kInitial);
		target.Bind(0, sgre::scene::MutableTextureStage::Pixel);
		const auto pixels = fixture.Read(target);
		for (std::size_t byte = 0; byte != pixels.size(); ++byte) {
			if (format != 88 || (byte & 3) != 3) {
				Require(pixels[byte] == kInitial[byte], "actual Metal staging/companion copy changed a source pixel");
			}
		}
		if (format == 88) {
			Require(fixture.Records[1].View.swizzle.alpha == MTLTextureSwizzleOne, "BGRX actual sampled companion must preserve the opaque-alpha view");
		}
		Require(fixture.Copies == 2 && fixture.Bindings == 1 && !target.State().Dirty, "one actual main write and one companion blit precede actual sampled binding");
		fixture.Binding = false;
		RequireFailure([&] {
			static_cast<void>(sgre::render::SpriteMutableStagingTexture(target.Resource(), &fixture));
		},
		    "binding requires");
		fixture.Binding = true;
	}
	Require(fixture.Allocated == 0 && std::none_of(fixture.Records.begin(), fixture.Records.end(), [](const auto& record) {
		return record.Live;
	}),
	    "staging/companion retirement must release both real registrations");
}

static void TestAbortedFrameAndFreshCopy() {
	MetalFixture fixture;
	sgre::scene::MutableTextureOwner target(*fixture.Backend, Spec());
	fixture.Capture(target, kInitial);
	target.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	Require(fixture.Read(target) == kInitial, "actual initial idle companion pixels missing");
	fixture.Begin();
	fixture.Capture(target, kAborted);
	target.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto binds = fixture.Bindings;
	// Drop the actual unsubmitted frame. The backend still retains its command identity for rejection.
	fixture.Pending = nil;
	RequireFailure([&] {
		target.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	},
	    "fresh real GPU copy");
	Require(fixture.Bindings == binds, "aborted companion cannot publish another sampled binding");
	fixture.Begin();
	fixture.Capture(target, kFresh);
	target.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	fixture.Complete();
	Require(fixture.Read(target) == kFresh, "fresh actual same-frame main/companion copies must recover after Abort");
	fixture.Capture(target, kFresh);
	fixture.DropSubmission = true;
	RequireFailure([&] {
		target.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	},
	    "submitted or pending frame");
	Require(target.State().Dirty, "failed actual copy submission must retain portable dirty state");
	fixture.DropSubmission = false;
	target.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	Require(fixture.Read(target) == kFresh, "fresh real companion copy must recover an invalidated copy command");
}

static void TestGuards() {
	MetalFixture tiny(15);
	RequireFailure([&] {
		sgre::scene::MutableTextureOwner owner(*tiny.Backend, Spec());
	},
	    "allocation limit");
	Require(tiny.Allocated == 0, "rejected staging creation must not publish a registration");
	MetalFixture partial(16);
	RequireFailure([&] {
		sgre::scene::MutableTextureOwner owner(*partial.Backend, Spec());
	},
	    "allocation limit");
	Require(partial.Allocated == 0, "failed companion allocation must retire the actual staging main");
	MetalFixture fixture;
	MetalFixture foreign;
	sgre::scene::MutableTextureOwner target(*fixture.Backend, Spec());
	auto destination_spec = Spec();
	destination_spec.Storage = sgre::scene::MutableTextureStorage::GpuOnly;
	sgre::scene::MutableTextureOwner destination(*fixture.Backend, destination_spec);
	RequireFailure([&] {
		foreign.Backend->Copy(destination.Resource(), target.Resource());
	},
	    "another backend");
	destination_spec.Description.Width = 1;
	sgre::scene::MutableTextureOwner wrong_size(*fixture.Backend, destination_spec);
	RequireFailure([&] {
		fixture.Backend->Copy(wrong_size.Resource(), target.Resource());
	},
	    "full-size same-format");
	MetalFixture no_copy(8192, false);
	sgre::scene::MutableTextureOwner unsupported(*no_copy.Backend, Spec());
	no_copy.Capture(unsupported, kInitial);
	RequireFailure([&] {
		unsupported.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	},
	    "GPU texture-copy service");
	Require(unsupported.State().Dirty, "missing genuine copy service cannot consume dirty state");
}

int main() {
	@autoreleasepool {
		try {
			for (const auto format : {28u, 87u, 88u}) {
				TestFormats(format);
			}
			TestAbortedFrameAndFreshCopy();
			TestGuards();
			std::cout << "Actual Metal staging RGBA/BGRA/BGRX blit/companion/Abort/retirement/domain/budget PASS\n";
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	}
}
