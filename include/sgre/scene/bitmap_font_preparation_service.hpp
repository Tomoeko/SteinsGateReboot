#pragma once

#include "sgre/scene/bitmap_font_receiving_document.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sgre::scene {

	struct BitmapFontPreparationLimits {
		std::size_t MaxPendingJobs = 64;
		std::size_t MaxQueuedBytes = 160 * 1024 * 1024;
		std::size_t MaxLiveDocuments = 4096;
	};

	class BitmapFontPreparationService {
	public:
		// The construction thread is the actual texture preparation owner. Platform callbacks remain live through Close.
		BitmapFontPreparationService(TextureCache::Upload upload, TextureCache::Release release, TextureCacheLimits cache_limits = {}, BitmapFontPreparationLimits limits = {}, TextureCache::UploadBc7 upload_bc7 = {});
		~BitmapFontPreparationService() noexcept;
		BitmapFontPreparationService(const BitmapFontPreparationService&) = delete;
		BitmapFontPreparationService& operator=(const BitmapFontPreparationService&) = delete;
		BitmapFontPreparationService(BitmapFontPreparationService&&) = delete;
		BitmapFontPreparationService& operator=(BitmapFontPreparationService&&) = delete;

		// Owner calls prepare immediately; worker calls wait for this real owner's PreparePending, including upload/purge.
		// A waiting worker cannot be joined by the host before pumping or cancelling its queued job through Close.
		[[nodiscard]] std::shared_ptr<const BitmapFontReceivingDocument> Prepare(std::vector<std::uint8_t> decoded_psb);
		// Explicit startup/transition preparation, outside ordinary frames. Never invoke from upload/release callbacks.
		[[nodiscard]] std::size_t PreparePending(std::size_t max_jobs = 1);
		// Final references released off-thread are queued; actual wrapper/cache disposal remains on this owner.
		void RetirePending();
		[[nodiscard]] std::size_t PendingCount() const;
		[[nodiscard]] std::size_t LiveDocumentCount() const;
		[[nodiscard]] TextureCache& Cache();
		[[nodiscard]] bool IsOwnerThread() const noexcept;
		[[nodiscard]] bool CanClose() const noexcept;
		// Stop before joining resource workers: reject future jobs and fail queued jobs while existing document pins stay live.
		void Stop();
		// Close only after all returned document/provider pins expire.
		void Close();

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
