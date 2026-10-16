#include "sgre/scene/motion_resources.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sgre::scene {

	struct MotionResourceSourceRecord {
		const resource::MotionSource* Source = nullptr;
		MotionTextureSlot Slot;
	};

	struct MotionResourceRecord {
		MotionProjectId Project;
		std::uint64_t DocumentInstance = 0;
		std::map<std::string, MotionResourceSourceRecord, std::less<>> Sources;
		std::size_t NameBytes = 0;
	};

	static std::string_view MotionResourceName(std::string_view name) {
		return name.substr(0, name.find('\0'));
	}

	class MotionResourceManager::Implementation {
	public:
		Implementation(TextureCache& cache, MotionResourceManagerLimits limits) : Projects(std::make_unique<MotionProjectRegistry>(limits.Projects)), Textures(cache, limits.MaxTextureSlots), External(limits.MaxExternalTextureSlots), Limits(limits) {
			if (limits.MaxSourceNameBytes == 0 || limits.MaxSourceNameBytes > 64 * 1024 * 1024) {
				throw std::runtime_error("motion resource source-name limit must be bounded and positive");
			}

			Records.reserve(limits.Projects.MaxProjects);
		}

		std::unique_ptr<MotionProjectRegistry> Projects;
		MotionTextureSlots Textures;
		MotionSharedTextures External;
		MotionResourceManagerLimits Limits;
		std::vector<MotionResourceRecord> Records;
		std::size_t NameBytes = 0;
		bool Mutating = false;
		bool Closed = false;

		class Mutation {
		public:
			explicit Mutation(Implementation& owner) : Owner(owner) {
				if (owner.Mutating || owner.Closed) {
					throw std::runtime_error("motion resource mutation requires its live preparation owner outside a resource callback");
				}

				owner.Mutating = true;
			}

			~Mutation() {
				Owner.Mutating = false;
			}

		private:
			Implementation& Owner;
		};

		auto Find(MotionProjectId project) const {
			return std::find_if(Records.begin(), Records.end(), [project](const auto& record) {
				return record.Project == project;
			});
		}

		MotionResourceRecord Prepare(std::uint64_t document_instance, const resource::MotionResource& resource, std::span<const std::uint8_t> bytes) {
			if (resource.Sources.size() > Limits.MaxTextureSlots) {
				throw std::runtime_error("motion resource source count exceeds its texture-slot limit");
			}

			MotionResourceRecord record;
			record.DocumentInstance = document_instance;
			try {
				for (const auto& source : resource.Sources) {
					const auto name = MotionResourceName(source.Name);
					if (name.size() > Limits.MaxSourceNameBytes - NameBytes - record.NameBytes) {
						throw std::runtime_error("motion resource source names exceed their aggregate limit");
					}

					const auto [position, inserted] = record.Sources.try_emplace(std::string(name));
					if (!inserted) {
						throw std::runtime_error("motion resource contains duplicate native source names");
					}

					try {
						position->second.Slot = Textures.Import(document_instance, source.Texture, bytes);
					} catch (...) {
						record.Sources.erase(position);

						throw;
					}

					position->second.Source = &source;
					record.NameBytes += name.size();
				}
			} catch (...) {
				const auto failure = std::current_exception();
				try {
					ReleaseSources(record);
				} catch (...) {
					// Preserve the preparation error after attempting all newly imported owners.
				}

				std::rethrow_exception(failure);
			}

			return record;
		}

		void ReleaseSources(const MotionResourceRecord& record) {
			std::exception_ptr failure;
			for (const auto& [name, source] : record.Sources) {
				static_cast<void>(name);
				try {
					Textures.Release(source.Slot);
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Close() {
			if (Closed) {
				return;
			}

			const Mutation mutation(*this);
			if (!External.CanClose()) {
				throw std::runtime_error("motion resource close requires retired external listeners and selected source references");
			}
			std::exception_ptr failure;
			try {
				Textures.Close();
			} catch (...) {
				failure = std::current_exception();
			}

			Records.clear();
			Projects.reset();
			NameBytes = 0;
			Closed = true;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	MotionResourceManager::MotionResourceManager(TextureCache& cache, MotionResourceManagerLimits limits) : m_Implementation(std::make_unique<Implementation>(cache, limits)) {
	}

	MotionSharedTextures& MotionResourceManager::ExternalTextures() const {
		if (m_Implementation->Closed) {
			throw std::runtime_error("motion external table requires its live actual resource manager");
		}

		return m_Implementation->External;
	}

	MotionResourceManager::~MotionResourceManager() {
		try {
			Close();
		} catch (...) {
			// Explicit Close reports service failures after every owned texture and project has retired.
		}
	}

	MotionProjectId MotionResourceManager::Register(MotionProjectIdentity identity, std::shared_ptr<const resource::MotionResource> resource, std::span<const std::uint8_t> bytes) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		if (identity.DocumentInstance == 0 || !resource) {
			throw std::runtime_error("motion resource registration requires a loaded document and immutable model");
		}

		const auto existing = std::find_if(state.Records.begin(), state.Records.end(), [identity](const auto& record) {
			return record.DocumentInstance == identity.DocumentInstance;
		});
		if (existing != state.Records.end()) {
			return state.Projects->Register(identity, std::move(resource));
		}

		if (state.Records.size() == state.Limits.Projects.MaxProjects) {
			throw std::runtime_error("motion resource registration capacity is exhausted");
		}

		auto prepared = state.Prepare(identity.DocumentInstance, *resource, bytes);
		try {
			prepared.Project = state.Projects->Register(identity, std::move(resource));
		} catch (...) {
			const auto failure = std::current_exception();
			try {
				state.ReleaseSources(prepared);
			} catch (...) {
				// Roll back uploaded owners without concealing the project preparation error.
			}

			std::rethrow_exception(failure);
		}

		const auto project = prepared.Project;
		state.NameBytes += prepared.NameBytes;
		state.Records.push_back(std::move(prepared));

		return project;
	}

	bool MotionResourceManager::Unregister(MotionProjectId project) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		const auto found = state.Find(project);
		if (found == state.Records.end()) {
			return false;
		}

		if (state.Projects->ReferenceCount(project) != 1) {
			return state.Projects->Unregister(project);
		}

		std::exception_ptr failure;
		try {
			state.ReleaseSources(*found);
		} catch (...) {
			failure = std::current_exception();
		}

		// The project/model remains live during every native source-texture disposal callback.
		static_cast<void>(state.Projects->Unregister(project));
		state.NameBytes -= found->NameBytes;
		state.Records.erase(state.Records.begin() + (found - state.Records.cbegin()));
		if (failure) {
			std::rethrow_exception(failure);
		}

		return true;
	}

	bool MotionResourceManager::Contains(MotionProjectId project) const {
		return !m_Implementation->Closed && m_Implementation->Projects->Contains(project);
	}

	std::size_t MotionResourceManager::Count() const {
		return m_Implementation->Closed ? 0 : m_Implementation->Projects->Count();
	}

	std::uint32_t MotionResourceManager::ReferenceCount(MotionProjectId project) const {
		if (!Contains(project)) {
			throw std::runtime_error("motion resource project registration does not exist");
		}

		return m_Implementation->Projects->ReferenceCount(project);
	}

	MotionProjectIdentity MotionResourceManager::Identity(MotionProjectId project) const {
		if (!Contains(project)) {
			throw std::runtime_error("motion resource identity requires a live registration");
		}

		return m_Implementation->Projects->Identity(project);
	}

	const resource::MotionResource& MotionResourceManager::Resource(MotionProjectId project) const {
		if (!Contains(project)) {
			throw std::runtime_error("motion resource model requires a live registration");
		}

		return m_Implementation->Projects->Resource(project);
	}

	std::size_t MotionResourceManager::WriteRegistrationOrder(std::span<MotionProjectId> output) const {
		return m_Implementation->Closed ? 0 : m_Implementation->Projects->WriteRegistrationOrder(output);
	}

	std::size_t MotionResourceManager::WriteSourceNames(MotionProjectId project, std::span<std::string_view> output) const {
		const auto found = m_Implementation->Find(project);
		if (found == m_Implementation->Records.end()) {
			return 0;
		}

		if (output.size() < found->Sources.size()) {
			throw std::runtime_error("motion source-name output is shorter than its registered map");
		}

		std::size_t index = 0;
		for (const auto& [name, source] : found->Sources) {
			static_cast<void>(source);
			output[index++] = name;
		}

		return index;
	}

	std::optional<RegisteredMotionSource> MotionResourceManager::FindSource(MotionProjectId project, std::string_view name) const {
		const auto found = m_Implementation->Find(project);
		if (found == m_Implementation->Records.end()) {
			return {};
		}

		const auto source = found->Sources.find(MotionResourceName(name));
		if (source == found->Sources.end()) {
			return {};
		}

		return RegisteredMotionSource{source->second.Source, source->second.Slot, m_Implementation->Textures.Get(source->second.Slot).Texture};
	}

	std::size_t MotionResourceManager::WriteCharacterNames(MotionProjectId project, std::span<std::string_view> output) const {
		return m_Implementation->Closed ? 0 : m_Implementation->Projects->WriteCharacterNames(project, output);
	}

	const resource::MotionObject* MotionResourceManager::FindCharacter(MotionProjectId project, std::string_view name) const {
		return m_Implementation->Closed ? nullptr : m_Implementation->Projects->FindCharacter(project, name);
	}

	std::optional<ResolvedMotionTimeline> MotionResourceManager::ResolveTimeline(std::string_view character, std::string_view timeline, std::optional<MotionProjectIdentity> requesting_project) const {
		return m_Implementation->Closed ? std::optional<ResolvedMotionTimeline>{} : m_Implementation->Projects->ResolveTimeline(character, timeline, requesting_project);
	}

	bool MotionResourceManager::RenameCharacter(MotionProjectId project, std::string_view old_name, std::string_view new_name) {
		const Implementation::Mutation mutation(*m_Implementation);

		return m_Implementation->Projects->RenameCharacter(project, old_name, new_name);
	}

	bool MotionResourceManager::RemoveCharacter(MotionProjectId project, std::string_view name) {
		const Implementation::Mutation mutation(*m_Implementation);

		return m_Implementation->Projects->RemoveCharacter(project, name);
	}

	void MotionResourceManager::SetFiltering(std::int32_t minification, std::int32_t magnification) {
		const Implementation::Mutation mutation(*m_Implementation);
		m_Implementation->Textures.SetFiltering(minification, magnification);
	}

	void MotionResourceManager::Close() {
		m_Implementation->Close();
	}

} // namespace sgre::scene
