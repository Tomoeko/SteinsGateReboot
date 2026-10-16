#include "sgre/render/motion_sprite_frame.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace sgre::render {

	static scene::TextureAddress FrameTextureAddress(std::uint32_t native) {
		switch (native) {
		case 0:
			return scene::TextureAddress::Clamp;
		case 1:
			return scene::TextureAddress::Repeat;
		case 2:
			return scene::TextureAddress::Mirror;
		default:
			throw std::runtime_error("motion frame texture address is outside its recovered domain");
		}
	}

	static std::size_t FramePipelineIndex(std::uint32_t mode, std::uint32_t key) {
		if ((mode > 5 && (mode < 16 || mode > 21)) || (key != 0 && key != 2 && key != 4 && key != 6)) {
			throw std::runtime_error("motion Metal frame requires measured ordinary blend modes and texture attributes0/2/4/6");
		}

		return static_cast<std::size_t>((mode & 15) + ((mode & 16) != 0 ? 6 : 0)) * 2 + ((key & 4) != 0 ? 1 : 0);
	}

	static SpritePassScissor FramePhysicalScissor(scene::MotionIntegerScissor rectangle, SpritePassDescription description) {
		// D3D attachment clipping intersects device scissors; Metal requires an in-target rectangle.
		const auto width = static_cast<std::int32_t>(description.Width);
		const auto height = static_cast<std::int32_t>(description.Height);
		const auto left = std::clamp(rectangle.MinimumX, 0, width);
		const auto top = std::clamp(rectangle.MinimumY, 0, height);
		const auto right = std::clamp(rectangle.MaximumX, left, width);
		const auto bottom = std::clamp(rectangle.MaximumY, top, height);
		SpritePassScissor result;
		result.X = static_cast<std::uint32_t>(left);
		result.Y = static_cast<std::uint32_t>(top);
		result.Width = static_cast<std::uint32_t>(right - left);
		result.Height = static_cast<std::uint32_t>(bottom - top);

		return result;
	}

	class MotionSpriteFrameRenderer::Implementation {
	public:
		struct TextureBinding {
			bool Bound = false;
			scene::CachedTextureHandle Handle;
			scene::CachedTexture Snapshot;
			scene::CachedTextureAddress Address;
			std::array<SpritePipelineHandle, 24> Pipelines;
		};

		Implementation(SpritePassRenderer& renderer, scene::TextureCache& textures, scene::SpritePipelineOptions options, std::size_t slots) : Renderer(renderer), Textures(textures), Options(options), Description(renderer.Description()), Output([this](const scene::MotionFrameEvent& event) {
			                                                                                                                                       Receive(event);
		                                                                                                                                       }) {
			if (slots == 0 || slots > 1024 || options.ColorFilter != scene::SpriteColorFilter::None || options.HasAuxiliaryTarget || options.BackbufferCopyPhase || !options.EnableBlending || !options.WriteColor) {
				throw std::runtime_error("motion Metal frame requires its actual bounded ordinary target/pipeline profile");
			}

			Bindings.resize(slots);
			Vertices.resize(Description.MaxVertices);
			Indices.resize(Description.MaxIndices);
			Draws.resize(Description.MaxDraws);
		}

		SpritePassRenderer& Renderer;
		scene::TextureCache& Textures;
		scene::SpritePipelineOptions Options;
		SpritePassDescription Description;
		std::vector<TextureBinding> Bindings;
		std::vector<scene::MotionSpriteVertex> Vertices;
		std::vector<std::uint32_t> Indices;
		std::vector<SpritePassDraw> Draws;
		std::function<void(const scene::MotionFrameEvent&)> Output;
		std::size_t VertexCount = 0;
		std::size_t IndexCount = 0;
		std::size_t DrawCount = 0;
		std::uint32_t BlendMode = 0;
		std::optional<std::size_t> Texture;
		SpritePassScissor Scissor;
		scene::MotionIntegerScissor AppliedDevice;
		std::array<float, 16> Projection;
		std::array<float, 4> ClearColor;
		SpritePassLoadAction LoadAction = SpritePassLoadAction::Clear;
		std::optional<std::uint64_t> Submission;
		bool Operating = false;
		bool Begun = false;
		bool Ended = false;

		void RequireCurrent(const TextureBinding& binding) const {
			const auto current = Textures.Get(binding.Handle);
			const auto address = Textures.Address(binding.Handle);
			if (current.Backend != binding.Snapshot.Backend || current.Description.Format != binding.Snapshot.Description.Format || current.Description.Width != binding.Snapshot.Description.Width || current.Description.Height != binding.Snapshot.Description.Height || current.Minification != binding.Snapshot.Minification || current.Magnification != binding.Snapshot.Magnification || address.Revision != binding.Address.Revision) {
				throw std::runtime_error("motion frame texture/filter/address changed; refresh its actual prepared binding before submission");
			}
		}

		void Append(const scene::MotionBatchSubmission& packet) {
			if (!Texture || packet.Mode != scene::MotionBatchMode::IndexedTriangleStrip || packet.Positions.size() != packet.Uvs.size() || packet.Positions.size() != packet.Colors.size() || packet.Positions.size() < 3 || packet.Indices.size() < 3) {
				throw std::runtime_error("motion Metal frame requires actual coupled indexed sprite packet streams");
			}
			if (DrawCount == Draws.size() || packet.Positions.size() > Vertices.size() - VertexCount || packet.Indices.size() > Indices.size() - IndexCount) {
				throw std::runtime_error("motion Metal frame packets exceed preallocated frame capacity");
			}

			const auto pipeline_index = FramePipelineIndex(BlendMode, packet.Key);
			const auto& binding = Bindings[*Texture];
			for (std::size_t index = 0; index < packet.Positions.size(); ++index) {
				auto& vertex = Vertices[VertexCount + index];
				vertex.Position = packet.Positions[index];
				vertex.Uv = packet.Uvs[index];
				// Packets store AARRGGBB; the sprite API accepts RRGGBBAA and performs its own BGRA8 packing.
				vertex.Color = std::rotl(packet.Colors[index], 8);
			}
			std::copy(packet.Indices.begin(), packet.Indices.end(), Indices.begin() + static_cast<std::ptrdiff_t>(IndexCount));
			auto& draw = Draws[DrawCount++];
			draw = SpritePassDraw{};
			draw.Texture = binding.Snapshot.Backend;
			draw.Pipeline = binding.Pipelines[pipeline_index];
			draw.Scissor = Scissor;
			draw.Vertices = std::span<const scene::MotionSpriteVertex>(Vertices).subspan(VertexCount, packet.Positions.size());
			draw.Indices = std::span<const std::uint32_t>(Indices).subspan(IndexCount, packet.Indices.size());
			VertexCount += packet.Positions.size();
			IndexCount += packet.Indices.size();
		}

		void Receive(const scene::MotionFrameEvent& event) {
			std::visit([&](const auto& value) {
				using Event = std::remove_cvref_t<decltype(value)>;
				if constexpr (std::is_same_v<Event, scene::MotionFrameBegin>) {
					if (Begun || Ended) {
						throw std::runtime_error("motion Metal frame received an invalid Begin order");
					}
					Begun = true;
				} else {
					if (!Begun || Ended) {
						throw std::runtime_error("motion Metal frame event is outside its actual Begin/End lifetime");
					}
					if constexpr (std::is_same_v<Event, scene::MotionFrameEnd>) {
						Submission = Renderer.Submit(std::span<const SpritePassDraw>(Draws).first(DrawCount), Projection, ClearColor, LoadAction);
						Ended = true;
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameBlend>) {
						static_cast<void>(FramePipelineIndex(value.Mode, 0));
						if (value.Parameter != 0) {
							throw std::runtime_error("motion Metal frame blend parameter execution remains unsupported");
						}
						BlendMode = value.Mode;
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameFilter>) {
						if (value.Id != 0) {
							throw std::runtime_error("motion Metal frame color-filter execution remains unsupported");
						}
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameTexture>) {
						if (value.Index < 0 || static_cast<std::size_t>(value.Index) >= Bindings.size() || !Bindings[static_cast<std::size_t>(value.Index)].Bound) {
							throw std::runtime_error("motion Metal frame texture lacks its actual prepared owner binding");
						}
						Texture = static_cast<std::size_t>(value.Index);
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameScissor>) {
						if (value.Update.Submission) {
							AppliedDevice = *value.Update.Submission;
							Scissor = FramePhysicalScissor(AppliedDevice, Description);
						}
					} else if constexpr (std::is_same_v<Event, scene::MotionBatchSubmission>) {
						Append(value);
					} else if constexpr (std::is_same_v<Event, scene::MotionMaskOperation>) {
						if (value.Kind != scene::MotionMaskOperationKind::Configure || value.Operation != 0 || value.ThresholdMinimum != 1 || value.ThresholdMaximum != 0) {
							throw std::runtime_error("motion Metal frame stencil/auxiliary execution remains unsupported");
						}
					} else {
						throw std::runtime_error("motion Metal frame received an unsupported mask/copy/blend event");
					}
				}
			},
			    event);
		}
	};

	MotionSpriteFrameRenderer::MotionSpriteFrameRenderer(SpritePassRenderer& renderer, scene::TextureCache& textures, scene::SpritePipelineOptions options, std::size_t texture_slots) : m_Implementation(std::make_unique<Implementation>(renderer, textures, options, texture_slots)) {
	}

	MotionSpriteFrameRenderer::~MotionSpriteFrameRenderer() = default;

	void MotionSpriteFrameRenderer::BindTexture(std::uint32_t actual_slot, scene::CachedTextureHandle actual_texture) {
		auto& state = *m_Implementation;
		if (state.Operating || actual_slot >= state.Bindings.size()) {
			throw std::runtime_error("motion Metal binding requires an idle actual bounded texture slot");
		}

		Implementation::TextureBinding binding;
		binding.Handle = actual_texture;
		binding.Snapshot = state.Textures.Get(actual_texture);
		binding.Address = state.Textures.Address(actual_texture);
		auto options = state.Options;
		if (binding.Snapshot.Minification == 0 && binding.Snapshot.Magnification == 0) {
			options.Sampling = scene::TextureSampling::Nearest;
		} else if (binding.Snapshot.Minification == 1 && binding.Snapshot.Magnification == 1) {
			options.Sampling = scene::TextureSampling::Linear;
		} else {
			throw std::runtime_error("motion Metal frame texture filtering pair remains unsupported");
		}
		options.AddressU = FrameTextureAddress(binding.Address.Horizontal);
		options.AddressV = FrameTextureAddress(binding.Address.Vertical);
		for (std::uint32_t flag = 0; flag < 2; ++flag) {
			for (std::uint32_t operation = 0; operation < 6; ++operation) {
				for (std::uint32_t key = 0; key <= 4; key += 4) {
					scene::MotionSpriteMaterial material;
					material.TextureFormat = binding.Snapshot.Description.Format;
					material.TextureAttributes = key;
					material.BlendMode = operation + flag * 16;
					binding.Pipelines[FramePipelineIndex(material.BlendMode, key)] = state.Renderer.PreparePipeline(scene::SelectSpritePipeline(material, options));
				}
			}
		}
		binding.Bound = true;
		state.Bindings[actual_slot] = std::move(binding);
	}

	void MotionSpriteFrameRenderer::UnbindTexture(std::uint32_t actual_slot) {
		auto& state = *m_Implementation;
		if (state.Operating || actual_slot >= state.Bindings.size()) {
			throw std::runtime_error("motion Metal unbinding requires an idle actual bounded texture slot");
		}

		state.Bindings[actual_slot] = Implementation::TextureBinding{};
	}

	MotionSpriteFrameSubmission MotionSpriteFrameRenderer::Submit(scene::MotionSpriteDrawContext& context, const scene::MotionPreparedFrame& globals, const scene::MotionFrameScratch& scratch, scene::MotionIntegerScissor actual_applied_device, const std::array<float, 16>& actual_projection, const std::array<float, 4>& clear_color, SpritePassLoadAction load_action) {
		auto& state = *m_Implementation;
		if (state.Operating || !state.Renderer.CanSubmit() || globals.Viewport.Width != static_cast<std::int32_t>(state.Description.Width) || globals.Viewport.Height != static_cast<std::int32_t>(state.Description.Height)) {
			throw std::runtime_error("motion Metal frame requires a ready actual target and matching physical viewport");
		}

		for (std::size_t index = 0; index < state.Bindings.size(); ++index) {
			const auto& binding = state.Bindings[index];
			if (binding.Bound) {
				state.RequireCurrent(binding);
				if (index >= globals.TextureSizes.size() || globals.TextureSizes[index].X != static_cast<float>(binding.Snapshot.Description.Width) || globals.TextureSizes[index].Y != static_cast<float>(binding.Snapshot.Description.Height)) {
					throw std::runtime_error("motion Metal frame texture dimensions differ from its actual uploaded source");
				}
			}
		}
		state.AppliedDevice = actual_applied_device;
		state.Scissor = FramePhysicalScissor(actual_applied_device, state.Description);
		state.Projection = actual_projection;
		state.ClearColor = clear_color;
		state.LoadAction = load_action;
		state.Submission.reset();
		state.VertexCount = 0;
		state.IndexCount = 0;
		state.DrawCount = 0;
		state.BlendMode = 0;
		state.Texture.reset();
		state.Begun = false;
		state.Ended = false;
		state.Operating = true;
		try {
			const bool populated = context.Size() != 0;
			context.Flush(globals, scratch, state.Output);
			if (populated) {
				if (!state.Begun || !state.Ended || !state.Submission) {
					throw std::runtime_error("motion Metal frame producer did not complete its actual frame");
				}
			} else {
				// An empty motion context emits no Begin/End; the caller still requests a target load operation.
				state.Submission = state.Renderer.Submit({}, actual_projection, clear_color, load_action);
			}
			MotionSpriteFrameSubmission result;
			result.Identity = *state.Submission;
			result.AppliedDevice = state.AppliedDevice;
			state.Operating = false;

			return result;
		} catch (...) {
			state.Operating = false;
			throw;
		}
	}

} // namespace sgre::render
