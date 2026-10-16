#pragma once

#include "sgre/scene/mutable_texture_owner.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace sgre::scene::tests {

	class TaskFixtureTexture final : public MutableTextureResource {
	public:
		TaskFixtureTexture(MutableTextureDescription description, std::vector<std::string>& events, bool& live, std::function<void()>& released) : m_Description(description), m_Events(events), m_Live(live), m_Released(released) {
			m_Live = true;
		}
		~TaskFixtureTexture() override {
			m_Events.push_back("texture");
			m_Live = false;
			if (m_Released) {
				m_Released();
			}
		}
		MutableTextureDescription Description() const noexcept override {
			return m_Description;
		}

	private:
		MutableTextureDescription m_Description;
		std::vector<std::string>& m_Events;
		bool& m_Live;
		std::function<void()>& m_Released;
	};

	class TaskFixtureView final : public MutableTextureView {
	public:
		TaskFixtureView(MutableTextureViewDescription description, std::vector<std::string>& events) : m_Description(description), m_Events(events) {
		}
		~TaskFixtureView() override {
			m_Events.push_back("view");
		}
		MutableTextureViewDescription Description() const noexcept override {
			return m_Description;
		}

	private:
		MutableTextureViewDescription m_Description;
		std::vector<std::string>& m_Events;
	};

	class TaskFixtureSampler final : public MutableTextureSampler {
	public:
		explicit TaskFixtureSampler(MutableTextureSamplerDescription description) : m_Description(description) {
		}
		MutableTextureSamplerDescription Description() const noexcept override {
			return m_Description;
		}

	private:
		MutableTextureSamplerDescription m_Description;
	};

	class TaskFixtureBackend final : public MutableTextureBackend {
	public:
		std::vector<std::string> Events;
		std::vector<std::byte> Uploaded;
		MutableTextureDescription Created;
		MutableTextureSamplerDescription Sampler;
		std::size_t UploadCount = 0;
		std::size_t BindCount = 0;
		bool Live = false;
		std::function<void()> OnTextureRelease;

		std::unique_ptr<MutableTextureResource> CreateTexture(const MutableTextureDescription& description, const MutableTextureInitialData& initial) override {
			if (!initial.Bytes.empty() || description.Width > 256 || description.Height > 256) {
				throw std::runtime_error("raw task fixture exceeds its bounded texture domain");
			}

			Created = description;

			return std::make_unique<TaskFixtureTexture>(description, Events, Live, OnTextureRelease);
		}

		std::unique_ptr<MutableTextureView> CreateView(MutableTextureResource&, const MutableTextureViewDescription& description) override {
			return std::make_unique<TaskFixtureView>(description, Events);
		}

		std::unique_ptr<MutableTextureSampler> CreateSampler(const MutableTextureSamplerDescription& description) override {
			Sampler = description;

			return std::make_unique<TaskFixtureSampler>(description);
		}

		MutableTextureMapping Map(MutableTextureResource&, MutableTextureMapMode) override {
			throw std::runtime_error("CPU-shadow task must not issue a physical map");
		}

		void Unmap(MutableTextureResource&) override {
			throw std::runtime_error("CPU-shadow task must not issue a physical unmap");
		}

		void Upload(MutableTextureResource&, std::span<const std::byte> bytes, std::uint32_t row_pitch) override {
			if (row_pitch != Created.Width * 4 || bytes.size() != std::size_t{Created.Width} * Created.Height * 4) {
				throw std::runtime_error("raw task upload omitted its exact packed shadow rows");
			}

			Uploaded.assign(bytes.begin(), bytes.end());
			++UploadCount;
		}

		void Copy(MutableTextureResource&, MutableTextureResource&) override {
			throw std::runtime_error("CPU-shadow task must not issue a companion copy");
		}

		void Bind(MutableTextureView*, MutableTextureSampler*, std::uint32_t slot, MutableTextureStage stage) override {
			if (slot != 0 || stage != MutableTextureStage::Pixel) {
				throw std::runtime_error("raw task fixture requires pixel texture zero");
			}

			++BindCount;
		}
	};

} // namespace sgre::scene::tests
