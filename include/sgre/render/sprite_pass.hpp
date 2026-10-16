#pragma once

#include "sgre/render/color_pass.hpp"
#include "sgre/scene/captured_opacity_map.hpp"
#include "sgre/scene/movie_packet.hpp"
#include "sgre/scene/mutable_texture_owner.hpp"
#include "sgre/scene/raw_texture_packet.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace sgre::render {
	class SpriteHostInputs;

	struct SpriteTextureHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const SpriteTextureHandle&) const = default;
	};

	struct SpritePipelineHandle {
		std::uint32_t Index = 0;
		const void* Owner = nullptr;
		bool operator==(const SpritePipelineHandle&) const = default;
	};

	struct SpritePassDescription {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::size_t MaxVertices = 65536;
		std::size_t MaxIndices = 262144;
		std::size_t MaxDraws = 4096;
		std::size_t MaxTextures = 1024;
		std::size_t MaxPipelines = 1024;
		// Zero opts into the selected device recommendation at creation; Description returns the resolved positive ceiling.
		std::size_t MaxTextureBytes = 256 * 1024 * 1024;
		std::size_t MaxTargetBytes = 64 * 1024 * 1024;
	};

	enum class SpritePassLoadAction {
		Clear,
		Preserve
	};

	struct SpritePassScissor {
		// Integer physical target pixels; a zero extent suppresses this draw.
		std::uint32_t X = 0;
		std::uint32_t Y = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	struct SpritePassDraw {
		SpriteTextureHandle Texture;
		SpritePipelineHandle Pipeline;
		// No value selects the entire render target, including after a clipped preceding draw.
		std::optional<SpritePassScissor> Scissor;
		// Snapshot the actual shader transform while this draw's camera/model scope is current.
		// No value uses the containing submission's transform for the existing single-model API.
		std::optional<std::array<float, 16>> TransformRegisters;
		// Geometry borrows caller-owned frame data; submission immediately copies it into a free upload slot.
		// Genuine native format4 heap inputs are exclusive with the caller-array input forms.
		const SpriteHostInputs* HostInputs = nullptr;
		std::span<const scene::MotionSpriteVertex> Vertices;
		std::span<const std::uint32_t> Indices;
		scene::MotionDrawTopology Topology = scene::MotionDrawTopology::TriangleStrip;
		// VS4 consumes the separate XY/UV packet and its packed ARGB uniform, without an index buffer.
		std::span<const scene::RawTextureVertex> RawTextureVertices;
		std::uint32_t RawTextureColor = 0xffffffff;
		// Native VS3 consumes format0 XY and a uniform, with no texture or index buffer.
		std::span<const resource::MotionPoint> FillRectangleVertices;
		std::uint32_t FillRectangleColor = 0xffffffff;
		// Native format5/VS2/PS2040: the packet is borrowed only through synchronous Submit/Append.
		const scene::CapturedOpacityMapDrawPacket* CapturedOpacityMap = nullptr;
		SpriteTextureHandle OpacityMapTexture;
		// Native format5/VS2/PS2041 samples color and alpha from this same texture.
		const scene::MovieAlphaDrawPacket* MovieAlpha = nullptr;
		// Copied programs PS1205/PS1129/PS1131 snapshot the borrowed slot-two texture and pixel cb0 affine.
		SpriteTextureHandle CopiedDestination;
		std::array<float, 4> CopiedUvAffine{};
	};

	class SpritePassRenderer {
	public:
		SpritePassRenderer(SpritePassDescription description, const std::filesystem::path& shader_source);
		// Targets in one actual graphics owner share device, ordered queue, textures and prepared pipelines.
		SpritePassRenderer(SpritePassDescription description, const SpritePassRenderer& resource_owner);
		~SpritePassRenderer();
		SpritePassRenderer(const SpritePassRenderer&) = delete;
		SpritePassRenderer& operator=(const SpritePassRenderer&) = delete;
		SpritePassRenderer(SpritePassRenderer&&) = delete;
		SpritePassRenderer& operator=(SpritePassRenderer&&) = delete;

		// Serialize the shared rendering context. Resource upload and pipeline preparation occur outside ordinary frames.
		[[nodiscard]] SpriteTextureHandle UploadTexture(resource::PixelFormat original_format, const resource::RgbaImage& image);
		[[nodiscard]] bool SupportsBc7Textures() const;
		// The original linear-UNORM blocks are copied before this call returns; the input owner may then retire.
		[[nodiscard]] SpriteTextureHandle UploadBc7Texture(const resource::Bc7TextureView& image);
		void ReleaseTexture(SpriteTextureHandle texture);
		// Idle registration borrows this target; its owner encloses every consumer and submitted buffers retain GPU use.
		[[nodiscard]] SpriteTextureHandle SampledTarget();
		// Desired sampler words belong to the actual sampled target, shared by its Projection and NativeTextureRef wrappers.
		[[nodiscard]] std::array<std::uint32_t, 2> SampledTargetFiltering(SpriteTextureHandle actual_target) const;
		void SetSampledTargetFiltering(SpriteTextureHandle actual_target, std::uint32_t minimum, std::uint32_t maximum);
		// Default sampled or copied render-target single-mip RGBA/BGRA/BGRX resources; the backend outlives its owners.
		// Copied target/copy capabilities share this same concrete backend; current target copies require RGBA8.
		// Ordinary shaders consume slot zero; copied PS1205/PS1129/PS1131 also consume pixel slot two.
		[[nodiscard]] std::unique_ptr<scene::MutableTextureBackend> CreateMutableTextureBackend();
		[[nodiscard]] SpriteTextureHandle MutableTextureBinding(std::uint32_t slot, scene::MutableTextureStage stage) const;
		[[nodiscard]] SpritePipelineHandle PreparePipeline(const scene::SpritePipeline& pipeline);
		[[nodiscard]] bool CanSubmit() const;
		[[nodiscard]] std::uint64_t Submit(std::span<const SpritePassDraw> draws, const std::array<float, 16>& projection_registers, const std::array<float, 4>& clear_color = {}, SpritePassLoadAction load_action = SpritePassLoadAction::Clear);
		[[nodiscard]] ColorPassPixels Capture(std::uint64_t submission);
		// Native Capture thumbnail preparation: render the actual staging companion with linear sampling, then read it once.
		// Requires submitted capture input and idle shared encoding; never called by ordinary frame drawing.
		[[nodiscard]] ColorPassPixels CaptureThumbnail(scene::MutableTextureOwner& actual_capture, std::uint32_t width, std::uint32_t height);
		// Read this actual target after the exact same-domain outer frame that last wrote it.
		[[nodiscard]] ColorPassPixels CaptureAfterFrame(const SpritePassRenderer& actual_frame_owner, std::uint64_t actual_submission);
		[[nodiscard]] SpritePassDescription Description() const;

	private:
		friend class SpriteFrameEncoder;
		friend class SpritePassPresentationBridge;
		friend class SpriteGraphicsTargetBridge;
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::render
