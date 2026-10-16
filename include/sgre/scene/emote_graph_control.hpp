#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace sgre::scene {

	struct EmoteGraphEdge {
		std::int32_t Lower = 0;
		std::int32_t Upper = 0;
	};

	struct EmoteGraphDefinition {
		std::vector<EmoteGraphEdge> Edges;
		std::vector<std::vector<std::int32_t>> Nodes;
	};

	// Native 2f6b20/2f7e40/2f8610: integer frame equivalence graph, not a linear curve.
	// Constructor owns the graph and all route scratch; Prepare/Evaluate allocate no storage.
	class EmoteGraphControl {
	public:
		explicit EmoteGraphControl(EmoteGraphDefinition definition);
		~EmoteGraphControl() noexcept;
		EmoteGraphControl(EmoteGraphControl&&) noexcept;
		EmoteGraphControl& operator=(EmoteGraphControl&&) noexcept;
		EmoteGraphControl(const EmoteGraphControl&) = delete;
		EmoteGraphControl& operator=(const EmoteGraphControl&) = delete;
		void Prepare(float start, float target);
		// Native graph import rebuilds the route against destination metadata.
		void AssignPath(const EmoteGraphControl& source);
		[[nodiscard]] float Evaluate(float ratio) const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
