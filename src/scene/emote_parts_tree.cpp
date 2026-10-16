#include "sgre/scene/emote_parts_tree.hpp"

#include "sgre/scene/emote_bloom_control.hpp"
#include "sgre/scene/emote_catenary_control.hpp"
#include "sgre/scene/emote_jelly_control.hpp"
#include "sgre/scene/emote_stretch_control.hpp"
#include "sgre/scene/emote_string_control.hpp"
#include "sgre/scene/emote_synchronize_control.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::scene {

	static constexpr std::size_t kMaximumEmoteParts = 65536;
	static constexpr std::size_t kMaximumEmotePartDepth = 128;
	static constexpr std::size_t kMaximumEmotePartNames = 8 * 1024 * 1024;

	using EmotePartControl = std::variant<EmoteStringControl*, EmoteJellyControl*, EmoteBloomControl*, EmoteCatenaryControl*, EmoteSynchronizeControl*, EmoteStretchControl*>;

	struct EmotePartNode {
		std::string Uid;
		std::string ParentUid;
		std::string Label;
		// Native2dbda0 zeroes both words. A part without an active callback never manufactures scale1.
		EmoteVelvetyPartPosture Retained{0, 0};
		std::vector<EmotePartControl> Controls;
	};

	class EmotePartsTree::Implementation {
	public:
		Implementation(const MotionPlayerCommands& primary, const resource::PsbValue& tree)
		    : Commands(primary) {
			if (&primary.LayerUids().UltimatePlayer() != &primary) {
				throw std::runtime_error("Emote PartsTree requires its actual primary plain owner");
			}
			Append(tree, {}, 0);
		}

		void RequireIdle() const {
			if (OwnerThread != std::this_thread::get_id() || Closed || Operating) {
				throw std::runtime_error("Emote PartsTree requires its live idle owner on the actual control thread");
			}
		}

		std::string ReadName(const resource::PsbValue& entry, std::string_view name) {
			const auto* value = entry.Find(name);
			if (value == nullptr) {
				return {};
			}
			const auto* text = std::get_if<std::string>(&value->Data);
			if (text == nullptr) {
				throw std::runtime_error("Emote PartsTree requires actual string " + std::string(name));
			}
			const auto native_name = std::string_view(*text).substr(0, text->find('\0'));
			if (native_name.size() >= kMaximumEmotePartNames - NameBytes) {
				throw std::runtime_error("Emote PartsTree exceeds bounded owned label storage");
			}
			NameBytes += native_name.size() + 1;
			return std::string(native_name);
		}

		void Append(const resource::PsbValue& list, std::string_view parent, std::size_t depth) {
			const auto* array = std::get_if<resource::PsbArray>(&list.Data);
			if (array == nullptr || depth > kMaximumEmotePartDepth || array->size() > kMaximumEmoteParts - Nodes.size()) {
				throw std::runtime_error("Emote PartsTree exceeds bounded actual array/depth/node storage");
			}
			// Parent is owned locally because appending descendants can move the flattened node vector.
			const std::string parent_name(parent);
			for (const auto& entry : *array) {
				if (std::get_if<resource::PsbObject>(&entry.Data) == nullptr || Nodes.size() == kMaximumEmoteParts || parent_name.size() >= kMaximumEmotePartNames - NameBytes) {
					throw std::runtime_error("Emote PartsTree requires bounded actual object entries");
				}
				EmotePartNode node;
				node.Uid = ReadName(entry, "uid");
				node.Label = ReadName(entry, "label");
				if (parent_name.size() >= kMaximumEmotePartNames - NameBytes) {
					throw std::runtime_error("Emote PartsTree exceeds bounded parent label storage");
				}
				NameBytes += parent_name.size() + 1;
				node.ParentUid = parent_name;
				const auto index = Nodes.size();
				Nodes.push_back(std::move(node));
				const auto uid = Nodes[index].Uid;
				if (const auto* children = entry.Find("children")) {
					Append(*children, uid, depth + 1);
				}
				// Original map insertion follows recursion and leaves an existing equal key unchanged.
				ByUid.try_emplace(uid, index);
			}
		}

		const MotionPlayerCommands& Commands;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::vector<EmotePartNode> Nodes;
		std::map<std::string, std::size_t, std::less<>> ByUid;
		std::size_t NameBytes = 0;
		std::size_t AttachedStrings = 0;
		std::size_t AttachedJellies = 0;
		std::size_t AttachedBlooms = 0;
		std::size_t AttachedCatenaries = 0;
		std::size_t AttachedSynchronize = 0;
		std::size_t AttachedStretch = 0;
		bool Sealed = false;
		bool Operating = false;
		bool Closed = false;
		bool Valid = true;
	};

	EmotePartsTree::EmotePartsTree(const MotionPlayerCommands& actual_primary, const resource::PsbValue& actual_tree)
	    : m_Implementation(std::make_unique<Implementation>(actual_primary, actual_tree)) {
	}

	EmotePartsTree::~EmotePartsTree() noexcept = default;

	template <class State, class Control>
	static bool AttachPartControl(State& state, Control& actual_control, std::size_t& count) {
		state.RequireIdle();
		if (state.Sealed || !state.Valid || !actual_control.UsesCommands(state.Commands)) {
			throw std::runtime_error("Emote PartsTree attachment requires the exact live primary control before dispatch");
		}
		actual_control.RequireReady();
		const auto found = state.ByUid.find(actual_control.Uid());
		if (found == state.ByUid.end()) {
			return false;
		}
		const EmotePartControl callback = &actual_control;
		for (const auto& node : state.Nodes) {
			if (std::find(node.Controls.begin(), node.Controls.end(), callback) != node.Controls.end()) {
				throw std::runtime_error("Emote PartsTree cannot duplicate one actual control callback");
			}
		}
		if (state.AttachedStrings + state.AttachedJellies + state.AttachedBlooms + state.AttachedCatenaries + state.AttachedSynchronize + state.AttachedStretch == kMaximumEmoteParts) {
			throw std::runtime_error("Emote PartsTree exceeds bounded concrete callback ownership");
		}
		state.Nodes[found->second].Controls.push_back(callback);
		++count;
		return true;
	}

	bool EmotePartsTree::AttachString(EmoteStringControl& actual_control) {
		auto& state = *m_Implementation;
		return AttachPartControl(state, actual_control, state.AttachedStrings);
	}

	bool EmotePartsTree::AttachJelly(EmoteJellyControl& actual_control) {
		auto& state = *m_Implementation;
		return AttachPartControl(state, actual_control, state.AttachedJellies);
	}

	bool EmotePartsTree::AttachBloom(EmoteBloomControl& actual_control) {
		auto& state = *m_Implementation;
		return AttachPartControl(state, actual_control, state.AttachedBlooms);
	}

	bool EmotePartsTree::AttachCatenary(EmoteCatenaryControl& actual_control) {
		auto& state = *m_Implementation;
		return AttachPartControl(state, actual_control, state.AttachedCatenaries);
	}
	bool EmotePartsTree::AttachSynchronize(EmoteSynchronizeControl& actual_control) {
		auto& state = *m_Implementation;
		return AttachPartControl(state, actual_control, state.AttachedSynchronize);
	}

	bool EmotePartsTree::AttachStretch(EmoteStretchControl& actual_control) {
		auto& state = *m_Implementation;
		return AttachPartControl(state, actual_control, state.AttachedStretch);
	}

	void EmotePartsTree::UpdateStrings(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame) {
		m_Implementation->RequireIdle();
		if (m_Implementation->AttachedJellies != 0 || m_Implementation->AttachedBlooms != 0 || m_Implementation->AttachedCatenaries != 0 || m_Implementation->AttachedSynchronize != 0 || m_Implementation->AttachedStretch != 0) {
			throw std::runtime_error("Emote PartsTree String-only dispatch contains actual non-String callbacks");
		}
		UpdateControls(elapsed_ticks, actual_frame);
	}

	void EmotePartsTree::UpdateControls(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (!state.Valid || !std::isfinite(elapsed_ticks) || !(elapsed_ticks > 0)) {
			throw std::runtime_error("Emote PartsTree physical dispatch requires the recovered positive finite physical step");
		}
		for (const auto& node : state.Nodes) {
			for (const auto& callback : node.Controls) {
				std::visit([](const auto* control) {
					control->RequireReady();
				},
				    callback);
			}
		}
		state.Sealed = true;
		state.Operating = true;
		try {
			for (auto& node : state.Nodes) {
				for (const auto& callback : node.Controls) {
					const EmoteVelvetyPartPosture* parent = nullptr;
					if (!node.ParentUid.empty()) {
						const auto found = state.ByUid.find(node.ParentUid);
						if (found == state.ByUid.end()) {
							throw std::runtime_error("Emote PartsTree lost its genuine containing parent UID");
						}
						parent = &state.Nodes[found->second].Retained;
					}
					std::visit([&](auto* control) {
						control->Update(elapsed_ticks, actual_frame, parent);
						if (control->LastUpdatedCount() != 0) {
							node.Retained = control->PartPosture();
						}
					},
					    callback);
				}
			}
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}

	std::optional<EmoteVelvetyPartPosture> EmotePartsTree::Posture(std::string_view uid) const {
		const auto& state = *m_Implementation;
		state.RequireIdle();
		const auto found = state.ByUid.find(uid);
		if (found == state.ByUid.end()) {
			return std::nullopt;
		}
		return state.Nodes[found->second].Retained;
	}

	std::size_t EmotePartsTree::PartCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->Nodes.size();
	}

	std::size_t EmotePartsTree::StringCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->AttachedStrings;
	}

	std::size_t EmotePartsTree::JellyCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->AttachedJellies;
	}

	std::size_t EmotePartsTree::BloomCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->AttachedBlooms;
	}

	std::size_t EmotePartsTree::CatenaryCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->AttachedCatenaries;
	}
	std::size_t EmotePartsTree::SynchronizeCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->AttachedSynchronize;
	}

	std::size_t EmotePartsTree::StretchCount() const {
		m_Implementation->RequireIdle();
		return m_Implementation->AttachedStretch;
	}

	void EmotePartsTree::Close() {
		auto& state = *m_Implementation;
		if (state.Closed) {
			return;
		}
		state.RequireIdle();
		for (auto& node : state.Nodes) {
			node.Controls.clear();
		}
		state.AttachedStrings = 0;
		state.AttachedJellies = 0;
		state.AttachedBlooms = 0;
		state.AttachedCatenaries = 0;
		state.AttachedSynchronize = 0;
		state.AttachedStretch = 0;
		state.Closed = true;
	}

} // namespace sgre::scene
