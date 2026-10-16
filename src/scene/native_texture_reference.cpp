#include "sgre/scene/native_texture_reference.hpp"

#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static void RequireNativeTexture(const NativeTextureSnapshot& source) {
		if (source.Texture.Owner == nullptr || source.Texture.Generation == 0 || !std::isfinite(source.Size.X) || !std::isfinite(source.Size.Y) || source.Size.X <= 0 || source.Size.Y <= 0 || source.Size.X > 16384 || source.Size.Y > 16384 || std::trunc(source.Size.X) != source.Size.X || std::trunc(source.Size.Y) != source.Size.Y || !std::isfinite(source.ActiveBounds.MinimumX) || !std::isfinite(source.ActiveBounds.MinimumY) || !std::isfinite(source.ActiveBounds.MaximumX) || !std::isfinite(source.ActiveBounds.MaximumY) || source.ActiveBounds.MaximumX < source.ActiveBounds.MinimumX || source.ActiveBounds.MaximumY < source.ActiveBounds.MinimumY || source.Minification > 1 || source.Magnification > 1) {
			throw std::runtime_error("NativeTextureRef requires its actual live bounded source descriptor");
		}
	}

	NativeTextureReference::NativeTextureReference(ReadSource read_source, SetFiltering filtering) : m_ReadSource(std::move(read_source)), m_SetFiltering(std::move(filtering)) {
		if (!m_ReadSource || !m_SetFiltering) {
			throw std::runtime_error("NativeTextureRef requires real source ownership and sampler services");
		}

		m_Description = m_ReadSource();
		RequireNativeTexture(m_Description);
	}

	NativeTextureReference::~NativeTextureReference() noexcept {
		if (m_Reservations != 0) {
			std::terminate();
		}
	}

	void NativeTextureReference::RequireCanRelease() const {
		if (m_Reservations != 0) {
			throw std::runtime_error("NativeTextureRef retirement requires its actual alternate listeners to release the descriptor first");
		}
	}

	const NativeTextureSnapshot& NativeTextureReference::Description() const noexcept {
		return m_Description;
	}

	NativeTextureSnapshot NativeTextureReference::Read() const {
		const auto current = m_ReadSource();
		RequireNativeTexture(current);
		if (current.Texture != m_Description.Texture || current.MutableSource != m_Description.MutableSource || current.Size.X != m_Description.Size.X || current.Size.Y != m_Description.Size.Y || current.ActiveBounds.MinimumX != m_Description.ActiveBounds.MinimumX || current.ActiveBounds.MinimumY != m_Description.ActiveBounds.MinimumY || current.ActiveBounds.MaximumX != m_Description.ActiveBounds.MaximumX || current.ActiveBounds.MaximumY != m_Description.ActiveBounds.MaximumY || current.OriginalFormat != m_Description.OriginalFormat) {
			throw std::runtime_error("NativeTextureRef source was replaced outside its actual retained descriptor lifetime");
		}

		return current;
	}

	void NativeTextureReference::SetSmoothing(bool enabled) {
		static_cast<void>(Read());
		const auto filter = enabled ? std::uint32_t{1} : std::uint32_t{0};
		m_SetFiltering(filter, filter);
		const auto current = Read();
		if (current.Minification != filter || current.Magnification != filter) {
			throw std::runtime_error("NativeTextureRef sampler mutation did not reach its actual source owner");
		}
	}

} // namespace sgre::scene
