#include "sgre/scene/motion_selected_sprite_player.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace sgre::scene {

	MotionSpriteDrawContext::MotionSpriteDrawContext(std::size_t capacity) : m_Queue(capacity) {
		if (capacity == 0 || capacity > 1024) {
			throw std::runtime_error("motion sprite context requires a bounded 1..1024-row queue");
		}

		m_Entries.reserve(capacity);
		m_OrderedCommands.resize(capacity);
		m_OrderedClips.resize(capacity);
	}

	void MotionSpriteDrawContext::Append(MotionSelectedSpritePlayer& owner, std::span<MotionSpriteDrawRow* const> rows) {
		if (m_Submitting || rows.size() > m_Queue.Capacity() - m_Entries.size()) {
			throw std::runtime_error("motion sprite draw cannot mutate submission or exceed context capacity");
		}
		if (!owner.ResourcesLive()) {
			throw std::runtime_error("motion sprite draw requires its actual live resource registration");
		}

		for (const auto* row : rows) {
			const auto actual = owner.PublishedRows();
			if (std::find(actual.begin(), actual.end(), row) == actual.end()) {
				throw std::runtime_error("motion sprite context requires this owner's actual published row identity");
			}

			if (row == nullptr || row->Command.Clip != &row->Clip || row->Clip.Geometry == nullptr || row->Clip.ClipGroup != 0 || row->Command.IndividualClip != nullptr || row->EmissiveColor != 0 || row->AmbientFactor != 1 || row->Command.Texture < 0) {
				throw std::runtime_error("motion sprite context requires actual ordinary row geometry without inherited masks, individual clips or auxiliary lighting");
			}
		}

		for (auto* row : rows) {
			m_Queue.Append(row->Order);
			Entry entry;
			entry.Owner = &owner;
			entry.Row = row;
			m_Entries.push_back(entry);
		}
	}

	bool MotionSpriteDrawContext::IsBorrowing(const MotionSelectedSpritePlayer& owner) const noexcept {
		return std::any_of(m_Entries.begin(), m_Entries.end(), [&](const Entry& entry) {
			return entry.Owner == &owner;
		});
	}

	std::size_t MotionSpriteDrawContext::Size() const noexcept {
		return m_Queue.Size();
	}

	bool MotionSpriteDrawContext::CanClear() const noexcept {
		return !m_Submitting;
	}

	void MotionSpriteDrawContext::Clear() {
		if (m_Submitting) {
			throw std::runtime_error("motion sprite context cannot clear during submission");
		}

		m_Queue.Clear();
		m_Entries.clear();
	}

	void MotionSpriteDrawContext::Flush(const MotionPreparedFrame& globals, const MotionFrameScratch& scratch, const std::function<void(const MotionFrameEvent&)>& output) {
		if (m_Submitting || !output || !globals.Queue.empty() || !globals.ClipQueue.empty() || !globals.Groups.empty() || !globals.Nodes.empty() || !globals.MaskSettings.empty() || !globals.Candidates.empty() || globals.MaskRegionClipping) {
			throw std::runtime_error("motion sprite context requires an idle actual frame driver/sink without a separate queue or unrecovered mask owner");
		}

		for (const auto& entry : m_Entries) {
			if (!entry.Owner->ResourcesLive()) {
				throw std::runtime_error("motion sprite context resource backing expired before submission");
			}
		}

		m_Submitting = true;
		try {
			m_Queue.Flush([&](std::span<const MotionRenderOrderReference> ordered) {
				for (std::size_t index = 0; index < ordered.size(); ++index) {
					const auto found = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const Entry& entry) {
						return std::addressof(entry.Row->Order) == std::addressof(ordered[index].get());
					});
					if (found == m_Entries.end()) {
						throw std::runtime_error("motion sorted context lost its actual row identity");
					}

					m_OrderedCommands[index] = found->Row->Command;
					m_OrderedClips[index] = found->Row->Clip;
				}

				auto frame = globals;
				frame.Queue = std::span<const MotionPreparedFrameCommand>(m_OrderedCommands).first(ordered.size());
				frame.ClipQueue = std::span<const MotionClipCommand>(m_OrderedClips).first(ordered.size());
				SubmitPreparedMotionFrame(frame, scratch, output);
			});
		} catch (...) {
			m_Submitting = false;
			throw;
		}

		m_Submitting = false;
		m_Entries.clear();
	}

} // namespace sgre::scene
