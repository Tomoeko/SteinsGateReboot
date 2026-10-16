#include "sgre/script/template_strings.hpp"

#include "sgre/script/source_expression.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int main() {
	try {
		sgre::script::SquirrelVm vm;
		vm.InstallRecoveredBaseLibrary();
		const auto method = vm.GetSlot(vm.RootTable(), "template");
		const std::array<std::pair<std::string, std::string>, 10> vectors{{{"", ""}, {"plain text", "plain text"}, {"$$", "$"}, {"a$$$$b", "a$$b"}, {"$9 $! $", "$9 $! $"}, {"a${}b", "ab"}, {"ending${", "ending"}, {"日本語 $$ λ", "日本語 $ λ"}, {std::string("before\0after", 12), "before"}, {"\\$9\n${}", "\\$9\n"}}};
		for (const auto& [source, expected] : vectors) {
			const std::array<sgre::script::SquirrelValue, 3> arguments{vm.RootTable(), sgre::script::SquirrelString(source), vm.CreateTable()};
			const auto result = vm.Invoke(method, arguments);
			Require(sgre::script::StringText(result.Value) == expected, "native-derived literal template scanner changed its byte result");
		}
		for (const auto* source : {"${a + 1}", "${dialog.field}", "${1}"}) {
			bool rejected = false;
			try {
				const std::array<sgre::script::SquirrelValue, 2> arguments{vm.RootTable(), sgre::script::SquirrelString(source)};
				static_cast<void>(vm.Invoke(method, arguments));
			} catch (const std::runtime_error& error) {
				rejected = std::string_view(error.what()).find("first-party source compiler") != std::string_view::npos;
			}
			Require(rejected, "unimplemented expression compiler became a successful template fallback");
		}
		const auto environment = vm.CreateTable();
		vm.NewSlot(environment, "dialog", sgre::script::SquirrelString("actual environment text"));
		vm.NewSlot(environment, "count_2", std::int32_t{17});
		vm.NewSlot(vm.RootTable(), "root_only", sgre::script::SquirrelString("fallback root"));
		const auto expand = [&](std::string source, const sgre::script::SquirrelValue& receiver, const sgre::script::SquirrelValue& env, const sgre::script::SquirrelValue& fallback = {}) {
			const std::array<sgre::script::SquirrelValue, 4> arguments{receiver, sgre::script::SquirrelString(std::move(source)), env, fallback};

			return sgre::script::StringText(vm.Invoke(method, arguments).Value);
		};
		Require(expand("%CLoading\n${dialog}", vm.RootTable(), environment) == "%CLoading\nactual environment text", "real getSystemText template must evaluate its actual bound environment");
		Require(expand("$count_2 / ${ dialog } / $root_only", vm.RootTable(), environment) == "17 / actual environment text / fallback root", "generic name compiler must preserve numeric conversion and implicit current-root fallback");
		Require(expand("${dialog", vm.RootTable(), environment) == "actual environment text", "native braced scanner accepts a final unterminated nonempty expression");
		Require(expand("$dialog", environment, {}) == "actual environment text", "unbound expression this must use the actual original callback receiver");
		Require(expand("<$missing>", vm.RootTable(), environment) == "<>", "actual missing-slot VM failure must select native empty fallback");
		Require(expand("<$missing>", vm.RootTable(), environment, std::int32_t{9}) == "<9>", "fallback must retain its actual value and native string conversion");
		vm.NewSlot(environment, "nul", sgre::script::SquirrelString(std::string("a\0b", 3)));
		Require(expand("${nul}", vm.RootTable(), environment) == std::string("a\0\0", 3), "native explicit converted length must preserve strncpy zero padding");
		Require(expand("${" + std::string(201, 'x') + "}", vm.RootTable(), environment, sgre::script::SquirrelString("long")) == "long", "overlong native expression must choose fallback before compilation");
		const auto name_program = sgre::script::TryCompileSquirrelNameExpression(vm, "dialog");
		Require(name_program && name_program->MainFunction.StackSize == 2 && name_program->MainFunction.HasVariableParameters && name_program->MainFunction.Instructions.front().Opcode == 9, "native name compiler must preserve GetK, stack extent and main varparams metadata");
		const auto this_program = sgre::script::TryCompileSquirrelNameExpression(vm, "this");
		Require(this_program && this_program->MainFunction.StackSize == 1 && this_program->MainFunction.Instructions.front().Opcode == 19 && this_program->MainFunction.Instructions.front().Argument1 == 0, "native this expression must return actual local0 without a name lookup");
		const auto this_closure = vm.BindEnvironment(vm.CreateClosure(*this_program), environment);
		const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
		Require(vm.Invoke(this_closure, receiver).Value == environment, "actual bound this identity must survive name compilation and invocation");
		for (const auto* keyword : {"parent", "return", "null", "true", "delegate", "constructor", "vargc", "vargv"}) {
			Require(!sgre::script::TryCompileSquirrelNameExpression(vm, keyword), "native keyword cannot become an arbitrary property identifier");
		}
		vm.SetRootTable(vm.CreateTable());
		const auto child = vm.CreateContext();
		Require(vm.HasSlot(vm.RootTableInContext(child), "template"), "actual child initialization lost its native template registration");
		const auto child_root = vm.RootTableInContext(child);
		vm.NewSlot(child_root, "scope", sgre::script::SquirrelString("child root"));
		vm.SetRootTable(vm.CreateTable());
		vm.NewSlot(vm.RootTable(), "scope", sgre::script::SquirrelString("other root"));
		const std::array<sgre::script::SquirrelValue, 3> child_arguments{child_root, sgre::script::SquirrelString("$scope"), vm.CreateTable()};
		Require(sgre::script::StringText(vm.InvokeInContext(child, method, child_arguments).Value) == "child root", "nested template evaluation must preserve the actual executing context root fallback");
		vm.ReleaseContext(child);
		vm.Close();
		std::cout << "native-derived template scanner, name compiler, actual contexts, environment and fallback passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
