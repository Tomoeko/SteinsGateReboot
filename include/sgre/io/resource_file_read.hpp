#pragma once

#include "sgre/io/raw_read_destination.hpp"
#include "sgre/io/wrapped_psb_destination.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sgre::resource {
	class ArchiveMounts;
} // namespace sgre::resource

namespace sgre::scene {
	class BitmapFontPreparationService;
	class MotionPreparationService;
	class ImagePreparationService;
	class ImageReceivingDocument;
	class MotionReceivingDocument;
	class BitmapFontReceivingDocument;
} // namespace sgre::scene

namespace sgre::io {
	class PendingRawReadRequests;

	enum class ResourceFileKind {
		Binary,
		Psb
	};

	struct ResourceReadFormat {
		ResourceFileKind Kind = ResourceFileKind::Binary;
		bool Wrapped = false;
		bool operator==(const ResourceReadFormat&) const = default;
	};

	// Diagnostic last-entered operations; these values never drive completion, cancellation or scheduling.
	enum class ResourceReceivingPhase : std::uint32_t {
		Unassigned,
		Queued,
		Selected,
		ResolvingPhysicalRegion,
		ReadingPhysicalBytes,
		PhysicalReadPrepared,
		PreparingObject,
		TransformingMzs,
		DecodingZstd,
		ReadingPsbHeader,
		ReadingPsbMetadata,
		PreparingMotion,
		PreparingFont,
		PreparingOrdinaryPsb,
		PreparingBinary,
		ObjectPrepared,
		PublishingCompletion,
		PreparingImage
	};

	struct ResourceReceivingObservation {
		ResourceReceivingPhase Phase = ResourceReceivingPhase::Unassigned;
		FileReadCompletion Completion = FileReadCompletion::Pending;
		bool CanRetire = true;
	};

	struct PreparedResourceFileRead {
		PreparedRawFileRead Region;
		// Logical mounted requests borrow the actual processor mount owner through queued/selected worker retirement.
		// Resolve on the selected worker; Region then supplies only the allocation bound, without a guessed physical path.
		const resource::ArchiveMounts* MountedSource = nullptr;
		ResourceReadFormat Format;
		// Original logical read identity is distinct from the prepared physical body filename/range.
		std::string OriginalFilename;
		std::string Seed;
		std::size_t ShellKeyPeriod = 131;
		std::size_t MaxDecodedBytes = 160 * 1024 * 1024;
		std::size_t MaxMetadataBytes = 64 * 1024 * 1024;
		// Optional real font producer; worker completion waits for its preparation-owner upload/purge.
		std::shared_ptr<scene::BitmapFontPreparationService> FontPreparation;
		std::shared_ptr<scene::MotionPreparationService> MotionPreparation;
		std::shared_ptr<scene::ImagePreparationService> ImagePreparation;
	};

	struct PreparedResourceFileResult {
		FileReadCompletion Completion = FileReadCompletion::Pending;
		std::vector<std::uint8_t> Bytes;
		std::unique_ptr<PublishedPsbFile> Psb;
		std::shared_ptr<const scene::BitmapFontReceivingDocument> Font;
		std::shared_ptr<const scene::MotionReceivingDocument> Motion;
		std::shared_ptr<const scene::ImageReceivingDocument> Image;
		std::string Error;
		bool UnsupportedProduction = false;
	};

	// Missing id/spec and exact win audio identifiers have a proved empty-texture receiving lane. Other populated targets reject.
	[[nodiscard]] bool RequiresDerivedPsbProduction(const resource::PsbDocument& document) noexcept;

	class ResourceFileReadDestination {
	public:
		// Actual cache Loading entry owns this receiving cell; request/worker borrows add no resource/VM root.
		explicit ResourceFileReadDestination(ResourceReadFormat format);
		~ResourceFileReadDestination() noexcept;
		ResourceFileReadDestination(const ResourceFileReadDestination&) = delete;
		ResourceFileReadDestination& operator=(const ResourceFileReadDestination&) = delete;
		ResourceFileReadDestination(ResourceFileReadDestination&&) = delete;
		ResourceFileReadDestination& operator=(ResourceFileReadDestination&&) = delete;
		void MarkQueued();
		[[nodiscard]] bool CancelQueued();
		void Select();
		[[nodiscard]] bool ReadPhysicalSelected(const PreparedResourceFileRead& source) noexcept;
		[[nodiscard]] FileReadCompletion PrepareSelectedObject(const PreparedResourceFileRead& source) noexcept;
		void PublishSelected() noexcept;
		[[nodiscard]] FileReadCompletion Completion() const noexcept;
		[[nodiscard]] bool CanRetire() const noexcept;
		[[nodiscard]] ResourceReadFormat Format() const noexcept;
		// Independently sampled atomics only; payload is never touched and the worker may advance during observation.
		[[nodiscard]] ResourceReceivingObservation ObserveReceiving() const noexcept;

	private:
		friend class sgre::resource::ResourceCache;
		void RequireOwnerThread() const;
		void StoreDiagnostic(std::string_view error) noexcept;
		[[nodiscard]] std::size_t PreparedByteCount() const;
		[[nodiscard]] std::uint32_t PreparedCharge() const;
		[[nodiscard]] PreparedResourceFileResult TakePrepared();
		const ResourceReadFormat m_Format;
		const std::thread::id m_OwnerThread;
		std::atomic<std::uint32_t> m_State;
		std::atomic<ResourceReceivingPhase> m_ObservedPhase = ResourceReceivingPhase::Unassigned;
		std::vector<std::uint8_t> m_PackedBytes;
		PreparedResourceFileResult m_Result;
		bool m_PhysicalPrepared = false;
		bool m_ObjectPrepared = false;
		bool m_Taken = false;
	};
} // namespace sgre::io
