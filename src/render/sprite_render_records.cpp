#include "sgre/render/sprite_render_records.hpp"

#include "sgre/render/sprite_graphics_targets.hpp"

#include "sgre/scene/motion_alternate_program.hpp"
#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::render {

	static void RequireOrdinaryRecordBlend(std::uint32_t mode, float parameter) {
		if ((mode > 5 && (mode < 16 || mode > 21) && mode != 0x110 && mode != 0x112 && mode != 0x118) || !std::isfinite(parameter) || parameter != 0) {
			throw std::runtime_error("render records require measured blend0..5/16..21 or copied0x110/0x112/0x118 and zero parameter");
		}
	}

	static void RequirePreparedRecordPipeline(const scene::SpritePipeline& pipeline, const SpritePreparedTextureMetadata& texture, std::uint32_t attributes, std::uint32_t blend_mode, scene::MotionVertexLayout layout = scene::MotionVertexLayout::XyColorUv) {
		scene::MotionShaderProgramInput input;
		input.Layout = layout;
		input.Texture = texture.Channels;
		input.TextureAttributes = attributes;
		input.BlendMode = blend_mode;
		scene::MotionShaderProgram shaders;
		if ((blend_mode & 0xf00) == 0x100) {
			scene::MotionAlternateProgramInput alternate;
			alternate.State = input;
			shaders = scene::SelectAlternateMotionShaderProgram(alternate).Program;
		} else {
			shaders = scene::SelectOrdinaryMotionShaderProgram(input);
		}
		scene::MotionSpriteMaterial material;
		material.TextureFormat = pipeline.TextureFormat;
		material.TextureAttributes = attributes;
		material.BlendMode = blend_mode;
		scene::SpritePipelineOptions options;
		options.Sampling = pipeline.Sampler.Minification;
		options.AddressU = pipeline.Sampler.AddressU;
		options.AddressV = pipeline.Sampler.AddressV;
		auto expected = scene::SelectSpritePipeline(material, options);
		expected.NativeVertexShaderIndex = shaders.VertexIndex;
		if (texture.Channels == scene::MotionShaderTexture::None && layout == scene::MotionVertexLayout::Xy) {
			expected.NativePixelShaderIndex = shaders.PixelIndex;
			expected.VertexColor = scene::SpriteVertexColor::Multiply;
		}
		const auto& blend = pipeline.Blend;
		const auto& wanted = expected.Blend;
		const auto& sampler = pipeline.Sampler;
		const auto& expected_sampler = expected.Sampler;
		if (pipeline.NativeVertexShaderIndex != shaders.VertexIndex || pipeline.NativePixelShaderIndex != shaders.PixelIndex || pipeline.NativeVertexShaderIndex != expected.NativeVertexShaderIndex || pipeline.NativePixelShaderIndex != expected.NativePixelShaderIndex || pipeline.NativeBlendStateIndex != expected.NativeBlendStateIndex || pipeline.VertexColor != expected.VertexColor || pipeline.PixelOperation != expected.PixelOperation || pipeline.ColorFilter != scene::SpriteColorFilter::None || blend.Enabled != wanted.Enabled || blend.RgbOperation != wanted.RgbOperation || blend.SourceRgb != wanted.SourceRgb || blend.DestinationRgb != wanted.DestinationRgb || blend.AlphaOperation != wanted.AlphaOperation || blend.SourceAlpha != wanted.SourceAlpha || blend.DestinationAlpha != wanted.DestinationAlpha || blend.WriteMask != wanted.WriteMask || sampler.Magnification != expected_sampler.Magnification || sampler.MipFilter != expected_sampler.MipFilter || sampler.AddressW != expected_sampler.AddressW || sampler.MipLodBias != expected_sampler.MipLodBias || sampler.MinLod != expected_sampler.MinLod || sampler.MaxLod != expected_sampler.MaxLod || sampler.MaxAnisotropy != expected_sampler.MaxAnisotropy || sampler.NativeFilterCode != expected_sampler.NativeFilterCode) {
			throw std::runtime_error("render record scalar/texture channels differ from its actual prepared ordinary/copied pipeline");
		}
	}

	SpriteRenderRecords::SpriteRenderRecords(SpritePassRenderer& actual_renderer, SpriteGraphicsCamera& actual_camera, RenderFrameStatistics& actual_statistics)
	    : m_Renderer(actual_renderer), m_Camera(actual_camera), m_Statistics(actual_statistics), m_Encoder(actual_renderer), m_Thread(std::this_thread::get_id()) {
	}

	void SpriteRenderRecords::RequireThread() const {
		if (std::this_thread::get_id() != m_Thread) {
			throw std::runtime_error("render records require their actual serialized owner thread");
		}
	}

	void SpriteRenderRecords::RequireRenderer(const SpritePassRenderer& actual_renderer) const {
		RequireThread();
		if (&actual_renderer != &m_Renderer) {
			throw std::runtime_error("render preparation/packet service requires this exact actual renderer owner");
		}
	}

	const SpritePassRenderer& SpriteRenderRecords::CurrentRenderTarget() const {
		RequireThread();
		return m_Encoder.CurrentRenderTarget();
	}

	std::uint64_t SpriteRenderRecords::TextureBindingRevision() const {
		RequireThread();
		return m_TextureBindingRevision;
	}

	void SpriteRenderRecords::SelectRenderTarget(SpriteGraphicsTargets& graphics, const std::shared_ptr<SpritePassRenderer>& actual_target, const std::optional<std::array<float, 4>>& clear_color) {
		RequireFrame();
		if (m_Depth != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle) {
			throw std::runtime_error("live target scope requires balanced records and an idle actual mask");
		}
		const bool changed = actual_target && actual_target.get() != &m_Encoder.CurrentRenderTarget();
		if (changed && m_TextureBindingRevision == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("live target binding revision exceeds its checked lifetime");
		}
		try {
			graphics.SelectFrameTarget(m_Encoder, actual_target, clear_color);
			if (changed) {
				++m_TextureBindingRevision;
			}
			InvalidateBindings();
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}
	}

	SpritePreparedTextureMetadata SpriteRenderRecords::PreparedTexture(SpriteTextureHandle actual_texture) const {
		RequireFrame();

		return m_Encoder.PreparedTexture(actual_texture);
	}

	SpriteTextureHandle SpriteRenderRecords::PrepareMutableTexture(scene::MutableTextureOwner& owner, std::uint32_t slot, scene::MutableTextureStage stage) {
		RequireFrame();
		return m_Encoder.PrepareMutableTexture(owner, slot, stage);
	}

	void SpriteRenderRecords::PrepareTextureMask() {
		RequireThread();
		if (m_Encoder.IsActive() || m_Depth != 0 || m_ResetPending) {
			throw std::runtime_error("texture-mask preparation requires inactive balanced real render records");
		}
		m_Encoder.PrepareTextureMask();
	}

	void SpriteRenderRecords::PrepareMotionPipelines(std::span<const scene::SpritePipeline> pipelines, std::span<SpritePipelineHandle> output) {
		RequireFrame();
		if (m_Depth != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle) {
			throw std::runtime_error("motion resource preparation requires balanced records and idle actual mask state");
		}
		try {
			m_Encoder.PrepareMotionPipelines(pipelines, output);
			InvalidateBindings();
		} catch (...) {
			if (!m_Failure) {
				m_Failure = std::current_exception();
			}
			throw;
		}
	}

	SpritePreparedCopiedTarget SpriteRenderRecords::PrepareMotionCopiedTarget(scene::MutableTextureBackend& backend, std::uint32_t width, std::uint32_t height) {
		RequireFrame();
		if (m_Depth != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle) {
			throw std::runtime_error("motion copied allocation requires balanced records and idle actual mask state");
		}
		try {
			auto result = m_Encoder.PrepareMotionCopiedTarget(backend, width, height);
			InvalidateBindings();
			return result;
		} catch (...) {
			if (!m_Failure) {
				m_Failure = std::current_exception();
			}
			throw;
		}
	}

	void SpriteRenderRecords::ConfigureTextureMask(std::uint8_t operation, float multiplier, float offset) {
		RequireFrame();
		try {
			m_Encoder.ConfigureTextureMask(operation, multiplier, offset);
		} catch (...) {
			if (!m_Failure) {
				m_Failure = std::current_exception();
			}
			throw;
		}
	}

	void SpriteRenderRecords::ApplyTextureMask(scene::MotionStencilAction action) {
		RequireFrame();
		try {
			m_Encoder.ApplyTextureMask(action);
		} catch (...) {
			if (!m_Failure) {
				m_Failure = std::current_exception();
			}
			throw;
		}
	}

	scene::MotionMaskPhase SpriteRenderRecords::TextureMaskPhase() const {
		RequireFrame();

		return m_Encoder.TextureMaskPhase();
	}

	SpriteTextureHandle SpriteRenderRecords::CopyCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& rectangle) {
		RequireFrame();
		try {
			return m_Encoder.CopyCurrentTarget(destination, rectangle);
		} catch (...) {
			if (!m_Failure) {
				m_Failure = std::current_exception();
			}
			throw;
		}
	}

	std::unique_ptr<scene::MutableTextureOwner> SpriteRenderRecords::PrepareCaptureTexture(scene::MutableTextureBackend& backend, const scene::MutableTextureSpec& spec) {
		RequireFrame();
		if (m_Depth != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle) {
			throw std::runtime_error("Capture allocation requires balanced records and the actual idle mask phase");
		}
		try {
			auto result = m_Encoder.PrepareCaptureTexture(backend, spec);
			InvalidateBindings();

			return result;
		} catch (...) {
			if (!m_Failure) {
				m_Failure = std::current_exception();
			}
			throw;
		}
	}

	std::unique_ptr<scene::MutableTextureOwner> SpriteRenderRecords::PrepareMovieTexture(scene::MutableTextureBackend& backend, std::uint32_t width, std::uint32_t height) {
		RequireFrame();
		if (m_Depth != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle) {
			throw std::runtime_error("movie allocation requires balanced records and the actual idle mask phase");
		}
		try {
			auto result = m_Encoder.PrepareMovieTexture(backend, width, height);
			InvalidateBindings();

			return result;
		} catch (...) {
			if (!m_Failure) {
				m_Failure = std::current_exception();
			}
			throw;
		}
	}

	void SpriteRenderRecords::CopyCaptureCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& rectangle) {
		RequireFrame();
		if (m_Depth != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle) {
			throw std::runtime_error("Capture copy requires balanced records and the actual idle mask phase");
		}
		try {
			m_Encoder.CopyCaptureCurrentTarget(destination, rectangle);
			InvalidateBindings();
		} catch (...) {
			if (!m_Failure) {
				m_Failure = std::current_exception();
			}
			throw;
		}
	}

	void SpriteRenderRecords::SetCopiedDestination(SpriteTextureHandle texture, const std::array<float, 4>& affine) {
		RequireRecord();
		const auto prepared = m_Encoder.PreparedTexture(texture);
		if (!prepared.Mutable || prepared.OriginalDxgiFormat != 28) {
			throw std::runtime_error("copied record destination requires its actual mutable RGBA target");
		}
		for (const auto value : affine) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("copied record destination requires finite actual region UV coefficients");
			}
		}

		auto& record = *m_Records[m_Depth];
		record.CopiedDestination = texture;
		record.CopiedUvAffine = affine;
	}

	void SpriteRenderRecords::ClearCopiedDestination() {
		RequireRecord();
		m_Records[m_Depth]->CopiedDestination.reset();
	}

	void SpriteRenderRecords::BindCopiedDraw(SpritePassDraw& draw, const scene::SpritePipeline& pipeline) const {
		const auto phase = m_Encoder.TextureMaskPhase();
		if ((phase >= scene::MotionMaskPhase::TextureFirst && phase <= scene::MotionMaskPhase::TextureSubtract) || (pipeline.NativePixelShaderIndex != 1129 && pipeline.NativePixelShaderIndex != 1131 && pipeline.NativePixelShaderIndex != 1137 && pipeline.NativePixelShaderIndex != 1289 && pipeline.NativePixelShaderIndex != 1281 && pipeline.NativePixelShaderIndex != 1283)) {
			return;
		}
		const auto& record = *m_Records[m_Depth];
		if (!record.CopiedDestination) {
			throw std::runtime_error("copied draw requires its actual current-region destination binding");
		}

		draw.CopiedDestination = *record.CopiedDestination;
		draw.CopiedUvAffine = record.CopiedUvAffine;
	}

	scene::SpritePipeline SpriteRenderRecords::PreparedPipeline(SpritePipelineHandle actual_pipeline) const {
		RequireFrame();

		return m_Encoder.PreparedPipeline(actual_pipeline);
	}

	scene::MotionPreparedFrame SpriteRenderRecords::OrdinaryFrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes) const {
		RequireFrame();

		return m_Camera.OrdinaryFrameGlobals(actual_texture_sizes);
	}

	scene::MotionPreparedFrame SpriteRenderRecords::SceneFrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes) const {
		RequireFrame();

		return m_Camera.SceneFrameGlobals(actual_texture_sizes);
	}

	void SpriteRenderRecords::RequireFrame() const {
		RequireThread();
		if (m_Failure) {
			std::rethrow_exception(m_Failure);
		}
		if (!m_Encoder.IsActive()) {
			throw std::runtime_error("render records require their actual active contained encoder");
		}
		m_Camera.RequireTarget(m_Encoder.CurrentRenderTarget());
	}

	void SpriteRenderRecords::RequireRecord() const {
		RequireFrame();
		if (m_Depth == 0 || !m_Records[m_Depth]) {
			throw std::runtime_error("render records require their actual begun physical record");
		}
	}

	void SpriteRenderRecords::InvalidateBindings() noexcept {
		m_CurrentPipeline.reset();
		m_CurrentInputs = nullptr;
	}

	scene::MotionIntegerScissor SpriteRenderRecords::SubmittedDeviceScissor() const {
		RequireFrame();

		return m_Encoder.SubmittedDeviceScissor();
	}

	void SpriteRenderRecords::ResetFrameState() {
		RequireThread();
		if (m_Failure) {
			std::rethrow_exception(m_Failure);
		}
		if (m_Encoder.IsActive() || m_Depth != 0 || m_ResetPending) {
			throw std::runtime_error("render Pre reset requires its actual inactive balanced owner without a pending reset");
		}

		m_CompletedSubmission.reset();
		m_Statistics.BeginFrame();
		InvalidateBindings();
		m_ResetPending = true;
	}

	void SpriteRenderRecords::BeginPreparedFrame(const std::array<float, 4>& clear_color, SpritePassLoadAction load_action) {
		RequireThread();
		if (m_Failure) {
			std::rethrow_exception(m_Failure);
		}
		if (!m_ResetPending || m_Encoder.IsActive() || m_Depth != 0) {
			throw std::runtime_error("prepared render Begin requires this exact owner's completed Pre reset");
		}
		m_Camera.RequireTarget(m_Renderer);
		m_Encoder.Begin(clear_color, load_action);

		m_ResetPending = false;
	}

	void SpriteRenderRecords::BeginFrame(const std::array<float, 4>& clear_color, SpritePassLoadAction load_action) {
		RequireThread();
		if (m_Encoder.IsActive() || m_Depth != 0 || m_ResetPending) {
			throw std::runtime_error("render frame requires its actual quiescent record/encoder owner");
		}
		m_Camera.RequireTarget(m_Renderer);
		m_Encoder.Begin(clear_color, load_action);

		m_Failure = {};
		// Actual Metal admission completes before consuming the preceding CPU counters.
		// This is a bounded owner join, not the whole native4c4a90 prehook/pool/target sequence.
		m_CompletedSubmission.reset();
		m_Statistics.BeginFrame();
		InvalidateBindings();
	}

	void SpriteRenderRecords::BeginRecord() {
		RequireFrame();
		if (m_Depth == m_Records.size() - 1) {
			throw std::runtime_error("render record nesting exceeds the native eight physical slots");
		}

		const auto next = m_Depth + 1;
		Record record;
		record.Scalars.NativeScalar48 = std::bit_cast<float>(std::uint32_t{0x3e808081});
		if (m_Records[next]) {
			record.Scalars.BlendParameter = m_Records[next]->Scalars.BlendParameter;
		}
		m_Records[next] = record;
		m_Depth = next;
		m_BlendMode = 0;
		m_BlendParameter = 0;
		// Native Begin clears filter ID but leaves the retained global filter color unchanged.
		InvalidateBindings();
	}

	void SpriteRenderRecords::EndRecord() {
		RequireRecord();

		--m_Depth;
		InvalidateBindings();
	}

	void SpriteRenderRecords::SetBlend(std::uint32_t mode, float parameter) {
		RequireRecord();
		RequireOrdinaryRecordBlend(mode, parameter);

		m_BlendMode = mode;
		m_BlendParameter = parameter;
	}

	void SpriteRenderRecords::SetPictureBlend(std::uint32_t mode) {
		RequireRecord();
		if (mode > 10) {
			throw std::runtime_error("Picture record blend is outside native ordinary rows0..10");
		}

		m_BlendMode = mode;
		m_BlendParameter = 0;
	}

	void SpriteRenderRecords::SetFilterColor(std::uint32_t color) {
		RequireRecord();

		m_FilterColor = color;
	}

	void SpriteRenderRecords::SetScissor(scene::MotionIntegerScissor rectangle) {
		RequireFrame();

		try {
			m_Encoder.SetScissor(rectangle);
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}
	}

	void SpriteRenderRecords::Append(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const SpriteHostInputs& actual_inputs, std::uint32_t texture_attributes) {
		AppendIndexed(texture, pipeline, actual_inputs, texture_attributes, scene::MotionDrawTopology::TriangleStrip);
	}

	void SpriteRenderRecords::AppendGlyphs(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const SpriteHostInputs& actual_inputs) {
		RequireRecord();
		if (m_BlendMode != 0 || m_BlendParameter != 0 || actual_inputs.VertexCount() % 4 != 0 || actual_inputs.IndexCount() % 6 != 0 || actual_inputs.IndexCount() / 6 != actual_inputs.VertexCount() / 4) {
			throw std::runtime_error("bitmap glyph record requires actual native blend0 and complete four-vertex/six-index glyph groups");
		}

		const auto bytes = actual_inputs.IndexBytes();
		const auto width = actual_inputs.IndexWordBytes();
		static constexpr std::array<std::uint32_t, 6> kGlyphIndices{0, 1, 2, 1, 3, 2};
		for (std::size_t index = 0; index < actual_inputs.IndexCount(); ++index) {
			std::uint32_t observed = 0;
			for (std::size_t byte = 0; byte < width; ++byte) {
				observed |= std::to_integer<std::uint32_t>(bytes[index * width + byte]) << (byte * 8);
			}
			if (observed != static_cast<std::uint32_t>(index / 6 * 4) + kGlyphIndices[index % 6]) {
				throw std::runtime_error("bitmap glyph indices differ from the actual native triangle-list groups");
			}
		}

		AppendIndexed(texture, pipeline, actual_inputs, 0, scene::MotionDrawTopology::TriangleList);
	}

	void SpriteRenderRecords::AppendIndexed(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const SpriteHostInputs& actual_inputs, std::uint32_t texture_attributes, scene::MotionDrawTopology topology) {
		RequireRecord();
		RequireOrdinaryRecordBlend(m_BlendMode, m_BlendParameter);
		if (actual_inputs.Layout() != scene::MotionVertexLayout::XyColorUv) {
			throw std::runtime_error("ordinary indexed record requires actual format4 host inputs");
		}
		if (!m_FilterColor || texture_attributes > 7) {
			throw std::runtime_error("render packet requires actual filter-zero color initialization and recovered texture-key low bits0..7");
		}
		const auto prepared_texture = m_Encoder.PreparedTexture(texture);
		const auto prepared_pipeline = m_Encoder.PreparedPipeline(pipeline);
		RequirePreparedRecordPipeline(prepared_pipeline, prepared_texture, texture_attributes, m_BlendMode);
		const auto element_count = actual_inputs.IndexCount();
		static_cast<void>(actual_inputs.VertexBytes());
		static_cast<void>(actual_inputs.IndexBytes());
		const auto registers = m_Camera.OrdinaryShaderRegisters();
		for (const auto value : registers) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("render packet requires finite actual camera/model registers before record mutation");
			}
		}

		// Native55a18f..55a281 supplies these scalar/resource/input fields before the real draw.
		auto& record = *m_Records[m_Depth];
		record.Scalars.TextureAttributes = texture_attributes;
		record.Scalars.BlendMode = m_BlendMode;
		record.Scalars.BlendParameter = m_BlendParameter;
		record.Scalars.FilterColor = *m_FilterColor;
		record.Texture = texture;
		record.Inputs = &actual_inputs;
		m_CurrentPipeline = pipeline;
		m_CurrentInputs = record.Inputs;
		SpritePassDraw draw;
		draw.Texture = *record.Texture;
		draw.Pipeline = *m_CurrentPipeline;
		draw.HostInputs = m_CurrentInputs;
		draw.Topology = topology;
		BindCopiedDraw(draw, prepared_pipeline);
		try {
			m_Encoder.Append(draw, registers);
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}

		// The encoder binds real resources every draw; this does not infer native pointer-cache suppression.
		m_Statistics.RecordDraw(element_count);
	}

	void SpriteRenderRecords::AppendMovieAlpha(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::MovieAlphaDrawPacket& packet) {
		RequireRecord();
		const auto prepared = m_Encoder.PreparedPipeline(pipeline);
		const auto source = m_Encoder.PreparedTexture(texture);
		if (m_BlendMode != 0 || m_BlendParameter != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle || prepared.NativeVertexShaderIndex != 2 || prepared.NativePixelShaderIndex != 2041 || prepared.NativeBlendStateIndex != 0 || !source.Mutable || source.OriginalDxgiFormat != 88) {
			throw std::runtime_error("movie alpha requires its actual RGB32 source and native blend0/VS2/PS2041 in an idle mask");
		}
		const auto registers = m_Camera.OrdinaryShaderRegisters();
		auto& record = *m_Records[m_Depth];
		record.Scalars.TextureAttributes = 0;
		record.Scalars.BlendMode = 0;
		record.Scalars.BlendParameter = 0;
		record.Texture = texture;
		record.Inputs = nullptr;
		m_CurrentPipeline = pipeline;
		m_CurrentInputs = nullptr;
		SpritePassDraw draw;
		draw.Texture = texture;
		draw.Pipeline = pipeline;
		draw.MovieAlpha = &packet;
		try {
			m_Encoder.Append(draw, registers);
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}
		m_Statistics.RecordDraw(4);
	}

	void SpriteRenderRecords::AppendCapturedOpacityMap(SpriteTextureHandle capture, SpriteTextureHandle map, SpritePipelineHandle pipeline, const scene::CapturedOpacityMapDrawPacket& packet) {
		RequireRecord();
		const auto prepared = m_Encoder.PreparedPipeline(pipeline);
		const auto source = m_Encoder.PreparedTexture(capture);
		const auto opacity = m_Encoder.PreparedTexture(map);
		// PS2040 reads phase/inverseVague and texture0/1 only; native4fb2d0 never initializes filterColor.
		if (m_BlendMode != 0 || m_BlendParameter != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle || prepared.NativeVertexShaderIndex != 2 || prepared.NativePixelShaderIndex != 2040 || prepared.NativeBlendStateIndex != 0 || prepared.Blend.WriteMask != 15 || prepared.Sampler.Minification != scene::TextureSampling::Linear || static_cast<std::uint32_t>(prepared.Sampler.AddressU) != packet.AddressU || static_cast<std::uint32_t>(prepared.Sampler.AddressV) != packet.AddressV || !source.Mutable || source.OriginalDxgiFormat != 28 || opacity.Mutable || opacity.RenderTarget || opacity.Channels == scene::MotionShaderTexture::None) {
			throw std::runtime_error("Captured opacity map record requires actual companion/Image resources, idle mask and prepared native VS2/PS2040 blend0 sampler");
		}
		const auto registers = m_Camera.OrdinaryShaderRegisters();
		for (const auto value : registers) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Captured opacity map requires finite actual camera/model registers");
			}
		}
		auto& record = *m_Records[m_Depth];
		record.Scalars.TextureAttributes = 0;
		record.Scalars.BlendMode = 0;
		record.Scalars.BlendParameter = 0;
		record.Texture = capture;
		record.Inputs = nullptr;
		m_CurrentPipeline = pipeline;
		m_CurrentInputs = nullptr;
		SpritePassDraw draw;
		draw.Texture = capture;
		draw.OpacityMapTexture = map;
		draw.Pipeline = pipeline;
		draw.CapturedOpacityMap = &packet;
		try {
			m_Encoder.Append(draw, registers);
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}
		m_Statistics.RecordDraw(4);
	}

	void SpriteRenderRecords::AppendFillRectangle(SpritePipelineHandle pipeline, const scene::FillRectangleDrawPacket& packet) {
		RequireRecord();
		const auto prepared = m_Encoder.PreparedPipeline(pipeline);
		if (m_BlendMode != 0 || m_BlendParameter != 0 || prepared.NativeVertexShaderIndex != 3 || prepared.NativePixelShaderIndex != 60 || prepared.NativeBlendStateIndex != 0) {
			throw std::runtime_error("FillRect requires its original format0 VS3/PS60 and default blend0 record");
		}
		RequirePreparedRecordPipeline(prepared, SpritePreparedTextureMetadata{}, 0, 0, scene::MotionVertexLayout::Xy);
		const auto registers = m_Camera.OrdinaryShaderRegisters();
		for (const auto value : registers) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("FillRect requires finite actual camera/model registers");
			}
		}
		auto& record = *m_Records[m_Depth];
		record.Scalars.TextureAttributes = 0;
		record.Scalars.BlendMode = 0;
		record.Scalars.BlendParameter = 0;
		record.Scalars.UniformArgb = packet.Color;
		record.Texture.reset();
		record.Inputs = nullptr;
		m_CurrentPipeline = pipeline;
		m_CurrentInputs = nullptr;
		SpritePassDraw draw;
		draw.Pipeline = pipeline;
		draw.FillRectangleVertices = packet.Vertices;
		draw.FillRectangleColor = packet.Color;
		try {
			m_Encoder.Append(draw, registers);
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}
		m_Statistics.RecordDraw(4);
	}

	void SpriteRenderRecords::AppendProjection(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const SpriteHostInputs& actual_inputs, std::uint32_t packed_argb) {
		RequireRecord();
		if (actual_inputs.Layout() != scene::MotionVertexLayout::XyzUv || actual_inputs.IndexWordBytes() != 2 || m_BlendMode > 1 || m_BlendParameter != 0 || actual_inputs.VertexCount() % 4 != 0 || actual_inputs.IndexCount() / 6 != actual_inputs.VertexCount() / 4 || actual_inputs.IndexCount() % 6 != 0) {
			throw std::runtime_error("ScreenProjection requires genuine format8 native triangle groups and recovered blend0/1");
		}
		const auto bytes = actual_inputs.IndexBytes();
		const auto width = actual_inputs.IndexWordBytes();
		static constexpr std::array<std::uint32_t, 6> kProjectionIndices{0, 1, 2, 1, 3, 2};
		for (std::size_t index = 0; index < actual_inputs.IndexCount(); ++index) {
			std::uint32_t observed = 0;
			for (std::size_t byte = 0; byte < width; ++byte) {
				observed |= std::to_integer<std::uint32_t>(bytes[index * width + byte]) << (byte * 8);
			}
			if (observed != static_cast<std::uint32_t>(index / 6 * 4) + kProjectionIndices[index % 6]) {
				throw std::runtime_error("ScreenProjection indices differ from native4fa997 triangle order");
			}
		}
		const auto texture_metadata = m_Encoder.PreparedTexture(texture);
		if (!texture_metadata.RenderTarget || texture_metadata.Channels != scene::MotionShaderTexture::Rgba || texture_metadata.OriginalDxgiFormat != 28) {
			throw std::runtime_error("ScreenProjection requires the actual live Screen target sampled view");
		}
		const auto attributes = packed_argb == 0xffffffff ? 4u : 0u;
		RequirePreparedRecordPipeline(m_Encoder.PreparedPipeline(pipeline), m_Encoder.PreparedTexture(texture), attributes, m_BlendMode, scene::MotionVertexLayout::XyzUv);
		const auto registers = m_Camera.OrdinaryShaderRegisters();
		for (const auto value : registers) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("ScreenProjection requires finite actual camera/model registers");
			}
		}

		auto& record = *m_Records[m_Depth];
		record.Scalars.TextureAttributes = attributes;
		record.Scalars.BlendMode = m_BlendMode;
		record.Scalars.BlendParameter = 0;
		record.Scalars.UniformArgb = packed_argb;
		record.Texture = texture;
		record.Inputs = &actual_inputs;
		m_CurrentPipeline = pipeline;
		m_CurrentInputs = &actual_inputs;
		SpritePassDraw draw;
		draw.Texture = texture;
		draw.Pipeline = pipeline;
		draw.HostInputs = &actual_inputs;
		draw.RawTextureColor = packed_argb;
		draw.Topology = scene::MotionDrawTopology::TriangleList;
		try {
			m_Encoder.Append(draw, registers);
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}
		m_Statistics.RecordDraw(actual_inputs.IndexCount());
	}

	void SpriteRenderRecords::AppendRaw(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet) {
		AppendRawPacket(texture, pipeline, packet, false);
	}

	void SpriteRenderRecords::AppendMovieRaw(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet) {
		AppendRawPacket(texture, pipeline, packet, true);
	}

	void SpriteRenderRecords::AppendPicture(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet, std::uint32_t raw_opacity) {
		RequireRecord();
		if (m_BlendMode > 10 || m_BlendParameter != 0 || packet.TextureAttributes != (raw_opacity == 255 ? 4u : 0u) || packet.Color != (((raw_opacity & 255u) << 24) | 0x00ffffffu)) {
			throw std::runtime_error("Picture packet requires its actual DWORD opacity, white ARGB uniform and ordinary blend0..10");
		}
		for (const auto& vertex : packet.Vertices) {
			if (!std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) || !std::isfinite(vertex.Uv.X) || !std::isfinite(vertex.Uv.Y)) {
				throw std::runtime_error("Picture packet requires finite actual XY/UV words");
			}
		}
		const auto source = m_Encoder.PreparedTexture(texture);
		if (source.Mutable || source.RenderTarget || (source.Channels != scene::MotionShaderTexture::Rgba && source.Channels != scene::MotionShaderTexture::Alpha && source.Channels != scene::MotionShaderTexture::Luminance)) {
			throw std::runtime_error("Picture requires its genuine immutable Image texture channels");
		}
		auto prepared = m_Encoder.PreparedPipeline(pipeline);
		const auto& sampler = prepared.Sampler;
		if (sampler.Minification == scene::TextureSampling::Nearest && sampler.Magnification == scene::TextureSampling::Linear && sampler.MipFilter == scene::TextureSampling::Nearest && sampler.NativeFilterCode == 4) {
			// Native mixed user filters both select sampler4. Validate every other ordinary field unchanged.
			prepared.Sampler.Magnification = scene::TextureSampling::Nearest;
			prepared.Sampler.NativeFilterCode = 0;
		}
		RequirePreparedRecordPipeline(prepared, source, packet.TextureAttributes, m_BlendMode, scene::MotionVertexLayout::XyUv);
		AppendPreparedFormat2(texture, pipeline, packet);
	}

	void SpriteRenderRecords::AppendRawPacket(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet, bool movie) {
		RequireRecord();
		RequireOrdinaryRecordBlend(m_BlendMode, m_BlendParameter);
		if (packet.TextureAttributes != (movie ? 0u : packet.Color == 0xffffffff ? 4u
		                                                                         : 0u)) {
			throw std::runtime_error("RawTex packet attributes differ from its actual ARGB uniform");
		}
		for (const auto& vertex : packet.Vertices) {
			if (!std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) || !std::isfinite(vertex.Uv.X) || !std::isfinite(vertex.Uv.Y)) {
				throw std::runtime_error("RawTex frame packet requires finite XY/UV words");
			}
		}
		if (movie && (m_BlendMode != 0 || m_BlendParameter != 0 || m_Encoder.TextureMaskPhase() != scene::MotionMaskPhase::Idle || m_Encoder.PreparedPipeline(pipeline).NativePixelShaderIndex != 240)) {
			throw std::runtime_error("movie format2 requires its native blend0/VS4/PS240 and idle mask");
		}
		const auto prepared_texture = m_Encoder.PreparedTexture(texture);
		if (!prepared_texture.Mutable || !prepared_texture.OriginalDxgiFormat || (*prepared_texture.OriginalDxgiFormat != 28 && *prepared_texture.OriginalDxgiFormat != 87 && *prepared_texture.OriginalDxgiFormat != 88)) {
			throw std::runtime_error("RawTex frame requires its recovered mutable RGBA/BGRA/BGRX storage");
		}
		RequirePreparedRecordPipeline(m_Encoder.PreparedPipeline(pipeline), prepared_texture, packet.TextureAttributes, m_BlendMode, scene::MotionVertexLayout::XyUv);
		AppendPreparedFormat2(texture, pipeline, packet);
	}

	void SpriteRenderRecords::AppendPreparedFormat2(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet) {
		const auto registers = m_Camera.OrdinaryShaderRegisters();
		for (const auto value : registers) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("RawTex frame requires finite actual camera/model registers");
			}
		}

		// RawTex and Picture both submit format2 XY/UV with a separate uniform and no indexed input owner.
		auto& record = *m_Records[m_Depth];
		record.Scalars.TextureAttributes = packet.TextureAttributes;
		record.Scalars.BlendMode = m_BlendMode;
		record.Scalars.BlendParameter = m_BlendParameter;
		record.Scalars.UniformArgb = packet.Color;
		record.Texture = texture;
		record.Inputs = nullptr;
		m_CurrentPipeline = pipeline;
		m_CurrentInputs = nullptr;
		SpritePassDraw draw;
		draw.Texture = texture;
		draw.Pipeline = pipeline;
		draw.RawTextureVertices = packet.Vertices;
		draw.RawTextureColor = packet.Color;
		BindCopiedDraw(draw, m_Encoder.PreparedPipeline(pipeline));
		try {
			m_Encoder.Append(draw, registers);
		} catch (...) {
			m_Failure = std::current_exception();

			throw;
		}

		m_Statistics.RecordDraw(4);
	}

	std::uint64_t SpriteRenderRecords::EndFrame() {
		RequireFrame();
		if (m_Depth != 0) {
			throw std::runtime_error("render frame cannot submit unbalanced actual record scopes");
		}

		try {
			const auto submitted = m_Encoder.End();
			m_CompletedSubmission = submitted;

			return submitted;
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}
	}

	void SpriteRenderRecords::AbortFrame() {
		RequireThread();
		std::exception_ptr failure;
		try {
			m_Encoder.Abort();
		} catch (...) {
			// The actual encoder completes its phase reset before reporting a same-thread OS cleanup error.
			failure = std::current_exception();
		}

		m_Depth = 0;
		m_ResetPending = false;
		m_CompletedSubmission.reset();
		m_Failure = {};
		InvalidateBindings();
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	std::optional<std::uint64_t> SpriteRenderRecords::CompletedSubmission() const {
		RequireThread();
		if (m_Encoder.IsActive()) {
			throw std::runtime_error("render completion observation requires the exact outer frame to retire");
		}

		return m_CompletedSubmission;
	}

	bool SpriteRenderRecords::IsActive() const {
		RequireThread();

		return m_Encoder.IsActive();
	}

	std::size_t SpriteRenderRecords::RecordDepth() const {
		RequireFrame();

		return m_Depth;
	}

	SpriteRenderRecordScalars SpriteRenderRecords::CurrentRecord() const {
		RequireRecord();

		return m_Records[m_Depth]->Scalars;
	}

} // namespace sgre::render
