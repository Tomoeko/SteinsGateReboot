#include "sgre/scene/emote_velvety_owner.hpp"

#include "sgre/scene/emote_bloom_control.hpp"
#include "sgre/scene/emote_catenary_control.hpp"
#include "sgre/scene/emote_collision_owner.hpp"
#include "sgre/scene/emote_emission_owner.hpp"
#include "sgre/scene/emote_jelly_control.hpp"
#include "sgre/scene/emote_parts_tree.hpp"
#include "sgre/scene/emote_stretch_control.hpp"
#include "sgre/scene/emote_string_control.hpp"
#include "sgre/scene/emote_synchronize_control.hpp"
#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::scene {
	static const resource::PsbValue& VelvetyField(const resource::PsbValue& owner, std::string_view name) {
		if (const auto* value = owner.Find(name)) {
			return *value;
		}
		throw std::runtime_error("containing Velvety owner requires actual metadata " + std::string(name));
	}

	static std::size_t VelvetyArrayCount(const resource::PsbValue* value) {
		if (value == nullptr || std::holds_alternative<std::monostate>(value->Data)) {
			return 0;
		}
		const auto* list = std::get_if<resource::PsbArray>(&value->Data);
		if (list == nullptr || list->size() > 65536) {
			throw std::runtime_error("containing Velvety declaration requires bounded actual array");
		}
		return list->size();
	}

	static float VelvetyNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1.0f : 0.0f;
		} else {
			throw std::runtime_error("containing Velvety height requires its actual numeric metadata");
		}
		if (!std::isfinite(result) || !(result > 0)) {
			throw std::runtime_error("containing Velvety height exceeds its finite positive physical domain");
		}
		return result;
	}

	template <typename Definition, typename ReadUid>
	static void RequireVelvetyDefinitions(const std::vector<Definition>& definitions, ReadUid&& uid) {
		if (definitions.size() > 65536) {
			throw std::runtime_error("containing Velvety family exceeds bounded actual owner count");
		}
		std::vector<std::string_view> names;
		names.reserve(definitions.size());
		for (const auto& definition : definitions) {
			names.push_back(uid(definition));
		}
		std::sort(names.begin(), names.end());
		if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
			throw std::runtime_error("containing Velvety duplicate family UID replacement semantics remain unrecovered");
		}
	}

	template <typename Control>
	static void OrderVelvetyControls(std::vector<std::unique_ptr<Control>>& controls) {
		std::sort(controls.begin(), controls.end(), [](const auto& left, const auto& right) {
			return left->Uid() < right->Uid();
		});
	}

	template <typename Controls, typename Action>
	static void VisitMatchingVelvetyControls(const Controls& destination, const Controls& source, Action&& action) {
		using Control = typename Controls::value_type::element_type;
		std::map<std::string_view, const Control*, std::less<>> originals;
		std::map<std::string_view, Control*, std::less<>> receiving;
		for (const auto& control : source) {
			originals.emplace(control->Uid(), control.get());
		}
		for (const auto& control : destination) {
			receiving.emplace(control->Uid(), control.get());
		}
		for (const auto& [uid, target] : receiving) {
			const auto found = originals.find(uid);
			if (found != originals.end()) {
				action(*target, *found->second);
			}
		}
	}

	class EmoteVelvetyOwner::Implementation {
	public:
		Implementation(const MotionRootScene& root, const MotionPlayerCommands& primary, const MotionScenePlayer& plain, const resource::PsbDocument& document, EmoteVariableControls& variables, const std::function<std::uint32_t()>& entropy)
		    : Commands(primary), Plain(plain) {
			const auto definition = ReadEmoteVelvetyFrameDefinition(document);
			if (!definition) {
				// Native2e6000 leaves Enabled clear and2f00c0 immediately returns.
				return;
			}
			if (!entropy || !variables.UsesCommands(primary) || &primary.LayerUids().UltimatePlayer() != &primary || !plain.CanMutate() || !plain.ResourcesLive()) {
				throw std::runtime_error("containing Velvety owner requires its actual live primary/variables/entropy services");
			}
			const auto& metadata = VelvetyField(document.Root, "metadata");
			const auto& common = VelvetyField(metadata, "velvetyCommonParams");
			if (const auto* depth = common.Find("velvetyPartsMaxDepth"); depth != nullptr && VelvetyNumber(*depth) != 1) {
				throw std::runtime_error("containing Velvety nondefault PartsMaxDepth requires actual recursive driver policy");
			}
			const auto& profile = VelvetyField(metadata, "charaProfile");
			Height = VelvetyNumber(VelvetyField(profile, "height"));
			if (VelvetyArrayCount(metadata.Find("hitTestInfo")) != 0) {
				throw std::runtime_error("containing Velvety hitTestInfo requires actual hit-test UID producer");
			}
			if (VelvetyArrayCount(metadata.Find("markerList")) != 0) {
				throw std::runtime_error("containing Velvety markerList requires actual marker UID producer");
			}
			const auto* emission_map = metadata.Find("emissionMap");
			if (VelvetyArrayCount(emission_map == nullptr ? nullptr : emission_map->Find("auto")) != 0) {
				throw std::runtime_error("containing Velvety automatic emission requires actual visual particle publication");
			}

			// Parse admission before installing any UID callbacks; the native creation order below is explicit.
			auto blooms = ReadEmoteBloomControlDefinitions(document);
			auto strings = ReadEmoteStringControlDefinitions(document);
			auto catenaries = ReadEmoteCatenaryControlDefinitions(document);
			auto jellies = ReadEmoteJellyControlDefinitions(document);
			auto stretches = ReadEmoteStretchDefinitions(document);
			auto synchronizations = ReadEmoteSynchronizeDefinitions(document);
			RequireVelvetyDefinitions(blooms, [](const auto& value) -> std::string_view {
				return value.Uid;
			});
			RequireVelvetyDefinitions(strings, [](const auto& value) -> std::string_view {
				return value.Physics.Uid;
			});
			RequireVelvetyDefinitions(catenaries, [](const auto& value) -> std::string_view {
				return value.Uid;
			});
			RequireVelvetyDefinitions(jellies, [](const auto& value) -> std::string_view {
				return value.Physics.Uid;
			});
			RequireVelvetyDefinitions(stretches, [](const auto& value) -> std::string_view {
				return value.Uid;
			});
			RequireVelvetyDefinitions(synchronizations, [](const auto& value) -> std::string_view {
				return value.Uid;
			});
			Frame = std::make_unique<EmoteVelvetyFrameOwner>(root, primary, plain, *definition, Height);
			Collisions = std::make_unique<EmoteCollisionOwner>(primary, document);
			// Native2e9280 consumes its separate entropy even when the real emissionMap is empty.
			Emissions = std::make_unique<EmoteEmissionOwner>(primary, document, &variables, entropy());
			if (const auto* tree = metadata.Find("velvetyPartsTree")) {
				Parts = std::make_unique<EmotePartsTree>(primary, *tree);
			}
			for (auto& value : blooms) {
				Blooms.push_back(std::make_unique<EmoteBloomControl>(primary, std::move(value), &variables));
				if (Parts && Parts->AttachBloom(*Blooms.back())) {
					++Attached;
				}
			}
			for (auto& value : strings) {
				Strings.push_back(std::make_unique<EmoteStringControl>(primary, std::move(value), &variables, Collisions.get()));
				if (Parts && Parts->AttachString(*Strings.back())) {
					++Attached;
				}
			}
			for (auto& value : catenaries) {
				Catenaries.push_back(std::make_unique<EmoteCatenaryControl>(primary, std::move(value), &variables));
				if (Parts && Parts->AttachCatenary(*Catenaries.back())) {
					++Attached;
				}
			}
			for (auto& value : jellies) {
				Jellies.push_back(std::make_unique<EmoteJellyControl>(primary, std::move(value), &variables, Collisions.get()));
				if (Parts && Parts->AttachJelly(*Jellies.back())) {
					++Attached;
				}
			}
			for (auto& value : stretches) {
				Stretches.push_back(std::make_unique<EmoteStretchControl>(primary, std::move(value)));
				if (Parts && Parts->AttachStretch(*Stretches.back())) {
					++Attached;
				}
			}
			for (auto& value : synchronizations) {
				Synchronize.push_back(std::make_unique<EmoteSynchronizeControl>(primary, std::move(value)));
				if (Parts && Parts->AttachSynchronize(*Synchronize.back())) {
					++Attached;
				}
			}
			if (!Parts) {
				OrderVelvetyControls(Blooms);
				OrderVelvetyControls(Strings);
				OrderVelvetyControls(Catenaries);
				OrderVelvetyControls(Jellies);
				OrderVelvetyControls(Stretches);
				OrderVelvetyControls(Synchronize);
			}
			Enabled = true;
		}

		void RequireIdle() const {
			if (Thread != std::this_thread::get_id() || Closed || Operating || !Valid || !Plain.CanMutate() || !Plain.ResourcesLive()) {
				throw std::runtime_error("containing Velvety phase requires its live idle unqueued actual plain owner");
			}
		}

		template <typename Controls>
		void RequireControls(const Controls& controls) const {
			for (const auto& control : controls) {
				control->RequireReady();
			}
		}

		template <typename Controls>
		void Dispatch(Controls& controls, float ticks, const EmoteVelvetyInputFrame& frame) {
			for (auto& control : controls) {
				control->Update(ticks, frame);
			}
		}

		template <typename Controls>
		void Accumulate(const Controls& controls) {
			for (const auto& control : controls) {
				Updated += control->LastUpdatedCount();
				Writes += control->LastMeshWriteCount();
			}
		}

		void Update(float ticks, const EmoteVelvetyGlobalControls& global) {
			RequireIdle();
			if (!std::isfinite(ticks) || !(ticks > 0) || ticks > 4096 * 1.5f || Serial == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("containing Velvety dispatch requires bounded positive actual elapsed ticks");
			}
			if (!Enabled) {
				return;
			}
			RequireControls(Blooms);
			RequireControls(Catenaries);
			RequireControls(Stretches);
			RequireControls(Synchronize);
			// Acquisition is outside noexcept UID Added and precedes actual collider and pool poses.
			for (auto& control : Strings) {
				control->FinishLayerChanges();
			}
			for (auto& control : Jellies) {
				control->FinishLayerChanges();
			}
			Operating = true;
			try {
				const auto frame = Frame->Prepare(ticks, global);
				Collisions->Prepare(frame);
				Emissions->Prepare(frame, ticks);
				if (Parts) {
					Parts->UpdateControls(ticks, frame);
				} else {
					Dispatch(Blooms, ticks, frame);
					Dispatch(Strings, ticks, frame);
					Dispatch(Catenaries, ticks, frame);
					Dispatch(Jellies, ticks, frame);
					Dispatch(Stretches, ticks, frame);
					Dispatch(Synchronize, ticks, frame);
				}
				Updated = 0;
				Writes = 0;
				Accumulate(Blooms);
				Accumulate(Strings);
				Accumulate(Catenaries);
				Accumulate(Jellies);
				Accumulate(Stretches);
				Accumulate(Synchronize);
				++Serial;
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}

		const MotionPlayerCommands& Commands;
		const MotionScenePlayer& Plain;
		std::unique_ptr<EmoteVelvetyFrameOwner> Frame;
		std::unique_ptr<EmoteCollisionOwner> Collisions;
		std::unique_ptr<EmoteEmissionOwner> Emissions;
		std::vector<std::unique_ptr<EmoteBloomControl>> Blooms;
		std::vector<std::unique_ptr<EmoteStringControl>> Strings;
		std::vector<std::unique_ptr<EmoteCatenaryControl>> Catenaries;
		std::vector<std::unique_ptr<EmoteJellyControl>> Jellies;
		std::vector<std::unique_ptr<EmoteStretchControl>> Stretches;
		std::vector<std::unique_ptr<EmoteSynchronizeControl>> Synchronize;
		// Parts contains pointers to the controls, so it retires before those owners and their group leases.
		std::unique_ptr<EmotePartsTree> Parts;
		std::thread::id Thread = std::this_thread::get_id();
		float Height = 0;
		std::size_t Attached = 0;
		std::size_t Updated = 0;
		std::size_t Writes = 0;
		std::uint64_t Serial = 0;
		bool Enabled = false;
		bool Closed = false;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteVelvetyOwner::EmoteVelvetyOwner(const MotionRootScene& root, const MotionPlayerCommands& primary, const MotionScenePlayer& plain, const resource::PsbDocument& document, EmoteVariableControls& variables, const std::function<std::uint32_t()>& entropy)
	    : m_Implementation(std::make_unique<Implementation>(root, primary, plain, document, variables, entropy)) {
	}
	EmoteVelvetyOwner::~EmoteVelvetyOwner() noexcept = default;

	void EmoteVelvetyOwner::Update(float elapsed_ticks, const EmoteVelvetyGlobalControls& actual_controls) {
		m_Implementation->Update(elapsed_ticks, actual_controls);
	}
	void EmoteVelvetyOwner::RequireCanSkip() const {
		const auto& state = *m_Implementation;
		state.RequireIdle();
		if (!state.Enabled) {
			return;
		}
		state.Frame->RequireCanSkip();
		state.Collisions->RequireCanSkip();
		for (const auto& control : state.Blooms) {
			control->RequireCanSkip();
		}
		for (const auto& control : state.Strings) {
			control->RequireCanSkip();
		}
		for (const auto& control : state.Catenaries) {
			control->RequireCanSkip();
		}
		for (const auto& control : state.Jellies) {
			control->RequireCanSkip();
		}
		for (const auto& control : state.Synchronize) {
			control->RequireCanSkip();
		}
	}

	void EmoteVelvetyOwner::Skip() {
		RequireCanSkip();
		auto& state = *m_Implementation;
		if (!state.Enabled) {
			return;
		}
		state.Operating = true;
		try {
			state.Frame->Skip();
			state.Collisions->Skip();
			for (auto& control : state.Blooms) {
				control->Skip();
			}
			for (auto& control : state.Strings) {
				control->Skip();
			}
			for (auto& control : state.Catenaries) {
				control->Skip();
			}
			for (auto& control : state.Jellies) {
				control->Skip();
			}
			for (auto& control : state.Synchronize) {
				control->Skip();
			}
			// Common+298 Stretch, PartsTree caches and emitted particles are not reset by2f50e0.
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}

	void EmoteVelvetyOwner::ClearPlain(MotionScenePlayer& actual_plain) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (&actual_plain != &state.Plain) {
			throw std::runtime_error("containing Velvety clear requires its exact actual plain owner");
		}
		state.Operating = true;
		try {
			if (state.Frame) {
				state.Frame->ClearPlain(actual_plain);
			} else {
				actual_plain.Clear();
			}
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}

	bool EmoteVelvetyOwner::Enabled() const noexcept {
		return m_Implementation->Enabled;
	}
	EmoteVelvetyOwnerCounts EmoteVelvetyOwner::Counts() const {
		const auto& state = *m_Implementation;
		state.RequireIdle();
		EmoteVelvetyOwnerCounts counts;
		counts.Bloom = state.Blooms.size();
		counts.String = state.Strings.size();
		counts.Catenary = state.Catenaries.size();
		counts.Jelly = state.Jellies.size();
		counts.Stretch = state.Stretches.size();
		counts.Synchronize = state.Synchronize.size();
		counts.Parts = state.Parts ? state.Parts->PartCount() : 0;
		counts.AttachedPartsControls = state.Attached;
		counts.Updated = state.Updated;
		counts.MeshWrites = state.Writes;
		return counts;
	}
	std::uint64_t EmoteVelvetyOwner::FrameSerial() const {
		m_Implementation->RequireIdle();
		return m_Implementation->Serial;
	}
	resource::MotionMeshMatrix EmoteVelvetyOwner::PhysicsToWorld() const {
		const auto& state = *m_Implementation;
		state.RequireIdle();
		if (!state.Frame) {
			throw std::runtime_error("containing Velvety conversion requires an actual enabled Common frame owner");
		}

		return state.Frame->PhysicsToWorld();
	}
	float EmoteVelvetyOwner::CharacterHeight() const {
		m_Implementation->RequireIdle();
		return m_Implementation->Height;
	}
	void EmoteVelvetyOwner::Close() {
		auto& state = *m_Implementation;
		if (state.Closed) {
			return;
		}
		if (state.Thread != std::this_thread::get_id() || state.Operating || !state.Plain.CanMutate()) {
			throw std::runtime_error("containing Velvety retirement requires actual completed unqueued phase");
		}
		state.Parts.reset();
		state.Synchronize.clear();
		state.Stretches.clear();
		state.Jellies.clear();
		state.Catenaries.clear();
		state.Strings.clear();
		state.Blooms.clear();
		state.Emissions.reset();
		state.Collisions.reset();
		state.Frame.reset();
		state.Closed = true;
	}
	void EmoteVelvetyOwner::RequireCanAssignState(const EmoteVelvetyOwner& source) const {
		const auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.RequireIdle();
		from.RequireIdle();
		if (!target.Enabled) {
			return;
		}
		if (!from.Enabled) {
			throw std::runtime_error("Emote enabled Common assignment requires initialized source Common state");
		}
		target.Frame->RequireCanAssignHistory(*from.Frame);
		target.Emissions->RequireCanAssignState(*from.Emissions);
		const auto check = [](const auto& receiving, const auto& original) {
			receiving.RequireCanAssignState(original);
		};
		VisitMatchingVelvetyControls(target.Blooms, from.Blooms, check);
		VisitMatchingVelvetyControls(target.Strings, from.Strings, check);
		VisitMatchingVelvetyControls(target.Catenaries, from.Catenaries, check);
		VisitMatchingVelvetyControls(target.Jellies, from.Jellies, check);
		VisitMatchingVelvetyControls(target.Stretches, from.Stretches, check);
		VisitMatchingVelvetyControls(target.Synchronize, from.Synchronize, check);
	}

	void EmoteVelvetyOwner::AssignState(const EmoteVelvetyOwner& source, const EmoteVelvetyGlobalControls& actual_controls) {
		auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.RequireIdle();
		from.RequireIdle();
		if (this == &source || !target.Enabled) {
			return;
		}
		// The containing zero-time calculation can install destination occurrences and their own collider leases.
		for (auto& control : target.Strings) {
			control->FinishLayerChanges();
		}
		for (auto& control : target.Jellies) {
			control->FinishLayerChanges();
		}
		RequireCanAssignState(source);
		target.Operating = true;
		try {
			static_cast<void>(target.Frame->Prepare(0, actual_controls));
			target.Frame->AssignHistory(*from.Frame);
			target.Emissions->AssignState(*from.Emissions);
			const auto assign = [](auto& receiving, const auto& original) {
				receiving.AssignState(original);
			};
			VisitMatchingVelvetyControls(target.Blooms, from.Blooms, assign);
			VisitMatchingVelvetyControls(target.Strings, from.Strings, assign);
			VisitMatchingVelvetyControls(target.Catenaries, from.Catenaries, assign);
			VisitMatchingVelvetyControls(target.Jellies, from.Jellies, assign);
			VisitMatchingVelvetyControls(target.Stretches, from.Stretches, assign);
			VisitMatchingVelvetyControls(target.Synchronize, from.Synchronize, assign);
			target.Operating = false;
		} catch (...) {
			target.Operating = false;
			target.Valid = false;
			throw;
		}
	}

} // namespace sgre::scene
