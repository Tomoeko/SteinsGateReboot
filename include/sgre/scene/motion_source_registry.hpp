#pragma once

#include "sgre/resource/motion.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {

	struct MotionProjectIdentity {
		// The resource owner assigns one nonzero ID per loaded document instance.
		std::uint64_t DocumentInstance = 0;
		std::uint32_t RootOffset = 0;

		[[nodiscard]] bool operator==(const MotionProjectIdentity&) const = default;
	};

	struct MotionSourceAlias {
		std::string Key;
		std::string Source;
		std::string Icon;
	};

	struct MotionSourceRegistration {
		MotionProjectIdentity Project;
		std::span<const resource::MotionSource> Sources;
		std::span<const MotionSourceAlias> Aliases;
	};

	struct ResolvedMotionIcon {
		MotionProjectIdentity Project;
		std::size_t RegistrationIndex = 0;
		const resource::MotionSource* Source = nullptr;
		const resource::MotionIcon* Icon = nullptr;
		bool ProjectLocal = false;
	};

	[[nodiscard]] std::vector<MotionSourceAlias> ReadMotionSourceAliases(const resource::PsbDocument& document);

	class PreparedMotionSourceRegistry {
	public:
		// Sources, icons, and aliases must remain alive and unchanged for the registry's lifetime.
		// The owner supplies the native registration order after deduplicating document instances.
		explicit PreparedMotionSourceRegistry(std::span<const MotionSourceRegistration> registrations);

		// Lookup does not allocate. The returned resource pointers borrow the registrations' owners.
		[[nodiscard]] std::optional<ResolvedMotionIcon> Resolve(std::string_view source, std::string_view icon, std::optional<MotionProjectIdentity> requesting_project = std::nullopt) const;

	private:
		struct SourceIndex {
			const resource::MotionSource* Source = nullptr;
			std::vector<const resource::MotionIcon*> Icons;
		};

		struct RegistrationIndex {
			MotionProjectIdentity Project;
			std::vector<SourceIndex> Sources;
			std::vector<const MotionSourceAlias*> Aliases;
		};

		[[nodiscard]] std::optional<ResolvedMotionIcon> Find(std::size_t registration_index, std::string_view& source, std::string_view& icon, bool project_local) const;

		std::vector<RegistrationIndex> m_Registrations;
	};

} // namespace sgre::scene
