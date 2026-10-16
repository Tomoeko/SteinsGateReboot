#include "sgre/scene/mutable_texture_owner.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view text) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(text) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("GPU dirty notification accepted an invalid owner phase");
}

class RecordedTexture final : public sgre::scene::MutableTextureResource {
public:
	explicit RecordedTexture(sgre::scene::MutableTextureDescription description) : Value(description) {
	}
	sgre::scene::MutableTextureDescription Description() const noexcept override {
		return Value;
	}
	sgre::scene::MutableTextureDescription Value;
	std::array<std::byte, 64> Bytes{};
};
class RecordedView final : public sgre::scene::MutableTextureView {
public:
	explicit RecordedView(sgre::scene::MutableTextureViewDescription description) : Value(description) {
	}
	sgre::scene::MutableTextureViewDescription Description() const noexcept override {
		return Value;
	}
	sgre::scene::MutableTextureViewDescription Value;
};
class RecordedSampler final : public sgre::scene::MutableTextureSampler {
public:
	explicit RecordedSampler(sgre::scene::MutableTextureSamplerDescription description) : Value(description) {
	}
	sgre::scene::MutableTextureSamplerDescription Description() const noexcept override {
		return Value;
	}
	sgre::scene::MutableTextureSamplerDescription Value;
};
class RecordedBackend final : public sgre::scene::MutableTextureBackend {
public:
	std::vector<std::string_view> Operations;
	bool FailCopy = false;
	std::unique_ptr<sgre::scene::MutableTextureResource> CreateTexture(const sgre::scene::MutableTextureDescription& description, const sgre::scene::MutableTextureInitialData&) override {
		return std::make_unique<RecordedTexture>(description);
	}
	std::unique_ptr<sgre::scene::MutableTextureView> CreateView(sgre::scene::MutableTextureResource&, const sgre::scene::MutableTextureViewDescription& description) override {
		return std::make_unique<RecordedView>(description);
	}
	std::unique_ptr<sgre::scene::MutableTextureSampler> CreateSampler(const sgre::scene::MutableTextureSamplerDescription& description) override {
		Operations.push_back("sampler");
		return std::make_unique<RecordedSampler>(description);
	}
	sgre::scene::MutableTextureMapping Map(sgre::scene::MutableTextureResource& resource, sgre::scene::MutableTextureMapMode) override {
		auto& texture = dynamic_cast<RecordedTexture&>(resource);
		return {texture.Bytes, 16, true};
	}
	void Unmap(sgre::scene::MutableTextureResource&) override {
	}
	void Upload(sgre::scene::MutableTextureResource&, std::span<const std::byte>, std::uint32_t) override {
		throw std::runtime_error("GPU dirty path cannot upload CPU bytes");
	}
	void Copy(sgre::scene::MutableTextureResource& destination, sgre::scene::MutableTextureResource& source) override {
		Operations.push_back("copy");
		Require(destination.Description().ShaderResource && !source.Description().ShaderResource && source.Description().Usage == sgre::scene::MutableTextureUsage::Staging, "companion copy changed actual source/destination roles");
		if (FailCopy) {
			throw std::runtime_error("recorded copy failure");
		}
	}
	void Bind(sgre::scene::MutableTextureView*, sgre::scene::MutableTextureSampler*, std::uint32_t, sgre::scene::MutableTextureStage) override {
		Operations.push_back("bind");
	}
};

int main() {
	try {
		RecordedBackend backend;
		sgre::scene::MutableTextureSpec spec;
		spec.Storage = sgre::scene::MutableTextureStorage::StagingCompanion;
		spec.Description.Width = 4;
		spec.Description.Height = 4;
		sgre::scene::MutableTextureOwner capture(backend, spec);
		Require(!capture.State().Dirty && capture.State().HasCompanion && !capture.State().HasView && capture.Companion()->State().HasView, "Storage4 must keep the real staging main separate from its sampled companion");
		capture.NotifyGpuWrite();
		Require(capture.State().Dirty && backend.Operations.empty(), "notification only marks the actual recorded GPU write dirty");
		backend.FailCopy = true;
		RequireFailure([&] {
			capture.Bind(3, sgre::scene::MutableTextureStage::Pixel);
		},
		    "copy failure");
		Require(capture.State().Dirty && backend.Operations == std::vector<std::string_view>{"copy"}, "failed real companion copy retains dirty state and cannot publish a bind");
		backend.FailCopy = false;
		backend.Operations.clear();
		capture.Bind(3, sgre::scene::MutableTextureStage::Pixel);
		Require(!capture.State().Dirty && backend.Operations == std::vector<std::string_view>{"copy", "sampler", "bind"}, "successful native companion copy precedes sampled binding and consumes dirty state");
		backend.Operations.clear();
		capture.Bind(3, sgre::scene::MutableTextureStage::Pixel);
		Require(backend.Operations == std::vector<std::string_view>{"bind"}, "ordinary repeated bind does not invent another GPU write/copy");
		Require(capture.Map(true).Succeeded, "recording backend exposes a real lock for the portable rejection discriminator");
		RequireFailure([&] {
			capture.NotifyGpuWrite();
		},
		    "unlocked staging-companion");
		Require(!capture.State().Dirty, "rejected locked notification must not change dirty state");
		capture.Unmap();
		capture.NotifyGpuWrite();
		Require(capture.State().Dirty, "a fresh actual GPU write can dirty the unlocked owner again");
		spec.Storage = sgre::scene::MutableTextureStorage::CpuShadow;
		sgre::scene::MutableTextureOwner shadow(backend, spec);
		RequireFailure([&] {
			shadow.NotifyGpuWrite();
		},
		    "staging-companion");
		Require(!shadow.State().Dirty, "non-staging notification cannot alter CPU shadow state");
		std::cout << "Mutable GPU write Storage4/lock/copy retry/companion bind PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
