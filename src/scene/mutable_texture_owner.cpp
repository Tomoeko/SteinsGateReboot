#include "sgre/scene/mutable_texture_owner.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {
	static std::uint32_t FormatBits(std::uint32_t format) {
		switch (format) {
		case 28:
		case 87:
		case 88:
			return 32;
		case 65:
		case 61:
			return 8;
		case 49:
			return 16;
		default:
			throw std::runtime_error("mutable texture CPU layout is not recovered for this original format");
		}
	}

	static std::size_t ShadowSize(const MutableTextureDescription& description, const MutableTextureLimits& limits) {
		const auto pixels = std::uint64_t{description.Width} * description.Height;
		const float bytes = static_cast<float>(pixels) * (static_cast<float>(FormatBits(description.OriginalFormat)) * 0.125f);
		if (!std::isfinite(bytes) || bytes < 1 || static_cast<long double>(bytes) > static_cast<long double>(limits.MaximumShadowBytes) || static_cast<long double>(bytes) >= static_cast<long double>(std::numeric_limits<std::size_t>::max())) {
			throw std::runtime_error("mutable texture shadow exceeds the configured byte limit");
		}

		return static_cast<std::size_t>(bytes);
	}

	static std::uint32_t DefaultRowPitch(const MutableTextureDescription& description) {
		const auto pitch = static_cast<float>(FormatBits(description.OriginalFormat)) * 0.125f * static_cast<float>(description.Width);
		if (!std::isfinite(pitch) || pitch >= 4294967296.0f) {
			throw std::runtime_error("mutable texture row pitch exceeds unsigned storage");
		}

		return static_cast<std::uint32_t>(pitch);
	}

	static MutableTextureDescription PrepareDescription(const MutableTextureSpec& spec) {
		auto description = spec.Description;
		description.Usage = MutableTextureUsage::Default;
		description.ShaderResource = true;
		description.RenderTarget = false;
		description.DepthStencil = false;
		description.CpuRead = false;
		description.CpuWrite = false;

		switch (spec.Storage) {
		case MutableTextureStorage::GpuOnly:
		case MutableTextureStorage::CpuShadow:
			break;
		case MutableTextureStorage::DynamicWrite:
			description.Usage = MutableTextureUsage::Dynamic;
			description.CpuWrite = true;
			break;
		case MutableTextureStorage::Staging:
		case MutableTextureStorage::StagingCompanion:
			description.Usage = MutableTextureUsage::Staging;
			description.ShaderResource = false;
			description.CpuRead = true;
			description.CpuWrite = true;
			break;
		case MutableTextureStorage::RenderTarget:
			description.RenderTarget = true;
			description.MipLevels = 1;
			break;
		case MutableTextureStorage::DepthStencil:
			description.ShaderResource = false;
			description.DepthStencil = true;
			description.MipLevels = 1;
			break;
		default:
			throw std::runtime_error("mutable texture storage kind is invalid");
		}

		return description;
	}

	MutableTextureOwner::MutableTextureOwner(MutableTextureBackend& backend, const MutableTextureSpec& spec, const MutableTextureInitialData& initial, const MutableTextureLimits& limits) : m_Backend(backend), m_Storage(spec.Storage), m_Description(PrepareDescription(spec)) {
		if (m_Description.Width == 0 || m_Description.Height == 0 || m_Description.Width > limits.MaximumExtent || m_Description.Height > limits.MaximumExtent || m_Description.MipLevels == 0 || m_Description.MipLevels > limits.MaximumMipLevels || m_Description.SampleCount == 0 || m_Description.SampleCount > limits.MaximumSampleCount) {
			throw std::runtime_error("mutable texture dimensions/mips/samples exceed the prepared domain");
		}

		m_Mappable = m_Storage == MutableTextureStorage::DynamicWrite || m_Storage == MutableTextureStorage::CpuShadow || m_Storage == MutableTextureStorage::Staging || m_Storage == MutableTextureStorage::StagingCompanion;

		if (m_Storage == MutableTextureStorage::CpuShadow) {
			// Authored storage is initialized safely; native blank allocation contents remain unverified pixels.
			m_Shadow.resize(ShadowSize(m_Description, limits));
		}

		auto prepared_initial = initial;
		if (!initial.Bytes.empty()) {
			const auto row_bytes = std::uint64_t{m_Description.Width} * FormatBits(m_Description.OriginalFormat) / 8;
			if (prepared_initial.RowPitch == 0) {
				prepared_initial.RowPitch = static_cast<std::uint32_t>(row_bytes);
			}
			const auto required = std::uint64_t{prepared_initial.RowPitch} * (m_Description.Height - 1) + row_bytes;
			if (prepared_initial.RowPitch < row_bytes || required > initial.Bytes.size() || m_Description.MipLevels != 1 || m_Description.SampleCount != 1) {
				throw std::runtime_error("mutable texture initial data layout is unsupported or truncated");
			}
		}

		m_Texture = m_Backend.CreateTexture(m_Description, prepared_initial);
		if (!m_Texture) {
			throw std::runtime_error("mutable texture platform creation failed");
		}

		const auto actual_description = m_Texture->Description();
		if (actual_description != m_Description) {
			throw std::runtime_error("mutable texture platform description differs from the prepared layout");
		}

		m_Description = actual_description;
		if (m_Description.ShaderResource) {
			m_View = m_Backend.CreateView(*m_Texture, {m_Description.OriginalFormat, m_Description.MipLevels, m_Description.SampleCount > 1});
		}

		if (m_Storage == MutableTextureStorage::StagingCompanion) {
			MutableTextureSpec companion;
			companion.Storage = MutableTextureStorage::GpuOnly;
			companion.Description = m_Description;
			m_Companion = std::make_unique<MutableTextureOwner>(backend, companion, MutableTextureInitialData{}, limits);
		}
	}

	MutableTextureOwner::~MutableTextureOwner() {
		m_Companion.reset();
		std::vector<std::byte>().swap(m_Shadow);
		m_View.reset();
		m_Sampler.reset();
		m_Texture.reset();
	}

	void MutableTextureOwner::SetFiltering(std::uint32_t minimum, std::uint32_t maximum) {
		static constexpr std::array<std::uint8_t, 6> linear{0, 1, 0, 1, 0, 1};
		static constexpr std::array<std::uint8_t, 6> mip{0, 1, 0, 0, 1, 1};
		if (minimum >= linear.size() || maximum >= linear.size()) {
			throw std::runtime_error("mutable texture filter index is outside the recovered table");
		}

		std::uint32_t filter = mip[minimum];
		if (linear[minimum] == linear[maximum]) {
			if (linear[minimum] != 0) {
				filter += 20;
			}
		} else if (linear[minimum] == 0) {
			filter += 4;
		} else {
			filter = mip[minimum] == 0 ? 16u : 4u;
		}

		// Original filtering alone does not invalidate the already created sampler.
		m_DesiredSampler.FilterWord = filter;
	}

	void MutableTextureOwner::SetAddress(std::uint32_t horizontal, std::uint32_t vertical) {
		static constexpr std::array<std::uint32_t, 3> modes{3, 1, 2};
		if (horizontal >= modes.size() || vertical >= modes.size()) {
			throw std::runtime_error("mutable texture address index is outside the recovered table");
		}

		m_DesiredSampler.AddressU = modes[horizontal];
		m_DesiredSampler.AddressV = modes[vertical];
		m_SamplerDirty = true;
	}

	std::uint32_t MutableTextureOwner::RowPitch() {
		if (m_RowPitch != 0) {
			return m_RowPitch;
		}

		if (!m_Locked && m_Mappable && m_Shadow.empty()) {
			const auto mode = m_Description.Usage == MutableTextureUsage::Dynamic ? MutableTextureMapMode::Discard : MutableTextureMapMode::Read;
			const auto mapped = m_Backend.Map(*m_Texture, mode);
			if (mapped.Succeeded) {
				m_Backend.Unmap(*m_Texture);
				m_RowPitch = mapped.RowPitch;
				return m_RowPitch;
			}
		}

		m_RowPitch = DefaultRowPitch(m_Description);
		return m_RowPitch;
	}

	MutableTextureMapping MutableTextureOwner::Map(bool preserve) {
		if (!m_Mappable || m_Locked) {
			return {};
		}

		MutableTextureMapping result;
		if (!m_Shadow.empty()) {
			result = {m_Shadow, m_RowPitch != 0 ? m_RowPitch : DefaultRowPitch(m_Description), true};
		} else {
			const auto mode = m_Description.Usage == MutableTextureUsage::Dynamic ? MutableTextureMapMode::Discard : MutableTextureMapMode::Write;
			result = m_Backend.Map(*m_Texture, mode);
			if (result.Succeeded) {
				m_RowPitch = result.RowPitch;
			}
		}

		// Even a failed native Map blocks reentry until Unmap; preserve controls only dirty marking.
		m_Locked = true;
		if (!preserve) {
			m_Dirty = true;
		}

		return result;
	}

	void MutableTextureOwner::Unmap() {
		if (m_Locked && m_Shadow.empty()) {
			m_Backend.Unmap(*m_Texture);
		}
		m_Locked = false;
	}

	void MutableTextureOwner::NotifyGpuWrite() {
		if (m_Storage != MutableTextureStorage::StagingCompanion || m_Companion == nullptr || m_Locked || m_Companion->m_Locked) {
			throw std::runtime_error("mutable GPU write notification requires an unlocked staging-companion owner");
		}
		m_Dirty = true;
	}

	void MutableTextureOwner::Bind(std::uint32_t slot, MutableTextureStage stage) {
		if (stage != MutableTextureStage::Vertex && stage != MutableTextureStage::Pixel) {
			throw std::runtime_error("mutable texture shader stage is invalid");
		}

		if (m_Companion) {
			if (m_Dirty) {
				m_Backend.Copy(*m_Companion->m_Texture, *m_Texture);
			}
			if (m_SamplerDirty) {
				m_Companion->m_DesiredSampler = m_DesiredSampler;
				m_Companion->m_SamplerDirty = true;
			}
			m_Companion->Bind(slot & 255u, stage);
		} else {
			if (m_Dirty && !m_Shadow.empty()) {
				m_Backend.Upload(*m_Texture, m_Shadow, RowPitch());
			}
			m_Dirty = false;
			if (!m_Sampler || m_SamplerDirty) {
				m_Sampler.reset();
				m_Sampler = m_Backend.CreateSampler(m_DesiredSampler);
			}
			m_Backend.Bind(m_View.get(), m_Sampler.get(), slot, stage);
		}
		m_Dirty = false;
		m_SamplerDirty = false;
	}

	MutableTextureState MutableTextureOwner::State() const noexcept {
		return {m_Storage, m_Description, m_DesiredSampler, m_RowPitch, m_Shadow.size(), m_Mappable, m_Locked, m_Dirty, m_SamplerDirty, static_cast<bool>(m_Companion), static_cast<bool>(m_View), static_cast<bool>(m_Sampler)};
	}

	const MutableTextureOwner* MutableTextureOwner::Companion() const noexcept {
		return m_Companion.get();
	}

	bool MutableTextureOwner::UsesBackend(const MutableTextureBackend& actual_backend) const noexcept {
		return &m_Backend == &actual_backend;
	}

	MutableTextureResource& MutableTextureOwner::Resource() noexcept {
		return *m_Texture;
	}

	const MutableTextureResource& MutableTextureOwner::Resource() const noexcept {
		return *m_Texture;
	}
} // namespace sgre::scene
