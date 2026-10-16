#include "sgre/scene/emote_collision_owner.hpp"

#include "sgre/scene/emote_jelly_physics.hpp"

#include "sgre/scene/emote_collider.hpp"
#include "sgre/scene/emote_string_physics.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::scene {
	static const resource::PsbValue& CollisionField(const resource::PsbValue& value, std::string_view name) {
		if (const auto* field = value.Find(name)) {
			return *field;
		}
		throw std::runtime_error("Emote collision metadata is missing " + std::string(name));
	}

	static float CollisionNumber(const resource::PsbValue& value) {
		float number = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			number = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			number = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			number = *boolean ? 1.0f : 0.0f;
		} else {
			throw std::runtime_error("Emote collision metadata requires native numeric fields");
		}
		if (!std::isfinite(number)) {
			throw std::runtime_error("Emote collision metadata requires finite native numeric fields");
		}
		return number;
	}

	static std::int32_t CollisionInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote collision integer exceeds the native finite conversion domain");
	}

	static EmoteColliderDefinition ReadCollider(const resource::PsbValue& value) {
		const auto type = CollisionInteger(CollisionField(value, "type"));
		if (type < 0 || type > 2) {
			throw std::runtime_error("Emote collisionMap contains an unrecovered collider type");
		}
		EmoteColliderDefinition definition;
		definition.Kind = static_cast<EmoteColliderKind>(static_cast<std::uint32_t>(type));
		if (definition.Kind == EmoteColliderKind::HalfPlane) {
			definition.First = {CollisionNumber(CollisionField(value, "x1")), CollisionNumber(CollisionField(value, "y1"))};
			definition.Second = {CollisionNumber(CollisionField(value, "x2")), CollisionNumber(CollisionField(value, "y2"))};
		} else {
			definition.First = {CollisionNumber(CollisionField(value, "x")), CollisionNumber(CollisionField(value, "y"))};
			definition.Radius = {CollisionNumber(CollisionField(value, "rh")), CollisionNumber(CollisionField(value, "rv"))};
			definition.Rotation = CollisionNumber(CollisionField(value, "rot"));
		}
		return definition;
	}

	static float ColliderAngle(float y, float x) {
		static constexpr float kPi = 3.1415927410125732421875f;
		static constexpr float kHalfPi = 1.57079637050628662109375f;
		float angle = 0;
		if (x == 0) {
			angle = y > 0 ? kHalfPi : y < 0 ? -kHalfPi
			                                : 0;
		} else if (std::abs(x) > std::abs(y)) {
			const auto ratio = y / x;
			const auto magnitude = std::abs(ratio);
			const auto correction = ((magnitude - 1.0f) * ratio) * (magnitude * 0.066299997270107269287109375f + 0.24469999969005584716796875f);
			angle = ratio * 0.785398185253143310546875f - correction;
			if (x < 0) {
				angle += y < 0 ? -kPi : kPi;
			}
		} else {
			const auto ratio = x / y;
			const auto magnitude = std::abs(ratio);
			const auto correction = ((magnitude - 1.0f) * ratio) * (magnitude * 0.066299997270107269287109375f + 0.24469999969005584716796875f);
			angle = (y > 0 ? kHalfPi : -kHalfPi) - (ratio * 0.785398185253143310546875f - correction);
		}
		angle += kHalfPi;
		while (angle < 0) {
			angle += 6.283185482025146484375f;
		}
		while (angle >= 6.283185482025146484375f) {
			angle += -6.283185482025146484375f;
		}
		return angle - kPi;
	}

	class EmoteCollisionGroup::Implementation final : public MotionLayerUidObserver {
	public:
		Implementation(const MotionPlayerCommands& primary, std::string uid, const resource::PsbArray& definitions)
		    : Commands(primary), Uids(primary.LayerUids()), Uid(std::move(uid)) {
			if (definitions.size() > 4096) {
				throw std::runtime_error("Emote collider group exceeds bounded actual shape storage");
			}
			Shapes.reserve(definitions.size());
			for (const auto& definition : definitions) {
				Shapes.push_back(std::make_unique<EmoteCollider>(ReadCollider(definition)));
			}
			Uids.Subscribe(Uid, *this);
			Subscribed = true;
			try {
				RequireReady();
			} catch (...) {
				Uids.Unsubscribe(Uid, *this);
				Subscribed = false;
				throw;
			}
		}

		~Implementation() noexcept override {
			try {
				if (Subscribed) {
					Uids.Unsubscribe(Uid, *this);
				}
			} catch (...) {
				std::terminate();
			}
		}

		void LayerAdded(std::string_view uid, const MotionLayerUidMatch& match) noexcept override {
			try {
				if (Operating || uid != Uid || &match.Layer.Player().LayerUids().UltimatePlayer() != &Commands || Matches == 65536) {
					throw std::runtime_error("Emote collider anchor Added requires the actual primary UID registration");
				}
				LastAnchor = match.Layer;
				++Matches;
			} catch (...) {
				if (!Failure) {
					Failure = std::current_exception();
				}
			}
		}

		void LayerRemoved(std::string_view uid, const MotionLayerUidMatch& match) noexcept override {
			if (Operating || uid != Uid || Matches == 0) {
				std::terminate();
			}
			--Matches;
			if (LastAnchor && *LastAnchor == match.Layer) {
				LastAnchor.reset();
			}
		}

		void RequireReady() const {
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Operating || !Valid) {
				throw std::runtime_error("Emote collision group requires its actual live idle UID owner");
			}
		}

		void Prepare(const EmoteVelvetyInputFrame& frame, bool consumed) {
			RequireReady();
			if (Matches != 0 && !LastAnchor) {
				throw std::runtime_error("Emote collider's native last anchor retired while another same-UID occurrence remained");
			}
			Operating = true;
			try {
				const bool active = consumed && Matches != 0 && LastAnchor->Active();
				EmoteColliderPosture posture;
				if (active) {
					std::array<resource::MotionPoint, 2> points{resource::MotionPoint{0, 0}, resource::MotionPoint{0, 1}};
					LastAnchor->TransformPoints(points);
					for (auto& point : points) {
						if (frame.Mirror) {
							point.X = -point.X;
						}
						const auto x = point.X;
						point.X = (point.Y * frame.WorldToPhysics.XY + x * frame.WorldToPhysics.XX) + frame.WorldToPhysics.TranslationX;
						point.Y = (point.Y * frame.WorldToPhysics.YY + x * frame.WorldToPhysics.YX) + frame.WorldToPhysics.TranslationY;
					}
					posture.Coordinate = points[0];
					posture.Rotation = ColliderAngle(points[1].Y - points[0].Y, points[1].X - points[0].X);
					posture.Cosine = std::cos(posture.Rotation);
					posture.Sine = std::sin(posture.Rotation);
				}
				for (auto& shape : Shapes) {
					shape->SetEnabled(active);
					if (active) {
						shape->ClearContact();
						shape->SetPosture(posture);
						if (Reset) {
							shape->ResetMotion();
						}
					}
				}
				Reset = !active;
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}

		const MotionPlayerCommands& Commands;
		MotionPlayerLayerUids& Uids;
		std::string Uid;
		std::vector<std::unique_ptr<EmoteCollider>> Shapes;
		std::optional<MotionLayerUidReference> LastAnchor;
		std::exception_ptr Failure;
		std::uint32_t Matches = 0;
		std::uint64_t FrameSerial = 0;
		bool Reset = true;
		bool Subscribed = false;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteCollisionGroup::EmoteCollisionGroup(std::unique_ptr<Implementation> implementation)
	    : m_Implementation(std::move(implementation)) {
		if (m_Implementation == nullptr) {
			throw std::runtime_error("Emote collision group requires its actual prepared UID implementation");
		}
	}
	EmoteCollisionGroup::~EmoteCollisionGroup() noexcept = default;

	void EmoteCollisionGroup::Attach(EmoteStringPhysics& actual_physics, std::uint32_t tress_mask) {
		m_Implementation->RequireReady();
		for (auto& shape : m_Implementation->Shapes) {
			actual_physics.AttachCollider(*shape, tress_mask);
		}
	}

	void EmoteCollisionGroup::Attach(EmoteJellyPhysics& actual_physics, std::uint32_t tress_mask) {
		m_Implementation->RequireReady();
		for (auto& shape : m_Implementation->Shapes) {
			actual_physics.AttachCollider(*shape, tress_mask);
		}
	}

	std::size_t EmoteCollisionGroup::ColliderCount() const noexcept {
		return m_Implementation->Shapes.size();
	}

	bool EmoteCollisionGroup::Contacted() const {
		m_Implementation->RequireReady();
		for (const auto& shape : m_Implementation->Shapes) {
			if (shape->Contacted()) {
				return true;
			}
		}
		return false;
	}

	std::uint64_t EmoteCollisionGroup::PreparedFrameSerial() const {
		m_Implementation->RequireReady();
		return m_Implementation->FrameSerial;
	}

	class EmoteCollisionOwner::Implementation {
	public:
		Implementation(const MotionPlayerCommands& primary, const resource::PsbDocument& document)
		    : Commands(primary) {
			if (&primary.LayerUids().UltimatePlayer() != &primary) {
				throw std::runtime_error("Emote collision owner requires its actual primary Commands");
			}
			const auto& metadata = CollisionField(document.Root, "metadata");
			Map = metadata.Find("collisionMap");
			if (Map != nullptr && !std::holds_alternative<std::monostate>(Map->Data) && !std::holds_alternative<resource::PsbObject>(Map->Data)) {
				throw std::runtime_error("Emote collisionMap requires its actual native dictionary or null");
			}
		}

		void RequireIdle() const {
			if (Closed || Operating || !Valid) {
				throw std::runtime_error("Emote collision owner is closed, active or invalid");
			}
		}

		const MotionPlayerCommands& Commands;
		const resource::PsbValue* Map = nullptr;
		std::map<std::string, std::shared_ptr<EmoteCollisionGroup>, std::less<>> Groups;
		std::uint64_t FrameSerial = 0;
		std::size_t KeyBytes = 0;
		bool Closed = false;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteCollisionOwner::EmoteCollisionOwner(const MotionPlayerCommands& actual_primary, const resource::PsbDocument& document)
	    : m_Implementation(std::make_unique<Implementation>(actual_primary, document)) {
	}
	EmoteCollisionOwner::~EmoteCollisionOwner() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	std::shared_ptr<EmoteCollisionGroup> EmoteCollisionOwner::Acquire(std::string_view uid) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		uid = uid.substr(0, uid.find('\0'));
		if (uid.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("Emote collision group UID exceeds bounded native strings");
		}
		if (const auto found = state.Groups.find(uid); found != state.Groups.end()) {
			return found->second;
		}
		const auto* definition = state.Map == nullptr ? nullptr : state.Map->Find(uid);
		if (definition == nullptr) {
			return {};
		}
		const auto* shapes = std::get_if<resource::PsbArray>(&definition->Data);
		if (shapes == nullptr) {
			shapes = std::get_if<resource::PsbArray>(&CollisionField(*definition, "colliders").Data);
		}
		if (shapes == nullptr || state.Groups.size() >= 4096 || uid.size() > 1024 * 1024 - state.KeyBytes) {
			throw std::runtime_error("Emote collisionMap requires a bounded actual collider array");
		}
		auto implementation = std::make_unique<EmoteCollisionGroup::Implementation>(state.Commands, std::string(uid), *shapes);
		// The group has a real shared lifetime: the driver cache and each actual physical instance retain it.
		auto group = std::make_shared<EmoteCollisionGroup>(std::move(implementation));
		state.Groups.emplace(uid, group);
		state.KeyBytes += uid.size();
		return group;
	}

	void EmoteCollisionOwner::Prepare(const EmoteVelvetyInputFrame& actual_frame) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		for (const auto value : {actual_frame.WorldToPhysics.XX, actual_frame.WorldToPhysics.XY, actual_frame.WorldToPhysics.TranslationX, actual_frame.WorldToPhysics.YX, actual_frame.WorldToPhysics.YY, actual_frame.WorldToPhysics.TranslationY, actual_frame.DepthScale}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote collider pose phase requires its actual finite global conversion");
			}
		}
		if (state.FrameSerial == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("Emote collider frame generation exhausted");
		}
		state.Operating = true;
		try {
			for (auto& [uid, group] : state.Groups) {
				static_cast<void>(uid);
				group->m_Implementation->Prepare(actual_frame, group.use_count() > 1);
				group->m_Implementation->FrameSerial = state.FrameSerial + 1;
			}
			++state.FrameSerial;
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}

	void EmoteCollisionOwner::Close() {
		auto& state = *m_Implementation;
		if (state.Closed) {
			return;
		}
		if (state.Operating) {
			throw std::runtime_error("Emote collider Close cannot interrupt an actual pose phase");
		}
		for (const auto& [uid, group] : state.Groups) {
			static_cast<void>(uid);
			if (group.use_count() != 1) {
				throw std::runtime_error("Emote collider Close requires retirement of actual physical instance leases");
			}
		}
		state.Groups.clear();
		state.Closed = true;
	}

	bool EmoteCollisionOwner::UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept {
		return &m_Implementation->Commands == &actual_primary;
	}

	std::uint64_t EmoteCollisionOwner::PreparedFrameSerial() const {
		m_Implementation->RequireIdle();
		return m_Implementation->FrameSerial;
	}

	std::size_t EmoteCollisionOwner::GroupCount() const noexcept {
		return m_Implementation->Groups.size();
	}
} // namespace sgre::scene
