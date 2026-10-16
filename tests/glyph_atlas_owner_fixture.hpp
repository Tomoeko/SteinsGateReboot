#pragma once

#include "sgre/scene/mutable_texture_owner.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace text_atlas_test {

	struct Observations {
		std::uint32_t Creates = 0;
		std::uint32_t Views = 0;
		std::uint32_t Samplers = 0;
		std::uint32_t Uploads = 0;
		std::uint32_t Binds = 0;
		std::uint32_t ReleasedTextures = 0;
		std::uint32_t ReleasedViews = 0;
		std::uint32_t ReleasedSamplers = 0;
		std::uint32_t LastPitch = 0;
		bool FailCreate = false;
		std::function<void()> Creating;
		std::function<void()> ReleasingTexture;
		sgre::scene::MutableTextureDescription Description;
		sgre::scene::MutableTextureSamplerDescription LastSampler;
		std::vector<std::byte> LastUpload;
	};

	class Texture final : public sgre::scene::MutableTextureResource {
	public:
		explicit Texture(Observations& observations) : m_Observations(observations) {
		}
		~Texture() override {
			++m_Observations.ReleasedTextures;
			if (m_Observations.ReleasingTexture) {
				m_Observations.ReleasingTexture();
			}
		}
		sgre::scene::MutableTextureDescription Description() const noexcept override {
			return m_Observations.Description;
		}

	private:
		Observations& m_Observations;
	};

	class View final : public sgre::scene::MutableTextureView {
	public:
		View(Observations& observations, sgre::scene::MutableTextureViewDescription description) : m_Observations(observations), m_Description(description) {
		}
		~View() override {
			++m_Observations.ReleasedViews;
		}
		sgre::scene::MutableTextureViewDescription Description() const noexcept override {
			return m_Description;
		}

	private:
		Observations& m_Observations;
		sgre::scene::MutableTextureViewDescription m_Description;
	};

	class Sampler final : public sgre::scene::MutableTextureSampler {
	public:
		Sampler(Observations& observations, sgre::scene::MutableTextureSamplerDescription description) : m_Observations(observations), m_Description(description) {
		}
		~Sampler() override {
			++m_Observations.ReleasedSamplers;
		}
		sgre::scene::MutableTextureSamplerDescription Description() const noexcept override {
			return m_Description;
		}

	private:
		Observations& m_Observations;
		sgre::scene::MutableTextureSamplerDescription m_Description;
	};

	class Backend final : public sgre::scene::MutableTextureBackend {
	public:
		Observations Observed;

		std::unique_ptr<sgre::scene::MutableTextureResource> CreateTexture(const sgre::scene::MutableTextureDescription& description, const sgre::scene::MutableTextureInitialData& initial) override {
			if (!initial.Bytes.empty() || description.Width != 1024 || description.Height != 1024 || description.OriginalFormat != 87 || description.MipLevels != 1 || description.SampleCount != 1 || description.CpuRead || description.CpuWrite || description.Usage != sgre::scene::MutableTextureUsage::Default || !description.ShaderResource || description.RenderTarget || description.DepthStencil) {
				throw std::runtime_error("text atlas native fixed texture specification differs");
			}

			++Observed.Creates;
			Observed.Description = description;
			if (Observed.Creating) {
				Observed.Creating();
			}
			if (Observed.FailCreate) {
				return {};
			}

			return std::make_unique<Texture>(Observed);
		}
		std::unique_ptr<sgre::scene::MutableTextureView> CreateView(sgre::scene::MutableTextureResource&, const sgre::scene::MutableTextureViewDescription& description) override {
			++Observed.Views;

			return std::make_unique<View>(Observed, description);
		}
		std::unique_ptr<sgre::scene::MutableTextureSampler> CreateSampler(const sgre::scene::MutableTextureSamplerDescription& description) override {
			++Observed.Samplers;
			Observed.LastSampler = description;

			return std::make_unique<Sampler>(Observed, description);
		}
		sgre::scene::MutableTextureMapping Map(sgre::scene::MutableTextureResource&, sgre::scene::MutableTextureMapMode) override {
			throw std::runtime_error("text atlas CpuShadow must never call platform Map");
		}
		void Unmap(sgre::scene::MutableTextureResource&) override {
			throw std::runtime_error("text atlas CpuShadow must never call platform Unmap");
		}
		void Upload(sgre::scene::MutableTextureResource&, std::span<const std::byte> bytes, std::uint32_t pitch) override {
			++Observed.Uploads;
			Observed.LastUpload.assign(bytes.begin(), bytes.end());
			Observed.LastPitch = pitch;
		}
		void Copy(sgre::scene::MutableTextureResource&, sgre::scene::MutableTextureResource&) override {
			throw std::runtime_error("text atlas must not construct a staging companion");
		}
		void Bind(sgre::scene::MutableTextureView* view, sgre::scene::MutableTextureSampler* sampler, std::uint32_t slot, sgre::scene::MutableTextureStage stage) override {
			if (!view || !sampler || slot != 0 || stage != sgre::scene::MutableTextureStage::Pixel) {
				throw std::runtime_error("text atlas fixture received an unexpected bind");
			}

			++Observed.Binds;
		}
	};

} // namespace text_atlas_test
