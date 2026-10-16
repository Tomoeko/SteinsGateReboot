#include "sgre/script/resource_file_service.hpp"

#include "sgre/io/mounted_resource_read.hpp"
#include "sgre/scene/bitmap_font_preparation_service.hpp"
#include "sgre/scene/image_preparation_service.hpp"
#include "sgre/scene/motion_preparation_service.hpp"

#include <array>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::script {
	static void ValidateFileServiceLimits(const ResourceFileServiceLimits& limits) {
		if (limits.MaxTargets == 0 || limits.MaxTargets > 65536 || limits.MaxNameBytes == 0 || limits.MaxNameBytes > 65536 || limits.MaxPackedBytes == 0 || limits.MaxPackedBytes > 160 * 1024 * 1024 || limits.MaxDecodedBytes == 0 || limits.MaxDecodedBytes > 160 * 1024 * 1024 || limits.MaxMetadataBytes == 0 || limits.MaxMetadataBytes > 64 * 1024 * 1024 || limits.MaxMetadataBytes > limits.MaxDecodedBytes || limits.ShellKeyPeriod == 0 || limits.ShellKeyPeriod > 65536) {
			throw std::runtime_error("resource file service requires checked target/identity/decoder bounds");
		}
	}

	static io::ResourceReadFormat FileFormat(ScriptResourceRequestMode mode) {
		io::ResourceReadFormat format;
		switch (mode) {
		case ScriptResourceRequestMode::PsbWrapped:
			format.Kind = io::ResourceFileKind::Psb;
			format.Wrapped = true;
			break;
		case ScriptResourceRequestMode::PsbRaw:
			format.Kind = io::ResourceFileKind::Psb;
			break;
		case ScriptResourceRequestMode::BinaryWrapped:
			format.Wrapped = true;
			break;
		case ScriptResourceRequestMode::BinaryRaw:
			break;
		default:
			throw std::runtime_error("resource request uses an unsupported native mode");
		}

		return format;
	}

	static void ValidateSortIdentity(std::string_view name, std::string_view read_filename) {
		const auto first = read_filename.find(".m");
		const auto sort_name = read_filename.size() >= 2 && first == read_filename.size() - 2 ? read_filename.substr(0, read_filename.size() - 2) : read_filename;
		if (sort_name != name) {
			throw std::runtime_error("resource identity is outside the proven native reader sorter-key domain");
		}
	}

	io::PreparedResourceFileRead PrepareMountedResourceRead(const resource::ArchiveMounts& mounts, std::string_view read_filename, io::ResourceReadFormat format, std::string_view seed, ResourceFileServiceLimits limits) {
		ValidateFileServiceLimits(limits);
		io::MountedResourceReadLimits mounted_limits;
		mounted_limits.MaxNameBytes = limits.MaxNameBytes;
		mounted_limits.MaxPackedBytes = limits.MaxPackedBytes;
		mounted_limits.MaxDecodedBytes = limits.MaxDecodedBytes;
		mounted_limits.MaxMetadataBytes = limits.MaxMetadataBytes;
		mounted_limits.ShellKeyPeriod = limits.ShellKeyPeriod;

		return io::PrepareMountedResourceRead(mounts, read_filename, format, seed, mounted_limits);
	}

	class ScriptResourceFileService::Implementation {
	public:
		Implementation(resource::ResourceCache& cache, resource::ArchiveMounts& mounts, io::FileReadProcessor& processor, std::string seed, ResourceFileServiceLimits limits, std::shared_ptr<scene::BitmapFontPreparationService> font_preparation, std::shared_ptr<scene::MotionPreparationService> motion_preparation, std::shared_ptr<scene::ImagePreparationService> image_preparation)
		    : Cache(cache), Mounts(mounts), Processor(processor), Seed(std::move(seed)), Limits(limits), FontPreparation(std::move(font_preparation)), MotionPreparation(std::move(motion_preparation)), ImagePreparation(std::move(image_preparation)) {
			ValidateFileServiceLimits(limits);
			if ((FontPreparation && !FontPreparation->IsOwnerThread()) || (MotionPreparation && !MotionPreparation->IsOwnerThread()) || (ImagePreparation && !ImagePreparation->IsOwnerThread())) {
				throw std::runtime_error("resource texture preparation requires the actual receiving-thread texture owner");
			}

			if (Seed.empty() || Seed.size() > 65536 || Seed.find('\0') != std::string::npos || processor.IsClosed() || !processor.UsesReceivingOwners(cache, mounts)) {
				throw std::runtime_error("resource file service requires the actual processor/cache/mount owners and configured cipher seed");
			}
		}

		resource::ResourceCache& Cache;
		resource::ArchiveMounts& Mounts;
		io::FileReadProcessor& Processor;
		std::string Seed;
		ResourceFileServiceLimits Limits;
		std::shared_ptr<scene::BitmapFontPreparationService> FontPreparation;
		std::shared_ptr<scene::MotionPreparationService> MotionPreparation;
		std::shared_ptr<scene::ImagePreparationService> ImagePreparation;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::size_t LiveTargets = 0;
		bool Closed = false;

		void RequireOpen() const {
			if (std::this_thread::get_id() != OwnerThread || Closed) {
				throw std::runtime_error("resource file service requires its actual open receiving-thread owner");
			}
		}

		class TargetReference {
		public:
			TargetReference(std::shared_ptr<Implementation> owner, std::string name)
			    : Owner(std::move(owner)), Name(std::move(name)), Handle(Owner->Cache.Acquire(Name)) {
				++Owner->LiveTargets;
			}

			~TargetReference() noexcept {
				if (std::this_thread::get_id() != Owner->OwnerThread) {
					std::terminate();
				}
				try {
					if (OwnsReference) {
						Owner->Cache.Release(Handle);
					}
				} catch (...) {
					std::terminate();
				}

				--Owner->LiveTargets;
			}

			std::shared_ptr<Implementation> Owner;
			std::string Name;
			resource::CachedResourceHandle Handle;
			bool OwnsReference = true;
		};

		static std::optional<ScriptResourceAsset> Poll(const std::shared_ptr<TargetReference>& target, io::ResourceReadFormat format) {
			auto& owner = *target->Owner;
			owner.RequireOpen();
			const std::array targets{target->Handle};
			if (owner.Cache.ObserveLoadingTargets(targets)) {
				return std::nullopt;
			}

			const auto info = owner.Cache.Info(target->Handle);
			if (info.UnsupportedProduction) {
				throw std::runtime_error("Resource publication requires unsupported native production for " + target->Name + ": " + info.Error);
			}

			ScriptResourceAsset asset;
			asset.Kind = format.Kind == io::ResourceFileKind::Psb ? ScriptResourceDataKind::Psb : ScriptResourceDataKind::Binary;
			if (info.Completion == resource::ResourceCompletion::Failed) {
				asset.Error = info.Error;
				// This real pin operation also retains the target lease in an error asset until native unload.
				// Find returns null before calling it on a failed target.
				asset.RetainBacking = [target]() {
					return target->Owner->Cache.PinBytes(target->Handle);
				};

				return asset;
			}

			if (format.Kind == io::ResourceFileKind::Psb && owner.Cache.HasImageFile(target->Handle)) {
				asset.Image = std::shared_ptr<const scene::ImageReceivingDocument>(target, &owner.Cache.ImageFile(target->Handle));
				asset.RetainImage = [target]() {
					return target->Owner->Cache.PinImageFile(target->Handle);
				};
				return asset;
			}

			if (format.Kind == io::ResourceFileKind::Psb && owner.Cache.HasMotionFile(target->Handle)) {
				asset.Motion = std::shared_ptr<const scene::MotionReceivingDocument>(target, &owner.Cache.MotionFile(target->Handle));
				asset.RetainMotion = [target]() {
					return target->Owner->Cache.PinMotionFile(target->Handle);
				};
				return asset;
			}

			if (format.Kind == io::ResourceFileKind::Psb && owner.Cache.HasBitmapFontFile(target->Handle)) {
				asset.Font = std::shared_ptr<const scene::BitmapFontReceivingDocument>(target, &owner.Cache.BitmapFontFile(target->Handle));
				asset.RetainFont = [target]() {
					return target->Owner->Cache.PinBitmapFontFile(target->Handle);
				};

				return asset;
			}

			asset.Backing = std::shared_ptr<const std::vector<std::uint8_t>>(target, &owner.Cache.ByteStorage(target->Handle));
			if (format.Kind == io::ResourceFileKind::Psb) {
				asset.Document = std::shared_ptr<const resource::PsbDocument>(target, &owner.Cache.PsbFile(target->Handle).Document());
				asset.RetainPsb = [target]() {
					auto pin = target->Owner->Cache.PinPsbFile(target->Handle);
					const auto* document = &pin->Document();
					const auto* backing = &pin->BackingStorage();
					ScriptPsbDocumentAsset retained;
					retained.Document = std::shared_ptr<const resource::PsbDocument>(pin, document);
					retained.Backing = std::shared_ptr<const std::vector<std::uint8_t>>(std::move(pin), backing);

					return retained;
				};
			} else {
				asset.RetainBacking = [target]() {
					return target->Owner->Cache.PinBytes(target->Handle);
				};
			}

			return asset;
		}

		ScriptResourceBindings::LoadPoll Request(const std::shared_ptr<Implementation>& self, std::string_view name, ScriptResourceRequestMode mode) {
			RequireOpen();
			if (Processor.IsClosed() || name.empty() || name.size() > Limits.MaxNameBytes || name.find('\0') != std::string_view::npos || LiveTargets == Limits.MaxTargets) {
				throw std::runtime_error("resource request requires its actual live reader and bounded target identity");
			}

			const auto format = FileFormat(mode);
			const auto filename = ResourceReadFilename(name, mode);
			ValidateSortIdentity(name, filename);
			const auto existing = Cache.FindNamedResource(name);
			std::optional<io::PreparedResourceFileRead> source;
			if (existing) {
				if (Cache.ReadFormat(*existing) != format || Cache.Info(*existing).References >= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
					throw std::runtime_error("coalesced resource requires its actual immutable mode and supported signed reference domain");
				}
			} else {
				source = PrepareMountedResourceRead(Mounts, filename, format, Seed, Limits);
				source->FontPreparation = FontPreparation;
				source->MotionPreparation = MotionPreparation;
				source->ImagePreparation = ImagePreparation;
			}

			auto target = std::make_shared<TargetReference>(self, std::string(name));
			if (source) {
				try {
					static_cast<void>(Processor.QueuePreparedResource(target->Handle, filename, std::move(*source)));
				} catch (...) {
					Cache.DiscardUnstartedRead(target->Handle);
					target->OwnsReference = false;

					throw;
				}
			}

			return [target = std::move(target), format]() {
				return Poll(target, format);
			};
		}
	};

	ScriptResourceFileService::ScriptResourceFileService(resource::ResourceCache& cache, resource::ArchiveMounts& mounts, io::FileReadProcessor& processor, std::string actual_seed, ResourceFileServiceLimits limits, std::shared_ptr<scene::BitmapFontPreparationService> font_preparation, std::shared_ptr<scene::MotionPreparationService> motion_preparation, std::shared_ptr<scene::ImagePreparationService> image_preparation)
	    : m_Implementation(std::make_shared<Implementation>(cache, mounts, processor, std::move(actual_seed), limits, std::move(font_preparation), std::move(motion_preparation), std::move(image_preparation))) {
	}

	ScriptResourceFileService::~ScriptResourceFileService() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	ScriptResourceBindings::TypedAssetRequest ScriptResourceFileService::RequestService() const {
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);

		return [weak](std::string_view name, ScriptResourceRequestMode mode) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("actual Resource file request service has expired");
			}

			return owner->Request(owner, name, mode);
		};
	}

	bool ScriptResourceFileService::CanClose() const noexcept {
		const auto& owner = *m_Implementation;

		return std::this_thread::get_id() == owner.OwnerThread && owner.LiveTargets == 0;
	}

	void ScriptResourceFileService::Close() {
		if (!CanClose()) {
			throw std::runtime_error("resource file service retirement requires all actual target leases to end first");
		}

		m_Implementation->Closed = true;
	}
} // namespace sgre::script
