#include "sgre/render/sprite_host_inputs.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace sgre::render {

	static constexpr std::size_t kMaximumSpriteHostElements = 1024 * 1024;
	static constexpr std::size_t kSpriteHostVertexBytes = 20;

	static void StoreHostWord(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value, std::size_t width = 4) {
		for (std::size_t index = 0; index < width; ++index) {
			bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 255u);
		}
	}

	SpriteHostInputs::SpriteHostInputs(FrameHostHeap& heap, std::span<const scene::MotionSpriteVertex> vertices, std::span<const std::uint32_t> indices) {
		if (vertices.size() < 3 || indices.size() < 3 || vertices.size() > kMaximumSpriteHostElements || indices.size() > kMaximumSpriteHostElements) {
			throw std::runtime_error("sprite host inputs require bounded actual indexed format4 geometry");
		}
		const std::size_t index_word_bytes = indices.size() > 0x7fffu ? 4 : 2;
		for (const auto& vertex : vertices) {
			for (const auto value : {vertex.Position.X, vertex.Position.Y, vertex.Uv.X, vertex.Uv.Y}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("sprite host input coordinates require the admitted finite native domain");
				}
			}
		}
		for (const auto index : indices) {
			if (index >= vertices.size() || (index_word_bytes == 2 && index > 0xffffu)) {
				throw std::runtime_error("sprite host index exceeds its actual vertices or native index word");
			}
		}
		m_Vertices = heap.Allocate(vertices.size() * kSpriteHostVertexBytes);
		m_Indices = heap.Allocate(indices.size() * index_word_bytes);
		const auto vertex_bytes = m_Vertices.Bytes();
		for (std::size_t index = 0; index < vertices.size(); ++index) {
			const auto& vertex = vertices[index];
			const auto offset = index * kSpriteHostVertexBytes;
			StoreHostWord(vertex_bytes, offset, std::bit_cast<std::uint32_t>(vertex.Position.X));
			StoreHostWord(vertex_bytes, offset + 4, std::bit_cast<std::uint32_t>(vertex.Position.Y));
			StoreHostWord(vertex_bytes, offset + 8, (vertex.Color >> 8) | ((vertex.Color & 255u) << 24));
			StoreHostWord(vertex_bytes, offset + 12, std::bit_cast<std::uint32_t>(vertex.Uv.X));
			StoreHostWord(vertex_bytes, offset + 16, std::bit_cast<std::uint32_t>(vertex.Uv.Y));
		}
		const auto index_bytes = m_Indices.Bytes();
		for (std::size_t index = 0; index < indices.size(); ++index) {
			StoreHostWord(index_bytes, index * index_word_bytes, indices[index], index_word_bytes);
		}
		m_VertexCount = static_cast<std::uint32_t>(vertices.size());
		m_IndexCount = static_cast<std::uint32_t>(indices.size());
		m_IndexWordBytes = static_cast<std::uint32_t>(index_word_bytes);
	}

	SpriteHostInputs::SpriteHostInputs(FrameHostHeap& heap, std::span<const scene::ScreenProjectionVertex> vertices, std::span<const std::uint32_t> indices)
	    : m_Layout(scene::MotionVertexLayout::XyzUv) {
		if (vertices.size() < 3 || indices.size() < 3 || vertices.size() > kMaximumSpriteHostElements || indices.size() > kMaximumSpriteHostElements) {
			throw std::runtime_error("ScreenProjection host inputs require bounded actual indexed format8 geometry");
		}
		const std::size_t index_word_bytes = indices.size() > 0x7fffu ? 4 : 2;
		for (const auto& vertex : vertices) {
			for (const auto value : {vertex.X, vertex.Y, vertex.Z, vertex.Uv.X, vertex.Uv.Y}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("ScreenProjection host inputs require finite XYZ/UV words");
				}
			}
		}
		for (const auto index : indices) {
			if (index >= vertices.size() || (index_word_bytes == 2 && index > 0xffffu)) {
				throw std::runtime_error("ScreenProjection index exceeds its actual vertices or native index word");
			}
		}
		m_Vertices = heap.Allocate(vertices.size() * kSpriteHostVertexBytes);
		m_Indices = heap.Allocate(indices.size() * index_word_bytes);
		const auto vertex_bytes = m_Vertices.Bytes();
		for (std::size_t index = 0; index < vertices.size(); ++index) {
			const auto& vertex = vertices[index];
			const std::array<float, 5> words{vertex.X, vertex.Y, vertex.Z, vertex.Uv.X, vertex.Uv.Y};
			for (std::size_t word = 0; word < words.size(); ++word) {
				StoreHostWord(vertex_bytes, index * kSpriteHostVertexBytes + word * 4, std::bit_cast<std::uint32_t>(words[word]));
			}
		}
		const auto index_bytes = m_Indices.Bytes();
		for (std::size_t index = 0; index < indices.size(); ++index) {
			StoreHostWord(index_bytes, index * index_word_bytes, indices[index], index_word_bytes);
		}
		m_VertexCount = static_cast<std::uint32_t>(vertices.size());
		m_IndexCount = static_cast<std::uint32_t>(indices.size());
		m_IndexWordBytes = static_cast<std::uint32_t>(index_word_bytes);
	}

	scene::MotionVertexLayout SpriteHostInputs::Layout() const noexcept {
		return m_Layout;
	}

	void SpriteHostInputs::RequireLive() const {
		if (m_Vertices.Bytes().size() != static_cast<std::size_t>(m_VertexCount) * kSpriteHostVertexBytes || m_Indices.Bytes().size() != static_cast<std::size_t>(m_IndexCount) * m_IndexWordBytes) {
			throw std::runtime_error("sprite host inputs require their exact live allocation spans");
		}
	}

	std::span<const std::byte> SpriteHostInputs::VertexBytes() const {
		RequireLive();

		return m_Vertices.Bytes();
	}

	std::span<const std::byte> SpriteHostInputs::IndexBytes() const {
		RequireLive();

		return m_Indices.Bytes();
	}

	std::uint32_t SpriteHostInputs::VertexCount() const {
		RequireLive();

		return m_VertexCount;
	}

	std::uint32_t SpriteHostInputs::IndexCount() const {
		RequireLive();

		return m_IndexCount;
	}

	std::uint32_t SpriteHostInputs::IndexWordBytes() const {
		RequireLive();

		return m_IndexWordBytes;
	}

	bool SpriteHostInputs::BelongsTo(const FrameHostHeap& actual_heap) const {
		return m_Vertices.BelongsTo(actual_heap) && m_Indices.BelongsTo(actual_heap);
	}

} // namespace sgre::render
