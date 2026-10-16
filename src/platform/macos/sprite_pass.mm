#include "sgre/render/sprite_pass.hpp"
#include "sgre/render/graphics_default_state.hpp"
#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/render/sprite_host_inputs.hpp"

#include "metal_support.hpp"
#include "mutable_texture_backend.hpp"
#include "sgre/scene/motion_shader_program.hpp"
#include "sprite_graphics_target_bridge.hpp"
#include "sprite_presentation_bridge.hpp"

#import <dispatch/dispatch.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace sgre::render {

	static constexpr std::size_t kSpriteVertexBytes = 20;
	static constexpr std::size_t kRawTextureVertexBytes = 16;
	static constexpr std::size_t kCapturedOpacityMapVertexBytes = 28;
	static constexpr std::size_t kDrawConstantBytes = 256;
	static constexpr std::size_t kSpriteFrameSlots = 3;
	static constexpr std::int64_t kSpriteUploadSlotWaitNanoseconds = 30'000'000'000;
	static constexpr std::uint32_t kMaximumSpriteDimension = 16384;

	static std::uint32_t TextureInterpretation(resource::PixelFormat format) {
		switch (format) {
		case resource::PixelFormat::Bc7:
			return 0;
		case resource::PixelFormat::Alpha8:
			return 1;
		case resource::PixelFormat::Luminance8:
			return 2;
		default:
			throw std::runtime_error("Metal sprite texture interpretation is unsupported");
		}
	}

	static MTLBlendFactor MetalBlendFactor(scene::SpriteBlendFactor factor) {
		switch (factor) {
		case scene::SpriteBlendFactor::Zero:
			return MTLBlendFactorZero;
		case scene::SpriteBlendFactor::One:
			return MTLBlendFactorOne;
		case scene::SpriteBlendFactor::SourceAlpha:
			return MTLBlendFactorSourceAlpha;
		case scene::SpriteBlendFactor::InverseSourceAlpha:
			return MTLBlendFactorOneMinusSourceAlpha;
		case scene::SpriteBlendFactor::DestinationColor:
			return MTLBlendFactorDestinationColor;
		case scene::SpriteBlendFactor::InverseDestinationColor:
			return MTLBlendFactorOneMinusDestinationColor;
		default:
			throw std::runtime_error("Metal sprite blend factor is unsupported");
		}
	}

	static MTLCullMode DefaultMetalCull(GraphicsRasterCull cull) {
		switch (cull) {
		case GraphicsRasterCull::None:
			return MTLCullModeNone;
		}

		throw std::runtime_error("default graphics culling state is outside the recovered native domain");
	}

	static MTLTriangleFillMode DefaultMetalFill(GraphicsRasterFill fill) {
		switch (fill) {
		case GraphicsRasterFill::Solid:
			return MTLTriangleFillModeFill;
		}

		throw std::runtime_error("default graphics triangle filling state is outside the recovered native domain");
	}

	static MTLBlendOperation MetalBlendOperation(scene::SpriteBlendOperation operation) {
		switch (operation) {
		case scene::SpriteBlendOperation::Add:
			return MTLBlendOperationAdd;
		case scene::SpriteBlendOperation::ReverseSubtract:
			return MTLBlendOperationReverseSubtract;
		default:
			throw std::runtime_error("Metal sprite blend operation is unsupported");
		}
	}

	static MTLSamplerAddressMode MetalAddress(scene::TextureAddress address) {
		switch (address) {
		case scene::TextureAddress::Clamp:
			return MTLSamplerAddressModeClampToEdge;
		case scene::TextureAddress::Repeat:
			return MTLSamplerAddressModeRepeat;
		case scene::TextureAddress::Mirror:
			return MTLSamplerAddressModeMirrorRepeat;
		default:
			throw std::runtime_error("Metal sprite sampler address is unsupported");
		}
	}

	static bool UsesCopiedDestination(std::uint32_t pixel_program) noexcept {
		return pixel_program == 1205 || pixel_program == 1129 || pixel_program == 1131 || pixel_program == 1137 || pixel_program == 1289 || pixel_program == 1281 || pixel_program == 1283;
	}

	static void ValidatePipeline(const scene::SpritePipeline& pipeline) {
		const auto texture = TextureInterpretation(pipeline.TextureFormat);
		const auto color = static_cast<std::uint32_t>(pipeline.VertexColor);
		const auto operation = static_cast<std::uint32_t>(pipeline.PixelOperation);
		if ((pipeline.NativeVertexShaderIndex != 1 && pipeline.NativeVertexShaderIndex != 2 && pipeline.NativeVertexShaderIndex != 3 && pipeline.NativeVertexShaderIndex != 4 && pipeline.NativeVertexShaderIndex != 10) || pipeline.ColorFilter != scene::SpriteColorFilter::None || color > 2 || operation > 2 || pipeline.NativeBlendStateIndex >= 12 || (pipeline.Blend.WriteMask != 0 && pipeline.Blend.WriteMask != 15)) {
			throw std::runtime_error("Metal sprite pipeline requires the recovered ordinary shader and write-mask subset");
		}

		if (pipeline.NativeVertexShaderIndex == 10 && texture != 0) {
			throw std::runtime_error("Metal ScreenProjection XYZ texture shader requires original RGBA color interpretation");
		}

		std::uint32_t specialization = 0;
		if (operation == 1) {
			specialization = 1;
		} else if (operation == 2) {
			specialization = 2;
		}
		const auto expected = (12 * (texture + 1) + 4 * color) * 15 + specialization;
		if (pipeline.NativeVertexShaderIndex == 2) {
			const auto& blend = pipeline.Blend;
			if ((pipeline.NativePixelShaderIndex != 2040 && pipeline.NativePixelShaderIndex != 2041) || blend.WriteMask != 15 || texture != 0 || color != 1 || operation != 0 || pipeline.NativeBlendStateIndex != 0 || !blend.Enabled || blend.RgbOperation != scene::SpriteBlendOperation::Add || blend.SourceRgb != scene::SpriteBlendFactor::SourceAlpha || blend.DestinationRgb != scene::SpriteBlendFactor::InverseSourceAlpha || blend.AlphaOperation != scene::SpriteBlendOperation::Add || blend.SourceAlpha != scene::SpriteBlendFactor::One || blend.DestinationAlpha != scene::SpriteBlendFactor::InverseSourceAlpha || (pipeline.NativePixelShaderIndex == 2040 && pipeline.Sampler.Minification != scene::TextureSampling::Linear)) {
				throw std::runtime_error("dual-UV source requires exact VS2/PS2040 or PS2041 and its native blend/sampler domain");
			}
		} else if (pipeline.NativeVertexShaderIndex == 3) {
			if (pipeline.NativePixelShaderIndex != 60 || color != 1 || operation != 0 || texture != 0 || pipeline.NativeBlendStateIndex != 0) {
				throw std::runtime_error("FillRect requires actual VS3/PS60 uniform-color square and native blend0");
			}
		} else if (pipeline.NativePixelShaderIndex == 1205) {
			const auto& blend = pipeline.Blend;
			const bool copied_blend = (pipeline.NativeBlendStateIndex == 10 && blend.Enabled) || (pipeline.NativeBlendStateIndex == 7 && !blend.Enabled);
			if (texture != 0 || color != 1 || operation != 0 || !copied_blend || blend.RgbOperation != scene::SpriteBlendOperation::Add || blend.SourceRgb != scene::SpriteBlendFactor::One || blend.DestinationRgb != scene::SpriteBlendFactor::Zero || blend.AlphaOperation != scene::SpriteBlendOperation::Add || blend.SourceAlpha != scene::SpriteBlendFactor::Zero || blend.DestinationAlpha != scene::SpriteBlendFactor::One) {
				throw std::runtime_error("Metal PS1205 requires copied additive RGBA/color multiplication and native blend state seven or ten");
			}
		} else if (pipeline.NativePixelShaderIndex == 1129 || pipeline.NativePixelShaderIndex == 1131) {
			const auto& blend = pipeline.Blend;
			const bool copied_blend = (pipeline.NativeBlendStateIndex == 10 && blend.Enabled) || (pipeline.NativeBlendStateIndex == 7 && !blend.Enabled);
			if (texture != 0 || color != 0 || operation != 0 || !copied_blend || blend.RgbOperation != scene::SpriteBlendOperation::Add || blend.SourceRgb != scene::SpriteBlendFactor::One || blend.DestinationRgb != scene::SpriteBlendFactor::Zero || blend.AlphaOperation != scene::SpriteBlendOperation::Add || blend.SourceAlpha != scene::SpriteBlendFactor::Zero || blend.DestinationAlpha != scene::SpriteBlendFactor::One) {
				throw std::runtime_error("Metal copied skip-color program requires RGBA, ignored vertex color and native blend state seven or ten");
			}
		} else if (pipeline.NativePixelShaderIndex == 1281 || pipeline.NativePixelShaderIndex == 1283) {
			const auto& blend = pipeline.Blend;
			const bool copied_blend = (pipeline.NativeBlendStateIndex == 10 && blend.Enabled) || (pipeline.NativeBlendStateIndex == 7 && !blend.Enabled);
			if (texture != 0 || color != 2 || operation != 0 || !copied_blend || blend.RgbOperation != scene::SpriteBlendOperation::Add || blend.SourceRgb != scene::SpriteBlendFactor::One || blend.DestinationRgb != scene::SpriteBlendFactor::Zero || blend.AlphaOperation != scene::SpriteBlendOperation::Add || blend.SourceAlpha != scene::SpriteBlendFactor::Zero || blend.DestinationAlpha != scene::SpriteBlendFactor::One) {
				throw std::runtime_error("Metal copied double-color program requires RGBA modulation and native blend state seven or ten");
			}
		} else if (pipeline.NativePixelShaderIndex == 1137 || pipeline.NativePixelShaderIndex == 1289) {
			const auto& blend = pipeline.Blend;
			const bool copied_blend = (pipeline.NativeBlendStateIndex == 10 && blend.Enabled) || (pipeline.NativeBlendStateIndex == 7 && !blend.Enabled);
			const auto expected_color = pipeline.NativePixelShaderIndex == 1137 ? 0u : 2u;
			if (texture != 0 || color != expected_color || operation != 0 || !copied_blend || blend.RgbOperation != scene::SpriteBlendOperation::Add || blend.SourceRgb != scene::SpriteBlendFactor::One || blend.DestinationRgb != scene::SpriteBlendFactor::Zero || blend.AlphaOperation != scene::SpriteBlendOperation::Add || blend.SourceAlpha != scene::SpriteBlendFactor::Zero || blend.DestinationAlpha != scene::SpriteBlendFactor::One) {
				throw std::runtime_error("Metal copied darken program requires its exact RGBA color mode and native blend state seven or ten");
			}
		} else if (pipeline.NativePixelShaderIndex != expected) {
			throw std::runtime_error("Metal sprite shader identity differs from its recovered operations");
		}

		const auto sampling = pipeline.Sampler.Minification;
		const bool common_filter = (sampling == scene::TextureSampling::Nearest || sampling == scene::TextureSampling::Linear) && pipeline.Sampler.Magnification == sampling && pipeline.Sampler.MipFilter == sampling;
		const bool picture_mixed_filter = pipeline.NativeVertexShaderIndex == 4 && sampling == scene::TextureSampling::Nearest && pipeline.Sampler.Magnification == scene::TextureSampling::Linear && pipeline.Sampler.MipFilter == scene::TextureSampling::Nearest && pipeline.Sampler.NativeFilterCode == 4;
		if ((!common_filter && !picture_mixed_filter) || pipeline.Sampler.AddressW != scene::TextureAddress::Repeat || pipeline.Sampler.MipLodBias != 0 || pipeline.Sampler.MinLod != -std::numeric_limits<float>::max() || pipeline.Sampler.MaxLod != std::numeric_limits<float>::max()) {
			throw std::runtime_error("Metal sprite sampler requires the recovered common single-mip settings");
		}
	}

	static bool FiniteCopiedUvAffine(const std::array<float, 4>& affine) {
		for (const auto value : affine) {
			if (!std::isfinite(value)) {
				return false;
			}
		}

		return true;
	}

	static auto PipelineKey(const scene::SpritePipeline& pipeline) {
		const auto& blend = pipeline.Blend;
		const auto& sampler = pipeline.Sampler;

		return std::tuple(pipeline.NativeVertexShaderIndex, pipeline.NativePixelShaderIndex, pipeline.TextureFormat, pipeline.VertexColor, pipeline.PixelOperation, blend.Enabled, blend.RgbOperation, blend.SourceRgb, blend.DestinationRgb, blend.AlphaOperation, blend.SourceAlpha, blend.DestinationAlpha, blend.WriteMask, sampler.Minification, sampler.Magnification, sampler.MipFilter, sampler.NativeFilterCode, sampler.AddressU, sampler.AddressV);
	}

	struct SpriteGpuTexture {
		id<MTLTexture> Texture = nil;
		std::uint32_t Interpretation = 0;
		std::uint32_t OriginalFormat = 0;
		__weak id<MTLTexture> MutableView = nil;
		__weak id<MTLSamplerState> MutableSampler = nil;
		bool Mutable = false;
		bool RenderTarget = false;
		std::array<std::uint32_t, 2> TargetFiltering{};
		__weak id<MTLTexture> TargetView = nil;
		std::size_t Bytes = 0;
		std::uint64_t Generation = 1;

		id<MTLTexture> SampledTexture() const {
			return Mutable ? MutableView : RenderTarget ? TargetView
			                                            : Texture;
		}
	};

	struct SpriteGpuPipeline {
		scene::SpritePipeline Configuration;
		id<MTLRenderPipelineState> State = nil;
		id<MTLSamplerState> Sampler = nil;
		std::array<id<MTLRenderPipelineState>, 6> MaskStates{};
		std::array<id<MTLRenderPipelineState>, 6> ColorMaskStates{};
		id<MTLRenderPipelineState> ResolvedMaskState = nil;
	};

	struct SpriteMutablePreparationState {
		const void* Owner = nullptr;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		id<MTLCommandBuffer> Commands = nil;
		bool MotionResources = false;
		bool RenderEncoderSuspended = false;
	};

	struct SpriteSharedGpuResources {
		std::atomic<const void*> LiveEncodingOwner = nullptr;
		std::atomic<const SpriteMutablePreparationState*> MutablePreparation = nullptr;
		id<MTLCommandBuffer> PendingFrameCommands = nil;
		std::atomic<bool> EncodingFaulted = false;
		std::vector<SpriteGpuTexture> Textures;
		std::vector<SpriteGpuPipeline> Pipelines;
		// Shader texture slots belong to the shared graphics context, including binds issued by another target.
		std::array<SpriteTextureHandle, 3> MutableBindings{};
		std::size_t TextureBytes = 0;
		std::size_t MaxTextures = 0;
		std::size_t MaxPipelines = 0;
		std::size_t MaxTextureBytes = 0;
		bool SupportsBc7 = false;
		id<MTLDevice> Device = nil;
		id<MTLCommandQueue> Queue = nil;
		id<MTLLibrary> Library = nil;
		id<MTLFunction> VertexFunction = nil;
		id<MTLFunction> CapturedOpacityMapVertexFunction = nil;
		id<MTLFunction> FillRectangleVertexFunction = nil;
		id<MTLFunction> RawTextureVertexFunction = nil;
		id<MTLFunction> ProjectionVertexFunction = nil;
		id<MTLSamplerState> MaskSampler = nil;
		bool TextureMaskEnabled = false;
	};

	static MTLVertexDescriptor* TextureMaskVertexLayout(std::uint32_t program) {
		MTLVertexDescriptor* layout = [[MTLVertexDescriptor alloc] init];
		layout.attributes[0].format = MTLVertexFormatFloat2;
		layout.attributes[0].offset = 0;
		layout.attributes[0].bufferIndex = 0;
		layout.attributes[1].offset = 8;
		layout.attributes[1].bufferIndex = 0;
		if (program == 4 || program == 10) {
			if (program == 10) {
				layout.attributes[0].format = MTLVertexFormatFloat3;
				layout.attributes[1].offset = 12;
			}
			layout.attributes[1].format = MTLVertexFormatFloat2;
			layout.layouts[0].stride = program == 10 ? kSpriteVertexBytes : kRawTextureVertexBytes;
		} else {
			layout.attributes[1].format = MTLVertexFormatUChar4Normalized_BGRA;
			layout.attributes[2].format = MTLVertexFormatFloat2;
			layout.attributes[2].offset = 12;
			layout.attributes[2].bufferIndex = 0;
			layout.layouts[0].stride = kSpriteVertexBytes;
		}

		return layout;
	}

	static void PrepareTextureMaskPipeline(SpriteSharedGpuResources& resources, SpriteGpuPipeline& record) {
		if (record.Configuration.NativeVertexShaderIndex == 2 || record.Configuration.NativeVertexShaderIndex == 3) {
			return;
		}

		const auto& base = record.Configuration;
		std::array<id<MTLRenderPipelineState>, 6> mask_states{};
		std::array<id<MTLRenderPipelineState>, 6> color_states{};
		id<MTLRenderPipelineState> resolved = nil;
		@autoreleasepool {
			for (std::uint32_t variant = 0; variant < 13; ++variant) {
				const bool resolve = variant == 12;
				if (resolve && base.NativePixelShaderIndex == 1205) {
					continue;
				}
				const auto index = variant % 6;
				const std::uint32_t phase = resolve ? 6 : 3 + index % 3;
				const std::uint32_t kind = variant >= 6 && !resolve ? 0 : TextureInterpretation(base.TextureFormat) + 1;
				const bool additional = !resolve && index >= 3;
				MTLFunctionConstantValues* values = [[MTLFunctionConstantValues alloc] init];
				[values setConstantValue:&kind type:MTLDataTypeUInt atIndex:3];
				[values setConstantValue:&additional type:MTLDataTypeBool atIndex:4];
				[values setConstantValue:&phase type:MTLDataTypeUInt atIndex:5];
				const auto color_operation = static_cast<std::uint32_t>(base.VertexColor);
				const auto pixel_operation = static_cast<std::uint32_t>(base.PixelOperation);
				[values setConstantValue:&color_operation type:MTLDataTypeUInt atIndex:6];
				[values setConstantValue:&pixel_operation type:MTLDataTypeUInt atIndex:7];
				NSError* error = nil;
				id<MTLFunction> fragment = nil;
				if (resolve && base.NativePixelShaderIndex == 1129) {
					fragment = [resources.Library newFunctionWithName:@"SampleCopiedAdditiveMaskedSkipColorFragment"];
				} else if (resolve && base.NativePixelShaderIndex == 1131) {
					fragment = [resources.Library newFunctionWithName:@"SampleCopiedOverlayMaskedSkipColorFragment"];
				} else if (resolve && base.NativePixelShaderIndex == 1137) {
					fragment = [resources.Library newFunctionWithName:@"SampleCopiedDarkenMaskedSkipColorFragment"];
				} else if (resolve && base.NativePixelShaderIndex == 1289) {
					fragment = [resources.Library newFunctionWithName:@"SampleCopiedDarkenMaskedDoubleColorFragment"];
				} else if (resolve && base.NativePixelShaderIndex == 1281) {
					fragment = [resources.Library newFunctionWithName:@"SampleCopiedAdditiveMaskedDoubleColorFragment"];
				} else if (resolve && base.NativePixelShaderIndex == 1283) {
					fragment = [resources.Library newFunctionWithName:@"SampleCopiedOverlayMaskedDoubleColorFragment"];
				} else {
					NSString* name = resolve ? @"SampleMotionResolvedMaskFragment" : @"SampleMotionTextureMaskRedFragment";
					fragment = [resources.Library newFunctionWithName:name constantValues:values error:&error];
				}
				if (fragment == nil) {
					throw MetalOperationError("specialize native texture-mask shader", error);
				}

				MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
				descriptor.vertexFunction = base.NativeVertexShaderIndex == 10 ? resources.ProjectionVertexFunction : base.NativeVertexShaderIndex == 4 ? resources.RawTextureVertexFunction
				                                                                                                                                        : resources.VertexFunction;
				descriptor.vertexDescriptor = TextureMaskVertexLayout(base.NativeVertexShaderIndex);
				descriptor.fragmentFunction = fragment;
				MTLRenderPipelineColorAttachmentDescriptor* color = descriptor.colorAttachments[0];
				color.pixelFormat = resolve ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatR8Unorm;
				color.blendingEnabled = resolve ? base.Blend.Enabled : YES;
				color.rgbBlendOperation = resolve ? MetalBlendOperation(base.Blend.RgbOperation) : MTLBlendOperationAdd;
				color.alphaBlendOperation = resolve ? MetalBlendOperation(base.Blend.AlphaOperation) : MTLBlendOperationAdd;
				// Native A8 alpha is represented by red; preserve its ONE/(1-sourceAlpha) or ZERO/(1-sourceAlpha) factors.
				color.sourceRGBBlendFactor = resolve ? MetalBlendFactor(base.Blend.SourceRgb) : (phase == 5 ? MTLBlendFactorZero : MTLBlendFactorOne);
				color.destinationRGBBlendFactor = resolve ? MetalBlendFactor(base.Blend.DestinationRgb) : MTLBlendFactorOneMinusSourceAlpha;
				color.sourceAlphaBlendFactor = resolve ? MetalBlendFactor(base.Blend.SourceAlpha) : (phase == 5 ? MTLBlendFactorZero : MTLBlendFactorOne);
				color.destinationAlphaBlendFactor = resolve ? MetalBlendFactor(base.Blend.DestinationAlpha) : MTLBlendFactorOneMinusSourceAlpha;
				color.writeMask = base.Blend.WriteMask == 0 ? MTLColorWriteMaskNone : (resolve ? MTLColorWriteMaskAll : MTLColorWriteMaskRed);
				id<MTLRenderPipelineState> pipeline = [resources.Device newRenderPipelineStateWithDescriptor:descriptor error:&error];
				if (pipeline == nil) {
					throw MetalOperationError("prepare native texture-mask pipeline", error);
				}
				if (resolve) {
					resolved = pipeline;
				} else if (variant < 6) {
					mask_states[index] = pipeline;
				} else {
					color_states[index] = pipeline;
				}
			}
		}
		// An unsuccessful specialization must leave the actual record eligible for a complete startup retry.
		record.MaskStates = std::move(mask_states);
		record.ColorMaskStates = std::move(color_states);
		record.ResolvedMaskState = resolved;
	}

	struct SpriteFrameSlot {
		id<MTLBuffer> Vertices = nil;
		id<MTLBuffer> Indices = nil;
		id<MTLBuffer> Constants = nil;
		id<MTLCommandBuffer> Commands = nil;
		dispatch_semaphore_t Completion = nil;
		bool CompletionPending = false;
		std::uint64_t Submission = 0;
	};

	static std::size_t RecommendedTextureBudget(id<MTLDevice> device) {
		const auto recommendation = device.recommendedMaxWorkingSetSize;
		if (recommendation == 0 || !std::in_range<std::size_t>(recommendation)) {
			throw std::runtime_error("Metal device recommendation cannot provide a positive representable texture budget");
		}

		// This is an application ceiling, not an allocation guarantee or a measurement of the registry's logical charges.
		return static_cast<std::size_t>(recommendation);
	}

	class SpritePassRenderer::Implementation {
	public:
		struct MutableLifetimeRecord {
			Implementation* Receiver = nullptr;
		};
		Implementation(SpritePassDescription description, const std::filesystem::path& shader_source, std::shared_ptr<SpriteSharedGpuResources> shared = {}) : Description(description), Resources(shared ? std::move(shared) : std::make_shared<SpriteSharedGpuResources>()), TextureBytes(Resources->TextureBytes), Textures(Resources->Textures), Pipelines(Resources->Pipelines) {
			if (description.Width == 0 || description.Height == 0 || description.Width > kMaximumSpriteDimension || description.Height > kMaximumSpriteDimension || description.MaxVertices < 3 || description.MaxVertices > 1024 * 1024 || description.MaxIndices < 3 || description.MaxIndices > 1024 * 1024 || description.MaxDraws == 0 || description.MaxDraws > 65536 || description.MaxTextures == 0 || description.MaxTextures > 65536 || description.MaxPipelines == 0 || description.MaxPipelines > 65536) {
				throw std::runtime_error("Metal sprite renderer requires bounded nonempty dimensions and resource limits");
			}

			RowBytes = ((static_cast<std::size_t>(description.Width) * 4 + 255) / 256) * 256;
			if (description.Height > description.MaxTargetBytes / RowBytes) {
				throw std::runtime_error("Metal sprite target/readback exceeds its allocation limit");
			}

			MutableLifetime = std::make_shared<Implementation::MutableLifetimeRecord>();
			MutableLifetime->Receiver = this;
			@autoreleasepool {
				if (Resources->Device == nil) {
					Resources->Device = FindMetalDevice();
					Resources->SupportsBc7 = Resources->Device.supportsBCTextureCompression;
					if (Description.MaxTextureBytes == 0) {
						Description.MaxTextureBytes = RecommendedTextureBudget(Resources->Device);
					}
					Resources->MaxTextures = description.MaxTextures;
					Resources->MaxPipelines = description.MaxPipelines;
					Resources->MaxTextureBytes = Description.MaxTextureBytes;
					Textures.reserve(description.MaxTextures);
					Pipelines.reserve(description.MaxPipelines);
					Resources->Queue = [Resources->Device newCommandQueue];
					Resources->Library = CompileMetalLibrary(Resources->Device, shader_source);
					Resources->VertexFunction = [Resources->Library newFunctionWithName:@"TransformSpriteVertex"];
					Resources->CapturedOpacityMapVertexFunction = [Resources->Library newFunctionWithName:@"TransformCapturedOpacityMapVertex"];
					Resources->FillRectangleVertexFunction = [Resources->Library newFunctionWithName:@"TransformFillRectangleVertex"];
					Resources->RawTextureVertexFunction = [Resources->Library newFunctionWithName:@"TransformRawTextureVertex"];
					Resources->ProjectionVertexFunction = [Resources->Library newFunctionWithName:@"TransformScreenProjectionVertex"];
				} else {
					if (Description.MaxTextureBytes == 0) {
						Description.MaxTextureBytes = Resources->MaxTextureBytes;
					}
					if (description.MaxTextures != Resources->MaxTextures || description.MaxPipelines != Resources->MaxPipelines || Description.MaxTextureBytes != Resources->MaxTextureBytes) {
						throw std::runtime_error("shared Metal targets require identical global texture and pipeline limits");
					}
				}
				Device = Resources->Device;
				Queue = Resources->Queue;
				Library = Resources->Library;
				VertexFunction = Resources->VertexFunction;
				CapturedOpacityMapVertexFunction = Resources->CapturedOpacityMapVertexFunction;
				FillRectangleVertexFunction = Resources->FillRectangleVertexFunction;
				RawTextureVertexFunction = Resources->RawTextureVertexFunction;
				ProjectionVertexFunction = Resources->ProjectionVertexFunction;
				if (Queue == nil || VertexFunction == nil) {
					throw std::runtime_error("Metal sprite queue or vertex shader is unavailable");
				}

				MTLTextureDescriptor* target = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:description.Width height:description.Height mipmapped:NO];
				target.storageMode = MTLStorageModePrivate;
				target.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
				Target = [Device newTextureWithDescriptor:target];
				Readback = [Device newBufferWithLength:RowBytes * description.Height options:MTLResourceStorageModeShared];
				if (Target == nil || Readback == nil) {
					throw std::runtime_error("Metal sprite target/readback allocation failed");
				}

				for (auto& slot : Slots) {
					slot.Vertices = [Device newBufferWithLength:description.MaxVertices * kCapturedOpacityMapVertexBytes options:MTLResourceStorageModeShared];
					slot.Indices = [Device newBufferWithLength:description.MaxIndices * sizeof(std::uint32_t) options:MTLResourceStorageModeShared];
					slot.Constants = [Device newBufferWithLength:description.MaxDraws * kDrawConstantBytes options:MTLResourceStorageModeShared];
					slot.Completion = dispatch_semaphore_create(0);
					if (slot.Vertices == nil || slot.Indices == nil || slot.Constants == nil || slot.Completion == nil) {
						throw std::runtime_error("Metal sprite upload allocation failed");
					}
				}
			}
		}

		~Implementation() {
			for (const auto& slot : Slots) {
				if (slot.Commands != nil && slot.Commands.status >= MTLCommandBufferStatusCommitted) {
					[slot.Commands waitUntilCompleted];
				}
			}
			if (TargetSample) {
				auto& record = Textures[TargetSample->Index];
				record.TargetView = nil;
				record.RenderTarget = false;
				if (record.Generation != std::numeric_limits<std::uint64_t>::max()) {
					++record.Generation;
				}
			}
			MutableLifetime->Receiver = nullptr;
		}

		std::shared_ptr<MutableLifetimeRecord> MutableLifetime;
		std::optional<SpriteTextureHandle> TargetSample;
		bool TargetInitialized = false;
		id<MTLCommandBuffer> LastTargetCommands = nil;
		std::weak_ptr<MutableLifetimeRecord> LastTargetProducer;
		std::uint64_t LastTargetSubmission = 0;
		void RequireIdleEncoding() const {
			if (Resources->LiveEncodingOwner.load() != nullptr || Resources->EncodingFaulted.load()) {
				throw std::runtime_error("Metal sprite operation requires quiescent successful live encoding");
			}
		}

		bool IsMutableBindingPreparation() const {
			const auto* preparation = Resources->MutablePreparation.load();
			return preparation != nullptr && preparation->OwnerThread == std::this_thread::get_id() && preparation->Owner == Resources->LiveEncodingOwner.load() && preparation->Commands != nil && preparation->Commands == Resources->PendingFrameCommands && !Resources->EncodingFaulted.load();
		}

		bool IsMotionResourcePreparation() const {
			const auto* preparation = Resources->MutablePreparation.load();
			return IsMutableBindingPreparation() && preparation->MotionResources && preparation->RenderEncoderSuspended;
		}

		void RequireMotionResourcePreparationOrIdle() const {
			if (!IsMotionResourcePreparation()) {
				RequireIdleEncoding();
			}
		}

		void RequireMutableBindingEncoding() const {
			if (!IsMutableBindingPreparation()) {
				RequireIdleEncoding();
			}
		}

		SpritePassDescription Description;
		std::size_t RowBytes = 0;
		std::shared_ptr<SpriteSharedGpuResources> Resources;
		std::size_t& TextureBytes;
		std::uint64_t NextSubmission = 1;
		std::uint64_t LatestSubmission = 0;
		const std::thread::id SlotOwnerThread = std::this_thread::get_id();
		std::vector<SpriteGpuTexture>& Textures;
		std::vector<SpriteGpuPipeline>& Pipelines;
		std::array<SpriteFrameSlot, kSpriteFrameSlots> Slots;
		id<MTLDevice> Device = nil;
		id<MTLCommandQueue> Queue = nil;
		id<MTLLibrary> Library = nil;
		id<MTLFunction> VertexFunction = nil;
		id<MTLFunction> CapturedOpacityMapVertexFunction = nil;
		id<MTLFunction> FillRectangleVertexFunction = nil;
		id<MTLFunction> RawTextureVertexFunction = nil;
		id<MTLFunction> ProjectionVertexFunction = nil;
		id<MTLTexture> Target = nil;
		id<MTLBuffer> Readback = nil;

		const SpriteGpuTexture& Texture(SpriteTextureHandle handle) const {
			if (handle.Owner != Resources.get() || handle.Index >= Textures.size() || Textures[handle.Index].SampledTexture() == nil || handle.Generation != Textures[handle.Index].Generation) {
				throw std::runtime_error("Metal sprite texture handle is stale or belongs to another renderer");
			}

			return Textures[handle.Index];
		}

		SpriteGpuTexture& MutableTexture(SpriteTextureHandle handle) {
			if (handle.Owner != Resources.get() || handle.Index >= Textures.size() || !Textures[handle.Index].Mutable || handle.Generation != Textures[handle.Index].Generation) {
				throw std::runtime_error("Metal mutable texture handle is stale or belongs to another renderer");
			}

			return Textures[handle.Index];
		}

		const SpriteGpuPipeline& Pipeline(SpritePipelineHandle handle) const {
			if (handle.Owner != Resources.get() || handle.Index >= Pipelines.size()) {
				throw std::runtime_error("Metal sprite pipeline handle belongs to another renderer or exceeds its range");
			}

			return Pipelines[handle.Index];
		}

		static bool Available(const SpriteFrameSlot& slot) {
			return slot.Commands == nil || slot.Commands.status == MTLCommandBufferStatusCompleted || slot.Commands.status == MTLCommandBufferStatusError;
		}

		void FinishSlotCompletion(SpriteFrameSlot& slot, std::string_view operation) const {
			if (slot.Commands == nil) {
				if (slot.CompletionPending) {
					throw std::runtime_error("Metal upload slot lost its actual pending command buffer");
				}
				return;
			}
			if (slot.Commands.status == MTLCommandBufferStatusError) {
				throw MetalOperationError(operation, slot.Commands.error);
			}
			if (slot.CompletionPending) {
				const auto commands = slot.Commands;
				const auto submission = slot.Submission;
				const auto waited = dispatch_semaphore_wait(slot.Completion, dispatch_time(DISPATCH_TIME_NOW, kSpriteUploadSlotWaitNanoseconds));
				if (waited != 0) {
					if (commands.status == MTLCommandBufferStatusError) {
						throw MetalOperationError(operation, commands.error);
					}
					throw std::runtime_error("Metal upload slot completion deadline exceeded; submission=" + std::to_string(submission) + " status=" + std::to_string(static_cast<std::uint32_t>(commands.status)));
				}
				// Exactly one real completion signal is consumed before this fixed slot can be reused.
				slot.CompletionPending = false;
				if (slot.Commands != commands || slot.Submission != submission) {
					throw std::runtime_error("Metal upload slot changed its retained command/submission while awaiting completion");
				}
			}
			if (slot.Commands.status == MTLCommandBufferStatusError) {
				throw MetalOperationError(operation, slot.Commands.error);
			}
			if (slot.Commands.status != MTLCommandBufferStatusCompleted) {
				throw std::runtime_error("Metal upload slot completion did not retire its actual submitted command buffer");
			}
		}

		SpriteFrameSlot& AcquireUploadSlot(std::string_view operation) {
			if (std::this_thread::get_id() != SlotOwnerThread) {
				throw std::runtime_error("Metal upload slots require their actual serialized renderer owner thread");
			}
			RequireIdleEncoding();
			for (const auto& slot : Slots) {
				if (slot.Commands.status == MTLCommandBufferStatusError) {
					throw MetalOperationError(operation, slot.Commands.error);
				}
			}
			auto found = std::find_if(Slots.begin(), Slots.end(), Available);
			if (found == Slots.end()) {
				found = std::min_element(Slots.begin(), Slots.end(), [](const auto& left, const auto& right) {
					return left.Submission < right.Submission;
				});
				if (found->Commands == nil || found->Submission == 0 || !found->CompletionPending) {
					throw std::runtime_error("Metal upload backpressure requires the oldest actual registered submitted frame");
				}
			}
			FinishSlotCompletion(*found, operation);

			return *found;
		}

		void ArmSlotCompletion(SpriteFrameSlot& slot, id<MTLCommandBuffer> commands) const {
			if (std::this_thread::get_id() != SlotOwnerThread || commands == nil || slot.Completion == nil || slot.CompletionPending) {
				throw std::runtime_error("Metal upload completion requires a fresh command and its consumed fixed-slot signal");
			}
			// The block retains only this OS semaphore, so no callback borrows a retired C++ renderer or slot.
			dispatch_semaphore_t completion = slot.Completion;
			[commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
			  static_cast<void>(completed);
			  dispatch_semaphore_signal(completion);
			}];
			slot.CompletionPending = true;
		}
	};

	MetalSpritePresentationSource SpritePassPresentationBridge::CreateSource(SpritePassRenderer& renderer) {
		if (![NSThread isMainThread]) {
			throw std::runtime_error("Metal sprite presentation bridge requires the main thread");
		}

		auto& state = *renderer.m_Implementation;
		if (state.Target == nil || (state.Target.usage & MTLTextureUsageShaderRead) == 0 || state.Target.hazardTrackingMode != MTLHazardTrackingModeTracked) {
			throw std::runtime_error("Metal sprite presentation requires an actual shader-readable hazard-tracked target");
		}

		MetalSpritePresentationSource source;
		source.Device = state.Device;
		source.Queue = state.Queue;
		source.Width = state.Description.Width;
		source.Height = state.Description.Height;
		const std::weak_ptr<SpritePassRenderer::Implementation::MutableLifetimeRecord> lifetime = state.MutableLifetime;
		const std::weak_ptr<SpriteSharedGpuResources> resources = state.Resources;
		const void* receiver_identity = &state;
		source.RequireAlive = [lifetime, receiver_identity](const MetalSpritePresentationSource& description) {
			if (![NSThread isMainThread]) {
				throw std::runtime_error("Metal presentation source metadata requires the main thread");
			}
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr || live->Receiver != receiver_identity) {
				throw std::runtime_error("Metal presentation renderer has closed or changed identity");
			}
			const auto& receiver = *live->Receiver;
			receiver.RequireIdleEncoding();
			if (receiver.Device != description.Device || receiver.Queue != description.Queue || receiver.Queue.device != receiver.Device || receiver.Description.Width != description.Width || receiver.Description.Height != description.Height || receiver.Target == nil || receiver.Target.device != receiver.Device || receiver.Target.width != description.Width || receiver.Target.height != description.Height) {
				throw std::runtime_error("Metal presentation source device, queue or actual target description changed");
			}
		};
		source.FaultCommit = [resources] {
			if (const auto live = resources.lock()) {
				live->EncodingFaulted = true;
			}
		};
		source.CurrentSubmission = [lifetime] {
			if (![NSThread isMainThread]) {
				throw std::runtime_error("Metal current presentation submission requires the main thread");
			}
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Metal current presentation renderer has closed");
			}
			const auto& receiver = *live->Receiver;
			receiver.RequireIdleEncoding();
			if (!receiver.TargetInitialized || receiver.LatestSubmission == 0) {
				throw std::runtime_error("Metal current presentation requires an actual initialized Submit");
			}

			return receiver.LatestSubmission;
		};
		source.RequireLatest = [lifetime](std::uint64_t submission) {
			if (![NSThread isMainThread]) {
				throw std::runtime_error("Metal sprite presentation frame access requires the main thread");
			}

			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Metal sprite presentation renderer has closed");
			}

			auto& receiver = *live->Receiver;
			receiver.RequireIdleEncoding();
			if (submission == 0 || submission != receiver.LatestSubmission || !receiver.TargetInitialized) {
				throw std::runtime_error("Metal sprite presentation requires the exact latest initialized submission");
			}
			const auto found = std::find_if(receiver.Slots.begin(), receiver.Slots.end(), [&](const auto& slot) {
				return slot.Submission == submission;
			});
			if (found == receiver.Slots.end() || found->Commands == nil) {
				throw std::runtime_error("Metal sprite presentation submission is unavailable");
			}
			if (found->Commands.status == MTLCommandBufferStatusError) {
				throw MetalOperationError("Metal sprite presentation source submission", found->Commands.error);
			}

			MetalSpritePresentationFrame frame;
			frame.Target = receiver.Target;
			frame.Producer = found->Commands;

			return frame;
		};

		return source;
	}

	void SpriteGraphicsTargetBridge::RequireIdleEncoding(const SpritePassRenderer& renderer) {
		renderer.m_Implementation->RequireIdleEncoding();
	}

	void SpriteGraphicsTargetBridge::ClearShaderTextures(SpritePassRenderer& renderer) {
		renderer.m_Implementation->RequireIdleEncoding();
		renderer.m_Implementation->Resources->MutableBindings = {};
	}

	SpritePassRenderer::SpritePassRenderer(SpritePassDescription description, const std::filesystem::path& shader_source) : m_Implementation(std::make_unique<Implementation>(description, shader_source)) {
	}

	SpritePassRenderer::SpritePassRenderer(SpritePassDescription description, const SpritePassRenderer& resource_owner) : m_Implementation([&] {
		                                                                                                                      resource_owner.m_Implementation->RequireIdleEncoding();

		                                                                                                                      return std::make_unique<Implementation>(description, std::filesystem::path{}, resource_owner.m_Implementation->Resources);
	                                                                                                                      }()) {
	}

	SpritePassRenderer::~SpritePassRenderer() = default;

	SpriteTextureHandle SpritePassRenderer::UploadTexture(resource::PixelFormat original_format, const resource::RgbaImage& image) {
		auto& state = *m_Implementation;
		state.RequireIdleEncoding();
		static_cast<void>(TextureInterpretation(original_format));
		if (image.Width == 0 || image.Height == 0 || image.Width > kMaximumSpriteDimension || image.Height > kMaximumSpriteDimension) {
			throw std::runtime_error("Metal sprite texture requires bounded positive dimensions");
		}

		const auto row_bytes = static_cast<std::size_t>(image.Width) * 4;
		if (image.Height > state.Description.MaxTextureBytes / row_bytes || image.Pixels.size() != row_bytes * image.Height || image.Pixels.size() > state.Description.MaxTextureBytes - state.TextureBytes) {
			throw std::runtime_error("Metal sprite texture pixels or aggregate allocation exceed their checked range");
		}

		const auto free = std::find_if(state.Textures.begin(), state.Textures.end(), [](const auto& texture) {
			return texture.Texture == nil && !texture.Mutable && !texture.RenderTarget && texture.Generation != std::numeric_limits<std::uint64_t>::max();
		});
		const auto index = static_cast<std::size_t>(free - state.Textures.begin());
		if (index == state.Textures.size() && index >= state.Description.MaxTextures) {
			throw std::runtime_error("Metal sprite texture count exceeds its allocation limit");
		}

		@autoreleasepool {
			MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:image.Width height:image.Height mipmapped:NO];
			descriptor.storageMode = MTLStorageModeShared;
			descriptor.usage = MTLTextureUsageShaderRead;
			id<MTLTexture> texture = [state.Device newTextureWithDescriptor:descriptor];
			if (texture == nil) {
				throw std::runtime_error("Metal sprite texture allocation failed");
			}

			[texture replaceRegion:MTLRegionMake2D(0, 0, image.Width, image.Height) mipmapLevel:0 withBytes:image.Pixels.data() bytesPerRow:row_bytes];
			if (index == state.Textures.size()) {
				state.Textures.emplace_back();
			}

			auto& record = state.Textures[index];
			record.Texture = texture;
			record.Interpretation = TextureInterpretation(original_format);
			record.Bytes = image.Pixels.size();
			state.TextureBytes += record.Bytes;
		}

		SpriteTextureHandle handle;
		handle.Index = static_cast<std::uint32_t>(index);
		handle.Generation = state.Textures[index].Generation;
		handle.Owner = state.Resources.get();

		return handle;
	}

	bool SpritePassRenderer::SupportsBc7Textures() const {
		return m_Implementation->Resources->SupportsBc7;
	}

	SpriteTextureHandle SpritePassRenderer::UploadBc7Texture(const resource::Bc7TextureView& image) {
		auto& state = *m_Implementation;
		state.RequireIdleEncoding();
		if (!state.Resources->SupportsBc7) {
			throw std::runtime_error("selected Metal device does not support immutable BC7 textures");
		}

		resource::ImageTexture description;
		description.Format = resource::PixelFormat::Bc7;
		description.Width = image.Width;
		description.Height = image.Height;
		description.PixelRange.Size = image.Blocks.size();
		const auto checked = resource::BorrowBc7Texture(description, image.Blocks, std::numeric_limits<std::size_t>::max());
		if (image.BlockRowBytes != checked.BlockRowBytes || image.Blocks.size() > state.Description.MaxTextureBytes - state.TextureBytes) {
			throw std::runtime_error("Metal BC7 block pitch or aggregate allocation exceeds its checked range");
		}
		const auto free = std::find_if(state.Textures.begin(), state.Textures.end(), [](const auto& texture) {
			return texture.Texture == nil && !texture.Mutable && !texture.RenderTarget && texture.Generation != std::numeric_limits<std::uint64_t>::max();
		});
		const auto index = static_cast<std::size_t>(free - state.Textures.begin());
		if (index == state.Textures.size() && index >= state.Description.MaxTextures) {
			throw std::runtime_error("Metal BC7 texture count exceeds its allocation limit");
		}

		@autoreleasepool {
			MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBC7_RGBAUnorm width:image.Width height:image.Height mipmapped:NO];
			descriptor.storageMode = MTLStorageModeShared;
			descriptor.usage = MTLTextureUsageShaderRead;
			id<MTLTexture> texture = [state.Device newTextureWithDescriptor:descriptor];
			if (texture == nil) {
				throw std::runtime_error("Metal immutable BC7 texture allocation failed");
			}
			// Metal's source stride is in block rows; the complete final edge block remains in the input span.
			[texture replaceRegion:MTLRegionMake2D(0, 0, image.Width, image.Height) mipmapLevel:0 withBytes:image.Blocks.data() bytesPerRow:image.BlockRowBytes];
			if (index == state.Textures.size()) {
				state.Textures.emplace_back();
			}
			auto& record = state.Textures[index];
			record.Texture = texture;
			record.Interpretation = TextureInterpretation(resource::PixelFormat::Bc7);
			record.Bytes = image.Blocks.size();
			state.TextureBytes += record.Bytes;
		}

		return SpriteTextureHandle{static_cast<std::uint32_t>(index), state.Textures[index].Generation, state.Resources.get()};
	}

	SpriteTextureHandle SpritePassRenderer::SampledTarget() {
		auto& state = *m_Implementation;
		state.RequireIdleEncoding();
		if (state.TargetSample) {
			static_cast<void>(state.Texture(*state.TargetSample));

			return *state.TargetSample;
		}
		const auto free = std::find_if(state.Textures.begin(), state.Textures.end(), [](const auto& texture) {
			return texture.Texture == nil && !texture.Mutable && !texture.RenderTarget && texture.Generation != std::numeric_limits<std::uint64_t>::max();
		});
		const auto index = static_cast<std::size_t>(free - state.Textures.begin());
		if (index == state.Textures.size()) {
			if (index == state.Description.MaxTextures) {
				throw std::runtime_error("Screen target sampled-view registry is full");
			}
			state.Textures.emplace_back();
		}
		auto& record = state.Textures[index];
		record.TargetView = state.Target;
		record.RenderTarget = true;
		record.Interpretation = 0;
		record.OriginalFormat = 28;
		record.Bytes = 0;
		state.TargetSample = SpriteTextureHandle{static_cast<std::uint32_t>(index), record.Generation, state.Resources.get()};

		return *state.TargetSample;
	}

	std::array<std::uint32_t, 2> SpritePassRenderer::SampledTargetFiltering(SpriteTextureHandle handle) const {
		const auto& state = *m_Implementation;
		const auto& texture = state.Texture(handle);
		if (!texture.RenderTarget) {
			throw std::runtime_error("sampled Screen filtering requires its actual registered target texture");
		}

		return texture.TargetFiltering;
	}

	void SpritePassRenderer::SetSampledTargetFiltering(SpriteTextureHandle handle, std::uint32_t minimum, std::uint32_t maximum) {
		auto& state = *m_Implementation;
		static_cast<void>(state.Texture(handle));
		if (!state.Textures[handle.Index].RenderTarget || minimum > 1 || maximum > 1 || state.Resources->EncodingFaulted.load()) {
			throw std::runtime_error("sampled Screen filtering requires a live target and recovered native filter pair");
		}
		// Native4f8dc0 changes desired source words; no upload, encoder operation or resource replacement occurs here.
		state.Textures[handle.Index].TargetFiltering = {minimum, maximum};
	}

	void SpritePassRenderer::ReleaseTexture(SpriteTextureHandle handle) {
		auto& state = *m_Implementation;
		state.RequireIdleEncoding();
		static_cast<void>(state.Texture(handle));
		auto& texture = state.Textures[handle.Index];
		if (texture.Mutable || texture.RenderTarget) {
			throw std::runtime_error("Metal mutable/target texture must be released through its actual owner");
		}
		if (texture.Generation == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("Metal sprite texture generation space exhausted");
		}

		// Submitted command buffers retain their Metal resources after this registry releases its reference.
		state.TextureBytes -= texture.Bytes;
		texture.Texture = nil;
		texture.Bytes = 0;
		++texture.Generation;
	}

	static std::size_t MutableStageIndex(scene::MutableTextureStage stage) {
		if (stage == scene::MutableTextureStage::Vertex) {
			return 0;
		}
		if (stage == scene::MutableTextureStage::Pixel) {
			return 1;
		}

		throw std::runtime_error("Metal mutable binding shader stage is invalid");
	}
	std::unique_ptr<scene::MutableTextureBackend> SpritePassRenderer::CreateMutableTextureBackend() {
		// The existing prepared component API copies this renderer's own physical target.
		return SpriteGraphicsTargetBridge::CreateMutableTextureBackend(*this, {}, {});
	}

	std::unique_ptr<scene::MutableTextureBackend> SpriteGraphicsTargetBridge::CreateMutableTextureBackend(SpritePassRenderer& allocation_owner, std::function<std::shared_ptr<SpritePassRenderer>()> require_current_target, std::function<bool()> owner_available) {
		auto& state = *allocation_owner.m_Implementation;
		const std::weak_ptr<SpritePassRenderer::Implementation::MutableLifetimeRecord> lifetime = state.MutableLifetime;
		const std::weak_ptr<SpriteSharedGpuResources> resource_domain = state.Resources;
		SpriteMutableTextureContext context;
		context.ResourceDomain = state.Resources.get();
		// Typed active allocation never invokes an application target or availability callback.
		context.DirectMotionPreparation = !require_current_target && !owner_available;
		context.Device = state.Device;
		context.Queue = state.Queue;
		context.MaximumBytes = state.Description.MaxTextureBytes;
		context.Available = [lifetime, owner_available]() {
			const auto live = lifetime.lock();
			return live && live->Receiver != nullptr && !live->Receiver->Resources->EncodingFaulted.load() && (live->Receiver->Resources->LiveEncodingOwner.load() == nullptr || live->Receiver->IsMotionResourcePreparation()) && (!owner_available || owner_available());
		};
		context.BindingAvailable = [lifetime, owner_available]() {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Metal mutable texture renderer has closed");
			}
			if (live->Receiver->Resources->EncodingFaulted.load() || (owner_available && !owner_available())) {
				return false;
			}
			return live->Receiver->Resources->LiveEncodingOwner.load() == nullptr || live->Receiver->IsMutableBindingPreparation();
		};
		context.IsPendingFrameUpload = [resource_domain](id<MTLCommandBuffer> commands) {
			const auto resources = resource_domain.lock();
			return resources && commands != nil && resources->PendingFrameCommands == commands;
		};
		context.EncodeUpload = [lifetime](id<MTLBuffer> bytes, std::size_t pitch, id<MTLTexture> texture, std::uint32_t width, std::uint32_t height) -> id<MTLCommandBuffer> {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Metal mutable upload renderer has closed");
			}
			auto& receiver = *live->Receiver;
			if (receiver.IsMotionResourcePreparation()) {
				throw std::runtime_error("motion resource preparation cannot upload or submit pixel data");
			}
			receiver.RequireMutableBindingEncoding();
			const bool inside_frame = receiver.IsMutableBindingPreparation();
			@autoreleasepool {
				id<MTLCommandBuffer> commands = inside_frame ? receiver.Resources->PendingFrameCommands : [receiver.Queue commandBuffer];
				if (commands == nil || !commands.retainedReferences || bytes == nil || texture == nil || bytes.device != receiver.Device || texture.device != receiver.Device) {
					throw std::runtime_error("Metal mutable upload requires its actual retained command buffer and device resources");
				}
				id<MTLBlitCommandEncoder> encoder = [commands blitCommandEncoder];
				if (encoder == nil) {
					throw std::runtime_error("Metal mutable upload cannot allocate its actual blit encoder");
				}
				@try {
					[encoder copyFromBuffer:bytes sourceOffset:0 sourceBytesPerRow:pitch sourceBytesPerImage:pitch * height sourceSize:MTLSizeMake(width, height, 1) toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
					[encoder endEncoding];
					encoder = nil;
					if (!inside_frame) {
						[commands commit];
					}
					return commands;
				} @catch (NSException* exception) {
					@try {
						if (encoder != nil) {
							[encoder endEncoding];
						}
					} @catch (NSException*) {
					}
					@
					throw exception;
				}
			}
		};
		context.EncodeTextureCopy = [lifetime](id<MTLTexture> destination, id<MTLTexture> source, std::uint32_t width, std::uint32_t height) -> id<MTLCommandBuffer> {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Capture companion copy renderer has closed");
			}
			auto& receiver = *live->Receiver;
			receiver.RequireMutableBindingEncoding();
			if (destination == nil || source == nil || destination == source || destination.device != receiver.Device || source.device != receiver.Device || destination.pixelFormat != source.pixelFormat || width == 0 || height == 0 || width > destination.width || width > source.width || height > destination.height || height > source.height) {
				throw std::runtime_error("Capture companion copy requires actual same-domain matching physical textures");
			}
			@autoreleasepool {
				const auto preparation = receiver.Resources->MutablePreparation.load();
				id<MTLCommandBuffer> commands = preparation ? preparation->Commands : [receiver.Queue commandBuffer];
				id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
				if (commands == nil || blit == nil) {
					throw std::runtime_error("Capture companion copy cannot allocate its actual blit encoder");
				}
				[blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1) toTexture:destination destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
				[blit endEncoding];
				if (!preparation) {
					[commands commit];
				}

				return commands;
			}
		};

		context.Register = [lifetime](std::size_t bytes, std::uint32_t original_format) {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Metal mutable texture renderer has closed");
			}

			auto& receiver = *live->Receiver;
			receiver.RequireMotionResourcePreparationOrIdle();
			if (bytes > receiver.Description.MaxTextureBytes - receiver.TextureBytes) {
				throw std::runtime_error("Metal mutable aggregate allocation exceeds the checked range");
			}
			const auto free = std::find_if(receiver.Textures.begin(), receiver.Textures.end(), [](const auto& texture) {
				return texture.Texture == nil && !texture.Mutable && !texture.RenderTarget && texture.Generation != std::numeric_limits<std::uint64_t>::max();
			});
			const auto index = static_cast<std::size_t>(free - receiver.Textures.begin());
			if (index == receiver.Textures.size() && index >= receiver.Description.MaxTextures) {
				throw std::runtime_error("Metal mutable texture count exceeds its allocation limit");
			}
			if (index == receiver.Textures.size()) {
				receiver.Textures.emplace_back();
			}

			auto& record = receiver.Textures[index];
			record.Mutable = true;
			record.Interpretation = 0;
			record.OriginalFormat = original_format;
			record.Bytes = bytes;
			receiver.TextureBytes += bytes;
			return SpriteTextureHandle{static_cast<std::uint32_t>(index), record.Generation, receiver.Resources.get()};
		};
		context.Retire = [resource_domain](SpriteTextureHandle handle) noexcept {
			const auto resources = resource_domain.lock();
			if (!resources || handle.Owner != resources.get() || handle.Index >= resources->Textures.size()) {
				return;
			}

			auto& record = resources->Textures[handle.Index];
			if (!record.Mutable || record.Generation != handle.Generation) {
				return;
			}

			resources->TextureBytes -= record.Bytes;
			record.MutableView = nil;
			record.MutableSampler = nil;
			record.Bytes = 0;
			record.Mutable = false;
			if (record.Generation != std::numeric_limits<std::uint64_t>::max()) {
				++record.Generation;
			}
		};
		context.SetView = [lifetime](SpriteTextureHandle handle, id<MTLTexture> view) {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Metal mutable texture renderer has closed");
			}
			auto& receiver = *live->Receiver;
			receiver.RequireMotionResourcePreparationOrIdle();
			receiver.MutableTexture(handle).MutableView = view;
		};
		context.Bind = [lifetime](SpriteTextureHandle handle, id<MTLSamplerState> sampler, std::uint32_t slot, scene::MutableTextureStage stage) {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Metal mutable texture renderer has closed");
			}
			if (slot >= 256) {
				throw std::runtime_error("Metal mutable binding slot exceeds its checked range");
			}

			auto& receiver = *live->Receiver;
			receiver.RequireMutableBindingEncoding();
			static_cast<void>(MutableStageIndex(stage));
			if (stage != scene::MutableTextureStage::Pixel || (slot != 0 && slot != 2)) {
				throw std::runtime_error("Metal mutable measured shaders bind only pixel slots zero and two");
			}
			receiver.MutableTexture(handle).MutableSampler = sampler;
			receiver.Resources->MutableBindings[slot] = handle;
		};

		context.RequireCopySource = [lifetime, require_current_target = std::move(require_current_target)]() {
			const auto allocation = lifetime.lock();
			if (!allocation || allocation->Receiver == nullptr) {
				throw std::runtime_error("Metal copied region allocation owner has closed");
			}

			const auto selected = require_current_target ? require_current_target() : std::shared_ptr<SpritePassRenderer>{};
			if (require_current_target && !selected) {
				throw std::runtime_error("actual graphics current target source is absent");
			}
			auto& receiver = selected ? *selected->m_Implementation : *allocation->Receiver;
			receiver.RequireIdleEncoding();
			if (receiver.Resources != allocation->Receiver->Resources || !receiver.TargetInitialized) {
				throw std::runtime_error("Metal copied source requires an initialized target in its actual resource domain");
			}

			MetalSpriteCopySource source;
			source.Target = receiver.Target;
			source.Width = receiver.Description.Width;
			source.Height = receiver.Description.Height;
			source.ResourceDomain = receiver.Resources.get();

			return source;
		};
		context.CopyFromTarget = [lifetime](id<MTLTexture> destination, const scene::MotionBounds& rectangle, const MetalSpriteCopySource& source) {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("Metal copied region renderer has closed");
			}
			auto& receiver = *live->Receiver;
			if (source.Target == nil || source.ResourceDomain != receiver.Resources.get() || source.Target.device != receiver.Device || source.Width != source.Target.width || source.Height != source.Target.height || destination == nil || destination.pixelFormat != source.Target.pixelFormat) {
				throw std::runtime_error("Metal copied region physical source/destination formats differ");
			}

			@autoreleasepool {
				id<MTLCommandBuffer> commands = [receiver.Queue commandBuffer];
				id<MTLBlitCommandEncoder> encoder = [commands blitCommandEncoder];
				if (commands == nil || encoder == nil) {
					throw std::runtime_error("Metal copied region command allocation failed");
				}
				const auto x = static_cast<std::size_t>(rectangle.MinimumX);
				const auto y = static_cast<std::size_t>(rectangle.MinimumY);
				const auto width = static_cast<std::size_t>(rectangle.MaximumX - rectangle.MinimumX);
				const auto height = static_cast<std::size_t>(rectangle.MaximumY - rectangle.MinimumY);
				[encoder copyFromTexture:source.Target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x, y, 0) sourceSize:MTLSizeMake(width, height, 1) toTexture:destination destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
				[encoder endEncoding];
				[commands commit];
				return commands;
			}
		};

		return CreateSpriteMutableTextureBackend(std::move(context));
	}
	SpriteTextureHandle SpritePassRenderer::MutableTextureBinding(std::uint32_t slot, scene::MutableTextureStage stage) const {
		if (slot >= 256) {
			throw std::runtime_error("Metal mutable binding slot exceeds its checked range");
		}

		static_cast<void>(MutableStageIndex(stage));
		if (stage != scene::MutableTextureStage::Pixel || (slot != 0 && slot != 2)) {
			throw std::runtime_error("Metal mutable measured shaders bind only pixel slots zero and two");
		}
		const auto handle = m_Implementation->Resources->MutableBindings[slot];
		static_cast<void>(m_Implementation->Texture(handle));

		return handle;
	}

	SpritePipelineHandle SpritePassRenderer::PreparePipeline(const scene::SpritePipeline& pipeline) {
		auto& state = *m_Implementation;
		state.RequireMotionResourcePreparationOrIdle();
		ValidatePipeline(pipeline);
		const auto key = PipelineKey(pipeline);
		const auto found = std::find_if(state.Pipelines.begin(), state.Pipelines.end(), [&](const auto& record) {
			return PipelineKey(record.Configuration) == key;
		});
		std::size_t index = static_cast<std::size_t>(found - state.Pipelines.begin());
		if (index == state.Pipelines.size()) {
			if (index >= state.Description.MaxPipelines) {
				throw std::runtime_error("Metal sprite pipeline count exceeds its allocation limit");
			}

			@autoreleasepool {
				MTLFunctionConstantValues* constants = [[MTLFunctionConstantValues alloc] init];
				const std::array<std::uint32_t, 3> values{TextureInterpretation(pipeline.TextureFormat), static_cast<std::uint32_t>(pipeline.VertexColor), static_cast<std::uint32_t>(pipeline.PixelOperation)};
				for (std::size_t constant = 0; constant < values.size(); ++constant) {
					[constants setConstantValue:&values[constant] type:MTLDataTypeUInt atIndex:constant];
				}

				NSError* error = nil;
				id<MTLFunction> fragment = nil;
				if (pipeline.NativeVertexShaderIndex == 2) {
					fragment = [state.Library newFunctionWithName:pipeline.NativePixelShaderIndex == 2041 ? @"SampleMovieAlphaFragment" : @"SampleCapturedOpacityMapFragment"];
				} else if (pipeline.NativeVertexShaderIndex == 3) {
					fragment = [state.Library newFunctionWithName:@"SquareFillRectangleColorFragment"];
				} else if (pipeline.NativePixelShaderIndex == 1205) {
					fragment = [state.Library newFunctionWithName:@"SampleCopiedAdditiveFragment"];
				} else if (pipeline.NativePixelShaderIndex == 1129) {
					fragment = [state.Library newFunctionWithName:@"SampleCopiedAdditiveSkipColorFragment"];
				} else if (pipeline.NativePixelShaderIndex == 1131) {
					fragment = [state.Library newFunctionWithName:@"SampleCopiedOverlaySkipColorFragment"];
				} else if (pipeline.NativePixelShaderIndex == 1137) {
					fragment = [state.Library newFunctionWithName:@"SampleCopiedDarkenSkipColorFragment"];
				} else if (pipeline.NativePixelShaderIndex == 1289) {
					fragment = [state.Library newFunctionWithName:@"SampleCopiedDarkenDoubleColorFragment"];
				} else if (pipeline.NativePixelShaderIndex == 1281) {
					fragment = [state.Library newFunctionWithName:@"SampleCopiedAdditiveDoubleColorFragment"];
				} else if (pipeline.NativePixelShaderIndex == 1283) {
					fragment = [state.Library newFunctionWithName:@"SampleCopiedOverlayDoubleColorFragment"];
				} else {
					fragment = [state.Library newFunctionWithName:@"SampleSpriteFragment" constantValues:constants error:&error];
				}
				if (fragment == nil) {
					throw MetalOperationError("specialize Metal sprite shader", error);
				}

				MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
				if (pipeline.NativeVertexShaderIndex == 2) {
					descriptor.vertexFunction = state.CapturedOpacityMapVertexFunction;
				} else if (pipeline.NativeVertexShaderIndex == 3) {
					descriptor.vertexFunction = state.FillRectangleVertexFunction;
				} else {
					descriptor.vertexFunction = pipeline.NativeVertexShaderIndex == 10 ? state.ProjectionVertexFunction : pipeline.NativeVertexShaderIndex == 4 ? state.RawTextureVertexFunction
					                                                                                                                                            : state.VertexFunction;
				}
				if (descriptor.vertexFunction == nil) {
					throw std::runtime_error("requested native texture vertex shader is unavailable in the provided Metal library");
				}
				descriptor.fragmentFunction = fragment;
				MTLRenderPipelineColorAttachmentDescriptor* color = descriptor.colorAttachments[0];
				color.pixelFormat = MTLPixelFormatRGBA8Unorm;
				color.blendingEnabled = pipeline.Blend.Enabled;
				color.rgbBlendOperation = MetalBlendOperation(pipeline.Blend.RgbOperation);
				color.alphaBlendOperation = MetalBlendOperation(pipeline.Blend.AlphaOperation);
				color.sourceRGBBlendFactor = MetalBlendFactor(pipeline.Blend.SourceRgb);
				color.destinationRGBBlendFactor = MetalBlendFactor(pipeline.Blend.DestinationRgb);
				color.sourceAlphaBlendFactor = MetalBlendFactor(pipeline.Blend.SourceAlpha);
				color.destinationAlphaBlendFactor = MetalBlendFactor(pipeline.Blend.DestinationAlpha);
				color.writeMask = pipeline.Blend.WriteMask == 15 ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
				MTLVertexDescriptor* layout = [[MTLVertexDescriptor alloc] init];
				layout.attributes[0].format = MTLVertexFormatFloat2;
				layout.attributes[0].offset = 0;
				layout.attributes[0].bufferIndex = 0;
				layout.attributes[1].offset = 8;
				layout.attributes[1].bufferIndex = 0;
				if (pipeline.NativeVertexShaderIndex == 2) {
					layout.attributes[1].format = MTLVertexFormatUChar4Normalized_BGRA;
					layout.attributes[2].format = MTLVertexFormatFloat2;
					layout.attributes[2].offset = 12;
					layout.attributes[2].bufferIndex = 0;
					layout.attributes[3].format = MTLVertexFormatFloat2;
					layout.attributes[3].offset = 20;
					layout.attributes[3].bufferIndex = 0;
					layout.layouts[0].stride = kCapturedOpacityMapVertexBytes;
				} else if (pipeline.NativeVertexShaderIndex == 3) {
					layout.layouts[0].stride = 8;
				} else if (pipeline.NativeVertexShaderIndex == 4 || pipeline.NativeVertexShaderIndex == 10) {
					if (pipeline.NativeVertexShaderIndex == 10) {
						layout.attributes[0].format = MTLVertexFormatFloat3;
						layout.attributes[1].offset = 12;
					}
					layout.attributes[1].format = MTLVertexFormatFloat2;
					layout.layouts[0].stride = pipeline.NativeVertexShaderIndex == 10 ? kSpriteVertexBytes : kRawTextureVertexBytes;
				} else {
					layout.attributes[1].format = MTLVertexFormatUChar4Normalized_BGRA;
					layout.attributes[2].format = MTLVertexFormatFloat2;
					layout.attributes[2].offset = 12;
					layout.attributes[2].bufferIndex = 0;
					layout.layouts[0].stride = kSpriteVertexBytes;
				}
				descriptor.vertexDescriptor = layout;
				SpriteGpuPipeline record;
				record.Configuration = pipeline;
				record.State = [state.Device newRenderPipelineStateWithDescriptor:descriptor error:&error];
				if (record.State == nil) {
					throw MetalOperationError("create Metal sprite pipeline", error);
				}

				MTLSamplerDescriptor* sampler = [[MTLSamplerDescriptor alloc] init];
				sampler.minFilter = pipeline.Sampler.Minification == scene::TextureSampling::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
				sampler.magFilter = pipeline.Sampler.Magnification == scene::TextureSampling::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
				sampler.mipFilter = pipeline.Sampler.MipFilter == scene::TextureSampling::Linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
				sampler.sAddressMode = MetalAddress(pipeline.Sampler.AddressU);
				sampler.tAddressMode = MetalAddress(pipeline.Sampler.AddressV);
				sampler.rAddressMode = MTLSamplerAddressModeRepeat;
				sampler.normalizedCoordinates = YES;
				// Every decoded original payload has one mip; native wide LOD bounds select that same level.
				sampler.lodMinClamp = 0;
				sampler.lodMaxClamp = 0;
				sampler.maxAnisotropy = 1;
				record.Sampler = [state.Device newSamplerStateWithDescriptor:sampler];
				if (record.Sampler == nil) {
					throw std::runtime_error("Metal sprite sampler allocation failed");
				}

				if (state.Resources->TextureMaskEnabled && pipeline.NativePixelShaderIndex != 2041) {
					PrepareTextureMaskPipeline(*state.Resources, record);
				}
				state.Pipelines.push_back(std::move(record));
			}
		}

		SpritePipelineHandle handle;
		handle.Index = static_cast<std::uint32_t>(index);
		handle.Owner = state.Resources.get();

		return handle;
	}

	bool SpritePassRenderer::CanSubmit() const {
		return !m_Implementation->Resources->EncodingFaulted.load() && m_Implementation->Resources->LiveEncodingOwner.load() == nullptr && std::any_of(m_Implementation->Slots.begin(), m_Implementation->Slots.end(), Implementation::Available);
	}

	static void UploadSpriteVertices(id<MTLBuffer> buffer, std::size_t byte_offset, std::span<const scene::MotionSpriteVertex> vertices) {
		static_assert(std::endian::native == std::endian::little && sizeof(float) == 4);
		auto* destination = static_cast<std::uint8_t*>(buffer.contents) + byte_offset;
		for (std::size_t index = 0; index < vertices.size(); ++index) {
			const auto& vertex = vertices[index];
			const std::array<float, 2> position{vertex.Position.X, vertex.Position.Y};
			const auto bgra = (vertex.Color >> 8) | ((vertex.Color & 0xff) << 24);
			const std::array<float, 2> uv{vertex.Uv.X, vertex.Uv.Y};
			std::memcpy(destination + index * kSpriteVertexBytes, position.data(), 8);
			std::memcpy(destination + index * kSpriteVertexBytes + 8, &bgra, 4);
			std::memcpy(destination + index * kSpriteVertexBytes + 12, uv.data(), 8);
		}
	}

	static void UploadRawTextureVertices(id<MTLBuffer> buffer, std::size_t byte_offset, std::span<const scene::RawTextureVertex> vertices) {
		static_assert(std::endian::native == std::endian::little && sizeof(float) == 4);
		auto* destination = static_cast<std::byte*>(buffer.contents) + byte_offset;
		for (std::size_t index = 0; index < vertices.size(); ++index) {
			const auto& vertex = vertices[index];
			const std::array<float, 4> words{vertex.Position.X, vertex.Position.Y, vertex.Uv.X, vertex.Uv.Y};
			std::memcpy(destination + index * kRawTextureVertexBytes, words.data(), kRawTextureVertexBytes);
		}
	}
	static void RequireMovieAlphaDraw(const SpritePassDraw& draw) {
		if (draw.MovieAlpha == nullptr || draw.CapturedOpacityMap != nullptr || draw.HostInputs != nullptr || !draw.Vertices.empty() || !draw.Indices.empty() || !draw.RawTextureVertices.empty() || !draw.FillRectangleVertices.empty() || draw.Topology != scene::MotionDrawTopology::TriangleStrip || draw.CopiedDestination != SpriteTextureHandle{}) {
			throw std::runtime_error("movie alpha requires its exclusive actual format5 indexed strip");
		}
		for (std::size_t index = 0; index < draw.MovieAlpha->Vertices.size(); ++index) {
			const auto& vertex = draw.MovieAlpha->Vertices[index];
			if (draw.MovieAlpha->Indices[index] != index || !std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) || !std::isfinite(vertex.ColorUv.X) || !std::isfinite(vertex.ColorUv.Y) || !std::isfinite(vertex.AlphaUv.X) || !std::isfinite(vertex.AlphaUv.Y)) {
				throw std::runtime_error("movie alpha requires finite native XY/dual-UV and index16 words");
			}
		}
	}

	static void UploadMovieAlpha(id<MTLBuffer> vertices, std::size_t vertex_offset, id<MTLBuffer> indices, std::size_t index_offset, const scene::MovieAlphaDrawPacket& packet) {
		for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
			const auto& vertex = packet.Vertices[index];
			const std::array<float, 2> position{vertex.Position.X, vertex.Position.Y};
			const std::array<float, 4> coordinates{vertex.ColorUv.X, vertex.ColorUv.Y, vertex.AlphaUv.X, vertex.AlphaUv.Y};
			auto* output = static_cast<std::byte*>(vertices.contents) + vertex_offset + index * kCapturedOpacityMapVertexBytes;
			std::memcpy(output, position.data(), sizeof(position));
			std::memcpy(output + 8, &vertex.Color, sizeof(vertex.Color));
			std::memcpy(output + 12, coordinates.data(), sizeof(coordinates));
		}
		std::memcpy(static_cast<std::byte*>(indices.contents) + index_offset, packet.Indices.data(), sizeof(packet.Indices));
	}

	static void RequireCapturedOpacityMapDraw(const SpritePassDraw& draw) {
		if (draw.CapturedOpacityMap == nullptr || draw.MovieAlpha != nullptr || draw.HostInputs != nullptr || !draw.Vertices.empty() || !draw.Indices.empty() || !draw.RawTextureVertices.empty() || !draw.FillRectangleVertices.empty() || draw.Topology != scene::MotionDrawTopology::TriangleStrip || draw.CopiedDestination != SpriteTextureHandle{}) {
			throw std::runtime_error("Captured opacity map requires its exclusive actual format5 indexed strip");
		}
		const auto& packet = *draw.CapturedOpacityMap;
		if (!std::isfinite(packet.Phase) || !std::isfinite(packet.InverseVague) || !(packet.InverseVague > 0) || packet.AddressU > 2 || packet.AddressV > 2) {
			throw std::runtime_error("Captured opacity map requires finite phase and positive inverse width with native sampler addresses");
		}
		for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
			const auto& vertex = packet.Vertices[index];
			if (packet.Indices[index] != static_cast<std::uint16_t>(index) || !std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) || !std::isfinite(vertex.SourceUv.X) || !std::isfinite(vertex.SourceUv.Y) || !std::isfinite(vertex.MapUv.X) || !std::isfinite(vertex.MapUv.Y)) {
				throw std::runtime_error("Captured opacity map requires finite actual XY/dual-UV words and native index16 order");
			}
		}
	}

	static void UploadCapturedOpacityMap(id<MTLBuffer> vertices, std::size_t vertex_offset, id<MTLBuffer> indices, std::size_t index_offset, const scene::CapturedOpacityMapDrawPacket& packet) {
		static_assert(std::endian::native == std::endian::little && sizeof(float) == 4);
		auto* destination = static_cast<std::byte*>(vertices.contents) + vertex_offset;
		for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
			const auto& vertex = packet.Vertices[index];
			const std::array<float, 2> position{vertex.Position.X, vertex.Position.Y};
			const std::array<float, 4> coordinates{vertex.SourceUv.X, vertex.SourceUv.Y, vertex.MapUv.X, vertex.MapUv.Y};
			std::memcpy(destination + index * kCapturedOpacityMapVertexBytes, position.data(), sizeof(position));
			// Packet color is already native ARGB; unlike Motion's authored RGBA it needs no byte rotation.
			std::memcpy(destination + index * kCapturedOpacityMapVertexBytes + 8, &vertex.Color, sizeof(vertex.Color));
			std::memcpy(destination + index * kCapturedOpacityMapVertexBytes + 12, coordinates.data(), sizeof(coordinates));
		}
		std::memcpy(static_cast<std::byte*>(indices.contents) + index_offset, packet.Indices.data(), sizeof(packet.Indices));
	}

	static void RequireFillRectangleDraw(const SpritePassDraw& draw) {
		if (draw.FillRectangleVertices.size() != 4 || draw.HostInputs != nullptr || !draw.Vertices.empty() || !draw.Indices.empty() || !draw.RawTextureVertices.empty() || draw.Texture != SpriteTextureHandle{} || draw.CapturedOpacityMap != nullptr || draw.MovieAlpha != nullptr || draw.Topology != scene::MotionDrawTopology::TriangleStrip) {
			throw std::runtime_error("FillRect requires four format0 XY vertices, no texture and nonindexed strip topology");
		}
		for (const auto& vertex : draw.FillRectangleVertices) {
			if (!std::isfinite(vertex.X) || !std::isfinite(vertex.Y)) {
				throw std::runtime_error("FillRect requires finite actual XY geometry");
			}
		}
	}

	static void UploadFillRectangleVertices(id<MTLBuffer> buffer, std::size_t offset, std::span<const resource::MotionPoint> vertices) {
		auto* destination = static_cast<std::byte*>(buffer.contents) + offset;
		for (std::size_t index = 0; index < vertices.size(); ++index) {
			const std::array<float, 2> words{vertices[index].X, vertices[index].Y};
			std::memcpy(destination + index * 8, words.data(), 8);
		}
	}

	static std::size_t DrawVertexCount(const SpritePassDraw& draw) {
		if (draw.MovieAlpha != nullptr) {
			return draw.MovieAlpha->Vertices.size();
		}

		if (draw.CapturedOpacityMap != nullptr) {
			return draw.CapturedOpacityMap->Vertices.size();
		}
		if (draw.HostInputs != nullptr) {
			return draw.HostInputs->VertexCount();
		}

		if (!draw.FillRectangleVertices.empty()) {
			return draw.FillRectangleVertices.size();
		}

		return draw.RawTextureVertices.empty() ? draw.Vertices.size() : draw.RawTextureVertices.size();
	}
	static std::size_t DrawVertexBytes(const SpritePassDraw& draw) {
		if (draw.MovieAlpha != nullptr) {
			return draw.MovieAlpha->Vertices.size() * kCapturedOpacityMapVertexBytes;
		}

		if (draw.CapturedOpacityMap != nullptr) {
			return draw.CapturedOpacityMap->Vertices.size() * kCapturedOpacityMapVertexBytes;
		}
		if (draw.HostInputs != nullptr) {
			return draw.HostInputs->VertexBytes().size();
		}

		if (!draw.FillRectangleVertices.empty()) {
			return draw.FillRectangleVertices.size() * 8;
		}

		return draw.RawTextureVertices.empty() ? draw.Vertices.size() * kSpriteVertexBytes : draw.RawTextureVertices.size() * kRawTextureVertexBytes;
	}
	static std::size_t DrawIndexCount(const SpritePassDraw& draw) {
		if (draw.MovieAlpha != nullptr) {
			return draw.MovieAlpha->Indices.size();
		}

		return draw.CapturedOpacityMap != nullptr ? draw.CapturedOpacityMap->Indices.size() : draw.HostInputs == nullptr ? draw.Indices.size()
		                                                                                                                 : draw.HostInputs->IndexCount();
	}
	static std::size_t DrawIndexBytes(const SpritePassDraw& draw) {
		if (draw.MovieAlpha != nullptr) {
			return sizeof(draw.MovieAlpha->Indices);
		}

		return draw.CapturedOpacityMap != nullptr ? sizeof(draw.CapturedOpacityMap->Indices) : draw.HostInputs == nullptr ? draw.Indices.size_bytes()
		                                                                                                                  : draw.HostInputs->IndexBytes().size();
	}
	static std::size_t AdvanceDrawIndexOffset(std::size_t offset, const SpritePassDraw& draw) {
		return (offset + DrawIndexBytes(draw) + 3) & ~std::size_t{3};
	}
	static void RequireDrawIndices(const SpritePassDraw& draw) {
		if (draw.MovieAlpha != nullptr) {
			RequireMovieAlphaDraw(draw);
			return;
		}

		if (draw.CapturedOpacityMap != nullptr) {
			RequireCapturedOpacityMapDraw(draw);
			return;
		}
		if (draw.HostInputs == nullptr) {
			for (const auto index : draw.Indices) {
				if (index >= draw.Vertices.size()) {
					throw std::runtime_error("Metal sprite strip index exceeds its draw's vertex range");
				}
			}

			return;
		}
		const auto bytes = draw.HostInputs->IndexBytes();
		const auto width = draw.HostInputs->IndexWordBytes();
		const auto count = draw.HostInputs->VertexCount();
		for (std::size_t offset = 0; offset < bytes.size(); offset += width) {
			std::uint32_t index = 0;
			for (std::size_t byte = 0; byte < width; ++byte) {
				index |= std::to_integer<std::uint32_t>(bytes[offset + byte]) << (byte * 8);
			}
			if (index >= count) {
				throw std::runtime_error("Metal sprite host index exceeds its actual frame input");
			}
		}
	}

	std::uint64_t SpritePassRenderer::Submit(std::span<const SpritePassDraw> draws, const std::array<float, 16>& projection_registers, const std::array<float, 4>& clear_color, SpritePassLoadAction load_action) {
		auto& state = *m_Implementation;
		state.RequireIdleEncoding();
		if (draws.size() > state.Description.MaxDraws || state.NextSubmission == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("Metal sprite draw count or submission identity exceeds its range");
		}

		if ((load_action != SpritePassLoadAction::Clear && load_action != SpritePassLoadAction::Preserve) || (load_action == SpritePassLoadAction::Preserve && !state.TargetInitialized)) {
			throw std::runtime_error("Metal sprite preserve requires an initialized target and supported load action");
		}

		std::size_t vertices = 0;
		std::size_t indices = 0;
		for (const auto& draw : draws) {
			if (draw.TransformRegisters && std::any_of(draw.TransformRegisters->begin(), draw.TransformRegisters->end(), [](float value) {
				    return !std::isfinite(value);
			    })) {
				throw std::runtime_error("Metal sprite per-draw transform requires finite shader registers");
			}
			if (draw.HostInputs != nullptr && (!draw.Vertices.empty() || !draw.Indices.empty() || !draw.RawTextureVertices.empty() || !draw.FillRectangleVertices.empty() || draw.CapturedOpacityMap != nullptr || draw.MovieAlpha != nullptr)) {
				throw std::runtime_error("Metal sprite host inputs require their exclusive actual geometry path");
			}
			if (draw.Scissor && (draw.Scissor->X > state.Description.Width || draw.Scissor->Y > state.Description.Height || draw.Scissor->Width > state.Description.Width - draw.Scissor->X || draw.Scissor->Height > state.Description.Height - draw.Scissor->Y)) {
				throw std::runtime_error("Metal sprite scissor exceeds physical target bounds");
			}

			if ((draw.Topology != scene::MotionDrawTopology::TriangleStrip && draw.Topology != scene::MotionDrawTopology::TriangleList) || (draw.Topology == scene::MotionDrawTopology::TriangleList && DrawIndexCount(draw) % 3 != 0)) {
				throw std::runtime_error("Metal sprite topology or triangle-list index count is invalid");
			}
			const auto& pipeline = state.Pipeline(draw.Pipeline).Configuration;
			const bool fill_rectangle = pipeline.NativeVertexShaderIndex == 3;
			const bool raw_texture = pipeline.NativeVertexShaderIndex == 4;
			const bool opacity_map = pipeline.NativePixelShaderIndex == 2040;
			const bool movie_alpha = pipeline.NativePixelShaderIndex == 2041;
			if (pipeline.NativeVertexShaderIndex == 10 && (draw.HostInputs == nullptr || draw.HostInputs->Layout() != scene::MotionVertexLayout::XyzUv || draw.Topology != scene::MotionDrawTopology::TriangleList)) {
				throw std::runtime_error("ScreenProjection requires genuine indexed format8 host geometry");
			}
			if (pipeline.NativeVertexShaderIndex == 1 && draw.HostInputs != nullptr && draw.HostInputs->Layout() != scene::MotionVertexLayout::XyColorUv) {
				throw std::runtime_error("ordinary sprite shader requires actual format4 host geometry");
			}
			if (!fill_rectangle && state.Texture(draw.Texture).SampledTexture() == state.Target) {
				throw std::runtime_error("Screen target cannot sample its own active color attachment");
			}
			if (movie_alpha) {
				RequireMovieAlphaDraw(draw);
			} else if (opacity_map) {
				RequireCapturedOpacityMapDraw(draw);
			} else if (fill_rectangle) {
				RequireFillRectangleDraw(draw);
			} else if (raw_texture) {
				if (draw.HostInputs != nullptr || !draw.Vertices.empty() || !draw.FillRectangleVertices.empty() || draw.CapturedOpacityMap != nullptr || draw.MovieAlpha != nullptr || draw.RawTextureVertices.size() != 4 || !draw.Indices.empty() || draw.Topology != scene::MotionDrawTopology::TriangleStrip) {
					throw std::runtime_error("Metal RawTex requires four XYUV vertices and a nonindexed triangle strip");
				}
			} else if (draw.MovieAlpha != nullptr || draw.CapturedOpacityMap != nullptr || !draw.FillRectangleVertices.empty() || !draw.RawTextureVertices.empty() || DrawVertexCount(draw) < 3 || DrawIndexCount(draw) < 3) {
				throw std::runtime_error("Metal sprite requires indexed XY/color/UV geometry");
			}
			if (DrawVertexCount(draw) > state.Description.MaxVertices - vertices || DrawIndexCount(draw) > state.Description.MaxIndices - indices) {
				throw std::runtime_error("Metal sprite frame geometry exceeds its allocation limit");
			}

			if (!fill_rectangle && state.Texture(draw.Texture).Interpretation != TextureInterpretation(state.Pipeline(draw.Pipeline).Configuration.TextureFormat)) {
				throw std::runtime_error("Metal sprite texture interpretation differs from its prepared shader");
			}

			if (!fill_rectangle && state.Texture(draw.Texture).Mutable && state.Texture(draw.Texture).MutableSampler == nil) {
				throw std::runtime_error("Metal mutable draw requires the owner sampler to be bound");
			}

			if (movie_alpha && (!state.Texture(draw.Texture).Mutable || state.Texture(draw.Texture).OriginalFormat != 88)) {
				throw std::runtime_error("movie alpha requires original RGB32 mutable storage");
			}
			if (opacity_map) {
				const auto& map = state.Texture(draw.OpacityMapTexture);
				const auto& source = state.Texture(draw.Texture);
				if (!source.Mutable || source.OriginalFormat != 28 || map.Mutable || map.RenderTarget || map.SampledTexture() == state.Target || static_cast<std::uint32_t>(pipeline.Sampler.AddressU) != draw.CapturedOpacityMap->AddressU || static_cast<std::uint32_t>(pipeline.Sampler.AddressV) != draw.CapturedOpacityMap->AddressV) {
					throw std::runtime_error("Captured opacity map requires actual Capture companion, supported raw-red Image texture and exact map sampler");
				}
			}

			if (UsesCopiedDestination(pipeline.NativePixelShaderIndex)) {
				const auto& copied = state.Texture(draw.CopiedDestination);
				if (!copied.Mutable || copied.OriginalFormat != 28 || copied.MutableSampler == nil || !FiniteCopiedUvAffine(draw.CopiedUvAffine)) {
					throw std::runtime_error(pipeline.NativePixelShaderIndex == 1205 ? "Metal PS1205 requires a live bound RGBA copied owner and finite pixel constants" : "Metal copied skip-color program requires a live bound RGBA copied owner and finite pixel constants");
				}
			}

			RequireDrawIndices(draw);

			vertices += DrawVertexCount(draw);
			indices += DrawIndexCount(draw);
		}

		auto& slot = state.AcquireUploadSlot("previous Metal sprite submission");

		std::size_t vertex_offset = 0;
		std::size_t index_offset = 0;
		std::size_t draw_index = 0;
		for (const auto& draw : draws) {
			auto* constants = static_cast<std::byte*>(slot.Constants.contents) + draw_index * kDrawConstantBytes;
			const auto& transform = draw.TransformRegisters ? *draw.TransformRegisters : projection_registers;
			std::memcpy(constants, transform.data(), transform.size() * sizeof(float));
			if (draw.MovieAlpha != nullptr) {
				UploadMovieAlpha(slot.Vertices, vertex_offset, slot.Indices, index_offset, *draw.MovieAlpha);
			} else if (draw.CapturedOpacityMap != nullptr) {
				UploadCapturedOpacityMap(slot.Vertices, vertex_offset, slot.Indices, index_offset, *draw.CapturedOpacityMap);
			} else if (!draw.FillRectangleVertices.empty()) {
				UploadFillRectangleVertices(slot.Vertices, vertex_offset, draw.FillRectangleVertices);
				const auto color = scene::UnpackMotionShaderColor(draw.FillRectangleColor);
				std::memcpy(constants + 64, color.data(), color.size() * sizeof(float));
			} else if (!draw.RawTextureVertices.empty()) {
				UploadRawTextureVertices(slot.Vertices, vertex_offset, draw.RawTextureVertices);
				const auto color = scene::UnpackMotionShaderColor(draw.RawTextureColor);
				std::memcpy(constants + 64, color.data(), color.size() * sizeof(float));
			} else if (draw.HostInputs != nullptr) {
				if (draw.HostInputs->Layout() == scene::MotionVertexLayout::XyzUv) {
					const auto color = scene::UnpackMotionShaderColor(draw.RawTextureColor);
					std::memcpy(constants + 64, color.data(), color.size() * sizeof(float));
				}
				const auto vertex_bytes = draw.HostInputs->VertexBytes();
				const auto index_bytes = draw.HostInputs->IndexBytes();
				std::memcpy(static_cast<std::byte*>(slot.Vertices.contents) + vertex_offset, vertex_bytes.data(), vertex_bytes.size());
				std::memcpy(static_cast<std::byte*>(slot.Indices.contents) + index_offset, index_bytes.data(), index_bytes.size());
			} else {
				UploadSpriteVertices(slot.Vertices, vertex_offset, draw.Vertices);
				std::memcpy(static_cast<std::uint8_t*>(slot.Indices.contents) + index_offset, draw.Indices.data(), draw.Indices.size_bytes());
			}
			std::memcpy(constants + 128, draw.CopiedUvAffine.data(), draw.CopiedUvAffine.size() * sizeof(float));
			if (draw.CapturedOpacityMap != nullptr) {
				const std::array<float, 2> pixel{draw.CapturedOpacityMap->Phase, draw.CapturedOpacityMap->InverseVague};
				std::memcpy(constants + 128, pixel.data(), sizeof(pixel));
			}
			vertex_offset += DrawVertexBytes(draw);
			index_offset = AdvanceDrawIndexOffset(index_offset, draw);
			++draw_index;
		}

		@autoreleasepool {
			MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
			pass.colorAttachments[0].texture = state.Target;
			pass.colorAttachments[0].loadAction = load_action == SpritePassLoadAction::Preserve ? MTLLoadActionLoad : MTLLoadActionClear;
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
			id<MTLCommandBuffer> commands = [state.Queue commandBuffer];
			id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
			if (commands == nil || encoder == nil) {
				throw std::runtime_error("Metal sprite command allocation failed");
			}

			const auto raster = PrepareDefaultGraphicsRenderState().Raster;
			[encoder setCullMode:DefaultMetalCull(raster.Cull)];
			[encoder setTriangleFillMode:DefaultMetalFill(raster.Fill)];
			[encoder setFrontFacingWinding:raster.FrontCounterClockwise ? MTLWindingCounterClockwise : MTLWindingClockwise];
			[encoder setDepthClipMode:raster.DepthClipEnabled ? MTLDepthClipModeClip : MTLDepthClipModeClamp];
			[encoder setDepthBias:static_cast<float>(raster.DepthBias) slopeScale:raster.SlopeScaledDepthBias clamp:raster.DepthBiasClamp];
			vertex_offset = 0;
			index_offset = 0;
			draw_index = 0;
			for (const auto& draw : draws) {
				const auto scissor = draw.Scissor.value_or(SpritePassScissor{0, 0, state.Description.Width, state.Description.Height});
				if (scissor.Width == 0 || scissor.Height == 0) {
					vertex_offset += DrawVertexBytes(draw);
					index_offset = AdvanceDrawIndexOffset(index_offset, draw);
					++draw_index;

					continue;
				}

				[encoder setScissorRect:MTLScissorRect{scissor.X, scissor.Y, scissor.Width, scissor.Height}];
				const auto& pipeline = state.Pipeline(draw.Pipeline);
				[encoder setRenderPipelineState:pipeline.State];
				[encoder setVertexBuffer:slot.Vertices offset:vertex_offset atIndex:0];
				[encoder setVertexBuffer:slot.Constants offset:draw_index * kDrawConstantBytes atIndex:1];
				const bool has_texture = draw.Texture.Owner != nullptr;
				id<MTLTexture> sampled_texture = has_texture ? state.Texture(draw.Texture).SampledTexture() : nil;
				id<MTLSamplerState> sampler = has_texture && state.Texture(draw.Texture).Mutable ? state.Texture(draw.Texture).MutableSampler : pipeline.Sampler;
				[encoder setFragmentTexture:sampled_texture atIndex:0];
				[encoder setFragmentSamplerState:sampler atIndex:0];
				if (draw.CapturedOpacityMap != nullptr) {
					[encoder setFragmentTexture:state.Texture(draw.OpacityMapTexture).SampledTexture() atIndex:1];
					[encoder setFragmentSamplerState:pipeline.Sampler atIndex:1];
					[encoder setFragmentBuffer:slot.Constants offset:draw_index * kDrawConstantBytes + 128 atIndex:0];
				}
				if (UsesCopiedDestination(pipeline.Configuration.NativePixelShaderIndex)) {
					const auto& copied = state.Texture(draw.CopiedDestination);
					[encoder setFragmentTexture:copied.SampledTexture() atIndex:2];
					[encoder setFragmentSamplerState:copied.MutableSampler atIndex:2];
					[encoder setFragmentBuffer:slot.Constants offset:draw_index * kDrawConstantBytes + 128 atIndex:0];
				}
				const auto topology = draw.Topology == scene::MotionDrawTopology::TriangleList ? MTLPrimitiveTypeTriangle : MTLPrimitiveTypeTriangleStrip;
				if (!draw.RawTextureVertices.empty() || !draw.FillRectangleVertices.empty()) {
					[encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
				} else {
					const auto index_type = draw.MovieAlpha != nullptr || draw.CapturedOpacityMap != nullptr || (draw.HostInputs != nullptr && draw.HostInputs->IndexWordBytes() == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
					[encoder drawIndexedPrimitives:topology indexCount:DrawIndexCount(draw) indexType:index_type indexBuffer:slot.Indices indexBufferOffset:index_offset];
				}
				vertex_offset += DrawVertexBytes(draw);
				index_offset = AdvanceDrawIndexOffset(index_offset, draw);
				++draw_index;
			}

			[encoder endEncoding];
			state.ArmSlotCompletion(slot, commands);
			slot.Commands = commands;
			slot.Submission = state.NextSubmission++;
			state.LatestSubmission = slot.Submission;
			[commands commit];
			state.TargetInitialized = true;
			state.LastTargetCommands = commands;
			state.LastTargetProducer = state.MutableLifetime;
			state.LastTargetSubmission = slot.Submission;
		}

		return slot.Submission;
	}

	ColorPassPixels SpritePassRenderer::Capture(std::uint64_t submission) {
		auto& state = *m_Implementation;
		state.RequireIdleEncoding();
		if (submission == 0 || submission != state.LatestSubmission) {
			throw std::runtime_error("Metal sprite capture requires the latest submission identity");
		}

		const auto found = std::find_if(state.Slots.begin(), state.Slots.end(), [&](const auto& slot) {
			return slot.Submission == submission;
		});
		if (found == state.Slots.end()) {
			throw std::runtime_error("Metal sprite capture submission is unavailable");
		}

		WaitForMetalCommand(found->Commands, "Metal sprite submission");
		@autoreleasepool {
			id<MTLCommandBuffer> copy = [state.Queue commandBuffer];
			id<MTLBlitCommandEncoder> encoder = [copy blitCommandEncoder];
			if (copy == nil || encoder == nil) {
				throw std::runtime_error("Metal sprite capture command allocation failed");
			}

			[encoder copyFromTexture:state.Target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(state.Description.Width, state.Description.Height, 1) toBuffer:state.Readback destinationOffset:0 destinationBytesPerRow:state.RowBytes destinationBytesPerImage:state.RowBytes * state.Description.Height];
			[encoder endEncoding];
			[copy commit];
			WaitForMetalCommand(copy, "Metal sprite capture");
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

	ColorPassPixels SpritePassRenderer::CaptureAfterFrame(const SpritePassRenderer& actual_frame_owner, std::uint64_t actual_submission) {
		auto& state = *m_Implementation;
		auto& producer = *actual_frame_owner.m_Implementation;
		state.RequireIdleEncoding();
		if (state.Resources != producer.Resources || actual_submission == 0 || actual_submission != state.LastTargetSubmission || state.LastTargetProducer.lock() != producer.MutableLifetime || !state.TargetInitialized || state.LastTargetCommands == nil) {
			throw std::runtime_error("Screen capture target was not last written by the exact actual outer frame");
		}
		WaitForMetalCommand(state.LastTargetCommands, "Screen outer submission");
		@autoreleasepool {
			id<MTLCommandBuffer> copy = [state.Queue commandBuffer];
			id<MTLBlitCommandEncoder> encoder = [copy blitCommandEncoder];
			if (copy == nil || encoder == nil) {
				throw std::runtime_error("Metal sprite capture command allocation failed");
			}

			[encoder copyFromTexture:state.Target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(state.Description.Width, state.Description.Height, 1) toBuffer:state.Readback destinationOffset:0 destinationBytesPerRow:state.RowBytes destinationBytesPerImage:state.RowBytes * state.Description.Height];
			[encoder endEncoding];
			[copy commit];
			WaitForMetalCommand(copy, "Metal sprite capture");
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

	ColorPassPixels SpritePassRenderer::CaptureThumbnail(scene::MutableTextureOwner& capture, std::uint32_t width, std::uint32_t height) {
		auto& state = *m_Implementation;
		state.RequireIdleEncoding();
		const auto source = capture.State();
		if (source.Storage != scene::MutableTextureStorage::StagingCompanion || source.Locked || !source.HasCompanion || capture.Companion() == nullptr || width == 0 || height == 0) {
			throw std::runtime_error("Capture thumbnail requires its actual unlocked staging/companion source and positive dimensions");
		}
		static_cast<void>(SpriteMutableStagingTexture(capture.Resource(), state.Resources.get()));
		auto description = state.Description;
		description.Width = width;
		description.Height = height;
		description.MaxVertices = 4;
		description.MaxIndices = 4;
		description.MaxDraws = 1;
		SpritePassRenderer thumbnail(description, *this);
		scene::MotionSpriteMaterial material;
		material.TextureFormat = resource::PixelFormat::Bc7;
		material.TextureAttributes = 4;
		material.BlendMode = 6;
		scene::SpritePipelineOptions options;
		options.Sampling = scene::TextureSampling::Linear;
		auto pipeline = scene::SelectSpritePipeline(material, options);
		pipeline.NativeVertexShaderIndex = 4;
		const auto prepared_pipeline = thumbnail.PreparePipeline(pipeline);
		capture.SetFiltering(1, 1);
		capture.Bind(0, scene::MutableTextureStage::Pixel);
		const auto packet = scene::PrepareRawTextureDrawPacket({-1, 1}, {2, -2}, 0xffffffff);
		SpritePassDraw draw;
		draw.Texture = SpriteMutableTextureHandle(capture.Companion()->Resource());
		draw.Pipeline = prepared_pipeline;
		draw.RawTextureVertices = packet.Vertices;
		const std::array<float, 16> projection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
		const auto submitted = thumbnail.Submit(std::span(&draw, 1), projection);

		return thumbnail.Capture(submitted);
	}

	SpritePassDescription SpritePassRenderer::Description() const {
		return m_Implementation->Description;
	}

	template <typename Operation>
	static decltype(auto) InvokeFrameMetalBoundary(Operation&& operation) {
		@try {
			return std::forward<Operation>(operation)();
		} @catch (NSException* exception) {
			std::string reason = "unknown Objective-C exception";
			@try {
				const char* text = exception.reason.UTF8String;
				if (text != nullptr) {
					reason = text;
				}
			} @catch (NSException*) {
			}
			throw std::runtime_error("live Metal frame operation failed: " + reason);
		}
	}

	class SpriteFrameEncoder::Implementation {
	public:
		explicit Implementation(SpritePassRenderer& renderer) : RendererOwner(renderer), Lifetime(renderer.m_Implementation->MutableLifetime), TargetLifetime(renderer.m_Implementation->MutableLifetime), TargetOwner(&renderer), Resources(renderer.m_Implementation->Resources) {
			TargetMasks.reserve(64);
			VisitedTargets.reserve(64);
		}

		SpritePassRenderer& RendererOwner;
		std::weak_ptr<SpritePassRenderer::Implementation::MutableLifetimeRecord> Lifetime;
		std::weak_ptr<SpritePassRenderer::Implementation::MutableLifetimeRecord> TargetLifetime;
		const SpritePassRenderer* TargetOwner = nullptr;
		id<MTLTexture> FrameTarget = nil;
		std::uint32_t TargetWidth = 0;
		std::uint32_t TargetHeight = 0;
		struct TargetMaskStorage {
			std::weak_ptr<SpritePassRenderer::Implementation::MutableLifetimeRecord> Lifetime;
			std::array<id<MTLTexture>, 2> Targets{};
			std::array<id<MTLTexture>, 2> Views{};
		};
		std::vector<TargetMaskStorage> TargetMasks;
		std::vector<std::weak_ptr<SpritePassRenderer::Implementation::MutableLifetimeRecord>> VisitedTargets;
		std::shared_ptr<SpriteSharedGpuResources> Resources;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		SpriteMutablePreparationState MutablePreparation{this, OwnerThread, nil};
		SpriteFrameSlot* Slot = nullptr;
		id<MTLCommandBuffer> Commands = nil;
		id<MTLRenderCommandEncoder> Encoder = nil;
		id<MTLTexture> ActiveTarget = nil;
		std::array<id<MTLTexture>, 2> MaskTargets{};
		std::array<id<MTLTexture>, 2> MaskViews{};
		std::size_t MaskDestination = 0;
		std::optional<std::size_t> MaskRead;
		std::optional<scene::MotionIntegerScissor> SavedMaskScissor;
		scene::MotionMaskPhase MaskPhase = scene::MotionMaskPhase::Idle;
		std::uint8_t MaskOperation = 0;
		std::array<float, 2> MaskCoefficients{1, 0};
		std::size_t VertexCount = 0;
		std::size_t IndexCount = 0;
		std::size_t VertexOffset = 0;
		std::size_t IndexOffset = 0;
		std::size_t DrawIndex = 0;
		std::exception_ptr FirstFailure;
		std::optional<scene::MotionIntegerScissor> DeviceScissor;
		bool Active = false;

		void RequireThread() const {
			if (std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("live Metal encoding requires its serialized owner thread");
			}
		}

		SpritePassRenderer::Implementation& RequireRenderer() const {
			RequireThread();
			const auto live = Lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("live Metal encoder renderer has retired");
			}

			return *live->Receiver;
		}

		SpritePassRenderer::Implementation& RequireActive() const {
			RequireThread();
			if (FirstFailure) {
				std::rethrow_exception(FirstFailure);
			}
			if (!Active || Encoder == nil || Commands == nil || Resources->LiveEncodingOwner.load() != this || Slot == nullptr) {
				throw std::runtime_error("live Metal operation requires its actual open encoder");
			}

			return RequireRenderer();
		}

		void SelectTargetMasks(SpritePassRenderer::Implementation& target) {
			const auto found = std::find_if(TargetMasks.begin(), TargetMasks.end(), [&](const auto& masks) {
				return masks.Lifetime.lock() == target.MutableLifetime;
			});
			if (found != TargetMasks.end()) {
				MaskTargets = found->Targets;
				MaskViews = found->Views;
				return;
			}
			const auto pixels = static_cast<std::size_t>(target.Description.Width) * target.Description.Height;
			if (pixels > target.Description.MaxTargetBytes / 2) {
				throw std::runtime_error("Screen mask attachments exceed the actual target allocation bound");
			}
			TargetMaskStorage masks;
			masks.Lifetime = target.MutableLifetime;
			MTLTextureDescriptor* description = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:target.Description.Width height:target.Description.Height mipmapped:NO];
			description.storageMode = MTLStorageModePrivate;
			description.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
			const MTLTextureSwizzleChannels swizzle{MTLTextureSwizzleZero, MTLTextureSwizzleZero, MTLTextureSwizzleZero, MTLTextureSwizzleRed};
			for (std::size_t index = 0; index < masks.Targets.size(); ++index) {
				masks.Targets[index] = [target.Device newTextureWithDescriptor:description];
				id<MTLTexture> texture = masks.Targets[index];
				masks.Views[index] = [texture newTextureViewWithPixelFormat:MTLPixelFormatR8Unorm textureType:MTLTextureType2D levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1) swizzle:swizzle];
				if (texture == nil || masks.Views[index] == nil || texture.hazardTrackingMode != MTLHazardTrackingModeTracked) {
					throw std::runtime_error("Screen requires actual hazard-tracked R8 mask attachments and alpha views");
				}
			}
			const auto vacant = std::find_if(TargetMasks.begin(), TargetMasks.end(), [](const auto& value) {
				return value.Lifetime.expired();
			});
			if (vacant != TargetMasks.end()) {
				*vacant = masks;
			} else {
				if (TargetMasks.size() == 1024) {
					throw std::runtime_error("Screen mask target cache exceeds the actual target registry bound");
				}
				TargetMasks.push_back(masks);
			}
			MaskTargets = masks.Targets;
			MaskViews = masks.Views;
		}

		void SelectRenderTarget(SpritePassRenderer& actual_target, const std::optional<std::array<float, 4>>& clear_color) {
			auto& renderer = RequireActive();
			auto& target = *actual_target.m_Implementation;
			if (target.Resources != Resources || MaskPhase != scene::MotionMaskPhase::Idle || ActiveTarget != FrameTarget || Resources->MutablePreparation.load() != nullptr || Commands != Resources->PendingFrameCommands || !Commands.retainedReferences || (clear_color && std::any_of(clear_color->begin(), clear_color->end(), [](float value) {
				    return !std::isfinite(value);
			    }))) {
				throw std::runtime_error("Screen target switch requires the same real domain/buffer and balanced native mask state");
			}
			const auto visited = std::find_if(VisitedTargets.begin(), VisitedTargets.end(), [&](const auto& weak) {
				return weak.lock() == target.MutableLifetime;
			});
			if (!clear_color && !target.TargetInitialized && visited == VisitedTargets.end()) {
				throw std::runtime_error("Screen target resume requires actual initialized or earlier encoded contents");
			}
			if (visited == VisitedTargets.end() && VisitedTargets.size() == 1024) {
				throw std::runtime_error("Screen target occurrences exceed the bounded actual frame");
			}
			[Encoder endEncoding];
			Encoder = nil;
			if (Resources->TextureMaskEnabled) {
				SelectTargetMasks(target);
			}
			TargetLifetime = target.MutableLifetime;
			TargetOwner = &actual_target;
			FrameTarget = target.Target;
			TargetWidth = target.Description.Width;
			TargetHeight = target.Description.Height;
			MaskDestination = 0;
			MaskRead.reset();
			SavedMaskScissor.reset();
			if (ActiveTarget != FrameTarget) {
				Resources->MutableBindings = {};
			}
			ActiveTarget = FrameTarget;
			DeviceScissor = scene::MotionIntegerScissor{0, 0, static_cast<std::int32_t>(TargetWidth), static_cast<std::int32_t>(TargetHeight)};
			MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
			pass.colorAttachments[0].texture = FrameTarget;
			pass.colorAttachments[0].loadAction = clear_color ? MTLLoadActionClear : MTLLoadActionLoad;
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			if (clear_color) {
				const auto& color = *clear_color;
				pass.colorAttachments[0].clearColor = MTLClearColorMake(color[0], color[1], color[2], color[3]);
			}
			Encoder = [Commands renderCommandEncoderWithDescriptor:pass];
			if (Encoder == nil) {
				throw std::runtime_error("Screen cannot create its real same-buffer target render pass");
			}
			ResetRasterAndViewport(FrameTarget);
			SetScissor(*DeviceScissor);
			if (visited == VisitedTargets.end()) {
				VisitedTargets.push_back(target.MutableLifetime);
			}
			static_cast<void>(renderer);
		}

		void ClearMotionPreparation() noexcept {
			const SpriteMutablePreparationState* expected = &MutablePreparation;
			static_cast<void>(Resources->MutablePreparation.compare_exchange_strong(expected, nullptr));
			MutablePreparation.Commands = nil;
			MutablePreparation.MotionResources = false;
			MutablePreparation.RenderEncoderSuspended = false;
		}

		template <typename Operation>
		auto PrepareMotionResources(Operation operation) {
			auto& renderer = RequireActive();
			if (MaskPhase != scene::MotionMaskPhase::Idle || ActiveTarget != FrameTarget || !DeviceScissor || Resources->MutablePreparation.load() != nullptr || Commands != Resources->PendingFrameCommands || !Commands.retainedReferences) {
				throw std::runtime_error("motion resource preparation requires its exact restored idle-mask render pass and command owner");
			}
			[Encoder endEncoding];
			Encoder = nil;
			MutablePreparation.Commands = Commands;
			MutablePreparation.MotionResources = true;
			MutablePreparation.RenderEncoderSuspended = true;
			Resources->MutablePreparation.store(&MutablePreparation);
			try {
				auto result = operation(renderer);
				ClearMotionPreparation();
				ResumeTarget(FrameTarget, MTLLoadActionLoad, 0);
				return result;
			} catch (...) {
				ClearMotionPreparation();
				throw;
			}
		}

		void RetainFailure() noexcept {
			if (!FirstFailure) {
				FirstFailure = std::current_exception();
			}
		}

		void Abort() {
			RequireThread();
			std::exception_ptr failure;
			try {
				InvokeFrameMetalBoundary([&] {
					if (Encoder != nil) {
						[Encoder endEncoding];
					}
				});
			} catch (...) {
				failure = std::current_exception();
			}
			Encoder = nil;
			const SpriteMutablePreparationState* preparation = &MutablePreparation;
			static_cast<void>(Resources->MutablePreparation.compare_exchange_strong(preparation, nullptr));
			MutablePreparation.Commands = nil;
			MutablePreparation.MotionResources = false;
			MutablePreparation.RenderEncoderSuspended = false;
			if (Resources->LiveEncodingOwner.load() == this) {
				Resources->PendingFrameCommands = nil;
			}
			Commands = nil;
			Slot = nullptr;
			Active = false;
			ActiveTarget = nil;
			MaskPhase = scene::MotionMaskPhase::Idle;
			MaskRead.reset();
			SavedMaskScissor.reset();
			DeviceScissor.reset();
			const void* expected = this;
			static_cast<void>(Resources->LiveEncodingOwner.compare_exchange_strong(expected, nullptr));
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void PrepareTextureMask() {
			auto& state = RequireRenderer();
			state.RequireIdleEncoding();
			if (Active) {
				throw std::runtime_error("texture-mask allocation requires its inactive actual frame owner");
			}
			const auto pixels = static_cast<std::size_t>(state.Description.Width) * state.Description.Height;
			if (pixels > state.Description.MaxTargetBytes / 2) {
				throw std::runtime_error("two real R8 mask targets exceed the target allocation bound");
			}

			@autoreleasepool {
				if (MaskTargets[0] == nil || MaskTargets[1] == nil) {
					std::array<id<MTLTexture>, 2> targets{};
					std::array<id<MTLTexture>, 2> views{};
					MTLTextureDescriptor* description = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:state.Description.Width height:state.Description.Height mipmapped:NO];
					description.storageMode = MTLStorageModePrivate;
					description.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
					const MTLTextureSwizzleChannels swizzle{MTLTextureSwizzleZero, MTLTextureSwizzleZero, MTLTextureSwizzleZero, MTLTextureSwizzleRed};
					for (std::size_t index = 0; index < targets.size(); ++index) {
						targets[index] = [state.Device newTextureWithDescriptor:description];
						id<MTLTexture> target = targets[index];
						views[index] = [target newTextureViewWithPixelFormat:MTLPixelFormatR8Unorm textureType:MTLTextureType2D levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1) swizzle:swizzle];
						if (target == nil || views[index] == nil || target.hazardTrackingMode != MTLHazardTrackingModeTracked) {
							throw std::runtime_error("texture mask requires two real hazard-tracked R8 targets and alpha-red sampled views");
						}
					}
					MaskTargets = std::move(targets);
					MaskViews = std::move(views);
				}
				if (Resources->MaskSampler == nil) {
					MTLSamplerDescriptor* sampler = [[MTLSamplerDescriptor alloc] init];
					sampler.minFilter = MTLSamplerMinMagFilterLinear;
					sampler.magFilter = MTLSamplerMinMagFilterLinear;
					sampler.mipFilter = MTLSamplerMipFilterLinear;
					sampler.sAddressMode = MTLSamplerAddressModeClampToEdge;
					sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
					sampler.rAddressMode = MTLSamplerAddressModeRepeat;
					sampler.normalizedCoordinates = YES;
					sampler.lodMinClamp = 0;
					sampler.lodMaxClamp = 0;
					sampler.maxAnisotropy = 1;
					Resources->MaskSampler = [state.Device newSamplerStateWithDescriptor:sampler];
					if (Resources->MaskSampler == nil) {
						throw std::runtime_error("texture-mask linear/clamp sampler allocation failed");
					}
				}
				for (auto& pipeline : Resources->Pipelines) {
					if (pipeline.MaskStates[0] == nil) {
						PrepareTextureMaskPipeline(*Resources, pipeline);
					}
				}
				Resources->TextureMaskEnabled = true;
				TargetMaskStorage root_masks;
				root_masks.Lifetime = state.MutableLifetime;
				root_masks.Targets = MaskTargets;
				root_masks.Views = MaskViews;
				if (TargetMasks.empty()) {
					TargetMasks.push_back(std::move(root_masks));
				}
			}
		}

		void ResetRasterAndViewport(id<MTLTexture> target) {
			const auto raster = PrepareDefaultGraphicsRenderState().Raster;
			[Encoder setCullMode:DefaultMetalCull(raster.Cull)];
			[Encoder setTriangleFillMode:DefaultMetalFill(raster.Fill)];
			[Encoder setFrontFacingWinding:raster.FrontCounterClockwise ? MTLWindingCounterClockwise : MTLWindingClockwise];
			[Encoder setDepthClipMode:raster.DepthClipEnabled ? MTLDepthClipModeClip : MTLDepthClipModeClamp];
			[Encoder setDepthBias:static_cast<float>(raster.DepthBias) slopeScale:raster.SlopeScaledDepthBias clamp:raster.DepthBiasClamp];
			[Encoder setViewport:MTLViewport{0, 0, static_cast<double>(target.width), static_cast<double>(target.height), 0, 1}];
			[Encoder setFragmentTexture:nil atIndex:0];
			[Encoder setFragmentTexture:nil atIndex:1];
			[Encoder setFragmentTexture:nil atIndex:2];
		}

		void ResumeTarget(id<MTLTexture> target, MTLLoadAction load, float clear) {
			if (Encoder != nil) {
				[Encoder endEncoding];
				Encoder = nil;
			}
			ActiveTarget = target;
			MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
			pass.colorAttachments[0].texture = target;
			pass.colorAttachments[0].loadAction = load;
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(clear, clear, clear, clear);
			Encoder = [Commands renderCommandEncoderWithDescriptor:pass];
			if (Encoder == nil) {
				throw std::runtime_error("texture-mask transition cannot create its real same-command-buffer render encoder");
			}

			ResetRasterAndViewport(target);
			if (!DeviceScissor) {
				throw std::runtime_error("texture-mask target switch lost its actual scissor request");
			}
			SetScissor(*DeviceScissor);
		}

		void ConfigureTextureMask(std::uint8_t operation, float multiplier, float offset) {
			static_cast<void>(RequireActive());
			if (!std::isfinite(multiplier) || !std::isfinite(offset)) {
				throw std::runtime_error("texture-mask shader coefficients must be finite");
			}
			MaskOperation = operation;
			MaskCoefficients = {multiplier, offset};
		}

		void ApplyTextureMask(scene::MotionStencilAction action) {
			static_cast<void>(RequireActive());
			if (!Resources->TextureMaskEnabled || MaskTargets[0] == nil || MaskTargets[1] == nil || Resources->MaskSampler == nil || !DeviceScissor) {
				throw std::runtime_error("texture-mask traversal requires its actual idle-prepared targets and shaders");
			}

			switch (action) {
			case scene::MotionStencilAction::Prepare:
				if (MaskPhase != scene::MotionMaskPhase::Idle && MaskPhase != scene::MotionMaskPhase::Resolved) {
					throw std::runtime_error("texture-mask Prepare requires the actual restored containing target");
				}
				SavedMaskScissor = DeviceScissor;
				MaskRead.reset();
				MaskPhase = scene::MotionMaskPhase::Prepared;
				break;
			case scene::MotionStencilAction::Begin:
				if (!SavedMaskScissor || MaskPhase == scene::MotionMaskPhase::Idle || MaskPhase == scene::MotionMaskPhase::Resolved) {
					throw std::runtime_error("texture-mask Begin requires an actual preceding Prepare");
				}
				if (MaskPhase == scene::MotionMaskPhase::Prepared) {
					MaskRead.reset();
					MaskPhase = scene::MotionMaskPhase::TextureFirst;
				} else {
					MaskRead = MaskDestination;
					MaskDestination = 1 - MaskDestination;
					MaskPhase = scene::MotionMaskPhase::TextureCombine;
				}
				DeviceScissor = SavedMaskScissor;
				ResumeTarget(MaskTargets[MaskDestination], MTLLoadActionClear, 0);
				break;
			case scene::MotionStencilAction::End:
				if (!SavedMaskScissor || MaskPhase == scene::MotionMaskPhase::Idle || MaskPhase == scene::MotionMaskPhase::Resolved) {
					throw std::runtime_error("texture-mask End requires an actual preceding Prepare");
				}
				MaskRead.reset();
				if (MaskPhase == scene::MotionMaskPhase::Prepared) {
					DeviceScissor = SavedMaskScissor;
					ResumeTarget(MaskTargets[MaskDestination], MTLLoadActionClear, 1);
				}
				MaskPhase = scene::MotionMaskPhase::TextureSubtract;
				break;
			case scene::MotionStencilAction::Resolve:
				if (!SavedMaskScissor || MaskPhase < scene::MotionMaskPhase::TextureFirst || MaskPhase > scene::MotionMaskPhase::TextureSubtract) {
					throw std::runtime_error("texture-mask Resolve requires genuine initialized mask rendering");
				}
				DeviceScissor = SavedMaskScissor;
				ResumeTarget(FrameTarget, MTLLoadActionLoad, 0);
				MaskRead = MaskDestination;
				MaskPhase = scene::MotionMaskPhase::Resolved;
				break;
			case scene::MotionStencilAction::Finish:
				if (MaskPhase != scene::MotionMaskPhase::Idle && MaskPhase != scene::MotionMaskPhase::Resolved) {
					throw std::runtime_error("texture-mask Finish requires the actual restored target after Resolve");
				}
				MaskRead.reset();
				SavedMaskScissor.reset();
				MaskPhase = scene::MotionMaskPhase::Idle;
				[Encoder setFragmentTexture:nil atIndex:1];
				break;
			default:
				throw std::runtime_error("texture-mask action is outside its recovered native phases");
			}
		}

		bool WritingMask() const noexcept {
			return MaskPhase >= scene::MotionMaskPhase::TextureFirst && MaskPhase <= scene::MotionMaskPhase::TextureSubtract;
		}

		std::uint32_t ActivePixelShader(SpritePipelineHandle handle, bool has_texture) const {
			const auto& state = RequireActive();
			const auto& pipeline = state.Pipeline(handle).Configuration;
			if (WritingMask()) {
				const auto kind = has_texture ? TextureInterpretation(pipeline.TextureFormat) + 1 : 0;
				return 2042 + (kind * 2 + static_cast<std::uint32_t>(MaskOperation != 0)) * 3 + static_cast<std::uint32_t>(MaskPhase) - 3;
			}
			if (MaskPhase == scene::MotionMaskPhase::Resolved) {
				return pipeline.NativePixelShaderIndex + (UsesCopiedDestination(pipeline.NativePixelShaderIndex) ? 19 : 15);
			}

			return pipeline.NativePixelShaderIndex;
		}

		SpriteTextureHandle CopyCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& rectangle) {
			auto& state = RequireActive();
			const auto description = destination.State();
			if (WritingMask() || ActiveTarget != FrameTarget || Resources->MutablePreparation.load() != nullptr || description.Storage != scene::MutableTextureStorage::RenderTarget || description.Locked || description.HasCompanion || description.SamplerDirty || !description.HasSampler || description.Description.OriginalFormat != 28) {
				throw std::runtime_error("live copied destination requires a distinct actual prepared RGBA render target and restored source");
			}
			const std::array<float, 4> values{rectangle.MinimumX, rectangle.MinimumY, rectangle.MaximumX, rectangle.MaximumY};
			for (const auto value : values) {
				if (!std::isfinite(value) || value < 0 || value != std::trunc(value) || value >= 2147483648.0f) {
					throw std::runtime_error("live copied rectangle requires nonnegative finite integer coordinates");
				}
			}
			if (rectangle.MaximumX <= rectangle.MinimumX || rectangle.MaximumY <= rectangle.MinimumY || rectangle.MaximumX > static_cast<float>(TargetWidth) || rectangle.MaximumY > static_cast<float>(TargetHeight) || rectangle.MaximumX - rectangle.MinimumX > static_cast<float>(description.Description.Width) || rectangle.MaximumY - rectangle.MinimumY > static_cast<float>(description.Description.Height)) {
				throw std::runtime_error("live copied rectangle exceeds its actual source or destination");
			}

			const auto handle = SpriteMutableTextureHandle(destination.Resource());
			const auto& texture = state.Texture(handle);
			id<MTLTexture> copied = texture.SampledTexture();
			if (!texture.Mutable || texture.MutableSampler == nil || copied == nil || copied == ActiveTarget || copied.pixelFormat != MTLPixelFormatRGBA8Unorm || copied.width != description.Description.Width || copied.height != description.Description.Height) {
				throw std::runtime_error("live copied destination does not match its actual resource/domain/extent");
			}

			[Encoder endEncoding];
			Encoder = nil;
			MutablePreparation.Commands = Commands;
			Resources->MutablePreparation.store(&MutablePreparation);
			try {
				id<MTLBlitCommandEncoder> blit = [Commands blitCommandEncoder];
				if (blit == nil) {
					throw std::runtime_error("live copied target cannot create its same-command-buffer blit");
				}
				[blit copyFromTexture:FrameTarget sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(static_cast<std::size_t>(rectangle.MinimumX), static_cast<std::size_t>(rectangle.MinimumY), 0) sourceSize:MTLSizeMake(static_cast<std::size_t>(rectangle.MaximumX - rectangle.MinimumX), static_cast<std::size_t>(rectangle.MaximumY - rectangle.MinimumY), 1) toTexture:copied destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
				[blit endEncoding];
				destination.Bind(2, scene::MutableTextureStage::Pixel);
				if (Resources->MutableBindings[2] != handle) {
					throw std::runtime_error("live copied target did not bind its exact destination owner");
				}
				Resources->MutablePreparation.store(nullptr);
				MutablePreparation.Commands = nil;
				ResumeTarget(FrameTarget, MTLLoadActionLoad, 0);

				return handle;
			} catch (...) {
				Resources->MutablePreparation.store(nullptr);
				MutablePreparation.Commands = nil;
				throw;
			}
		}

		void CopyCaptureCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& rectangle) {
			auto& state = RequireActive();
			const auto description = destination.State();
			if (MaskPhase != scene::MotionMaskPhase::Idle || ActiveTarget != FrameTarget || Resources->MutablePreparation.load() != nullptr || description.Storage != scene::MutableTextureStorage::StagingCompanion || description.Locked || !description.HasCompanion || description.Description.OriginalFormat != 28) {
				throw std::runtime_error("Capture copy requires its actual unlocked staging/companion owner and ordinary current target");
			}
			for (const auto value : {rectangle.MinimumX, rectangle.MinimumY, rectangle.MaximumX, rectangle.MaximumY}) {
				if (!std::isfinite(value) || value < 0 || value != std::trunc(value) || value >= 2147483648.0f) {
					throw std::runtime_error("Capture copy bounds require nonnegative finite integer pixels");
				}
			}
			if (rectangle.MaximumX <= rectangle.MinimumX || rectangle.MaximumY <= rectangle.MinimumY || rectangle.MaximumX > static_cast<float>(TargetWidth) || rectangle.MaximumY > static_cast<float>(TargetHeight) || rectangle.MaximumX - rectangle.MinimumX != static_cast<float>(description.Description.Width) || rectangle.MaximumY - rectangle.MinimumY != static_cast<float>(description.Description.Height)) {
				throw std::runtime_error("Capture copy must exactly cover its clipped staging allocation inside the actual target");
			}
			[Encoder endEncoding];
			Encoder = nil;
			MutablePreparation.Commands = Commands;
			Resources->MutablePreparation.store(&MutablePreparation);
			try {
				id<MTLTexture> staging = SpriteMutableStagingTexture(destination.Resource(), state.Resources.get());
				if (staging == ActiveTarget || staging.pixelFormat != FrameTarget.pixelFormat || staging.width != description.Description.Width || staging.height != description.Description.Height) {
					throw std::runtime_error("Capture physical staging destination differs from its actual source or description");
				}
				id<MTLBlitCommandEncoder> blit = [Commands blitCommandEncoder];
				if (blit == nil) {
					throw std::runtime_error("Capture cannot create its same-buffer current-target blit");
				}
				[blit copyFromTexture:FrameTarget sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(static_cast<std::size_t>(rectangle.MinimumX), static_cast<std::size_t>(rectangle.MinimumY), 0) sourceSize:MTLSizeMake(description.Description.Width, description.Description.Height, 1) toTexture:staging destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
				[blit endEncoding];
				destination.NotifyGpuWrite();
				Resources->MutablePreparation.store(nullptr);
				MutablePreparation.Commands = nil;
				ResumeTarget(FrameTarget, MTLLoadActionLoad, 0);
			} catch (...) {
				Resources->MutablePreparation.store(nullptr);
				MutablePreparation.Commands = nil;
				throw;
			}
		}

		void Begin(const std::array<float, 4>& clear_color, SpritePassLoadAction load_action) {
			auto& state = RequireRenderer();
			state.RequireIdleEncoding();
			if (Active || state.NextSubmission == std::numeric_limits<std::uint64_t>::max() || (load_action != SpritePassLoadAction::Clear && load_action != SpritePassLoadAction::Preserve) || (load_action == SpritePassLoadAction::Preserve && !state.TargetInitialized)) {
				throw std::runtime_error("live Metal Begin requires an idle real slot, available identity and initialized preserve target");
			}
			if (std::any_of(clear_color.begin(), clear_color.end(), [](float value) {
				    return !std::isfinite(value);
			    })) {
				throw std::runtime_error("live Metal Begin requires finite clear values");
			}
			auto& free = state.AcquireUploadSlot("previous live Metal submission");
			const void* expected = nullptr;
			if (!Resources->LiveEncodingOwner.compare_exchange_strong(expected, this)) {
				throw std::runtime_error("live Metal Begin requires exclusive shared graphics encoding");
			}
			Active = true;
			Slot = &free;
			FirstFailure = {};
			VertexCount = 0;
			IndexCount = 0;
			VertexOffset = 0;
			IndexOffset = 0;
			DrawIndex = 0;
			ActiveTarget = state.Target;
			FrameTarget = state.Target;
			TargetOwner = &RendererOwner;
			TargetLifetime = state.MutableLifetime;
			TargetWidth = state.Description.Width;
			TargetHeight = state.Description.Height;
			VisitedTargets.clear();
			VisitedTargets.push_back(state.MutableLifetime);
			MaskPhase = scene::MotionMaskPhase::Idle;
			MaskRead.reset();
			SavedMaskScissor.reset();
			try {
				@autoreleasepool {
					if (Resources->TextureMaskEnabled) {
						SelectTargetMasks(state);
					}
					MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
					pass.colorAttachments[0].texture = state.Target;
					pass.colorAttachments[0].loadAction = load_action == SpritePassLoadAction::Preserve ? MTLLoadActionLoad : MTLLoadActionClear;
					pass.colorAttachments[0].storeAction = MTLStoreActionStore;
					pass.colorAttachments[0].clearColor = MTLClearColorMake(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
					Commands = [state.Queue commandBuffer];
					if (Commands == nil || !Commands.retainedReferences) {
						throw std::runtime_error("live Metal Begin requires a real resource-retaining command buffer");
					}
					Resources->PendingFrameCommands = Commands;
					Encoder = [Commands renderCommandEncoderWithDescriptor:pass];
					if (Encoder == nil) {
						throw std::runtime_error("live Metal Begin cannot create its actual render encoder");
					}
					const auto raster = PrepareDefaultGraphicsRenderState().Raster;
					[Encoder setCullMode:DefaultMetalCull(raster.Cull)];
					[Encoder setTriangleFillMode:DefaultMetalFill(raster.Fill)];
					[Encoder setFrontFacingWinding:raster.FrontCounterClockwise ? MTLWindingCounterClockwise : MTLWindingClockwise];
					[Encoder setDepthClipMode:raster.DepthClipEnabled ? MTLDepthClipModeClip : MTLDepthClipModeClamp];
					[Encoder setDepthBias:static_cast<float>(raster.DepthBias) slopeScale:raster.SlopeScaledDepthBias clamp:raster.DepthBiasClamp];
					[Encoder setScissorRect:MTLScissorRect{0, 0, state.Description.Width, state.Description.Height}];
					DeviceScissor = scene::MotionIntegerScissor{0, 0, static_cast<std::int32_t>(state.Description.Width), static_cast<std::int32_t>(state.Description.Height)};
				}
			} catch (...) {
				RetainFailure();
				const auto first = FirstFailure;
				try {
					Abort();
				} catch (...) {
				}
				std::rethrow_exception(first);
			}
		}

		SpriteTextureHandle PrepareMutableTexture(scene::MutableTextureOwner& owner, std::uint32_t slot, scene::MutableTextureStage stage) {
			auto& state = RequireActive();
			const auto value = owner.State();
			const bool cpu_shadow = value.Storage == scene::MutableTextureStorage::CpuShadow && !value.HasCompanion;
			const bool staging_companion = value.Storage == scene::MutableTextureStorage::StagingCompanion && value.HasCompanion && owner.Companion() != nullptr;
			if (Resources->MutablePreparation.load() != nullptr || !DeviceScissor || (!cpu_shadow && !staging_companion) || value.Locked || stage != scene::MutableTextureStage::Pixel || (slot != 0 && slot != 2)) {
				throw std::runtime_error("live mutable preparation requires one unlocked actual CPU-shadow or staging-companion pixel texture and exact encoder scope");
			}
			// Storage4 writes its nonsampled main; native Bind copies it to this exact sampled companion.
			const auto texture = SpriteMutableTextureHandle(staging_companion ? owner.Companion()->Resource() : owner.Resource());
			static_cast<void>(state.MutableTexture(texture));
			const auto scissor = *DeviceScissor;
			[Encoder endEncoding];
			Encoder = nil;
			MutablePreparation.Commands = Commands;
			Resources->MutablePreparation.store(&MutablePreparation);
			try {
				owner.Bind(slot, stage);
				if (Resources->MutableBindings[slot] != texture) {
					throw std::runtime_error("live mutable preparation did not bind its exact actual texture resource");
				}
				static_cast<void>(state.MutableTexture(texture));
				@autoreleasepool {
					MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
					pass.colorAttachments[0].texture = ActiveTarget;
					pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
					pass.colorAttachments[0].storeAction = MTLStoreActionStore;
					Encoder = [Commands renderCommandEncoderWithDescriptor:pass];
					if (Encoder == nil) {
						throw std::runtime_error("live mutable preparation cannot resume its actual stored render target");
					}
					const auto raster = PrepareDefaultGraphicsRenderState().Raster;
					[Encoder setCullMode:DefaultMetalCull(raster.Cull)];
					[Encoder setTriangleFillMode:DefaultMetalFill(raster.Fill)];
					[Encoder setFrontFacingWinding:raster.FrontCounterClockwise ? MTLWindingCounterClockwise : MTLWindingClockwise];
					[Encoder setDepthClipMode:raster.DepthClipEnabled ? MTLDepthClipModeClip : MTLDepthClipModeClamp];
					[Encoder setDepthBias:static_cast<float>(raster.DepthBias) slopeScale:raster.SlopeScaledDepthBias clamp:raster.DepthBiasClamp];
					SetScissor(scissor);
				}
				Resources->MutablePreparation.store(nullptr);
				MutablePreparation.Commands = nil;
				return texture;
			} catch (...) {
				Resources->MutablePreparation.store(nullptr);
				MutablePreparation.Commands = nil;
				throw;
			}
		}

		void SetScissor(scene::MotionIntegerScissor rectangle) {
			static_cast<void>(RequireActive());
			const auto width = static_cast<std::int32_t>(TargetWidth);
			const auto height = static_cast<std::int32_t>(TargetHeight);
			const auto left = std::clamp(rectangle.MinimumX, 0, width);
			const auto top = std::clamp(rectangle.MinimumY, 0, height);
			const auto right = std::clamp(rectangle.MaximumX, left, width);
			const auto bottom = std::clamp(rectangle.MaximumY, top, height);
			[Encoder setScissorRect:MTLScissorRect{static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top), static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)}];
			DeviceScissor = rectangle;
		}

		void Append(const SpritePassDraw& draw, const std::array<float, 16>& current_transform) {
			auto& state = RequireActive();
			if (DrawIndex == state.Description.MaxDraws) {
				throw std::runtime_error("live Metal draws exceed the prepared upload slot capacity");
			}
			const auto& transform = draw.TransformRegisters ? *draw.TransformRegisters : current_transform;
			if (std::any_of(transform.begin(), transform.end(), [](float value) {
				    return !std::isfinite(value);
			    })) {
				throw std::runtime_error("live Metal draw requires finite actual transform registers");
			}
			const auto vertices = VertexCount;
			const auto indices = IndexCount;
			if (draw.HostInputs != nullptr && (!draw.Vertices.empty() || !draw.Indices.empty() || !draw.RawTextureVertices.empty() || !draw.FillRectangleVertices.empty() || draw.CapturedOpacityMap != nullptr || draw.MovieAlpha != nullptr)) {
				throw std::runtime_error("Metal sprite host inputs require their exclusive actual geometry path");
			}
			if (draw.Scissor) {
				throw std::runtime_error("live Metal drawing requires the actual encoder scissor, without a per-draw override");
			}

			if ((draw.Topology != scene::MotionDrawTopology::TriangleStrip && draw.Topology != scene::MotionDrawTopology::TriangleList) || (draw.Topology == scene::MotionDrawTopology::TriangleList && DrawIndexCount(draw) % 3 != 0)) {
				throw std::runtime_error("Metal sprite topology or triangle-list index count is invalid");
			}
			const auto& pipeline_configuration = state.Pipeline(draw.Pipeline).Configuration;
			const bool fill_rectangle = pipeline_configuration.NativeVertexShaderIndex == 3;
			const bool raw_texture = pipeline_configuration.NativeVertexShaderIndex == 4;
			const bool opacity_map = pipeline_configuration.NativePixelShaderIndex == 2040;
			const bool movie_alpha = pipeline_configuration.NativePixelShaderIndex == 2041;
			if (movie_alpha && MaskPhase != scene::MotionMaskPhase::Idle) {
				throw std::runtime_error("movie alpha mask coupling awaits original evidence");
			}
			if (pipeline_configuration.NativeVertexShaderIndex == 10 && (draw.HostInputs == nullptr || draw.HostInputs->Layout() != scene::MotionVertexLayout::XyzUv || draw.Topology != scene::MotionDrawTopology::TriangleList)) {
				throw std::runtime_error("ScreenProjection requires genuine indexed format8 host geometry");
			}
			if (pipeline_configuration.NativeVertexShaderIndex == 1 && draw.HostInputs != nullptr && draw.HostInputs->Layout() != scene::MotionVertexLayout::XyColorUv) {
				throw std::runtime_error("ordinary sprite shader requires actual format4 host geometry");
			}
			if (draw.Texture.Owner != nullptr && state.Texture(draw.Texture).SampledTexture() == ActiveTarget) {
				throw std::runtime_error("Screen target cannot sample its own active color attachment");
			}
			if (movie_alpha) {
				RequireMovieAlphaDraw(draw);
			} else if (opacity_map) {
				RequireCapturedOpacityMapDraw(draw);
			} else if (fill_rectangle) {
				RequireFillRectangleDraw(draw);
			} else if (raw_texture) {
				if (draw.HostInputs != nullptr || !draw.Vertices.empty() || !draw.FillRectangleVertices.empty() || draw.CapturedOpacityMap != nullptr || draw.MovieAlpha != nullptr || draw.RawTextureVertices.size() != 4 || !draw.Indices.empty() || draw.Topology != scene::MotionDrawTopology::TriangleStrip) {
					throw std::runtime_error("Metal RawTex requires four XYUV vertices and a nonindexed triangle strip");
				}
			} else if (draw.MovieAlpha != nullptr || draw.CapturedOpacityMap != nullptr || !draw.FillRectangleVertices.empty() || !draw.RawTextureVertices.empty() || DrawVertexCount(draw) < 3 || DrawIndexCount(draw) < 3) {
				throw std::runtime_error("Metal sprite requires indexed XY/color/UV geometry");
			}
			if (DrawVertexCount(draw) > state.Description.MaxVertices - vertices || DrawIndexCount(draw) > state.Description.MaxIndices - indices) {
				throw std::runtime_error("Metal sprite frame geometry exceeds its allocation limit");
			}

			const bool has_texture = draw.Texture.Owner != nullptr;
			if (fill_rectangle && MaskPhase != scene::MotionMaskPhase::Idle) {
				throw std::runtime_error("FillRect mask-phase shader semantics remain outside the recovered ordinary path");
			}
			if (!has_texture && ((!WritingMask() && !fill_rectangle) || draw.Texture != SpriteTextureHandle{})) {
				throw std::runtime_error("absent texture is admitted only as the actual native color-only mask source");
			}
			if (has_texture && state.Texture(draw.Texture).Interpretation != TextureInterpretation(state.Pipeline(draw.Pipeline).Configuration.TextureFormat)) {
				throw std::runtime_error("Metal sprite texture interpretation differs from its prepared shader");
			}

			if (has_texture && state.Texture(draw.Texture).Mutable && state.Texture(draw.Texture).MutableSampler == nil) {
				throw std::runtime_error("Metal mutable draw requires the owner sampler to be bound");
			}

			if (movie_alpha && (!state.Texture(draw.Texture).Mutable || state.Texture(draw.Texture).OriginalFormat != 88)) {
				throw std::runtime_error("movie alpha requires original RGB32 mutable storage");
			}
			if (opacity_map) {
				const auto& map = state.Texture(draw.OpacityMapTexture);
				const auto& source = state.Texture(draw.Texture);
				if (MaskPhase != scene::MotionMaskPhase::Idle || !source.Mutable || source.OriginalFormat != 28 || map.Mutable || map.RenderTarget || map.SampledTexture() == ActiveTarget || static_cast<std::uint32_t>(pipeline_configuration.Sampler.AddressU) != draw.CapturedOpacityMap->AddressU || static_cast<std::uint32_t>(pipeline_configuration.Sampler.AddressV) != draw.CapturedOpacityMap->AddressV) {
					throw std::runtime_error("Captured opacity map requires idle mask, actual Capture companion, supported raw-red Image texture and exact map sampler");
				}
			}

			if (!WritingMask() && UsesCopiedDestination(pipeline_configuration.NativePixelShaderIndex)) {
				const auto& copied = state.Texture(draw.CopiedDestination);
				if (!copied.Mutable || copied.OriginalFormat != 28 || copied.MutableSampler == nil || !FiniteCopiedUvAffine(draw.CopiedUvAffine)) {
					throw std::runtime_error(pipeline_configuration.NativePixelShaderIndex == 1205 ? "Metal PS1205 requires a live bound RGBA copied owner and finite pixel constants" : "Metal copied skip-color program requires a live bound RGBA copied owner and finite pixel constants");
				}
			}

			RequireDrawIndices(draw);

			auto& slot = *Slot;
			auto* constants = static_cast<std::byte*>(slot.Constants.contents) + DrawIndex * kDrawConstantBytes;
			std::memcpy(constants, transform.data(), transform.size() * sizeof(float));
			if (draw.MovieAlpha != nullptr) {
				UploadMovieAlpha(slot.Vertices, VertexOffset, slot.Indices, IndexOffset, *draw.MovieAlpha);
			} else if (draw.CapturedOpacityMap != nullptr) {
				UploadCapturedOpacityMap(slot.Vertices, VertexOffset, slot.Indices, IndexOffset, *draw.CapturedOpacityMap);
			} else if (!draw.FillRectangleVertices.empty()) {
				UploadFillRectangleVertices(slot.Vertices, VertexOffset, draw.FillRectangleVertices);
				const auto color = scene::UnpackMotionShaderColor(draw.FillRectangleColor);
				std::memcpy(constants + 64, color.data(), color.size() * sizeof(float));
			} else if (!draw.RawTextureVertices.empty()) {
				UploadRawTextureVertices(slot.Vertices, VertexOffset, draw.RawTextureVertices);
				auto color = scene::UnpackMotionShaderColor(draw.RawTextureColor);
				std::memcpy(constants + 64, color.data(), color.size() * sizeof(float));
			} else if (draw.HostInputs != nullptr) {
				if (draw.HostInputs->Layout() == scene::MotionVertexLayout::XyzUv) {
					const auto color = scene::UnpackMotionShaderColor(draw.RawTextureColor);
					std::memcpy(constants + 64, color.data(), color.size() * sizeof(float));
				}
				const auto vertex_bytes = draw.HostInputs->VertexBytes();
				const auto index_bytes = draw.HostInputs->IndexBytes();
				std::memcpy(static_cast<std::byte*>(slot.Vertices.contents) + VertexOffset, vertex_bytes.data(), vertex_bytes.size());
				std::memcpy(static_cast<std::byte*>(slot.Indices.contents) + IndexOffset, index_bytes.data(), index_bytes.size());
			} else {
				UploadSpriteVertices(slot.Vertices, VertexOffset, draw.Vertices);
				std::memcpy(static_cast<std::uint8_t*>(slot.Indices.contents) + IndexOffset, draw.Indices.data(), draw.Indices.size_bytes());
			}
			std::memcpy(constants + 128, draw.CopiedUvAffine.data(), draw.CopiedUvAffine.size() * sizeof(float));
			if (draw.CapturedOpacityMap != nullptr) {
				const std::array<float, 2> pixel{draw.CapturedOpacityMap->Phase, draw.CapturedOpacityMap->InverseVague};
				std::memcpy(constants + 128, pixel.data(), sizeof(pixel));
			}
			const auto& pipeline = state.Pipeline(draw.Pipeline);
			id<MTLRenderPipelineState> selected = pipeline.State;
			if (WritingMask()) {
				const auto index = static_cast<std::uint32_t>(MaskPhase) - 3 + (MaskOperation != 0 ? 3 : 0);
				selected = has_texture ? pipeline.MaskStates[index] : pipeline.ColorMaskStates[index];
				const std::array<float, 8> pixel{MaskCoefficients[0], MaskCoefficients[1], 0, 0, 1.0f / TargetWidth, 1.0f / TargetHeight, 0, 0};
				std::memcpy(constants + 128, pixel.data(), sizeof(pixel));
			} else if (MaskPhase == scene::MotionMaskPhase::Resolved) {
				selected = pipeline.ResolvedMaskState;
				const std::array<float, 4> screen{1.0f / TargetWidth, 1.0f / TargetHeight, 0, 0};
				std::memcpy(constants + 128, screen.data(), sizeof(screen));
				if (UsesCopiedDestination(pipeline_configuration.NativePixelShaderIndex)) {
					std::memcpy(constants + 144, draw.CopiedUvAffine.data(), sizeof(draw.CopiedUvAffine));
				}
			}
			if (selected == nil) {
				throw std::runtime_error("active native mask program has no actual startup-prepared Metal pipeline");
			}
			[Encoder setRenderPipelineState:selected];
			[Encoder setVertexBuffer:slot.Vertices offset:VertexOffset atIndex:0];
			[Encoder setVertexBuffer:slot.Constants offset:DrawIndex * kDrawConstantBytes atIndex:1];
			id<MTLTexture> sampled_texture = has_texture ? state.Texture(draw.Texture).SampledTexture() : nil;
			id<MTLSamplerState> sampler = has_texture && state.Texture(draw.Texture).Mutable ? state.Texture(draw.Texture).MutableSampler : pipeline.Sampler;
			[Encoder setFragmentTexture:sampled_texture atIndex:0];
			[Encoder setFragmentSamplerState:sampler atIndex:0];
			if (draw.CapturedOpacityMap != nullptr) {
				[Encoder setFragmentTexture:state.Texture(draw.OpacityMapTexture).SampledTexture() atIndex:1];
				[Encoder setFragmentSamplerState:pipeline.Sampler atIndex:1];
				[Encoder setFragmentBuffer:slot.Constants offset:DrawIndex * kDrawConstantBytes + 128 atIndex:0];
			}
			if (WritingMask() || MaskPhase == scene::MotionMaskPhase::Resolved) {
				if (MaskRead && *MaskRead == MaskDestination && WritingMask()) {
					throw std::runtime_error("native mask source aliases its actual render destination");
				}
				id<MTLTexture> mask_view = MaskRead ? MaskViews[*MaskRead] : nil;
				[Encoder setFragmentTexture:mask_view atIndex:1];
				[Encoder setFragmentSamplerState:Resources->MaskSampler atIndex:1];
				[Encoder setFragmentBuffer:slot.Constants offset:DrawIndex * kDrawConstantBytes + 128 atIndex:0];
			}
			if (!WritingMask() && UsesCopiedDestination(pipeline.Configuration.NativePixelShaderIndex)) {
				const auto& copied = state.Texture(draw.CopiedDestination);
				[Encoder setFragmentTexture:copied.SampledTexture() atIndex:2];
				[Encoder setFragmentSamplerState:copied.MutableSampler atIndex:2];
				[Encoder setFragmentBuffer:slot.Constants offset:DrawIndex * kDrawConstantBytes + 128 atIndex:0];
			}
			const auto topology = draw.Topology == scene::MotionDrawTopology::TriangleList ? MTLPrimitiveTypeTriangle : MTLPrimitiveTypeTriangleStrip;
			if (!draw.RawTextureVertices.empty() || !draw.FillRectangleVertices.empty()) {
				[Encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
			} else {
				const auto index_type = draw.MovieAlpha != nullptr || draw.CapturedOpacityMap != nullptr || (draw.HostInputs != nullptr && draw.HostInputs->IndexWordBytes() == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
				[Encoder drawIndexedPrimitives:topology indexCount:DrawIndexCount(draw) indexType:index_type indexBuffer:slot.Indices indexBufferOffset:IndexOffset];
			}
			VertexCount += DrawVertexCount(draw);
			IndexCount += DrawIndexCount(draw);
			VertexOffset += DrawVertexBytes(draw);
			IndexOffset = AdvanceDrawIndexOffset(IndexOffset, draw);
			++DrawIndex;
		}

		std::uint64_t End() {
			auto& state = RequireActive();
			if (MaskPhase != scene::MotionMaskPhase::Idle || ActiveTarget != FrameTarget || FrameTarget != state.Target || TargetOwner != &RendererOwner) {
				throw std::runtime_error("live frame End requires actual mask Finish and restored original target");
			}
			[Encoder endEncoding];
			Encoder = nil;
			state.ArmSlotCompletion(*Slot, Commands);
			Slot->Commands = Commands;
			try {
				InvokeFrameMetalBoundary([&] {
					[Commands commit];
				});
			} catch (...) {
				state.Resources->EncodingFaulted = true;
				throw;
			}
			const auto identity = state.NextSubmission++;
			Slot->Submission = identity;
			state.LatestSubmission = identity;
			state.TargetInitialized = true;
			for (const auto& weak : VisitedTargets) {
				if (const auto target = weak.lock(); target && target->Receiver != nullptr) {
					target->Receiver->TargetInitialized = true;
					target->Receiver->LastTargetCommands = Commands;
					target->Receiver->LastTargetProducer = state.MutableLifetime;
					target->Receiver->LastTargetSubmission = identity;
				}
			}
			Resources->PendingFrameCommands = nil;
			Commands = nil;
			Slot = nullptr;
			Active = false;
			DeviceScissor.reset();
			const void* expected = this;
			if (!Resources->LiveEncodingOwner.compare_exchange_strong(expected, nullptr)) {
				state.Resources->EncodingFaulted = true;
				throw std::runtime_error("live Metal completed with a changed shared encoding owner");
			}

			return identity;
		}
	};

	SpriteFrameEncoder::SpriteFrameEncoder(SpritePassRenderer& actual_renderer) : m_Implementation(std::make_unique<Implementation>(actual_renderer)) {
	}

	SpriteFrameEncoder::~SpriteFrameEncoder() {
		try {
			m_Implementation->Abort();
		} catch (...) {
			std::terminate();
		}
	}

	void SpriteFrameEncoder::PrepareTextureMask() {
		InvokeFrameMetalBoundary([&] {
			m_Implementation->PrepareTextureMask();
		});
	}

	void SpriteFrameEncoder::ConfigureTextureMask(std::uint8_t operation, float multiplier, float offset) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			state.ConfigureTextureMask(operation, multiplier, offset);
		} catch (...) {
			state.RetainFailure();
			throw;
		}
	}

	void SpriteFrameEncoder::ApplyTextureMask(scene::MotionStencilAction action) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			InvokeFrameMetalBoundary([&] {
				state.ApplyTextureMask(action);
			});
		} catch (...) {
			state.RetainFailure();
			throw;
		}
	}

	scene::MotionMaskPhase SpriteFrameEncoder::TextureMaskPhase() const {
		static_cast<void>(m_Implementation->RequireActive());

		return m_Implementation->MaskPhase;
	}

	std::uint32_t SpriteFrameEncoder::ActivePixelShader(SpritePipelineHandle pipeline, bool has_texture) const {
		return m_Implementation->ActivePixelShader(pipeline, has_texture);
	}

	SpriteTextureHandle SpriteFrameEncoder::CopyCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& rectangle) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			return InvokeFrameMetalBoundary([&] {
				return state.CopyCurrentTarget(destination, rectangle);
			});
		} catch (...) {
			state.RetainFailure();
			throw;
		}
	}

	std::unique_ptr<scene::MutableTextureOwner> SpriteFrameEncoder::PrepareCaptureTexture(scene::MutableTextureBackend& backend, const scene::MutableTextureSpec& spec) {
		auto& state = *m_Implementation;
		static_cast<void>(state.RequireActive());
		if (spec.Storage != scene::MutableTextureStorage::StagingCompanion || spec.Description.Width == 0 || spec.Description.Height == 0 || spec.Description.Width > 16384 || spec.Description.Height > 16384 || spec.Description.OriginalFormat != 28 || spec.Description.MipLevels != 1 || spec.Description.SampleCount != 1 || !SpriteMutableBackendAllowsMotionPreparation(backend, state.Resources.get())) {
			throw std::runtime_error("Capture allocation requires its exact bounded current-format staging/companion spec and backend");
		}
		try {
			return InvokeFrameMetalBoundary([&] {
				return state.PrepareMotionResources([&](auto&) {
					return std::make_unique<scene::MutableTextureOwner>(backend, spec);
				});
			});
		} catch (...) {
			state.ClearMotionPreparation();
			state.RetainFailure();
			throw;
		}
	}

	std::unique_ptr<scene::MutableTextureOwner> SpriteFrameEncoder::PrepareMovieTexture(scene::MutableTextureBackend& backend, std::uint32_t width, std::uint32_t height) {
		auto& state = *m_Implementation;
		static_cast<void>(state.RequireActive());
		if (width == 0 || height == 0 || width > 16384 || height > 16384 || width % 16 != 0 || height % 16 != 0 || !SpriteMutableBackendAllowsMotionPreparation(backend, state.Resources.get())) {
			throw std::runtime_error("movie allocation requires its bounded native rounded RGB32 extent and exact backend");
		}
		try {
			return InvokeFrameMetalBoundary([&] {
				return state.PrepareMotionResources([&](auto&) {
					scene::MutableTextureSpec spec;
					spec.Storage = scene::MutableTextureStorage::CpuShadow;
					spec.Description.Width = width;
					spec.Description.Height = height;
					spec.Description.OriginalFormat = 88;

					return std::make_unique<scene::MutableTextureOwner>(backend, spec);
				});
			});
		} catch (...) {
			state.ClearMotionPreparation();
			state.RetainFailure();
			throw;
		}
	}

	void SpriteFrameEncoder::CopyCaptureCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& rectangle) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			InvokeFrameMetalBoundary([&] {
				state.CopyCaptureCurrentTarget(destination, rectangle);
			});
		} catch (...) {
			state.RetainFailure();
			throw;
		}
	}

	const SpritePassRenderer& SpriteFrameEncoder::CurrentRenderTarget() const {
		auto& state = *m_Implementation;
		static_cast<void>(state.RequireActive());
		const auto target = state.TargetLifetime.lock();
		if (!target || target->Receiver == nullptr || state.TargetOwner == nullptr || target->Receiver->Target != state.FrameTarget) {
			throw std::runtime_error("live Screen target owner has retired or changed physical identity");
		}
		return *state.TargetOwner;
	}

	void SpriteFrameEncoder::SelectRenderTarget(SpritePassRenderer& actual_target, const std::optional<std::array<float, 4>>& clear_color) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			InvokeFrameMetalBoundary([&] {
				state.SelectRenderTarget(actual_target, clear_color);
			});
		} catch (...) {
			state.RetainFailure();
			throw;
		}
	}

	void SpriteFrameEncoder::PrepareMotionPipelines(std::span<const scene::SpritePipeline> pipelines, std::span<SpritePipelineHandle> output) {
		auto& state = *m_Implementation;
		static_cast<void>(state.RequireActive());
		if (pipelines.empty() || pipelines.size() != output.size() || pipelines.size() > 30) {
			throw std::runtime_error("motion pipeline preparation requires one bounded exact typed output table");
		}
		try {
			InvokeFrameMetalBoundary([&] {
				static_cast<void>(state.PrepareMotionResources([&](auto&) {
					const auto live = state.Lifetime.lock();
					if (!live || live->Receiver == nullptr) {
						throw std::runtime_error("motion pipeline preparation lost its actual renderer");
					}
					for (std::size_t index = 0; index < pipelines.size(); ++index) {
						// This public operation now admits only this exact private typed scope in addition to idle setup.
						output[index] = state.RendererOwner.PreparePipeline(pipelines[index]);
					}
					return true;
				}));
			});
		} catch (...) {
			state.ClearMotionPreparation();
			state.RetainFailure();
			throw;
		}
	}

	SpritePreparedCopiedTarget SpriteFrameEncoder::PrepareMotionCopiedTarget(scene::MutableTextureBackend& backend, std::uint32_t width, std::uint32_t height) {
		auto& state = *m_Implementation;
		static_cast<void>(state.RequireActive());
		if (width == 0 || height == 0 || width > 16384 || height > 16384 || !SpriteMutableBackendAllowsMotionPreparation(backend, state.Resources.get())) {
			throw std::runtime_error("motion copied allocation requires exact positive dimensions and this actual backend domain");
		}
		try {
			return InvokeFrameMetalBoundary([&] {
				return state.PrepareMotionResources([&](auto&) {
					scene::MutableTextureSpec spec;
					spec.Storage = scene::MutableTextureStorage::RenderTarget;
					spec.Description.Width = width;
					spec.Description.Height = height;
					spec.Description.OriginalFormat = 28;
					SpritePreparedCopiedTarget result;
					result.Owner = std::make_unique<scene::MutableTextureOwner>(backend, spec);
					result.Owner->SetFiltering(0, 0);
					result.Owner->SetAddress(0, 0);
					result.Owner->Bind(2, scene::MutableTextureStage::Pixel);
					result.Handle = SpriteMutableTextureHandle(result.Owner->Resource());
					return result;
				});
			});
		} catch (...) {
			state.ClearMotionPreparation();
			state.RetainFailure();
			throw;
		}
	}

	void SpriteFrameEncoder::Begin(const std::array<float, 4>& clear_color, SpritePassLoadAction load_action) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			InvokeFrameMetalBoundary([&] {
				state.Begin(clear_color, load_action);
			});
		} catch (...) {
			state.RetainFailure();
			const auto first = state.FirstFailure;
			try {
				state.Abort();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void SpriteFrameEncoder::SetScissor(scene::MotionIntegerScissor rectangle) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			InvokeFrameMetalBoundary([&] {
				state.SetScissor(rectangle);
			});
		} catch (...) {
			state.RetainFailure();
			throw;
		}
	}

	scene::MotionIntegerScissor SpriteFrameEncoder::SubmittedDeviceScissor() const {
		const auto& state = *m_Implementation;
		static_cast<void>(state.RequireActive());
		if (!state.DeviceScissor) {
			throw std::runtime_error("live Metal scissor query lacks its actual successful device request");
		}

		return *state.DeviceScissor;
	}

	SpriteTextureHandle SpriteFrameEncoder::PrepareMutableTexture(scene::MutableTextureOwner& owner, std::uint32_t slot, scene::MutableTextureStage stage) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			return InvokeFrameMetalBoundary([&] {
				return state.PrepareMutableTexture(owner, slot, stage);
			});
		} catch (...) {
			const SpriteMutablePreparationState* preparation = &state.MutablePreparation;
			static_cast<void>(state.Resources->MutablePreparation.compare_exchange_strong(preparation, nullptr));
			state.MutablePreparation.Commands = nil;
			state.RetainFailure();
			throw;
		}
	}

	void SpriteFrameEncoder::Append(const SpritePassDraw& draw, const std::array<float, 16>& current_transform) {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			InvokeFrameMetalBoundary([&] {
				state.Append(draw, current_transform);
			});
		} catch (...) {
			state.RetainFailure();
			throw;
		}
	}

	std::uint64_t SpriteFrameEncoder::End() {
		auto& state = *m_Implementation;
		state.RequireThread();
		try {
			return InvokeFrameMetalBoundary([&] {
				return state.End();
			});
		} catch (...) {
			state.RetainFailure();
			throw;
		}
	}

	void SpriteFrameEncoder::Abort() {
		m_Implementation->Abort();
	}

	bool SpriteFrameEncoder::IsActive() const {
		m_Implementation->RequireThread();

		return m_Implementation->Active;
	}

	static scene::MotionShaderTexture PreparedTextureChannels(std::uint32_t interpretation) {
		switch (interpretation) {
		case 0:
			return scene::MotionShaderTexture::Rgba;
		case 1:
			return scene::MotionShaderTexture::Alpha;
		case 2:
			return scene::MotionShaderTexture::Luminance;
		default:
			throw std::runtime_error("prepared sprite texture has an unsupported actual channel interpretation");
		}
	}

	scene::SpritePipeline SpriteFrameEncoder::PreparedPipeline(SpritePipelineHandle actual_pipeline) const {
		const auto& state = m_Implementation->RequireActive();

		return state.Pipeline(actual_pipeline).Configuration;
	}

	SpritePreparedTextureMetadata SpriteFrameEncoder::PreparedTexture(SpriteTextureHandle actual_texture) const {
		const auto& state = m_Implementation->RequireActive();
		const auto& texture = state.Texture(actual_texture);

		return InvokeFrameMetalBoundary([&] {
			id<MTLTexture> sampled = texture.SampledTexture();
			if (sampled == nil || sampled.width == 0 || sampled.height == 0 || sampled.width > kMaximumSpriteDimension || sampled.height > kMaximumSpriteDimension) {
				throw std::runtime_error("prepared sprite metadata requires the actual supported sampled texture extent");
			}
			SpritePreparedTextureMetadata result;
			result.Channels = PreparedTextureChannels(texture.Interpretation);
			result.Width = static_cast<std::uint32_t>(sampled.width);
			result.Height = static_cast<std::uint32_t>(sampled.height);
			result.Mutable = texture.Mutable;
			result.RenderTarget = texture.RenderTarget;
			if (texture.Mutable || texture.RenderTarget) {
				result.OriginalDxgiFormat = texture.OriginalFormat;
			} else if (sampled.pixelFormat == MTLPixelFormatBC7_RGBAUnorm) {
				result.OriginalDxgiFormat = 98;
			}

			return result;
		});
	}

} // namespace sgre::render
