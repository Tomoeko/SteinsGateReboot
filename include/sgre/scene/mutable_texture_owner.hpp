#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace sgre::scene {
	enum class MutableTextureStorage : std::uint32_t {
		GpuOnly = 0,
		DynamicWrite = 1,
		CpuShadow = 2,
		Staging = 3,
		StagingCompanion = 4,
		RenderTarget = 5,
		DepthStencil = 6
	};
	enum class MutableTextureUsage : std::uint32_t {
		Default = 0,
		Dynamic = 2,
		Staging = 3
	};
	enum class MutableTextureMapMode : std::uint32_t {
		Read = 1,
		Write = 3,
		Discard = 4
	};
	enum class MutableTextureStage : std::uint32_t {
		Vertex = 0,
		Pixel = 1
	};
	struct MutableTextureDescription {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t MipLevels = 1;
		std::uint32_t OriginalFormat = 28;
		std::uint32_t SampleCount = 1;
		MutableTextureUsage Usage = MutableTextureUsage::Default;
		bool ShaderResource = true;
		bool RenderTarget = false;
		bool DepthStencil = false;
		bool CpuRead = false;
		bool CpuWrite = false;
		bool operator==(const MutableTextureDescription&) const = default;
	};
	struct MutableTextureSpec {
		MutableTextureStorage Storage = MutableTextureStorage::CpuShadow;
		MutableTextureDescription Description;
	};
	struct MutableTextureInitialData {
		// The source owner keeps these bytes alive through synchronous creation.
		std::span<const std::byte> Bytes;
		std::uint32_t RowPitch = 0;
	};
	struct MutableTextureViewDescription {
		std::uint32_t OriginalFormat = 28;
		std::uint32_t MipLevels = 1;
		bool Multisampled = false;
		bool operator==(const MutableTextureViewDescription&) const = default;
	};
	struct MutableTextureSamplerDescription {
		std::uint32_t FilterWord = 0;
		std::uint32_t AddressU = 3;
		std::uint32_t AddressV = 3;
		std::uint32_t AddressW = 1;
		float LodBias = 0;
		std::uint32_t MaximumAnisotropy = 2;
		std::uint32_t Comparison = 1;
		std::array<float, 4> BorderColor{};
		float MinimumLod = -std::numeric_limits<float>::max();
		float MaximumLod = std::numeric_limits<float>::max();
		bool operator==(const MutableTextureSamplerDescription&) const = default;
	};
	struct MutableTextureMapping {
		// The texture/backend allocation owns the mapped bytes until explicit Unmap or owner destruction.
		std::span<std::byte> Bytes;
		std::uint32_t RowPitch = 0;
		bool Succeeded = false;
	};
	class MutableTextureResource {
	public:
		virtual ~MutableTextureResource() = default;
		[[nodiscard]] virtual MutableTextureDescription Description() const noexcept = 0;
	};
	class MutableTextureView {
	public:
		virtual ~MutableTextureView() = default;
		[[nodiscard]] virtual MutableTextureViewDescription Description() const noexcept = 0;
	};
	class MutableTextureSampler {
	public:
		virtual ~MutableTextureSampler() = default;
		[[nodiscard]] virtual MutableTextureSamplerDescription Description() const noexcept = 0;
	};
	// Successful mappings expose a complete bounded base-level byte range; releases are nonthrowing.
	class MutableTextureBackend {
	public:
		virtual ~MutableTextureBackend() = default;
		[[nodiscard]] virtual std::unique_ptr<MutableTextureResource> CreateTexture(const MutableTextureDescription& description, const MutableTextureInitialData& initial) = 0;
		[[nodiscard]] virtual std::unique_ptr<MutableTextureView> CreateView(MutableTextureResource& resource, const MutableTextureViewDescription& description) = 0;
		[[nodiscard]] virtual std::unique_ptr<MutableTextureSampler> CreateSampler(const MutableTextureSamplerDescription& description) = 0;
		[[nodiscard]] virtual MutableTextureMapping Map(MutableTextureResource& resource, MutableTextureMapMode mode) = 0;
		virtual void Unmap(MutableTextureResource& resource) = 0;
		virtual void Upload(MutableTextureResource& resource, std::span<const std::byte> bytes, std::uint32_t row_pitch) = 0;
		virtual void Copy(MutableTextureResource& destination, MutableTextureResource& source) = 0;
		virtual void Bind(MutableTextureView* view, MutableTextureSampler* sampler, std::uint32_t slot, MutableTextureStage stage) = 0;
	};
	struct MutableTextureLimits {
		std::size_t MaximumShadowBytes = 64 * 1024 * 1024;
		std::uint32_t MaximumExtent = 16384;
		std::uint32_t MaximumMipLevels = 32;
		std::uint32_t MaximumSampleCount = 32;
	};
	struct MutableTextureState {
		MutableTextureStorage Storage = MutableTextureStorage::GpuOnly;
		MutableTextureDescription Description;
		MutableTextureSamplerDescription DesiredSampler;
		std::uint32_t CachedRowPitch = 0;
		std::size_t ShadowBytes = 0;
		bool Mappable = false;
		bool Locked = false;
		bool Dirty = false;
		bool SamplerDirty = true;
		bool HasCompanion = false;
		bool HasView = false;
		bool HasSampler = false;
	};
	class MutableTextureOwner {
	public:
		// Backend lifetime exceeds this exclusive owner and its optional companion.
		MutableTextureOwner(MutableTextureBackend& backend, const MutableTextureSpec& spec, const MutableTextureInitialData& initial = {}, const MutableTextureLimits& limits = {});
		~MutableTextureOwner();
		MutableTextureOwner(const MutableTextureOwner&) = delete;
		MutableTextureOwner& operator=(const MutableTextureOwner&) = delete;
		MutableTextureOwner(MutableTextureOwner&&) = delete;
		MutableTextureOwner& operator=(MutableTextureOwner&&) = delete;
		void SetFiltering(std::uint32_t minimum, std::uint32_t maximum);
		void SetAddress(std::uint32_t horizontal, std::uint32_t vertical);
		[[nodiscard]] std::uint32_t RowPitch();
		// Preserve suppresses dirty marking; native device mapping still uses Write or Discard.
		[[nodiscard]] MutableTextureMapping Map(bool preserve = false);
		void Unmap();
		// Native Capture marks Storage4 dirty only after a real GPU copy has been recorded into its staging main.
		void NotifyGpuWrite();
		void Bind(std::uint32_t slot, MutableTextureStage stage);
		[[nodiscard]] MutableTextureState State() const noexcept;
		// Identity only: a frame receiver must bind through this texture's actual enclosing backend.
		[[nodiscard]] bool UsesBackend(const MutableTextureBackend& actual_backend) const noexcept;
		[[nodiscard]] const MutableTextureOwner* Companion() const noexcept;
		// Borrowed platform resource; this exclusive owner must outlive every operation using it.
		[[nodiscard]] MutableTextureResource& Resource() noexcept;
		[[nodiscard]] const MutableTextureResource& Resource() const noexcept;

	private:
		MutableTextureBackend& m_Backend;
		MutableTextureStorage m_Storage;
		MutableTextureDescription m_Description;
		MutableTextureSamplerDescription m_DesiredSampler;
		std::unique_ptr<MutableTextureResource> m_Texture;
		std::unique_ptr<MutableTextureView> m_View;
		std::unique_ptr<MutableTextureSampler> m_Sampler;
		std::unique_ptr<MutableTextureOwner> m_Companion;
		std::vector<std::byte> m_Shadow;
		std::uint32_t m_RowPitch = 0;
		bool m_Mappable = false;
		bool m_Locked = false;
		bool m_Dirty = false;
		bool m_SamplerDirty = true;
	};
} // namespace sgre::scene
