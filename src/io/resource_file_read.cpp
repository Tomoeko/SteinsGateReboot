#include "sgre/io/resource_file_read.hpp"

#include "sgre/io/file_reader.hpp"
#include "sgre/io/mounted_resource_read.hpp"
#include "sgre/resource/mzs_shell.hpp"
#include "sgre/resource/zstd_decoder.hpp"
#include "sgre/scene/bitmap_font_preparation_service.hpp"
#include "sgre/scene/image_preparation_service.hpp"
#include "sgre/scene/motion_preparation_service.hpp"

#include <exception>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace sgre::io {
	static constexpr std::uint32_t kResourceUnassigned = 5;
	static constexpr std::uint32_t kResourceQueued = 6;
	static constexpr std::uint32_t kResourceSelected = 7;
	static constexpr std::size_t kMaxResourceFileBytes = 160 * 1024 * 1024;

	bool RequiresDerivedPsbProduction(const resource::PsbDocument& document) noexcept {
		if (!std::holds_alternative<resource::PsbObject>(document.Root.Data)) {
			return false;
		}

		const auto* id = document.Root.Find("id");
		const auto* spec = document.Root.Find("spec");
		if (!id || !spec) {
			return false;
		}

		const auto* id_text = std::get_if<std::string>(&id->Data);
		const auto* spec_text = std::get_if<std::string>(&spec->Data);
		// Native 0x1404d6510 misses both audio ids; the default MTex owner keeps its texture vector empty.
		const bool known_audio = id_text && spec_text && (*id_text == "sound" || *id_text == "sound_archive") && *spec_text == "win";

		return !known_audio;
	}

	ResourceFileReadDestination::ResourceFileReadDestination(ResourceReadFormat format)
	    : m_Format(format), m_OwnerThread(std::this_thread::get_id()), m_State(kResourceUnassigned) {
		if (format.Kind != ResourceFileKind::Binary && format.Kind != ResourceFileKind::Psb) {
			throw std::runtime_error("resource receiving cell has an unsupported native payload kind");
		}

		m_Result.Error = "resource read request did not complete";
	}

	ResourceFileReadDestination::~ResourceFileReadDestination() noexcept {
		if (!CanRetire()) {
			std::terminate();
		}
	}

	void ResourceFileReadDestination::RequireOwnerThread() const {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("resource receiving ownership requires its actual manager thread");
		}
	}

	void ResourceFileReadDestination::MarkQueued() {
		RequireOwnerThread();
		auto expected = kResourceUnassigned;
		if (!m_State.compare_exchange_strong(expected, kResourceQueued, std::memory_order_release, std::memory_order_relaxed)) {
			throw std::runtime_error("resource receiving cell accepts one actual pending request");
		}
		m_ObservedPhase.store(ResourceReceivingPhase::Queued, std::memory_order_release);
	}

	bool ResourceFileReadDestination::CancelQueued() {
		RequireOwnerThread();
		auto expected = kResourceQueued;

		return m_State.compare_exchange_strong(expected, static_cast<std::uint32_t>(FileReadCompletion::Cancelled), std::memory_order_release, std::memory_order_relaxed);
	}

	void ResourceFileReadDestination::Select() {
		auto expected = kResourceQueued;
		if (!m_State.compare_exchange_strong(expected, kResourceSelected, std::memory_order_acq_rel, std::memory_order_relaxed)) {
			throw std::runtime_error("resource selection requires an actual uncancelled pending node");
		}
		m_ObservedPhase.store(ResourceReceivingPhase::Selected, std::memory_order_release);
	}

	void ResourceFileReadDestination::StoreDiagnostic(std::string_view error) noexcept {
		try {
			if (!error.empty()) {
				m_Result.Error = std::string(error.substr(0, 4096));
			}
		} catch (...) {
		}
	}

	bool ResourceFileReadDestination::ReadPhysicalSelected(const PreparedResourceFileRead& source) noexcept {
		if (m_State.load(std::memory_order_acquire) != kResourceSelected || m_PhysicalPrepared) {
			std::terminate();
		}

		m_Result.Completion = FileReadCompletion::ReadFailed;
		try {
			if (source.MountedSource && (!source.Region.Filename.empty() || source.Region.Offset != 0 || source.Region.Length != 0)) {
				throw std::runtime_error("logical mounted source cannot also substitute a physical region");
			}

			m_ObservedPhase.store(ResourceReceivingPhase::ResolvingPhysicalRegion, std::memory_order_release);
			const auto region = source.MountedSource ? ResolveMountedResourceReadRegion(*source.MountedSource, source.OriginalFilename, source.Region.MaxBytes) : source.Region;
			if (source.Format != m_Format || region.Filename.empty() || region.Filename.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos || region.MaxBytes == 0 || region.MaxBytes > kMaxResourceFileBytes) {
				throw std::runtime_error("resource read requires its immutable format and checked physical region");
			}

			m_ObservedPhase.store(ResourceReceivingPhase::ReadingPhysicalBytes, std::memory_order_release);
			FileReader reader(region.Filename);
			const auto size = region.Length == 0 ? reader.Size() : region.Length;
			if (size > region.MaxBytes || size >= std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("resource physical size exceeds its native word/allocation bound");
			}

			m_PackedBytes = reader.Read(region.Offset, static_cast<std::size_t>(size));
			m_Result.Completion = FileReadCompletion::Succeeded;
		} catch (const std::exception& error) {
			StoreDiagnostic(error.what());
		} catch (...) {
		}

		m_PhysicalPrepared = true;
		m_ObservedPhase.store(ResourceReceivingPhase::PhysicalReadPrepared, std::memory_order_release);

		return m_Result.Completion == FileReadCompletion::Succeeded;
	}

	FileReadCompletion ResourceFileReadDestination::PrepareSelectedObject(const PreparedResourceFileRead& source) noexcept {
		if (m_State.load(std::memory_order_acquire) != kResourceSelected || !m_PhysicalPrepared || m_ObjectPrepared) {
			std::terminate();
		}

		m_ObservedPhase.store(ResourceReceivingPhase::PreparingObject, std::memory_order_release);
		if (m_Result.Completion == FileReadCompletion::Succeeded) {
			m_Result.Completion = FileReadCompletion::PostprocessFailed;
			try {
				if (source.Format != m_Format || source.OriginalFilename.empty() || source.OriginalFilename.size() > 65536 || source.OriginalFilename.find('\0') != std::string::npos || source.MaxDecodedBytes == 0 || source.MaxDecodedBytes > kMaxResourceFileBytes) {
					throw std::runtime_error("resource preparation requires its immutable original identity and decoded bound");
				}

				auto bytes = std::move(m_PackedBytes);
				if (m_Format.Wrapped) {
					if (source.Seed.empty() || source.Seed.size() > 65536 || source.Seed.find('\0') != std::string::npos || source.ShellKeyPeriod == 0 || source.ShellKeyPeriod > 65536) {
						throw std::runtime_error("wrapped resource requires actual cipher seed and key-period bounds");
					}

					if (bytes.size() < 4 || bytes[0] != 'm' || bytes[1] != 'z' || bytes[2] != 's' || bytes[3] != 0) {
						m_Result.UnsupportedProduction = true;
						throw std::runtime_error("resource wrapper production supports the observed MZS domain only");
					}

					m_ObservedPhase.store(ResourceReceivingPhase::TransformingMzs, std::memory_order_release);
					const auto shell = resource::DecodeMzsShell(bytes, source.OriginalFilename, source.Seed, source.ShellKeyPeriod, source.MaxDecodedBytes);
					m_ObservedPhase.store(ResourceReceivingPhase::DecodingZstd, std::memory_order_release);
					bytes = resource::DecodeZstd(shell.CompressedBytes, shell.DecodedSize, source.MaxDecodedBytes);
				} else if (bytes.size() > source.MaxDecodedBytes) {
					throw std::runtime_error("raw resource exceeds its decoded ownership bound");
				}

				if (m_Format.Kind == ResourceFileKind::Psb) {
					if (source.MaxMetadataBytes == 0 || source.MaxMetadataBytes > 64 * 1024 * 1024 || source.MaxMetadataBytes > source.MaxDecodedBytes) {
						throw std::runtime_error("resource PSB metadata requires a checked decoded allocation bound");
					}

					if (bytes.size() >= 8 && bytes[0] == 'P' && bytes[1] == 'S' && bytes[2] == 'B' && bytes[3] == 0 && ((bytes[4] != 3 && bytes[4] != 4) || bytes[5] != 0 || bytes[6] != 0 || bytes[7] != 0)) {
						m_Result.UnsupportedProduction = true;
					}

					m_ObservedPhase.store(ResourceReceivingPhase::ReadingPsbHeader, std::memory_order_release);
					const auto header = resource::ReadPsbHeader(bytes);
					if (header.DataOffset > bytes.size() || header.DataOffset > source.MaxMetadataBytes) {
						throw std::runtime_error("resource PSB metadata exceeds its actual decoded extent");
					}

					m_ObservedPhase.store(ResourceReceivingPhase::ReadingPsbMetadata, std::memory_order_release);
					auto document = resource::ReadPsbMetadata(std::span<const std::uint8_t>(bytes).first(header.DataOffset), bytes.size());
					if (document.SerializedSize != bytes.size()) {
						throw std::runtime_error("resource PSB serialized size differs from its actual decoded bytes");
					}
					if (RequiresDerivedPsbProduction(document)) {
						const auto* id = document.Root.Find("id");
						const auto* spec = document.Root.Find("spec");
						const auto* id_text = id ? std::get_if<std::string>(&id->Data) : nullptr;
						const auto* spec_text = spec ? std::get_if<std::string>(&spec->Data) : nullptr;
						if (source.ImagePreparation && id_text && spec_text && *id_text == "image" && *spec_text == "win") {
							m_ObservedPhase.store(ResourceReceivingPhase::PreparingImage, std::memory_order_release);
							m_Result.Image = source.ImagePreparation->Prepare(std::move(bytes));
						} else if (source.MotionPreparation && id_text && spec_text && *id_text == "motion" && *spec_text == "win") {
							m_ObservedPhase.store(ResourceReceivingPhase::PreparingMotion, std::memory_order_release);
							m_Result.Motion = source.MotionPreparation->Prepare(std::move(bytes));
						} else if (source.FontPreparation && id_text && spec_text && *id_text == "font" && *spec_text == "win") {
							m_ObservedPhase.store(ResourceReceivingPhase::PreparingFont, std::memory_order_release);
							m_Result.Font = source.FontPreparation->Prepare(std::move(bytes));
						} else {
							m_Result.UnsupportedProduction = true;
							throw std::runtime_error("resource PSB id/spec requires unrecovered target-specific MTex/purge production");
						}

					} else {
						m_ObservedPhase.store(ResourceReceivingPhase::PreparingOrdinaryPsb, std::memory_order_release);
						m_Result.Psb = std::make_unique<PublishedPsbFile>(std::move(bytes), std::move(document));
					}
				} else {
					m_ObservedPhase.store(ResourceReceivingPhase::PreparingBinary, std::memory_order_release);
					m_Result.Bytes = std::move(bytes);
				}

				m_Result.Completion = FileReadCompletion::Succeeded;
				m_Result.Error.clear();
			} catch (const std::exception& error) {
				StoreDiagnostic(error.what());
			} catch (...) {
			}
		}

		std::vector<std::uint8_t>{}.swap(m_PackedBytes);
		m_ObjectPrepared = true;
		m_ObservedPhase.store(ResourceReceivingPhase::ObjectPrepared, std::memory_order_release);

		return m_Result.Completion;
	}

	void ResourceFileReadDestination::PublishSelected() noexcept {
		if (m_State.load(std::memory_order_acquire) != kResourceSelected || !m_ObjectPrepared) {
			std::terminate();
		}

		// The owning payload pointer/byte count is complete before this last worker access.
		m_ObservedPhase.store(ResourceReceivingPhase::PublishingCompletion, std::memory_order_release);
		m_State.store(static_cast<std::uint32_t>(m_Result.Completion), std::memory_order_release);
	}

	FileReadCompletion ResourceFileReadDestination::Completion() const noexcept {
		const auto state = m_State.load(std::memory_order_acquire);

		return state >= kResourceUnassigned ? FileReadCompletion::Pending : static_cast<FileReadCompletion>(state);
	}

	ResourceReceivingObservation ResourceFileReadDestination::ObserveReceiving() const noexcept {
		ResourceReceivingObservation observation;
		observation.Phase = m_ObservedPhase.load(std::memory_order_acquire);
		const auto state = m_State.load(std::memory_order_acquire);
		observation.Completion = state >= kResourceUnassigned ? FileReadCompletion::Pending : static_cast<FileReadCompletion>(state);
		observation.CanRetire = state != kResourceQueued && state != kResourceSelected;

		return observation;
	}

	bool ResourceFileReadDestination::CanRetire() const noexcept {
		const auto state = m_State.load(std::memory_order_acquire);

		return state != kResourceQueued && state != kResourceSelected;
	}

	ResourceReadFormat ResourceFileReadDestination::Format() const noexcept {
		return m_Format;
	}

	std::size_t ResourceFileReadDestination::PreparedByteCount() const {
		RequireOwnerThread();
		if (Completion() == FileReadCompletion::Pending || m_Taken) {
			throw std::runtime_error("resource byte count requires an actual completed unconsumed result");
		}

		if (m_Result.Image) {
			return m_Result.Image->MetadataBytes().size();
		}

		if (m_Result.Motion) {
			return m_Result.Motion->MetadataBytes().size();
		}

		if (m_Result.Font) {
			return m_Result.Font->MetadataBytes().size();
		}

		return m_Result.Psb ? m_Result.Psb->Bytes().size() : m_Result.Bytes.size();
	}

	std::uint32_t ResourceFileReadDestination::PreparedCharge() const {
		const auto byte_count = PreparedByteCount();
		// Native PSB+28 retains its original supplied length after font pixel purge.
		const auto charge = m_Result.Image ? m_Result.Image->Document().SerializedSize : (m_Result.Motion ? m_Result.Motion->Document().SerializedSize : (m_Result.Font ? m_Result.Font->Document().SerializedSize : byte_count));
		if (charge >= std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("resource prepared charge exceeds its native uint32 domain");
		}

		return static_cast<std::uint32_t>(charge);
	}

	PreparedResourceFileResult ResourceFileReadDestination::TakePrepared() {
		RequireOwnerThread();
		const auto completion = Completion();
		if (completion == FileReadCompletion::Pending || m_Taken) {
			throw std::runtime_error("resource receiving cell requires one actual completed result");
		}

		m_Taken = true;
		auto result = std::move(m_Result);
		result.Completion = completion;

		return result;
	}
} // namespace sgre::io
