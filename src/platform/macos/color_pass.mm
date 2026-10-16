#include "sgre/render/color_pass.hpp"

#include "metal_support.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace sgre::render {

	static constexpr std::size_t kVertexBytes = 9 * sizeof(float);
	static constexpr std::size_t kFrameSlots = 3;
	static constexpr std::uint32_t kMaximumTargetDimension = 16384;

	struct ColorFrameSlot {
		id<MTLBuffer> Vertices = nil;
		id<MTLBuffer> Indices = nil;
		id<MTLBuffer> Constants = nil;
		id<MTLCommandBuffer> Commands = nil;
		std::uint64_t Submission = 0;
	};

	class ColorPassRenderer::Implementation {
	public:
		explicit Implementation(ColorPassDescription description, const std::filesystem::path& shader_source) : Description(description) {
			if (description.Width == 0 || description.Height == 0 || description.Width > kMaximumTargetDimension || description.Height > kMaximumTargetDimension || description.MaxVertices < 3 || description.MaxIndices < 3 || description.MaxVertices > 65536 || description.MaxIndices > 1024 * 1024) {
				throw std::runtime_error("Metal color pass requires bounded nonempty dimensions and geometry limits");
			}

			const auto width = static_cast<std::size_t>(description.Width);
			const auto height = static_cast<std::size_t>(description.Height);
			if (width > std::numeric_limits<std::size_t>::max() / 4 || height > description.MaxPixelBytes / (width * 4)) {
				throw std::runtime_error("Metal color target exceeds its pixel allocation limit");
			}

			RowBytes = ((width * 4 + 255) / 256) * 256;
			if (height > description.MaxPixelBytes / RowBytes) {
				throw std::runtime_error("Metal color readback exceeds its padded allocation limit");
			}

			@autoreleasepool {
				Device = FindMetalDevice();
				Queue = [Device newCommandQueue];
				if (Queue == nil) {
					throw std::runtime_error("Metal command queue is unavailable");
				}

				id<MTLLibrary> library = CompileMetalLibrary(Device, shader_source);
				NSError* error = nil;

				MTLRenderPipelineDescriptor* pipeline = [[MTLRenderPipelineDescriptor alloc] init];
				pipeline.vertexFunction = [library newFunctionWithName:@"TransformColorVertex"];
				pipeline.fragmentFunction = [library newFunctionWithName:@"PassthroughColorFragment"];
				pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
				pipeline.colorAttachments[0].blendingEnabled = NO;
				MTLVertexDescriptor* layout = [[MTLVertexDescriptor alloc] init];
				layout.attributes[0].format = MTLVertexFormatFloat3;
				layout.attributes[0].offset = 0;
				layout.attributes[0].bufferIndex = 0;
				layout.attributes[1].format = MTLVertexFormatFloat4;
				layout.attributes[1].offset = 3 * sizeof(float);
				layout.attributes[1].bufferIndex = 0;
				layout.attributes[2].format = MTLVertexFormatFloat2;
				layout.attributes[2].offset = 7 * sizeof(float);
				layout.attributes[2].bufferIndex = 0;
				layout.layouts[0].stride = kVertexBytes;
				pipeline.vertexDescriptor = layout;
				Pipeline = [Device newRenderPipelineStateWithDescriptor:pipeline error:&error];
				if (Pipeline == nil) {
					throw MetalOperationError("create color pass pipeline", error);
				}

				MTLTextureDescriptor* target = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width height:height mipmapped:NO];
				target.storageMode = MTLStorageModePrivate;
				target.usage = MTLTextureUsageRenderTarget;
				Target = [Device newTextureWithDescriptor:target];
				Readback = [Device newBufferWithLength:RowBytes * height options:MTLResourceStorageModeShared];
				if (Target == nil || Readback == nil) {
					throw std::runtime_error("Metal color target/readback allocation failed");
				}

				for (auto& slot : Slots) {
					slot.Vertices = [Device newBufferWithLength:description.MaxVertices * kVertexBytes options:MTLResourceStorageModeShared];
					slot.Indices = [Device newBufferWithLength:description.MaxIndices * sizeof(std::uint16_t) options:MTLResourceStorageModeShared];
					slot.Constants = [Device newBufferWithLength:16 * sizeof(float) options:MTLResourceStorageModeShared];
					if (slot.Vertices == nil || slot.Indices == nil || slot.Constants == nil) {
						throw std::runtime_error("Metal color upload allocation failed");
					}
				}
			}
		}

		~Implementation() {
			for (const auto& slot : Slots) {
				if (slot.Commands != nil) {
					[slot.Commands waitUntilCompleted];
				}
			}
		}

		ColorPassDescription Description;
		std::size_t RowBytes = 0;
		std::array<ColorFrameSlot, kFrameSlots> Slots;
		std::uint64_t NextSubmission = 1;
		std::uint64_t LatestSubmission = 0;
		id<MTLDevice> Device = nil;
		id<MTLCommandQueue> Queue = nil;
		id<MTLRenderPipelineState> Pipeline = nil;
		id<MTLTexture> Target = nil;
		id<MTLBuffer> Readback = nil;

		static bool Available(const ColorFrameSlot& slot) {
			return slot.Commands == nil || slot.Commands.status == MTLCommandBufferStatusCompleted || slot.Commands.status == MTLCommandBufferStatusError;
		}
	};

	ColorPassRenderer::ColorPassRenderer(ColorPassDescription description, const std::filesystem::path& shader_source) : m_Implementation(std::make_unique<Implementation>(description, shader_source)) {
	}

	ColorPassRenderer::~ColorPassRenderer() = default;

	bool ColorPassRenderer::CanSubmit() const {
		return std::any_of(m_Implementation->Slots.begin(), m_Implementation->Slots.end(), Implementation::Available);
	}

	std::uint64_t ColorPassRenderer::Submit(std::span<const ColorPassVertex> vertices, std::span<const std::uint16_t> strip_indices, const std::array<float, 16>& column_major_matrix, const std::array<float, 4>& clear_color) {
		auto& state = *m_Implementation;
		if (vertices.size() < 3 || vertices.size() > state.Description.MaxVertices || strip_indices.size() < 3 || strip_indices.size() > state.Description.MaxIndices) {
			throw std::runtime_error("Metal color geometry exceeds its configured range");
		}

		for (const auto index : strip_indices) {
			if (index >= vertices.size()) {
				throw std::runtime_error("Metal color index exceeds its vertex range");
			}
		}

		if (state.NextSubmission == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("Metal color submission identity exhausted");
		}

		const auto found = std::find_if(state.Slots.begin(), state.Slots.end(), Implementation::Available);
		if (found == state.Slots.end()) {
			throw std::runtime_error("Metal color submission queue is full");
		}

		auto& slot = *found;
		if (slot.Commands.status == MTLCommandBufferStatusError) {
			throw MetalOperationError("previous color pass submission", slot.Commands.error);
		}

		// Metal's attribute layout consumes explicit float fields, never a native C++ struct packing.
		auto* destination = static_cast<std::uint8_t*>(slot.Vertices.contents);
		for (std::size_t index = 0; index < vertices.size(); ++index) {
			std::memcpy(destination + index * kVertexBytes, vertices[index].Position.data(), 3 * sizeof(float));
			std::memcpy(destination + index * kVertexBytes + 3 * sizeof(float), vertices[index].Color.data(), 4 * sizeof(float));
			std::memcpy(destination + index * kVertexBytes + 7 * sizeof(float), vertices[index].TextureCoordinates.data(), 2 * sizeof(float));
		}

		std::memcpy(slot.Indices.contents, strip_indices.data(), strip_indices.size_bytes());
		std::memcpy(slot.Constants.contents, column_major_matrix.data(), column_major_matrix.size() * sizeof(float));

		@autoreleasepool {
			MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
			pass.colorAttachments[0].texture = state.Target;
			pass.colorAttachments[0].loadAction = MTLLoadActionClear;
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
			id<MTLCommandBuffer> commands = [state.Queue commandBuffer];
			id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
			if (commands == nil || encoder == nil) {
				throw std::runtime_error("Metal color command allocation failed");
			}

			[encoder setRenderPipelineState:state.Pipeline];
			[encoder setVertexBuffer:slot.Vertices offset:0 atIndex:0];
			[encoder setVertexBuffer:slot.Constants offset:0 atIndex:1];
			[encoder setCullMode:MTLCullModeNone];
			[encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip indexCount:strip_indices.size() indexType:MTLIndexTypeUInt16 indexBuffer:slot.Indices indexBufferOffset:0];
			[encoder endEncoding];
			slot.Commands = commands;
			slot.Submission = state.NextSubmission++;
			state.LatestSubmission = slot.Submission;
			[commands commit];
		}

		return slot.Submission;
	}

	ColorPassPixels ColorPassRenderer::Capture(std::uint64_t submission) {
		auto& state = *m_Implementation;
		if (submission == 0 || submission != state.LatestSubmission) {
			throw std::runtime_error("Metal color capture requires the latest submission identity");
		}

		const auto found = std::find_if(state.Slots.begin(), state.Slots.end(), [&](const auto& slot) {
			return slot.Submission == submission;
		});
		if (found == state.Slots.end()) {
			throw std::runtime_error("Metal color capture submission is unavailable");
		}

		[found->Commands waitUntilCompleted];
		if (found->Commands.status == MTLCommandBufferStatusError) {
			throw MetalOperationError("color pass submission", found->Commands.error);
		}

		@autoreleasepool {
			id<MTLCommandBuffer> copy = [state.Queue commandBuffer];
			id<MTLBlitCommandEncoder> encoder = [copy blitCommandEncoder];
			if (copy == nil || encoder == nil) {
				throw std::runtime_error("Metal color capture command allocation failed");
			}

			[encoder copyFromTexture:state.Target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(state.Description.Width, state.Description.Height, 1) toBuffer:state.Readback destinationOffset:0 destinationBytesPerRow:state.RowBytes destinationBytesPerImage:state.RowBytes * state.Description.Height];
			[encoder endEncoding];
			[copy commit];
			[copy waitUntilCompleted];
			if (copy.status == MTLCommandBufferStatusError) {
				throw MetalOperationError("color pass capture", copy.error);
			}
		}

		ColorPassPixels pixels;
		pixels.Width = state.Description.Width;
		pixels.Height = state.Description.Height;
		const auto row_bytes = static_cast<std::size_t>(pixels.Width) * 4;
		pixels.Rgba.resize(row_bytes * pixels.Height);
		const auto* source = static_cast<const std::uint8_t*>(state.Readback.contents);
		for (std::size_t row = 0; row < pixels.Height; ++row) {
			std::memcpy(pixels.Rgba.data() + row * row_bytes, source + row * state.RowBytes, row_bytes);
		}

		return pixels;
	}

	ColorPassDescription ColorPassRenderer::Description() const {
		return m_Implementation->Description;
	}

} // namespace sgre::render
