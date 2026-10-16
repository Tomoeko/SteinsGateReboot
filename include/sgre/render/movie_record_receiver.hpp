#pragma once

#include "sgre/media/movie_playback.hpp"
#include "sgre/render/sprite_render_records.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sgre::render {

	class MovieRecordReceiver {
	public:
		// The real outer-frame records and renderer enclose all retained movie textures.
		MovieRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, std::size_t maximum_owners = 64);
		void Draw(const media::MovieDrawFrame& frame);
		[[nodiscard]] bool CanRetire(const void* owner) const;
		void Retire(const void* owner);
		[[nodiscard]] bool Empty() const noexcept;

	private:
		struct Owner {
			const void* Identity = nullptr;
			std::unique_ptr<scene::MutableTextureOwner> Texture;
			std::shared_ptr<const media::MovieRgb32Frame> Sample;
			std::uint64_t FrameSerial = 0;
			bool Uploaded = false;
		};

		[[nodiscard]] Owner& PrepareOwner(const media::MovieDrawFrame& frame);
		SpriteRenderRecords& m_Records;
		SpritePassRenderer& m_Renderer;
		std::unique_ptr<scene::MutableTextureBackend> m_Backend;
		std::array<SpritePipelineHandle, 2> m_Pipelines;
		std::vector<Owner> m_Owners;
		std::size_t m_MaximumOwners = 0;
		bool m_Drawing = false;
	};

} // namespace sgre::render
