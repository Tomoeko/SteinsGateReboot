#include "mutable_texture_backend.hpp"

#include "metal_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::render {
	static constexpr std::size_t kMutableUploadSlots = 3;
	static constexpr std::size_t kRegionCopySlots = 3;
	static constexpr std::uint32_t kMaximumMutableExtent = 16384;

	static MTLPixelFormat MutableMetalFormat(std::uint32_t format) {
		if (format == 28) {
			return MTLPixelFormatRGBA8Unorm;
		}
		if (format == 87 || format == 88) {
			return MTLPixelFormatBGRA8Unorm;
		}

		throw std::runtime_error("Metal mutable texture requires original RGBA8/BGRA8/BGRX8 format");
	}
	static bool IsMutableStaging(const scene::MutableTextureDescription& description) {
		return description.Usage == scene::MutableTextureUsage::Staging && !description.ShaderResource && !description.RenderTarget && !description.DepthStencil && description.CpuRead && description.CpuWrite && description.MipLevels == 1 && description.SampleCount == 1;
	}

	static MTLSamplerAddressMode MutableMetalAddress(std::uint32_t address) {
		if (address == 1) {
			return MTLSamplerAddressModeRepeat;
		}
		if (address == 2) {
			return MTLSamplerAddressModeMirrorRepeat;
		}
		if (address == 3) {
			return MTLSamplerAddressModeClampToEdge;
		}

		throw std::runtime_error("Metal mutable sampler address is outside the recovered domain");
	}
	struct MutableUploadSlot {
		id<MTLBuffer> Bytes = nil;
		id<MTLCommandBuffer> Commands = nil;
	};
	class MetalMutableTexture;
	struct MutableUploadLifetime {
		MetalMutableTexture* Texture = nullptr;
	};
	class MetalMutableTexture final : public scene::MutableTextureResource {
	public:
		MetalMutableTexture(std::shared_ptr<SpriteMutableTextureContext> context, const scene::MutableTextureDescription& description) : Context(std::move(context)), Value(description) {
			Lifetime->Texture = this;
		}
		~MetalMutableTexture() override {
			Lifetime->Texture = nullptr;
			if (Handle.Owner != nullptr) {
				Context->Retire(Handle);
			}
		}
		scene::MutableTextureDescription Description() const noexcept override {
			return Value;
		}
		std::shared_ptr<SpriteMutableTextureContext> Context;
		scene::MutableTextureDescription Value;
		SpriteTextureHandle Handle;
		id<MTLTexture> Texture = nil;
		std::array<MutableUploadSlot, kMutableUploadSlots> Slots;
		MutableUploadSlot* LastUpload = nullptr;
		id<MTLCommandBuffer> LastCompanionCopy = nil;
		bool CompanionCopyInvalid = false;
		std::shared_ptr<MutableUploadLifetime> Lifetime = std::make_shared<MutableUploadLifetime>();
		std::size_t RowBytes = 0;
	};
	class MetalMutableView final : public scene::MutableTextureView {
	public:
		// Does not keep the portable resource alive after its exclusive owner retires.
		std::weak_ptr<MutableUploadLifetime> UploadLifetime;
		scene::MutableTextureViewDescription Description() const noexcept override {
			return Value;
		}
		std::shared_ptr<SpriteMutableTextureContext> Context;
		scene::MutableTextureViewDescription Value;
		SpriteTextureHandle Handle;
		id<MTLTexture> Texture = nil;
	};
	class MetalMutableSampler final : public scene::MutableTextureSampler {
	public:
		scene::MutableTextureSamplerDescription Description() const noexcept override {
			return Value;
		}
		std::shared_ptr<SpriteMutableTextureContext> Context;
		scene::MutableTextureSamplerDescription Value;
		id<MTLSamplerState> Sampler = nil;
	};
	class MetalCopiedTargetView final : public scene::MotionCopiedTargetView {
	public:
		id<MTLTexture> Texture = nil;
	};

	static void RequireMutableContext(const SpriteMutableTextureContext& context) {
		if (!context.Available()) {
			throw std::runtime_error("Metal mutable texture renderer has closed");
		}
	}
	static void RequireMutableBindingContext(const SpriteMutableTextureContext& context) {
		if (!context.BindingAvailable()) {
			throw std::runtime_error("Metal mutable binding requires its idle renderer or exact frame preparation owner");
		}
	}
	static MetalMutableTexture& RequireMutableTexture(scene::MutableTextureResource& resource, const std::shared_ptr<SpriteMutableTextureContext>& context, bool binding = false) {
		if (binding) {
			RequireMutableBindingContext(*context);
		} else {
			RequireMutableContext(*context);
		}
		auto* texture = dynamic_cast<MetalMutableTexture*>(&resource);
		if (texture == nullptr || texture->Context != context) {
			throw std::runtime_error("Metal mutable texture belongs to another backend");
		}

		return *texture;
	}
	static bool UploadSlotAvailable(const MutableUploadSlot& slot, const SpriteMutableTextureContext& context) {
		return slot.Commands == nil || slot.Commands.status == MTLCommandBufferStatusCompleted || slot.Commands.status == MTLCommandBufferStatusError || (slot.Commands.status == MTLCommandBufferStatusNotEnqueued && !context.IsPendingFrameUpload(slot.Commands));
	}
	static void UploadMutableBytes(MetalMutableTexture& texture, std::span<const std::byte> bytes, std::uint32_t row_pitch) {
		RequireMutableBindingContext(*texture.Context);
		if (texture.RowBytes == 0) {
			throw std::runtime_error("Metal mutable render target has no CPU upload storage");
		}
		const auto row_bytes = std::size_t{texture.Value.Width} * 4;
		const auto required = std::uint64_t{row_pitch} * (texture.Value.Height - 1) + row_bytes;
		if (row_pitch < row_bytes || required > bytes.size()) {
			throw std::runtime_error("Metal mutable upload is truncated or its pitch is too small");
		}

		const auto found = std::find_if(texture.Slots.begin(), texture.Slots.end(), [&](const auto& slot) {
			return UploadSlotAvailable(slot, *texture.Context);
		});
		if (found == texture.Slots.end()) {
			throw std::runtime_error("Metal mutable upload queue is full");
		}
		if (found->Commands.status == MTLCommandBufferStatusError) {
			throw MetalOperationError("previous Metal mutable upload", found->Commands.error);
		}

		auto* destination = static_cast<std::byte*>(found->Bytes.contents);
		for (std::size_t row = 0; row < texture.Value.Height; ++row) {
			std::memcpy(destination + row * texture.RowBytes, bytes.data() + row * row_pitch, row_bytes);
		}

		found->Commands = texture.Context->EncodeUpload(found->Bytes, texture.RowBytes, texture.Texture, texture.Value.Width, texture.Value.Height);
		texture.LastUpload = &*found;
	}

	static void RestoreAbortedMutableUpload(MetalMutableTexture& texture) {
		// Cancelled frames never committed their blit, although portable Bind consumed the dirty bit.
		// Replay the latest actual staging bytes only when the next genuine bind needs them.
		auto* latest = texture.LastUpload;
		if (latest != nullptr && latest->Commands.status == MTLCommandBufferStatusNotEnqueued && !texture.Context->IsPendingFrameUpload(latest->Commands)) {
			latest->Commands = texture.Context->EncodeUpload(latest->Bytes, texture.RowBytes, texture.Texture, texture.Value.Width, texture.Value.Height);
		}
	}

	class MetalMutableTextureBackend final : public scene::MutableTextureBackend, public scene::MotionCopiedTargetBackend, public scene::MotionRegionCopyBackend {
	public:
		explicit MetalMutableTextureBackend(SpriteMutableTextureContext context) : m_Context(std::make_shared<SpriteMutableTextureContext>(std::move(context))) {
			if (m_Context->ResourceDomain == nullptr || m_Context->Device == nil || m_Context->Queue == nil || m_Context->MaximumBytes == 0 || !m_Context->Available || !m_Context->BindingAvailable || !m_Context->IsPendingFrameUpload || !m_Context->EncodeUpload || !m_Context->Register || !m_Context->Retire || !m_Context->SetView || !m_Context->Bind) {
				throw std::runtime_error("Metal mutable backend context is incomplete");
			}
		}
		bool AllowsMotionPreparation(const void* domain) const noexcept {
			return domain != nullptr && m_Context->ResourceDomain == domain && m_Context->DirectMotionPreparation;
		}

		std::unique_ptr<scene::MutableTextureResource> CreateTexture(const scene::MutableTextureDescription& description, const scene::MutableTextureInitialData& initial) override {
			RequireMutableContext(*m_Context);
			const auto format = MutableMetalFormat(description.OriginalFormat);
			const bool staging = IsMutableStaging(description);
			const bool sampled = description.Usage == scene::MutableTextureUsage::Default && description.ShaderResource && !description.DepthStencil && !description.CpuRead && !description.CpuWrite;
			if (description.Width == 0 || description.Height == 0 || description.Width > kMaximumMutableExtent || description.Height > kMaximumMutableExtent || description.MipLevels != 1 || description.SampleCount != 1 || (!staging && !sampled)) {
				throw std::runtime_error("Metal mutable texture requires a default sampled single-mip or exact GPU-blit staging description");
			}

			const auto row_bytes = std::size_t{description.Width} * 4;
			const auto upload_pitch = (description.RenderTarget || staging) ? 0u : ((row_bytes + 255) / 256) * 256;
			if ((description.RenderTarget || staging) && !initial.Bytes.empty()) {
				throw std::runtime_error("Metal copied render target/staging initial data is outside the recovered GPU-copy domain");
			}
			if (description.Height > m_Context->MaximumBytes / (row_bytes + kMutableUploadSlots * upload_pitch)) {
				throw std::runtime_error("Metal mutable texture/upload buffers exceed the allocation limit");
			}
			const auto allocation = (row_bytes + kMutableUploadSlots * upload_pitch) * description.Height;
			auto result = std::make_unique<MetalMutableTexture>(m_Context, description);
			result->RowBytes = upload_pitch;
			result->Handle = m_Context->Register(allocation, description.OriginalFormat);

			@autoreleasepool {
				MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:description.Width height:description.Height mipmapped:NO];
				descriptor.storageMode = MTLStorageModePrivate;
				// Staging keeps its original portable CpuRead/Write descriptor, but ordinary Capture only uses GPU blits.
				descriptor.usage = staging ? MTLTextureUsageUnknown : MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
				if (description.RenderTarget) {
					descriptor.usage |= MTLTextureUsageRenderTarget;
				}
				result->Texture = [m_Context->Device newTextureWithDescriptor:descriptor];
				if (result->Texture == nil) {
					throw std::runtime_error("Metal mutable texture allocation failed");
				}

				for (auto& slot : result->Slots) {
					if (description.RenderTarget || staging) {
						break;
					}
					slot.Bytes = [m_Context->Device newBufferWithLength:upload_pitch * description.Height options:MTLResourceStorageModeShared];
					if (slot.Bytes == nil) {
						throw std::runtime_error("Metal mutable upload buffer allocation failed");
					}
				}
			}

			if (!initial.Bytes.empty()) {
				UploadMutableBytes(*result, initial.Bytes, initial.RowPitch);
			}

			return result;
		}
		std::unique_ptr<scene::MutableTextureView> CreateView(scene::MutableTextureResource& resource, const scene::MutableTextureViewDescription& description) override {
			auto& texture = RequireMutableTexture(resource, m_Context);
			if (!texture.Value.ShaderResource || texture.Value.Usage != scene::MutableTextureUsage::Default || description.OriginalFormat != texture.Value.OriginalFormat || description.MipLevels != 1 || description.Multisampled) {
				throw std::runtime_error("Metal mutable view differs from the supported texture layout");
			}

			auto result = std::make_unique<MetalMutableView>();
			result->Context = m_Context;
			result->Value = description;
			result->Handle = texture.Handle;
			result->UploadLifetime = texture.Lifetime;
			@autoreleasepool {
				auto swizzle = MTLTextureSwizzleChannelsDefault;
				if (description.OriginalFormat == 88) {
					swizzle.alpha = MTLTextureSwizzleOne;
				}
				result->Texture = [texture.Texture newTextureViewWithPixelFormat:MutableMetalFormat(description.OriginalFormat) textureType:MTLTextureType2D levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1) swizzle:swizzle];
				if (result->Texture == nil) {
					throw std::runtime_error("Metal mutable sampled view allocation failed");
				}
			}
			m_Context->SetView(texture.Handle, result->Texture);

			return result;
		}
		std::unique_ptr<scene::MotionCopiedTargetView> CreateRenderTargetView(scene::MutableTextureResource& resource) override {
			auto& texture = RequireMutableTexture(resource, m_Context);
			if (!texture.Value.RenderTarget) {
				throw std::runtime_error("Metal copied target view requires the actual render-target texture");
			}

			auto result = std::make_unique<MetalCopiedTargetView>();
			// A distinct scoped RTV owner retains the same physical texture after MTexture retirement.
			result->Texture = texture.Texture;
			return result;
		}

		void CopyFromCurrentTarget(scene::MutableTextureResource& resource, const scene::MotionBounds& rectangle) override {
			auto& texture = RequireMutableTexture(resource, m_Context);
			if (!m_Context->RequireCopySource || !m_Context->CopyFromTarget) {
				throw std::runtime_error("Metal copied region has no actual source target service");
			}
			const auto source = m_Context->RequireCopySource();
			if (!texture.Value.RenderTarget || texture.Value.OriginalFormat != source.OriginalFormat) {
				throw std::runtime_error("Metal copied region requires a same-format render-target resource from this backend");
			}
			const std::array<float, 4> values{rectangle.MinimumX, rectangle.MinimumY, rectangle.MaximumX, rectangle.MaximumY};
			for (const auto value : values) {
				if (!std::isfinite(value) || value < 0 || value != std::trunc(value) || value >= 2147483648.0f) {
					throw std::runtime_error("Metal copied region coordinates must be nonnegative bounded integers");
				}
			}
			if (rectangle.MaximumX <= rectangle.MinimumX || rectangle.MaximumY <= rectangle.MinimumY || rectangle.MaximumX > static_cast<float>(source.Width) || rectangle.MaximumY > static_cast<float>(source.Height) || rectangle.MaximumX - rectangle.MinimumX > static_cast<float>(texture.Value.Width) || rectangle.MaximumY - rectangle.MinimumY > static_cast<float>(texture.Value.Height)) {
				throw std::runtime_error("Metal copied region exceeds its source or destination bounds");
			}

			const auto slot = std::find_if(m_CopyCommands.begin(), m_CopyCommands.end(), [](const auto& commands) {
				return commands == nil || commands.status == MTLCommandBufferStatusCompleted || commands.status == MTLCommandBufferStatusError;
			});
			if (slot == m_CopyCommands.end()) {
				throw std::runtime_error("Metal copied region command queue is full");
			}
			if ((*slot).status == MTLCommandBufferStatusError) {
				throw MetalOperationError("previous Metal copied region", (*slot).error);
			}

			*slot = m_Context->CopyFromTarget(texture.Texture, rectangle, source);
		}

		std::unique_ptr<scene::MutableTextureSampler> CreateSampler(const scene::MutableTextureSamplerDescription& description) override {
			RequireMutableBindingContext(*m_Context);
			if ((description.FilterWord & ~21u) != 0 || description.AddressW != 1 || description.LodBias != 0 || description.MaximumAnisotropy != 2 || description.Comparison != 1 || description.BorderColor != std::array<float, 4>{} || description.MinimumLod != -std::numeric_limits<float>::max() || description.MaximumLod != std::numeric_limits<float>::max()) {
				throw std::runtime_error("Metal mutable sampler differs from the recovered single-mip domain");
			}

			auto result = std::make_unique<MetalMutableSampler>();
			result->Context = m_Context;
			result->Value = description;
			@autoreleasepool {
				MTLSamplerDescriptor* sampler = [[MTLSamplerDescriptor alloc] init];
				sampler.minFilter = (description.FilterWord & 16) != 0 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
				sampler.magFilter = (description.FilterWord & 4) != 0 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
				sampler.mipFilter = (description.FilterWord & 1) != 0 ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
				sampler.sAddressMode = MutableMetalAddress(description.AddressU);
				sampler.tAddressMode = MutableMetalAddress(description.AddressV);
				sampler.rAddressMode = MTLSamplerAddressModeRepeat;
				sampler.normalizedCoordinates = YES;
				sampler.lodMinClamp = 0;
				sampler.lodMaxClamp = 0;
				sampler.maxAnisotropy = 1;
				result->Sampler = [m_Context->Device newSamplerStateWithDescriptor:sampler];
				if (result->Sampler == nil) {
					throw std::runtime_error("Metal mutable sampler allocation failed");
				}
			}

			return result;
		}
		scene::MutableTextureMapping Map(scene::MutableTextureResource& resource, scene::MutableTextureMapMode) override {
			static_cast<void>(RequireMutableTexture(resource, m_Context));
			throw std::runtime_error("Metal mutable backend uses the portable CPU shadow; hardware maps are unsupported");
		}
		void Unmap(scene::MutableTextureResource& resource) override {
			static_cast<void>(RequireMutableTexture(resource, m_Context));
			throw std::runtime_error("Metal mutable backend hardware unmap is unsupported");
		}
		void Upload(scene::MutableTextureResource& resource, std::span<const std::byte> bytes, std::uint32_t row_pitch) override {
			UploadMutableBytes(RequireMutableTexture(resource, m_Context, true), bytes, row_pitch);
		}
		void Copy(scene::MutableTextureResource& destination, scene::MutableTextureResource& source) override {
			auto& target = RequireMutableTexture(destination, m_Context, true);
			auto& origin = RequireMutableTexture(source, m_Context, true);
			if (!m_Context->EncodeTextureCopy) {
				throw std::runtime_error("Metal staging companion requires its actual GPU texture-copy service");
			}
			if (&target == &origin || !IsMutableStaging(origin.Value) || target.Value.Usage != scene::MutableTextureUsage::Default || !target.Value.ShaderResource || target.Value.RenderTarget || target.Value.DepthStencil || target.Value.CpuRead || target.Value.CpuWrite || target.Value.Width != origin.Value.Width || target.Value.Height != origin.Value.Height || target.Value.OriginalFormat != origin.Value.OriginalFormat || target.Value.MipLevels != 1 || target.Value.SampleCount != 1 || target.Texture == nil || origin.Texture == nil || target.Texture.device != m_Context->Device || origin.Texture.device != m_Context->Device) {
				throw std::runtime_error("Metal staging companion copy requires same-backend full-size same-format actual resources");
			}
			// Failed/aborted GPU copies have no CPU staging bytes that can honestly be replayed at Bind.
			target.CompanionCopyInvalid = true;
			const auto commands = m_Context->EncodeTextureCopy(target.Texture, origin.Texture, origin.Value.Width, origin.Value.Height);
			if (commands == nil || !commands.retainedReferences || commands.commandQueue.device != m_Context->Device || (commands.status == MTLCommandBufferStatusNotEnqueued && !m_Context->IsPendingFrameUpload(commands))) {
				throw std::runtime_error("Metal staging companion copy did not return its actual retained submitted or pending frame commands");
			}
			if (commands.status == MTLCommandBufferStatusError) {
				throw MetalOperationError("Metal staging companion copy", commands.error);
			}
			target.LastCompanionCopy = commands;
			target.CompanionCopyInvalid = false;
		}
		void Bind(scene::MutableTextureView* view, scene::MutableTextureSampler* sampler, std::uint32_t slot, scene::MutableTextureStage stage) override {
			RequireMutableBindingContext(*m_Context);
			auto* texture_view = dynamic_cast<MetalMutableView*>(view);
			auto* texture_sampler = dynamic_cast<MetalMutableSampler*>(sampler);
			if (texture_view == nullptr || texture_sampler == nullptr || texture_view->Context != m_Context || texture_sampler->Context != m_Context) {
				throw std::runtime_error("Metal mutable binding requires a live view/sampler from the same backend");
			}
			const auto upload = texture_view->UploadLifetime.lock();
			if (!upload || upload->Texture == nullptr) {
				throw std::runtime_error("Metal mutable bind requires its actual still-live texture upload owner");
			}
			auto& texture = *upload->Texture;
			if (texture.CompanionCopyInvalid || (texture.LastCompanionCopy != nil && texture.LastCompanionCopy.status == MTLCommandBufferStatusNotEnqueued && !m_Context->IsPendingFrameUpload(texture.LastCompanionCopy))) {
				texture.CompanionCopyInvalid = true;
				throw std::runtime_error("Metal staging companion bind requires a fresh real GPU copy after an aborted or failed copy");
			}
			if (texture.LastCompanionCopy.status == MTLCommandBufferStatusError) {
				throw MetalOperationError("previous Metal staging companion copy", texture.LastCompanionCopy.error);
			}
			m_Context->Bind(texture_view->Handle, texture_sampler->Sampler, slot, stage);
			RestoreAbortedMutableUpload(*upload->Texture);
		}

	private:
		std::shared_ptr<SpriteMutableTextureContext> m_Context;
		std::array<id<MTLCommandBuffer>, kRegionCopySlots> m_CopyCommands{};
	};
	bool SpriteMutableBackendAllowsMotionPreparation(const scene::MutableTextureBackend& backend, const void* domain) noexcept {
		const auto* actual = dynamic_cast<const MetalMutableTextureBackend*>(&backend);
		return actual != nullptr && actual->AllowsMotionPreparation(domain);
	}

	SpriteTextureHandle SpriteMutableTextureHandle(const scene::MutableTextureResource& resource) {
		const auto* texture = dynamic_cast<const MetalMutableTexture*>(&resource);
		if (texture == nullptr || texture->Handle.Owner == nullptr) {
			throw std::runtime_error("Metal mutable resource handle requires its actual platform texture owner");
		}

		return texture->Handle;
	}

	id<MTLTexture> SpriteMutableStagingTexture(const scene::MutableTextureResource& resource, const void* actual_domain) {
		const auto* texture = dynamic_cast<const MetalMutableTexture*>(&resource);
		if (texture == nullptr || actual_domain == nullptr || texture->Context->ResourceDomain != actual_domain || texture->Handle.Owner != actual_domain || texture->Texture == nil || texture->Texture.device != texture->Context->Device || !IsMutableStaging(texture->Value)) {
			throw std::runtime_error("Metal staging recipient requires its actual live nonsampled texture in this resource domain");
		}
		RequireMutableBindingContext(*texture->Context);
		static_cast<void>(MutableMetalFormat(texture->Value.OriginalFormat));
		return texture->Texture;
	}

	std::unique_ptr<scene::MutableTextureBackend> CreateSpriteMutableTextureBackend(SpriteMutableTextureContext context) {
		return std::make_unique<MetalMutableTextureBackend>(std::move(context));
	}
} // namespace sgre::render
