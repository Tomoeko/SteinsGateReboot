#include "sgre/render/motion_sprite_record_receiver.hpp"

#include "sgre/render/frame_host_pool.hpp"
#include "sgre/render/sprite_host_inputs.hpp"
#include "sgre/scene/motion_copy_region.hpp"
#include "sgre/scene/motion_copy_target.hpp"
#include "sgre/scene/motion_selected_sprite_player.hpp"
#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/motion_stencil_state.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::render {

	static constexpr std::size_t kMaximumLiveMotionTextureSlots = 1024;
	static constexpr std::size_t kMaximumPreparedMotionFrameEvents = 1048576;

	static std::size_t PacketPipelineIndex(std::uint32_t mode, std::uint32_t key) {
		if (key > 7) {
			throw std::runtime_error("live motion packet requires actual texture attribute bits0..2");
		}
		if (mode == 0x118) {
			return (key & 4) == 0 ? 26 : 27;
		}
		if (mode == 0x110 || mode == 0x112) {
			if ((key & 4) != 0) {
				return mode == 0x110 ? 24 : 25;
			}

			return mode == 0x110 ? 28 : 29;
		}
		if (mode > 5 && (mode < 16 || mode > 21)) {
			throw std::runtime_error("live motion packet requires measured ordinary blend or copied additive/overlay/darken variants");
		}

		return static_cast<std::size_t>((mode & 15) + ((mode & 16) != 0 ? 6 : 0)) * 2 + ((key & 4) != 0 ? 1 : 0);
	}

	static scene::TextureAddress PacketTextureAddress(std::uint32_t value) {
		switch (value) {
		case 0:
			return scene::TextureAddress::Clamp;
		case 1:
			return scene::TextureAddress::Repeat;
		case 2:
			return scene::TextureAddress::Mirror;
		default:
			throw std::runtime_error("live motion texture address is outside the recovered native indices");
		}
	}

	static scene::MotionShaderTexture PacketTextureChannels(resource::PixelFormat format) {
		switch (format) {
		case resource::PixelFormat::Bc7:
			return scene::MotionShaderTexture::Rgba;
		case resource::PixelFormat::Alpha8:
			return scene::MotionShaderTexture::Alpha;
		case resource::PixelFormat::Luminance8:
			return scene::MotionShaderTexture::Luminance;
		default:
			throw std::runtime_error("live motion cached resource has an unsupported channel interpretation");
		}
	}

	static void FingerprintWord(std::uint64_t& hash, std::uint64_t word) {
		hash ^= word;
		hash *= 1099511628211ull;
	}

	static void FingerprintReal(std::uint64_t& hash, float value) {
		FingerprintWord(hash, std::bit_cast<std::uint32_t>(value));
	}

	static void FingerprintBounds(std::uint64_t& hash, const scene::MotionBounds& bounds) {
		FingerprintReal(hash, bounds.MinimumX);
		FingerprintReal(hash, bounds.MinimumY);
		FingerprintReal(hash, bounds.MaximumX);
		FingerprintReal(hash, bounds.MaximumY);
	}

	static std::uint64_t EventFingerprint(const scene::MotionFrameEvent& event) {
		std::uint64_t hash = 14695981039346656037ull;
		FingerprintWord(hash, event.index());
		std::visit([&](const auto& value) {
			using Event = std::remove_cvref_t<decltype(value)>;
			if constexpr (std::is_same_v<Event, scene::MotionFrameBlend>) {
				FingerprintWord(hash, value.Mode);
				FingerprintReal(hash, value.Parameter);
			} else if constexpr (std::is_same_v<Event, scene::MotionFrameFilter>) {
				FingerprintWord(hash, value.Id);
				FingerprintWord(hash, value.Color);
			} else if constexpr (std::is_same_v<Event, scene::MotionFrameTexture>) {
				FingerprintWord(hash, static_cast<std::uint32_t>(value.Index));
			} else if constexpr (std::is_same_v<Event, scene::MotionMaskOperation>) {
				FingerprintWord(hash, static_cast<std::uint32_t>(value.Kind));
				FingerprintWord(hash, value.Reference);
				FingerprintWord(hash, value.Operation);
				FingerprintReal(hash, value.ThresholdMinimum);
				FingerprintReal(hash, value.ThresholdMaximum);
				FingerprintWord(hash, std::hash<const scene::MotionClipCommand*>{}(value.Command));
			} else if constexpr (std::is_same_v<Event, scene::MotionFrameCandidate>) {
				FingerprintWord(hash, std::hash<const scene::MotionPreparedFrameCommand*>{}(value.Command));
			} else if constexpr (std::is_same_v<Event, scene::MotionFrameScissor>) {
				FingerprintBounds(hash, value.Update.Raw);
				FingerprintBounds(hash, value.Update.Rounded);
				FingerprintWord(hash, value.Update.Submission.has_value());
				if (value.Update.Submission) {
					FingerprintWord(hash, static_cast<std::uint32_t>(value.Update.Submission->MinimumX));
					FingerprintWord(hash, static_cast<std::uint32_t>(value.Update.Submission->MinimumY));
					FingerprintWord(hash, static_cast<std::uint32_t>(value.Update.Submission->MaximumX));
					FingerprintWord(hash, static_cast<std::uint32_t>(value.Update.Submission->MaximumY));
				}
			} else if constexpr (std::is_same_v<Event, scene::MotionFrameBlendBounds>) {
				FingerprintBounds(hash, value.Bounds);
			} else if constexpr (std::is_same_v<Event, scene::MotionBatchSubmission>) {
				FingerprintWord(hash, static_cast<std::uint32_t>(value.Mode));
				FingerprintWord(hash, value.Key);
				FingerprintWord(hash, value.Positions.size());
				FingerprintWord(hash, value.Uvs.size());
				FingerprintWord(hash, value.Colors.size());
				FingerprintWord(hash, value.Indices.size());
				for (const auto& position : value.Positions) {
					FingerprintReal(hash, position.X);
					FingerprintReal(hash, position.Y);
				}
				for (const auto& uv : value.Uvs) {
					FingerprintReal(hash, uv.X);
					FingerprintReal(hash, uv.Y);
				}
				for (const auto color : value.Colors) {
					FingerprintWord(hash, color);
				}
				for (const auto index : value.Indices) {
					FingerprintWord(hash, index);
				}
			}
		},
		    event);

		return hash;
	}

	static bool SameReal(float left, float right) {
		return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
	}

	static bool SameBounds(const scene::MotionBounds& left, const scene::MotionBounds& right) {
		return SameReal(left.MinimumX, right.MinimumX) && SameReal(left.MinimumY, right.MinimumY) && SameReal(left.MaximumX, right.MaximumX) && SameReal(left.MaximumY, right.MaximumY);
	}

	static void AccumulateCopyBounds(scene::MotionBounds& accumulated, const scene::MotionBounds& bounds) {
		accumulated.MinimumX = std::min(accumulated.MinimumX, bounds.MinimumX);
		accumulated.MinimumY = std::min(accumulated.MinimumY, bounds.MinimumY);
		accumulated.MaximumX = std::max(accumulated.MaximumX, bounds.MaximumX);
		accumulated.MaximumY = std::max(accumulated.MaximumY, bounds.MaximumY);
	}

	class MotionSpriteRecordReceiver::Implementation {
	public:
		using InputOwner = std::variant<std::reference_wrapper<FrameHostHeap>, std::reference_wrapper<FrameHostPool>>;

		struct Binding {
			std::optional<scene::CachedTextureHandle> Handle;
			std::optional<scene::MotionFrameExternalTexture> External;
			scene::NativeTextureSnapshot Native;
			scene::CachedTexture Texture;
			scene::CachedTextureAddress Address;
			std::array<SpritePipelineHandle, 30> Pipelines;
			std::size_t PipelineCount = 24;
		};

		struct CopyRequest {
			scene::MotionCopyRegionPlan Plan;
			scene::MutableTextureOwner* Target = nullptr;
			SpriteTextureHandle Handle;
			std::array<float, 4> UvAffine{};
		};

		Implementation(SpriteRenderRecords& records, SpritePassRenderer& renderer, SpriteGraphicsTargets& graphics, scene::TextureCache& textures, InputOwner input_owner, std::size_t texture_slots)
		    : Records(records), Renderer(renderer), Graphics(graphics), Textures(textures), InputProducer(input_owner) {
			Records.RequireRenderer(renderer);
			RequireInputOwner();
			if (Records.IsActive() || texture_slots == 0 || texture_slots > kMaximumLiveMotionTextureSlots) {
				throw std::runtime_error("live motion receiver preparation requires actual quiescence and bounded texture slots");
			}

			const auto description = Renderer.Description();
			Bindings.resize(texture_slots);
			PreparedBindings.resize(texture_slots);
			ExternalBindings.resize(texture_slots);
			TextureSizes.resize(texture_slots);
			Vertices.resize(description.MaxVertices);
			MaximumIndices = description.MaxIndices;
			// Fixed construction supports nonmovable input owners; emplace never relocates an earlier batch.
			Inputs = std::vector<std::optional<SpriteHostInputs>>(description.MaxDraws);
			InputBorrows = std::vector<std::optional<FrameHostInputBorrow>>(description.MaxDraws);
			EventFingerprints.resize(kMaximumPreparedMotionFrameEvents);
			Copies.resize(description.MaxDraws);
			NewCopyTargets.resize(description.MaxDraws);
			RetiredCopies.resize(description.MaxDraws + 1);
			CopyBackend = Renderer.CreateMutableTextureBackend();
			Records.PrepareTextureMask();
			Output = [this](const scene::MotionFrameEvent& event) {
				Receive(event);
			};
		}

		~Implementation() noexcept {
			try {
				if (InputCount != 0 && Records.IsActive()) {
					std::terminate();
				}
			} catch (...) {
				std::terminate();
			}
		}

		SpriteRenderRecords& Records;
		SpritePassRenderer& Renderer;
		SpriteGraphicsTargets& Graphics;
		scene::TextureCache& Textures;
		InputOwner InputProducer;
		std::vector<Binding> Bindings;
		std::vector<Binding> PreparedBindings;
		std::vector<Binding> ExternalBindings;
		std::vector<resource::MotionPoint> TextureSizes;
		std::vector<scene::MotionSpriteVertex> Vertices;
		std::vector<std::optional<SpriteHostInputs>> Inputs;
		std::vector<std::optional<FrameHostInputBorrow>> InputBorrows;
		std::function<void(const scene::MotionFrameEvent&)> Output;
		std::size_t InputCount = 0;
		std::size_t MaximumIndices = 0;
		std::optional<std::int32_t> Texture;
		std::uint32_t BlendMode = 0;
		bool Operating = false;
		bool Begun = false;
		bool Ended = false;
		std::optional<scene::MotionPreparedFrame> Frame;
		std::vector<std::uint64_t> EventFingerprints;
		std::vector<CopyRequest> Copies;
		std::unique_ptr<scene::MutableTextureBackend> CopyBackend;
		std::vector<std::unique_ptr<scene::MutableTextureOwner>> NewCopyTargets;
		std::vector<std::unique_ptr<scene::MutableTextureOwner>> RetiredCopies;
		std::size_t RetiredCopyCount = 0;
		bool PreparingLive = false;
		bool ActualOccurrence = false;
		std::unique_ptr<scene::MutableTextureOwner> RetainedCopy;
		SpriteTextureHandle RetainedCopyHandle;
		std::array<float, 4> RetainedCopyAffine{};
		scene::MutableTextureOwner* ActualCopy = nullptr;
		SpriteTextureHandle ActualCopyHandle;
		std::array<float, 4> ActualCopyAffine{};
		std::size_t PreparedEventCount = 0;
		std::size_t PreparedCopyCount = 0;
		std::size_t NewCopyCount = 0;
		std::size_t EventIndex = 0;
		std::size_t CopyIndex = 0;
		bool RetainedCopySelected = false;
		std::uint64_t CopyBindingRevision = 0;
		std::uint64_t PreparedBindingRevision = 0;
		bool ActualCopySelected = false;
		bool UsingPreparedFrame = false;
		bool FrameDrawn = false;
		bool FrameFailed = false;
		bool FrameHasMask = false;

		void RequireInputOwner() const {
			std::visit([&](auto producer) {
				using Owner = std::remove_reference_t<decltype(producer.get())>;
				if constexpr (std::is_same_v<Owner, FrameHostPool>) {
					producer.get().RequireRecords(Records);
				} else {
					static_cast<void>(producer.get().FrameCounter());
				}
			},
			    InputProducer);
		}

		void RequireQuiescentPreparation(bool allow_prepared_frame = false) const {
			Records.RequireRenderer(Renderer);
			if (Operating || Records.IsActive() || (Frame && !allow_prepared_frame)) {
				throw std::runtime_error("live motion binding/input retirement requires actual outer-frame quiescence");
			}
		}

		void RequireCurrent(const Binding& binding, bool check_prepared_gpu) const {
			if (binding.External) {
				const auto& external = *binding.External;
				if (external.Table == nullptr || external.Reference == nullptr || external.Table->Get(external.Slot).External != external.Reference) {
					throw std::runtime_error("live motion external binding lost its actual descriptor registration");
				}
				const auto current = external.Reference->Read();
				if (current.Texture != binding.Native.Texture || current.Minification != binding.Native.Minification || current.Magnification != binding.Native.Magnification || current.AddressU != binding.Native.AddressU || current.AddressV != binding.Native.AddressV || current.OriginalFormat != binding.Native.OriginalFormat || !SameReal(current.Size.X, binding.Native.Size.X) || !SameReal(current.Size.Y, binding.Native.Size.Y)) {
					throw std::runtime_error("live motion external source or sampler changed before record admission");
				}
				if (check_prepared_gpu) {
					const auto actual = Records.PreparedTexture(current.Texture);
					if (!SameReal(current.Size.X, static_cast<float>(actual.Width)) || !SameReal(current.Size.Y, static_cast<float>(actual.Height)) || actual.Channels != scene::ClassifyMotionShaderTexture(true, current.OriginalFormat) || (actual.OriginalDxgiFormat && *actual.OriginalDxgiFormat != current.OriginalFormat)) {
						throw std::runtime_error("live motion external resource differs from its actual native descriptor");
					}
					for (std::size_t index = 0; index < binding.PipelineCount; ++index) {
						static_cast<void>(Records.PreparedPipeline(binding.Pipelines[index]));
					}
				}

				return;
			}
			const auto texture = Textures.Get(*binding.Handle);
			const auto address = Textures.Address(*binding.Handle);
			if (texture.Backend != binding.Texture.Backend || texture.Description.Format != binding.Texture.Description.Format || texture.Description.Width != binding.Texture.Description.Width || texture.Description.Height != binding.Texture.Description.Height || texture.Minification != binding.Texture.Minification || texture.Magnification != binding.Texture.Magnification || address.Revision != binding.Address.Revision) {
				throw std::runtime_error("live motion prepared resource/filter/address changed before record admission");
			}
			if (!check_prepared_gpu) {
				return;
			}

			const auto actual = Records.PreparedTexture(texture.Backend);
			if (actual.Width != texture.Description.Width || actual.Height != texture.Description.Height || actual.Channels != PacketTextureChannels(texture.Description.Format)) {
				throw std::runtime_error("live motion actual sampled resource differs from its prepared cache binding");
			}
			for (std::size_t index = 0; index < binding.PipelineCount; ++index) {
				static_cast<void>(Records.PreparedPipeline(binding.Pipelines[index]));
			}
		}

		std::size_t RequireDenseBindings(bool check_prepared_gpu = true) const {
			std::size_t count = 0;
			bool gap = false;
			for (const auto& binding : Bindings) {
				if (!binding.Handle) {
					gap = true;
					continue;
				}
				if (gap) {
					throw std::runtime_error("live motion receiver requires a dense actual texture prefix; sparse holes remain unsupported");
				}
				RequireCurrent(binding, check_prepared_gpu);
				++count;
			}

			return count;
		}

		std::size_t RequirePreparedBindings(const scene::MotionPreparedFrame& frame, bool check_prepared_gpu = true) const {
			if (!ActualOccurrence) {
				return RequireDenseBindings(check_prepared_gpu);
			}
			if (frame.TextureSizes.size() > Bindings.size()) {
				throw std::runtime_error("actual motion sparse local slot extent exceeds its prepared storage");
			}
			for (std::size_t slot = 0; slot < frame.TextureSizes.size(); ++slot) {
				if (Bindings[slot].Handle) {
					RequireCurrent(Bindings[slot], check_prepared_gpu);
				} else if (frame.TextureSizes[slot].X != 0 || frame.TextureSizes[slot].Y != 0 || TextureSizes[slot].X != 0 || TextureSizes[slot].Y != 0) {
					throw std::runtime_error("actual motion empty local slot must remain unbound with zero dimensions");
				}
			}

			return frame.TextureSizes.size();
		}

		void PrepareBindingPipelines(Binding& binding, resource::PixelFormat channel_class, std::uint32_t minimum, std::uint32_t maximum, std::uint32_t address_u, std::uint32_t address_v, bool live) {
			std::array<scene::SpritePipeline, 30> descriptions;
			std::size_t description_count = 0;
			const auto prepare_pipeline = [&](const scene::SpritePipeline& pipeline) {
				if (!live) {
					return Renderer.PreparePipeline(pipeline);
				}
				if (description_count == descriptions.size()) {
					throw std::runtime_error("motion pipeline descriptions exceed actual typed preparation storage");
				}
				descriptions[description_count++] = pipeline;
				return SpritePipelineHandle{};
			};
			scene::SpritePipelineOptions options;
			if (minimum == 0 && maximum == 0) {
				options.Sampling = scene::TextureSampling::Nearest;
			} else if (minimum == 1 && maximum == 1) {
				options.Sampling = scene::TextureSampling::Linear;
			} else {
				throw std::runtime_error("live motion texture requires its recovered common filtering pair");
			}
			options.AddressU = PacketTextureAddress(address_u);
			options.AddressV = PacketTextureAddress(address_v);
			for (std::uint32_t flag = 0; flag < 2; ++flag) {
				for (std::uint32_t operation = 0; operation < 6; ++operation) {
					for (std::uint32_t key = 0; key <= 4; key += 4) {
						scene::MotionSpriteMaterial material;
						material.TextureFormat = channel_class;
						material.TextureAttributes = key;
						material.BlendMode = operation + flag * 16;
						binding.Pipelines[PacketPipelineIndex(material.BlendMode, key)] = prepare_pipeline(scene::SelectSpritePipeline(material, options));
					}
				}
			}
			if (channel_class == resource::PixelFormat::Bc7) {
				for (const auto mode : std::array<std::uint32_t, 2>{0x110, 0x112}) {
					scene::MotionSpriteMaterial material;
					material.TextureFormat = channel_class;
					material.TextureAttributes = 4;
					material.BlendMode = mode;
					binding.Pipelines[PacketPipelineIndex(mode, 4)] = prepare_pipeline(scene::SelectSpritePipeline(material, options));
				}
				for (const std::uint32_t key : std::array<std::uint32_t, 2>{0, 4}) {
					scene::MotionSpriteMaterial material;
					material.TextureFormat = channel_class;
					material.TextureAttributes = key;
					material.BlendMode = 0x118;
					binding.Pipelines[PacketPipelineIndex(0x118, key)] = prepare_pipeline(scene::SelectSpritePipeline(material, options));
				}
				// Append after the existing indices24..27: live typed outputs retain identical index order.
				for (const auto mode : std::array<std::uint32_t, 2>{0x110, 0x112}) {
					scene::MotionSpriteMaterial material;
					material.TextureFormat = channel_class;
					material.TextureAttributes = 0;
					material.BlendMode = mode;
					binding.Pipelines[PacketPipelineIndex(mode, 0)] = prepare_pipeline(scene::SelectSpritePipeline(material, options));
				}
				binding.PipelineCount = 30;
			}

			if (live) {
				Records.PrepareMotionPipelines(std::span<const scene::SpritePipeline>(descriptions).first(description_count), std::span<SpritePipelineHandle>(binding.Pipelines).first(description_count));
			}
		}

		Binding PrepareBinding(scene::CachedTextureHandle actual_texture, bool live) {
			Binding binding;
			binding.Handle = actual_texture;
			binding.Texture = Textures.Get(actual_texture);
			binding.Address = Textures.Address(actual_texture);
			PrepareBindingPipelines(binding, binding.Texture.Description.Format, static_cast<std::uint32_t>(binding.Texture.Minification), static_cast<std::uint32_t>(binding.Texture.Magnification), binding.Address.Horizontal, binding.Address.Vertical, live);

			return binding;
		}

		static resource::PixelFormat ExternalChannelClass(std::uint32_t original_format) {
			switch (scene::ClassifyMotionShaderTexture(true, original_format)) {
			case scene::MotionShaderTexture::Rgba:
				return resource::PixelFormat::Bc7;
			case scene::MotionShaderTexture::Alpha:
				return resource::PixelFormat::Alpha8;
			case scene::MotionShaderTexture::Luminance:
				return resource::PixelFormat::Luminance8;
			default:
				throw std::runtime_error("motion external source has no recovered ordinary shader channel interpretation");
			}
		}

		static std::uint32_t ExternalAddressIndex(std::uint32_t native_word) {
			switch (native_word) {
			case 3:
				return 0;
			case 1:
				return 1;
			case 2:
				return 2;
			default:
				throw std::runtime_error("motion external source has an unsupported native address word");
			}
		}

		void PrepareExternalTextures(const scene::MotionPreparedFrame& frame, bool live) {
			if (frame.ExternalTextures.size() > ExternalBindings.size()) {
				throw std::runtime_error("motion external texture domain exceeds this receiver's actual source capacity");
			}
			for (std::size_t index = 0; index < frame.ExternalTextures.size(); ++index) {
				const auto& actual = frame.ExternalTextures[index];
				if (actual.Reference == nullptr) {
					ExternalBindings[index] = Binding{};
					continue;
				}
				if (actual.Table == nullptr || actual.Slot.Index != index || actual.Table->Get(actual.Slot).External != actual.Reference) {
					throw std::runtime_error("motion external frame must retain its exact descriptor slot and resource owner");
				}
				const auto snapshot = actual.Reference->Read();
				if (snapshot.MutableSource != nullptr) {
					SpriteTextureHandle bound;
					if (live) {
						bound = Records.PrepareMutableTexture(*snapshot.MutableSource, 0, scene::MutableTextureStage::Pixel);
					} else {
						snapshot.MutableSource->Bind(0, scene::MutableTextureStage::Pixel);
						bound = Renderer.MutableTextureBinding(0, scene::MutableTextureStage::Pixel);
					}
					if (bound != snapshot.Texture) {
						throw std::runtime_error("motion external mutable preparation changed its actual descriptor texture identity");
					}
				}
				static_cast<void>(Records.PreparedTexture(snapshot.Texture));
				auto& prepared = ExternalBindings[index];
				const auto same_source = prepared.External && prepared.External->Table == actual.Table && prepared.External->Slot == actual.Slot && prepared.External->Reference == actual.Reference && prepared.Native.Texture == snapshot.Texture && prepared.Native.OriginalFormat == snapshot.OriginalFormat && prepared.Native.Minification == snapshot.Minification && prepared.Native.Magnification == snapshot.Magnification && prepared.Native.AddressU == snapshot.AddressU && prepared.Native.AddressV == snapshot.AddressV;
				if (!same_source) {
					Binding replacement;
					replacement.External = actual;
					replacement.Native = snapshot;
					PrepareBindingPipelines(replacement, ExternalChannelClass(snapshot.OriginalFormat), snapshot.Minification, snapshot.Magnification, ExternalAddressIndex(snapshot.AddressU), ExternalAddressIndex(snapshot.AddressV), live);
					prepared = replacement;
				}
				RequireCurrent(prepared, true);
			}
			for (std::size_t index = frame.ExternalTextures.size(); index < ExternalBindings.size(); ++index) {
				ExternalBindings[index] = Binding{};
			}
		}

		const Binding& ResolveBinding(std::int32_t index) const {
			if (index >= 0) {
				if (static_cast<std::size_t>(index) >= Bindings.size() || !Bindings[static_cast<std::size_t>(index)].Handle) {
					throw std::runtime_error("motion texture event lacks its actual prepared local binding");
				}

				return Bindings[static_cast<std::size_t>(index)];
			}
			const auto shared = ~std::bit_cast<std::uint32_t>(index);
			if (shared >= ExternalBindings.size() || !ExternalBindings[shared].External) {
				throw std::runtime_error("motion texture event lacks its actual prepared external descriptor");
			}

			return ExternalBindings[shared];
		}

		void BindActualTextures(std::span<const scene::CachedTextureHandle> actual_slots, const scene::MotionPreparedFrame& frame) {
			if (actual_slots.size() > Bindings.size() || actual_slots.size() != frame.TextureSizes.size()) {
				throw std::runtime_error("actual motion occurrence requires its exact bounded local slot extent and dimensions");
			}
			// Only the exact default handle is empty; every real or partial handle keeps its actual cache identity checks.
			for (std::size_t slot = 0; slot < actual_slots.size(); ++slot) {
				if (actual_slots[slot] == scene::CachedTextureHandle{}) {
					if (frame.TextureSizes[slot].X != 0 || frame.TextureSizes[slot].Y != 0) {
						throw std::runtime_error("actual motion empty local slot cannot supply fabricated texture dimensions");
					}
					continue;
				}
				const auto texture = Textures.Get(actual_slots[slot]);
				if (!SameReal(frame.TextureSizes[slot].X, static_cast<float>(texture.Description.Width)) || !SameReal(frame.TextureSizes[slot].Y, static_cast<float>(texture.Description.Height))) {
					throw std::runtime_error("actual motion occurrence local texture dimensions differ from its exact cache owner");
				}
				static_cast<void>(Records.PreparedTexture(texture.Backend));
			}
			for (std::size_t slot = 0; slot < actual_slots.size(); ++slot) {
				if (actual_slots[slot] == scene::CachedTextureHandle{}) {
					Bindings[slot] = Binding{};
					TextureSizes[slot] = {};
					continue;
				}
				auto found = std::find_if(PreparedBindings.begin(), PreparedBindings.end(), [&](const auto& binding) {
					return binding.Handle == actual_slots[slot];
				});
				bool reusable = false;
				if (found != PreparedBindings.end()) {
					const auto texture = Textures.Get(actual_slots[slot]);
					const auto address = Textures.Address(actual_slots[slot]);
					reusable = found->Texture.Backend == texture.Backend && found->Texture.Description.Width == texture.Description.Width && found->Texture.Description.Height == texture.Description.Height && found->Texture.Description.Format == texture.Description.Format && found->Texture.Minification == texture.Minification && found->Texture.Magnification == texture.Magnification && found->Address.Revision == address.Revision;
				} else {
					found = std::find_if(PreparedBindings.begin(), PreparedBindings.end(), [&](const auto& binding) {
						return !binding.Handle || !Textures.Contains(*binding.Handle);
					});
				}
				if (found == PreparedBindings.end()) {
					throw std::runtime_error("actual motion texture owners exceed the prepared cache capacity");
				}
				if (!reusable) {
					*found = PrepareBinding(actual_slots[slot], true);
				}
				Bindings[slot] = *found;
				TextureSizes[slot] = frame.TextureSizes[slot];
			}
			for (std::size_t slot = actual_slots.size(); slot < Bindings.size(); ++slot) {
				Bindings[slot] = Binding{};
				TextureSizes[slot] = resource::MotionPoint{};
			}
		}

		void Append(const scene::MotionBatchSubmission& packet) {
			if (!Texture || packet.Mode != scene::MotionBatchMode::IndexedTriangleStrip || packet.Positions.size() != packet.Uvs.size() || packet.Positions.size() != packet.Colors.size() || packet.Positions.size() < 3 || packet.Indices.size() < 3 || packet.Indices.size() > MaximumIndices || packet.Positions.size() > Vertices.size() || InputCount == Inputs.size()) {
				throw std::runtime_error("live motion packet exceeds its actual indexed geometry/input capacity");
			}
			if (UsingPreparedFrame && (BlendMode & 0xf00) != 0x100) {
				Records.ClearCopiedDestination();
				ActualCopySelected = false;
			}
			const auto pipeline_index = PacketPipelineIndex(BlendMode, packet.Key);
			const auto& binding = ResolveBinding(*Texture);
			RequireCurrent(binding, true);
			if (pipeline_index >= binding.PipelineCount) {
				throw std::runtime_error("live copied motion packet requires its actual RGBA source");
			}
			for (std::size_t index = 0; index < packet.Positions.size(); ++index) {
				auto& vertex = Vertices[index];
				vertex.Position = packet.Positions[index];
				vertex.Uv = packet.Uvs[index];
				// The packet carries AARRGGBB; the host-input writer accepts RRGGBBAA before native BGRA packing.
				vertex.Color = std::rotl(packet.Colors[index], 8);
			}
			auto& inputs = Inputs[InputCount];
			const auto vertices = std::span<const scene::MotionSpriteVertex>(Vertices).first(packet.Positions.size());
			std::visit([&](auto producer) {
				using Owner = std::remove_reference_t<decltype(producer.get())>;
				if constexpr (std::is_same_v<Owner, FrameHostPool>) {
					producer.get().EmplaceInputs(inputs, InputBorrows[InputCount], vertices, packet.Indices);
				} else {
					inputs.emplace(producer.get(), vertices, packet.Indices);
				}
			},
			    InputProducer);
			++InputCount;
			Records.Append(binding.External ? binding.Native.Texture : binding.Texture.Backend, binding.Pipelines[pipeline_index], *inputs, packet.Key);
		}

		void RequireFrameTextures(const scene::MotionPreparedFrame& frame, std::size_t count) const {
			if (frame.TextureSizes.size() != count) {
				throw std::runtime_error("prepared motion frame requires the exact actual local texture slot extent");
			}
			for (std::size_t index = 0; index < count; ++index) {
				if (!SameReal(frame.TextureSizes[index].X, TextureSizes[index].X) || !SameReal(frame.TextureSizes[index].Y, TextureSizes[index].Y)) {
					throw std::runtime_error("prepared motion frame texture dimensions changed from the actual registered source");
				}
			}
			const auto description = Records.IsActive() ? Records.CurrentRenderTarget().Description() : Renderer.Description();
			if (frame.Viewport.Width <= 0 || frame.Viewport.Height <= 0 || static_cast<std::uint32_t>(frame.Viewport.Width) != description.Width || static_cast<std::uint32_t>(frame.Viewport.Height) != description.Height) {
				throw std::runtime_error("prepared motion frame viewport differs from its exact actual render target");
			}
		}

		void RequireCurrentFrameGlobals(const scene::MotionPreparedFrame& frame, bool actual_slots) const {
			const auto globals = actual_slots ? Records.SceneFrameGlobals(frame.TextureSizes) : Records.OrdinaryFrameGlobals(frame.TextureSizes);
			if (!SameBounds(globals.InitialScissor, frame.InitialScissor) || !SameBounds(globals.AppliedRoundedScissor, frame.AppliedRoundedScissor) || globals.Viewport.Width != frame.Viewport.Width || globals.Viewport.Height != frame.Viewport.Height) {
				throw std::runtime_error("prepared motion publication lost its actual camera/clip/viewport owner");
			}
			for (std::size_t index = 0; index < globals.View.Coefficients.size(); ++index) {
				if (!SameReal(globals.View.Coefficients[index], frame.View.Coefficients[index]) || !SameReal(globals.Model.Coefficients[index], frame.Model.Coefficients[index])) {
					throw std::runtime_error("prepared motion publication lost its actual view/model scope");
				}
			}
		}

		CopyRequest PrepareCopy(const scene::MotionCopyRegionPlan& plan, scene::MutableTextureOwner*& target, scene::MotionCopiedTargetSize& size, SpriteTextureHandle& actual_handle) {
			CopyRequest request;
			request.Plan = plan;
			if (plan.Action != scene::MotionCopyRegionAction::Copy) {
				return request;
			}
			const auto update = scene::PrepareMotionCopiedTargetUpdate(size, plan.Rectangle);
			if (update.Replace) {
				if (NewCopyCount == NewCopyTargets.size() || RetiredCopyCount + NewCopyCount + static_cast<std::size_t>(RetainedCopy != nullptr) >= RetiredCopies.size()) {
					throw std::runtime_error("prepared copied regions exceed the actual frame replacement capacity");
				}
				if (!CopyBackend) {
					CopyBackend = Renderer.CreateMutableTextureBackend();
				}
				scene::MutableTextureSpec spec;
				spec.Storage = scene::MutableTextureStorage::RenderTarget;
				spec.Description.Width = static_cast<std::uint32_t>(update.Size.Width);
				spec.Description.Height = static_cast<std::uint32_t>(update.Size.Height);
				spec.Description.OriginalFormat = 28;
				auto& replacement = NewCopyTargets[NewCopyCount++];
				if (PreparingLive) {
					auto prepared = Records.PrepareMotionCopiedTarget(*CopyBackend, spec.Description.Width, spec.Description.Height);
					replacement = std::move(prepared.Owner);
					actual_handle = prepared.Handle;
				} else {
					replacement = std::make_unique<scene::MutableTextureOwner>(*CopyBackend, spec);
					replacement->SetFiltering(0, 0);
					replacement->SetAddress(0, 0);
					replacement->Bind(2, scene::MutableTextureStage::Pixel);
					actual_handle = Renderer.MutableTextureBinding(2, scene::MutableTextureStage::Pixel);
				}
				target = replacement.get();
				size = update.Size;
			}
			if (target == nullptr) {
				throw std::runtime_error("prepared copied region lacks its actual retained/replacement target");
			}
			request.Target = target;
			if (!PreparingLive) {
				target->Bind(2, scene::MutableTextureStage::Pixel);
				actual_handle = Renderer.MutableTextureBinding(2, scene::MutableTextureStage::Pixel);
			}
			request.Handle = actual_handle;
			const auto width = static_cast<float>(size.Width);
			const auto height = static_cast<float>(size.Height);
			request.UvAffine = {1.0f / width, 1.0f / height, (-plan.Rectangle.MinimumX + 0.0f) / width, (-plan.Rectangle.MinimumY + 0.0f) / height};

			return request;
		}

		void Prepare(const scene::MotionPreparedFrame& frame, const scene::MotionFrameScratch& scratch) {
			if (!PreparingLive) {
				RequireQuiescentPreparation();
			}
			RequireInputOwner();
			if (Frame || (!PreparingLive && InputCount != 0) || Graphics.CurrentTarget().get() != (PreparingLive ? &Records.CurrentRenderTarget() : &Renderer)) {
				throw std::runtime_error("motion frame preparation requires an unborrowed receiver and exact actual target");
			}
			RequireFrameTextures(frame, RequirePreparedBindings(frame, false));
			PrepareExternalTextures(frame, PreparingLive);
			Frame = frame;
			PreparedBindingRevision = Records.TextureBindingRevision();
			PreparedEventCount = 0;
			PreparedCopyCount = 0;
			NewCopyCount = 0;
			FrameDrawn = false;
			FrameFailed = false;
			FrameHasMask = false;
			const auto binding_revision = Records.TextureBindingRevision();
			if (CopyBindingRevision != binding_revision) {
				// Native changed target selection clears pixel resources0..3, including copied t2.
				RetainedCopySelected = false;
				CopyBindingRevision = binding_revision;
			}
			ActualCopy = RetainedCopy.get();
			ActualCopyHandle = RetainedCopyHandle;
			ActualCopyAffine = RetainedCopyAffine;
			ActualCopySelected = RetainedCopySelected;
			auto* target = RetainedCopy.get();
			auto target_handle = RetainedCopyHandle;
			scene::MotionCopiedTargetSize size;
			if (target != nullptr) {
				size.Width = static_cast<std::int32_t>(target->State().Description.Width);
				size.Height = static_cast<std::int32_t>(target->State().Description.Height);
			}
			scene::MotionBounds accumulated;
			auto clip = frame.InitialScissor;
			std::optional<std::int32_t> texture;
			std::uint32_t blend = 0;
			float parameter = 0;
			bool selected = RetainedCopySelected;
			bool has_mask = false;
			std::size_t packets = 0;
			try {
				const auto prepare_event = [&](const scene::MotionFrameEvent& event) {
					if (PreparedEventCount == EventFingerprints.size()) {
						throw std::runtime_error("prepared motion frame exceeds its bounded exact event storage");
					}
					EventFingerprints[PreparedEventCount++] = EventFingerprint(event);
					if (const auto* value = std::get_if<scene::MotionFrameBlend>(&event)) {
						static_cast<void>(PacketPipelineIndex(value->Mode, 4));
						if (value->Parameter != 0) {
							throw std::runtime_error("live prepared motion blend parameter remains outside its measured zero domain");
						}
						blend = value->Mode;
						parameter = value->Parameter;
					} else if (const auto* value = std::get_if<scene::MotionFrameScissor>(&event)) {
						// Graphics+b8 is raw, even when rounded-cache equality suppresses its physical request.
						clip = value->Update.Raw;
					} else if (const auto* value = std::get_if<scene::MotionFrameTexture>(&event)) {
						static_cast<void>(ResolveBinding(value->Index));
						texture = value->Index;
					} else if (const auto* value = std::get_if<scene::MotionFrameFilter>(&event)) {
						if (value->Id != 0) {
							throw std::runtime_error("live motion color-filter execution remains unsupported");
						}
					} else if (const auto* value = std::get_if<scene::MotionFrameBlendBounds>(&event)) {
						if (PreparedCopyCount == Copies.size()) {
							throw std::runtime_error("prepared motion copied requests exceed the actual packet capacity");
						}
						AccumulateCopyBounds(accumulated, value->Bounds);
						scene::MotionCopyRegionInput input;
						input.Target = frame.Viewport;
						input.View = frame.View;
						input.Model = frame.Model;
						input.Accumulated = accumulated;
						input.Clip = clip;
						input.BlendMode = blend;
						input.BlendParameter = parameter;
						const auto plan = scene::PrepareMotionCopyRegion(input);
						Copies[PreparedCopyCount++] = PrepareCopy(plan, target, size, target_handle);
						if (plan.Action == scene::MotionCopyRegionAction::Copy) {
							accumulated = scene::MotionBounds{};
							selected = true;
						} else if (plan.Action == scene::MotionCopyRegionAction::Disable) {
							selected = false;
						}
					} else if (const auto* value = std::get_if<scene::MotionMaskOperation>(&event)) {
						has_mask = has_mask || value->Kind != scene::MotionMaskOperationKind::Configure || value->Operation != 0 || value->ThresholdMinimum != 1 || value->ThresholdMaximum != 0;
					} else if (const auto* value = std::get_if<scene::MotionBatchSubmission>(&event)) {
						if ((blend & 0xf00) != 0x100) {
							selected = false;
						}
						if (!texture || value->Mode != scene::MotionBatchMode::IndexedTriangleStrip || value->Positions.size() < 3 || value->Positions.size() != value->Uvs.size() || value->Positions.size() != value->Colors.size() || value->Positions.size() > Vertices.size() || value->Indices.size() < 3 || value->Indices.size() > MaximumIndices || ++packets > Inputs.size() - InputCount) {
							throw std::runtime_error("prepared motion packet exceeds the actual geometry/input capacity");
						}
						if (PacketPipelineIndex(blend, value->Key) >= ResolveBinding(*texture).PipelineCount || ((blend & 0xf00) == 0x100 && !selected)) {
							throw std::runtime_error("prepared copied motion packet lacks its actual RGBA source or prior nonempty copy");
						}
					}
				};
				// The visitor is synchronous; wrapping its reference avoids copying this large capture into an allocated function target.
				scene::SubmitPreparedMotionFrame(frame, scratch, std::cref(prepare_event));
				FrameHasMask = has_mask;
				if (has_mask) {
					if (Graphics.MaskMode() != 1) {
						throw std::runtime_error("live prepared masks require the actual texture-mask mode1 selection");
					}
				}
			} catch (...) {
				Frame.reset();
				for (auto& replacement : NewCopyTargets) {
					replacement.reset();
				}
				throw;
			}
		}

		void ApplyCopy() {
			if (!UsingPreparedFrame || CopyIndex == PreparedCopyCount) {
				throw std::runtime_error("live copied motion event lacks its exact idle-prepared region request");
			}
			const auto& request = Copies[CopyIndex++];
			if (request.Plan.Action == scene::MotionCopyRegionAction::Copy) {
				if (request.Target == nullptr) {
					throw std::runtime_error("live copied motion request lost its exact target owner");
				}
				const auto handle = Records.CopyCurrentTarget(*request.Target, request.Plan.Rectangle);
				if (handle != request.Handle) {
					throw std::runtime_error("live copied motion request changed its prepared physical identity");
				}
				Records.SetCopiedDestination(handle, request.UvAffine);
				ActualCopy = request.Target;
				ActualCopyHandle = handle;
				ActualCopyAffine = request.UvAffine;
				ActualCopySelected = true;
			} else if (request.Plan.Action == scene::MotionCopyRegionAction::Disable) {
				Records.ClearCopiedDestination();
				ActualCopySelected = false;
			}
		}

		void HoldCopiedOwner(std::unique_ptr<scene::MutableTextureOwner>& owner) {
			if (!owner) {
				return;
			}
			if (RetiredCopyCount == RetiredCopies.size()) {
				throw std::runtime_error("motion copied owner retirement exceeded its preflighted outer-frame storage");
			}
			RetiredCopies[RetiredCopyCount++] = std::move(owner);
		}

		void RetirePreparedFrame(bool keep_outer_owners = false) {
			if (ActualCopy != nullptr && ActualCopy != RetainedCopy.get()) {
				const auto found = std::find_if(NewCopyTargets.begin(), NewCopyTargets.end(), [&](const auto& owner) {
					return owner.get() == ActualCopy;
				});
				if (found == NewCopyTargets.end()) {
					throw std::runtime_error("retained copied motion target lost its actual frame allocation owner");
				}
				if (keep_outer_owners) {
					HoldCopiedOwner(RetainedCopy);
				}
				RetainedCopy = std::move(*found);
			}
			if (ActualCopy != nullptr) {
				RetainedCopyHandle = ActualCopyHandle;
				RetainedCopyAffine = ActualCopyAffine;
			}
			RetainedCopySelected = ActualCopySelected && !FrameFailed;
			for (auto& replacement : NewCopyTargets) {
				if (keep_outer_owners) {
					HoldCopiedOwner(replacement);
				} else {
					replacement.reset();
				}
			}
			ActualCopy = nullptr;
			Frame.reset();
			PreparedEventCount = 0;
			PreparedCopyCount = 0;
			NewCopyCount = 0;
		}

		void Receive(const scene::MotionFrameEvent& event) {
			if (UsingPreparedFrame && (EventIndex == PreparedEventCount || EventFingerprints[EventIndex++] != EventFingerprint(event))) {
				throw std::runtime_error("motion scene publication changed its exact prepared event/geometry stream");
			}
			std::visit([&](const auto& value) {
				using Event = std::remove_cvref_t<decltype(value)>;
				if constexpr (std::is_same_v<Event, scene::MotionFrameBegin>) {
					if (Begun || Ended) {
						throw std::runtime_error("live motion producer repeated its actual record Begin");
					}
					Records.BeginRecord();
					Begun = true;
					if (UsingPreparedFrame && ActualCopySelected) {
						Records.SetCopiedDestination(ActualCopyHandle, ActualCopyAffine);
					}
				} else {
					if (!Begun || Ended) {
						throw std::runtime_error("live motion event lies outside its actual Begin/End record occurrence");
					}
					if constexpr (std::is_same_v<Event, scene::MotionFrameEnd>) {
						Records.EndRecord();
						Ended = true;
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameBlend>) {
						static_cast<void>(PacketPipelineIndex(value.Mode, 4));
						Records.SetBlend(value.Mode, value.Parameter);
						BlendMode = value.Mode;
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameFilter>) {
						if (value.Id != 0) {
							throw std::runtime_error("live motion color-filter execution remains unsupported");
						}
						Records.SetFilterColor(value.Color);
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameTexture>) {
						static_cast<void>(ResolveBinding(value.Index));
						Texture = value.Index;
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameScissor>) {
						if (value.Update.Submission) {
							Records.SetScissor(*value.Update.Submission);
						}
					} else if constexpr (std::is_same_v<Event, scene::MotionBatchSubmission>) {
						Append(value);
					} else if constexpr (std::is_same_v<Event, scene::MotionMaskOperation>) {
						if (!UsingPreparedFrame) {
							if (value.Kind != scene::MotionMaskOperationKind::Configure || value.Operation != 0 || value.ThresholdMinimum != 1 || value.ThresholdMaximum != 0) {
								throw std::runtime_error("live motion mask execution requires an actual prepared scene frame");
							}
							Graphics.ConfigureOrdinaryMaskControl(value.ThresholdMinimum, value.ThresholdMaximum);
						} else if (value.Kind == scene::MotionMaskOperationKind::Configure) {
							Graphics.ConfigureMaskControl(value.Operation, value.ThresholdMinimum, value.ThresholdMaximum);
							Records.ConfigureTextureMask(value.Operation, value.ThresholdMinimum, value.ThresholdMaximum);
						} else {
							if (value.Kind != scene::MotionMaskOperationKind::Prepare && value.Kind != scene::MotionMaskOperationKind::Begin && value.Kind != scene::MotionMaskOperationKind::End && value.Kind != scene::MotionMaskOperationKind::Resolve) {
								throw std::runtime_error("live motion mask event has no actual texture-phase action");
							}
							const auto action = value.Kind == scene::MotionMaskOperationKind::Prepare ? scene::MotionStencilAction::Prepare : value.Kind == scene::MotionMaskOperationKind::Begin ? scene::MotionStencilAction::Begin
							    : value.Kind == scene::MotionMaskOperationKind::End                                                                                                               ? scene::MotionStencilAction::End
							                                                                                                                                                                      : scene::MotionStencilAction::Resolve;
							Records.ApplyTextureMask(action);
						}
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameMaskFinish>) {
						if (!UsingPreparedFrame) {
							throw std::runtime_error("live motion mask finish requires an actual prepared scene frame");
						}
						Records.ApplyTextureMask(scene::MotionStencilAction::Finish);
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameCandidate>) {
						if (!UsingPreparedFrame || value.Command == nullptr) {
							throw std::runtime_error("live motion candidate requires its actual prepared command binding");
						}
					} else if constexpr (std::is_same_v<Event, scene::MotionFrameBlendBounds>) {
						ApplyCopy();
					} else {
						throw std::runtime_error("live motion received an unsupported mask/copy/blend event");
					}
				}
			},
			    event);
		}
	};

	MotionSpriteRecordReceiver::MotionSpriteRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, SpriteGraphicsTargets& graphics, scene::TextureCache& textures, FrameHostHeap& heap, std::size_t texture_slots)
	    : m_Implementation(std::make_unique<Implementation>(records, renderer, graphics, textures, std::ref(heap), texture_slots)) {
	}

	MotionSpriteRecordReceiver::MotionSpriteRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, SpriteGraphicsTargets& graphics, scene::TextureCache& textures, FrameHostPool& pool, std::size_t texture_slots) {
		pool.RequireRecords(records);
		m_Implementation = std::make_unique<Implementation>(records, renderer, graphics, textures, std::ref(pool), texture_slots);
	}

	MotionSpriteRecordReceiver::~MotionSpriteRecordReceiver() = default;

	void MotionSpriteRecordReceiver::BindTexture(std::uint32_t actual_slot, scene::CachedTextureHandle actual_texture) {
		auto& state = *m_Implementation;
		state.RequireQuiescentPreparation();
		if (actual_slot >= state.Bindings.size()) {
			throw std::runtime_error("live motion binding exceeds its actual prepared slot table");
		}

		auto binding = state.PrepareBinding(actual_texture, false);
		state.TextureSizes[actual_slot].X = static_cast<float>(binding.Texture.Description.Width);
		state.TextureSizes[actual_slot].Y = static_cast<float>(binding.Texture.Description.Height);
		state.Bindings[actual_slot] = std::move(binding);
	}

	void MotionSpriteRecordReceiver::UnbindTexture(std::uint32_t actual_slot) {
		auto& state = *m_Implementation;
		state.RequireQuiescentPreparation();
		if (actual_slot >= state.Bindings.size()) {
			throw std::runtime_error("live motion unbinding exceeds its actual prepared slot table");
		}

		state.Bindings[actual_slot] = Implementation::Binding{};
		state.TextureSizes[actual_slot] = resource::MotionPoint{};
	}

	void MotionSpriteRecordReceiver::Draw(scene::MotionSpriteDrawContext& context, const scene::MotionFrameScratch& scratch) {
		auto& state = *m_Implementation;
		state.Records.RequireRenderer(state.Renderer);
		if (state.Operating || state.Frame || state.Graphics.CurrentTarget().get() != &state.Records.CurrentRenderTarget()) {
			throw std::runtime_error("live motion draw requires its idle receiver and exact current graphics target");
		}
		state.RequireInputOwner();
		const auto count = state.RequireDenseBindings();
		const auto globals = state.Records.OrdinaryFrameGlobals(std::span<const resource::MotionPoint>(state.TextureSizes).first(count));
		const auto depth = state.Records.RecordDepth();
		state.Texture.reset();
		state.BlendMode = 0;
		state.Begun = false;
		state.Ended = false;
		state.Operating = true;
		try {
			context.Flush(globals, scratch, state.Output);
			if (state.Begun != state.Ended || state.Records.RecordDepth() != depth) {
				throw std::runtime_error("live motion producer did not complete its actual record occurrence");
			}
		} catch (...) {
			const auto first = std::current_exception();
			if (state.Begun) {
				// A real record/event mutation invalidates the whole actual outer encoding.
				try {
					state.Records.AbortFrame();
				} catch (...) {
					// Corrected records complete cleanup before reporting an OS error; retain the original cause.
				}
			}
			state.Operating = false;
			std::rethrow_exception(first);
		}
		state.Operating = false;
	}

	void MotionSpriteRecordReceiver::PrepareFrame(const scene::MotionPreparedFrame& actual_frame, const scene::MotionFrameScratch& scratch) {
		m_Implementation->Prepare(actual_frame, scratch);
	}

	void MotionSpriteRecordReceiver::DrawPreparedFrame(const scene::MotionFrameScratch& scratch) {
		auto& state = *m_Implementation;
		state.Records.RequireRenderer(state.Renderer);
		if (state.Operating || !state.Frame || state.FrameDrawn || state.Graphics.CurrentTarget().get() != (state.ActualOccurrence ? &state.Records.CurrentRenderTarget() : &state.Renderer)) {
			throw std::runtime_error("live prepared motion draw requires its exact unconsumed publication and current target");
		}
		state.RequireInputOwner();
		if (state.PreparedBindingRevision != state.Records.TextureBindingRevision()) {
			throw std::runtime_error("prepared motion target resource bindings changed before drawing");
		}
		state.RequireFrameTextures(*state.Frame, state.RequirePreparedBindings(*state.Frame));
		state.RequireCurrentFrameGlobals(*state.Frame, state.ActualOccurrence);
		if (state.FrameHasMask && state.Graphics.MaskMode() != 1) {
			throw std::runtime_error("prepared motion publication lost its actual texture-mask mode1 selection");
		}
		const auto depth = state.Records.RecordDepth();
		state.Texture.reset();
		state.BlendMode = 0;
		state.Begun = false;
		state.Ended = false;
		state.EventIndex = 0;
		state.CopyIndex = 0;
		state.ActualCopy = state.RetainedCopy.get();
		state.ActualCopyHandle = state.RetainedCopyHandle;
		state.ActualCopyAffine = state.RetainedCopyAffine;
		state.ActualCopySelected = state.RetainedCopySelected;
		state.UsingPreparedFrame = true;
		state.Operating = true;
		try {
			scene::SubmitPreparedMotionFrame(*state.Frame, scratch, state.Output);
			if (!state.Begun || !state.Ended || state.EventIndex != state.PreparedEventCount || state.CopyIndex != state.PreparedCopyCount || state.Records.RecordDepth() != depth) {
				throw std::runtime_error("prepared motion producer did not complete its exact record/event/copy occurrence");
			}
		} catch (...) {
			const auto first = std::current_exception();
			state.FrameFailed = true;
			if (state.Begun) {
				try {
					state.Records.AbortFrame();
				} catch (...) {
				}
			}
			state.FrameDrawn = true;
			state.UsingPreparedFrame = false;
			state.Operating = false;
			std::rethrow_exception(first);
		}
		state.FrameDrawn = true;
		state.UsingPreparedFrame = false;
		state.Operating = false;
	}

	void MotionSpriteRecordReceiver::DrawActualFrame(const scene::MotionPreparedFrame& frame, std::span<const scene::CachedTextureHandle> actual_slots, const scene::MotionFrameScratch& scratch) {
		auto& state = *m_Implementation;
		state.Records.RequireRenderer(state.Renderer);
		if (state.Operating || state.ActualOccurrence || state.Frame || !state.Records.IsActive() || state.Records.RecordDepth() != 0 || state.Records.TextureMaskPhase() != scene::MotionMaskPhase::Idle || state.Graphics.CurrentTarget().get() != &state.Records.CurrentRenderTarget()) {
			throw std::runtime_error("actual motion occurrence requires its exact balanced live primary and idle mask/receiver owners");
		}
		state.RequireInputOwner();
		std::visit([](auto producer) {
			using Owner = std::remove_reference_t<decltype(producer.get())>;
			if constexpr (std::is_same_v<Owner, FrameHostPool>) {
				producer.get().RequireCompletedWriters();
			}
		},
		    state.InputProducer);
		state.RequireCurrentFrameGlobals(frame, true);
		state.ActualOccurrence = true;
		try {
			state.BindActualTextures(actual_slots, frame);
			state.PreparingLive = true;
			state.Prepare(frame, scratch);
			state.PreparingLive = false;
			DrawPreparedFrame(scratch);
			state.RetirePreparedFrame(true);
			state.ActualOccurrence = false;
		} catch (...) {
			const auto failure = std::current_exception();
			state.PreparingLive = false;
			state.ActualOccurrence = false;
			state.FrameFailed = true;
			try {
				state.Records.AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(failure);
		}
	}

	void MotionSpriteRecordReceiver::ReleaseFrameInputs() {
		auto& state = *m_Implementation;
		state.RequireQuiescentPreparation(true);

		for (std::size_t index = 0; index < state.InputCount; ++index) {
			state.InputBorrows[index].reset();
			state.Inputs[index].reset();
		}
		state.InputCount = 0;
		if (state.Frame) {
			state.RetirePreparedFrame();
		}
		// An external later draw can discard the same command buffer after this receiver's last occurrence.
		// Keep its allocation/hysteresis, but never sample a copy whose commands were not submitted.
		if (!state.Records.CompletedSubmission()) {
			state.RetainedCopySelected = false;
		}
		for (std::size_t index = 0; index < state.RetiredCopyCount; ++index) {
			state.RetiredCopies[index].reset();
		}
		state.RetiredCopyCount = 0;
	}

} // namespace sgre::render
