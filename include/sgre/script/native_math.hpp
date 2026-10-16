#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <cstdint>
#include <memory>

namespace sgre::script {
	class ScriptMathBindings {
	public:
		// One owner models the original scheduler thread's shared CRT random state.
		explicit ScriptMathBindings(SquirrelVm& vm);
		~ScriptMathBindings();
		ScriptMathBindings(const ScriptMathBindings&) = delete;
		ScriptMathBindings& operator=(const ScriptMathBindings&) = delete;
		ScriptMathBindings(ScriptMathBindings&&) = delete;
		ScriptMathBindings& operator=(ScriptMathBindings&&) = delete;

		void Seed(std::uint32_t seed);
		[[nodiscard]] std::int32_t Random();

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
