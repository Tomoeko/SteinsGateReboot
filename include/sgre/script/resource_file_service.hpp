#pragma once

#include "sgre/io/file_read_processor.hpp"
#include "sgre/script/resource_bindings.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace sgre::script {
	struct ResourceFileServiceLimits {
		std::size_t MaxTargets = 65536;
		std::size_t MaxNameBytes = 4096;
		std::size_t MaxPackedBytes = 160 * 1024 * 1024;
		std::size_t MaxDecodedBytes = 160 * 1024 * 1024;
		std::size_t MaxMetadataBytes = 64 * 1024 * 1024;
		std::size_t ShellKeyPeriod = 131;
	};

	// Actual registered mounted lookup only. Caller supplied the real body-file mapping at mount admission.
	[[nodiscard]] io::PreparedResourceFileRead PrepareMountedResourceRead(const resource::ArchiveMounts& mounts, std::string_view read_filename, io::ResourceReadFormat format, std::string_view seed, ResourceFileServiceLimits limits = {});

	class ScriptResourceFileService {
	public:
		// These actual receiving/common owners enclose all target observers; returned find pins own cache state.
		ScriptResourceFileService(resource::ResourceCache& cache, resource::ArchiveMounts& mounts, io::FileReadProcessor& processor, std::string actual_seed, ResourceFileServiceLimits limits = {}, std::shared_ptr<scene::BitmapFontPreparationService> font_preparation = {}, std::shared_ptr<scene::MotionPreparationService> motion_preparation = {}, std::shared_ptr<scene::ImagePreparationService> image_preparation = {});
		~ScriptResourceFileService() noexcept;
		ScriptResourceFileService(const ScriptResourceFileService&) = delete;
		ScriptResourceFileService& operator=(const ScriptResourceFileService&) = delete;
		ScriptResourceFileService(ScriptResourceFileService&&) = delete;
		ScriptResourceFileService& operator=(ScriptResourceFileService&&) = delete;
		[[nodiscard]] ScriptResourceBindings::TypedAssetRequest RequestService() const;
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
