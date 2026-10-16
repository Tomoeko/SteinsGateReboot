#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/resource/resource_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace sgre::resource {
	struct PsbJobLimits {
		std::size_t MaxOutstandingJobs = 64;
		std::size_t MaxDocumentSlots = 65536;
		std::size_t MaxMetadataBytes = 64 * 1024 * 1024;
		std::size_t MaxDecodedBytes = 160 * 1024 * 1024;
	};

	class PsbResourceJobs {
	public:
		using ReadOperation = std::function<std::vector<std::uint8_t>()>;
		// Cache and every reader's captured owners must outlive this queue. Only workers execute readers.
		explicit PsbResourceJobs(ResourceCache& cache, PsbJobLimits limits = {});
		~PsbResourceJobs();
		PsbResourceJobs(const PsbResourceJobs&) = delete;
		PsbResourceJobs& operator=(const PsbResourceJobs&) = delete;
		PsbResourceJobs(PsbResourceJobs&&) = delete;
		PsbResourceJobs& operator=(PsbResourceJobs&&) = delete;

		// Queue an existing acquired/prefetched pending handle; this queue does not add a cache reference.
		void Queue(CachedResourceHandle resource, ReadOperation read);
		// Owner-thread publication between frames; never waits for I/O or runs a decoder.
		[[nodiscard]] bool PublishCompleted();
		[[nodiscard]] std::size_t OutstandingJobs() const;
		[[nodiscard]] std::shared_ptr<const PsbDocument> Document(CachedResourceHandle resource) const;
		// Explicit transition/clear maintenance releases metadata whose cache generation was evicted.
		void CollectEvictedDocuments();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::resource
