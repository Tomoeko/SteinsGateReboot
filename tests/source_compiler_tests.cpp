#include "sgre/script/source_compiler.hpp"

#include "sgre/script/input_bindings.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sgre::script::CompileSquirrelSource;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("source compiler accepted an unsupported input: " + std::string(context));
}

static SquirrelValue Run(SquirrelVm& vm, std::string_view source, const SquirrelValue& receiver) {
	const auto closure = vm.CreateClosure(CompileSquirrelSource(vm, source, "source-fixture"));
	const std::array<SquirrelValue, 1> arguments{receiver};
	const auto result = vm.Invoke(closure, arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed, "compiled source unexpectedly suspended");
	return result.Value;
}

static void TestLiteralsStatementsAndMainPrototype() {
	SquirrelVm vm;
	const auto empty = CompileSquirrelSource(vm, "");
	Require(std::get<std::string>(empty.MainFunction.SourceName) == "unnamedbuffer" && std::get<std::string>(empty.MainFunction.Name) == "main" && empty.MainFunction.Parameters == std::vector<sgre::script::SquirrelLiteral>{std::string("this")} && empty.MainFunction.HasVariableParameters && empty.MainFunction.OuterBindings.empty() && !empty.MainFunction.IsGenerator, "source main must retain native source/name, implicit this, varparams and no lexical caller");
	Require(std::holds_alternative<std::monostate>(Run(vm, "", vm.RootTable())) && std::holds_alternative<std::monostate>(Run(vm, "return;", vm.RootTable())), "empty source and empty return require the real implicit null result");
	Require(std::get<bool>(Run(vm, "return true;", vm.RootTable())) && !std::get<bool>(Run(vm, "return false;", vm.RootTable())), "compiled boolean literals differ");
	Require(std::holds_alternative<std::monostate>(Run(vm, "return null;", vm.RootTable())), "compiled null literal differs");
	Require(std::get<std::int32_t>(Run(vm, "return 2147483647;", vm.RootTable())) == 2147483647, "compiled signed32 positive limit differs");
	Require(sgre::script::StringText(Run(vm, "return \"quoted\\\" and slash\\\\\";", vm.RootTable())) == "quoted\" and slash\\", "compiled string escapes differ");
	Require(std::get<std::int32_t>(Run(vm, "17; return 23; 99;", vm.RootTable())) == 23, "expression statements or return control differ");
	const auto receiver = vm.CreateTable();
	Require(Run(vm, "return this;", receiver) == receiver, "implicit this must return the actual call receiver");
	const auto main = vm.CreateClosure(CompileSquirrelSource(vm, "return 41;", "variable-main"));
	const std::array<SquirrelValue, 3> surplus{receiver, std::int32_t{8}, std::string("ignored")};
	Require(std::get<std::int32_t>(vm.Invoke(main, surplus).Value) == 41, "main prototype must accept native variable arguments");
}

static void TestCompileIsInertAndCallsPreserveReceivers() {
	SquirrelVm vm;
	std::size_t calls = 0;
	const auto receiver = vm.CreateTable();
	const auto tools = vm.CreateTable();
	vm.NewSlot(receiver, std::string("tools"), tools);
	vm.NewSlot(receiver, std::string("needle"), std::string("arbitrary-key"));
	const auto method = vm.CreateNativeClosure("generic_lookup", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && arguments[0] == tools && sgre::script::StringText(arguments[1]) == "arbitrary-key" && std::get<std::int32_t>(arguments[2]) == 17, "compiled method call lost its object receiver or actual arguments");
		++calls;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{73});
	});
	vm.NewSlot(tools, std::string("lookup"), method);
	auto program = CompileSquirrelSource(vm, "return this.tools.lookup(this[\"needle\"], 17);", "method-source");
	Require(calls == 0 && std::get<std::string>(program.MainFunction.SourceName) == "method-source", "compilation must not invoke source or lose its supplied source name");
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 1> arguments{receiver};
	Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 73 && calls == 1, "compiled dot call did not execute exactly once");
	Require(std::get<std::int32_t>(Run(vm, "return this.tools[\"lookup\"](this.needle, 17);", receiver)) == 73 && calls == 2, "dynamic indexed call must preserve the same object receiver");
	vm.NewSlot(receiver, std::string("touch"), vm.CreateNativeClosure("touch", [&](SquirrelVm&, std::span<const SquirrelValue> actual) {
		Require(actual.size() == 1 && actual[0] == receiver, "unqualified call must use the actual implicit this receiver");
		++calls;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{9});
	}));
	const auto statement = vm.CreateClosure(CompileSquirrelSource(vm, "touch();", "statement-source"));
	Require(calls == 2, "compiling an expression statement must not execute its callback");
	Require(std::holds_alternative<std::monostate>(vm.Invoke(statement, arguments).Value) && calls == 3, "call expression statement must execute then return implicit null");
	for (const auto source : {"return\ntouch();", "return //comment\ntouch();"}) {
		const auto empty_return = vm.CreateClosure(CompileSquirrelSource(vm, source, "return-newline-source"));
		Require(calls == 3 && std::holds_alternative<std::monostate>(vm.Invoke(empty_return, arguments).Value) && calls == 3, "newline after return must emit empty return before the later, unexecuted expression statement");
	}
	const auto block_comment = vm.CreateClosure(CompileSquirrelSource(vm, "return /*comment\nnext line*/ touch();", "block-comment-source"));
	Require(calls == 3 && std::get<std::int32_t>(vm.Invoke(block_comment, arguments).Value) == 9 && calls == 4, "LF inside a block comment must change line accounting without terminating the return expression");
}

static void TestNestedFunctionsParametersAndEquality() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	const auto unused = vm.CreateTable();
	vm.NewSlot(receiver, std::string("answer"), std::int32_t{47});
	vm.NewSlot(unused, std::string("answer"), std::int32_t{11});
	const auto program = CompileSquirrelSource(vm, "return function(unused, key) { return this[key] == 47; };", "nested-source");
	Require(program.MainFunction.Functions.size() == 1, "anonymous function must produce a real child prototype");
	const auto& nested = program.MainFunction.Functions.front();
	Require(nested.Parameters == std::vector<sgre::script::SquirrelLiteral>{std::string("this"), std::string("unused"), std::string("key")} && !nested.HasVariableParameters && std::get<std::string>(nested.SourceName) == "nested-source", "nested prototype must distinguish implicit this from explicit parameters and disable varparams");
	const auto factory = vm.CreateClosure(program);
	const std::array<SquirrelValue, 1> factory_arguments{vm.RootTable()};
	const auto function = vm.Invoke(factory, factory_arguments).Value;
	Require(vm.Type(function) == sgre::script::SquirrelValueType::Closure, "anonymous function must return a genuine VM closure");
	const std::array<SquirrelValue, 3> arguments{receiver, unused, std::string("answer")};
	Require(std::get<bool>(vm.Invoke(function, arguments).Value), "unused explicit self must never replace the actual receiver");
	Require(!std::get<bool>(Run(vm, "return this.answer != 47;", receiver)) && std::get<bool>(Run(vm, "return this.answer != 48;", receiver)), "compiled equality/inequality instructions differ");
	Require(!std::get<bool>(Run(vm, "return this == null;", receiver)), "equality must compare actual runtime values rather than compile-time truth");
	const auto local = Run(vm, "return function(value) { return value; };", receiver);
	const std::array<SquirrelValue, 2> local_arguments{receiver, std::string("parameter-not-root")};
	Require(sgre::script::StringText(vm.Invoke(local, local_arguments).Value) == "parameter-not-root", "explicit local parameter must resolve before implicit receiver lookup");
	RequireFailure([&] {
		const std::array<SquirrelValue, 1> missing{receiver};
		static_cast<void>(vm.Invoke(local, missing));
	},
	    "parameter");
	const auto callback = vm.CreateNativeClosure("local_callback", [&](SquirrelVm&, std::span<const SquirrelValue> actual) {
		Require(actual.size() == 1 && actual[0] == receiver, "local callable must receive current this, not the callable or an explicit self argument");
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{31});
	});
	vm.NewSlot(receiver, std::string("finish"), callback);
	const auto tail_program = CompileSquirrelSource(vm, "return function() { return this.finish(); };", "tail-source");
	const auto& nested_instructions = tail_program.MainFunction.Functions.front().Instructions;
	Require(std::any_of(nested_instructions.begin(), nested_instructions.end(), [](const auto& instruction) {
		return instruction.Opcode == 5;
	}),
	    "nested return-call must use the recovered real TailCall instruction");
	const auto ordinary = CompileSquirrelSource(vm, "return this.finish();", "main-call-source");
	Require(std::any_of(ordinary.MainFunction.Instructions.begin(), ordinary.MainFunction.Instructions.end(), [](const auto& instruction) {
		return instruction.Opcode == 6;
	}) && std::none_of(ordinary.MainFunction.Instructions.begin(), ordinary.MainFunction.Instructions.end(), [](const auto& instruction) {
		return instruction.Opcode == 5;
	}),
	    "parentless main return-call must retain the ordinary Call instruction");
	const auto local_call = Run(vm, "return function(callback) { return callback(); };", receiver);
	const std::array<SquirrelValue, 2> callback_arguments{receiver, callback};
	Require(std::get<std::int32_t>(vm.Invoke(local_call, callback_arguments).Value) == 31, "local callable return-call lost its actual result");
	vm.NewSlot(vm.RootTable(), std::string("outer_name"), std::int32_t{83});
	const auto outer = Run(vm, "return function(outer_name) { return function() { return outer_name; }; };", receiver);
	const std::array<SquirrelValue, 2> outer_arguments{receiver, std::int32_t{12}};
	const auto inner = vm.Invoke(outer, outer_arguments).Value;
	const std::array<SquirrelValue, 1> inner_arguments{receiver};
	Require(std::get<std::int32_t>(vm.Invoke(inner, inner_arguments).Value) == 83, "Squirrel2 functions without explicit captures must not invent an automatic enclosing-local capture");
}

static void TestConstantSnapshotsAndCurrentContextRoot() {
	SquirrelVm vm;
	vm.NewSlot(vm.ConstantTable(), std::string("COMPILE_TOKEN"), std::int32_t{37});
	vm.NewSlot(vm.RootTable(), std::string("COMPILE_TOKEN"), std::int32_t{91});
	const auto constant = vm.CreateClosure(CompileSquirrelSource(vm, "return COMPILE_TOKEN;", "constant-source"));
	vm.SetSlot(vm.ConstantTable(), std::string("COMPILE_TOKEN"), std::int32_t{62});
	const auto receiver = vm.CreateTable();
	vm.NewSlot(receiver, std::string("COMPILE_TOKEN"), std::int32_t{84});
	const std::array<SquirrelValue, 1> arguments{receiver};
	Require(std::get<std::int32_t>(vm.Invoke(constant, arguments).Value) == 37, "constant lookup must compile a literal snapshot before runtime receiver/root lookup");
	const auto local = Run(vm, "return function(COMPILE_TOKEN) { return COMPILE_TOKEN; };", receiver);
	const std::array<SquirrelValue, 2> local_arguments{receiver, std::int32_t{19}};
	Require(std::get<std::int32_t>(vm.Invoke(local, local_arguments).Value) == 19, "a real local parameter must shadow the shared compile-time constant");
	vm.NewSlot(vm.RootTable(), std::string("context_value"), std::int32_t{101});
	const auto child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.NewSlot(child_root, std::string("context_value"), std::int32_t{202});
	vm.SetRootTableInContext(child, child_root);
	std::size_t compilations = 0;
	const auto compile = vm.CreateNativeClosure("compile_in_actual_context", [&](SquirrelVm& actual, std::span<const SquirrelValue>) {
		Require(actual.CurrentContext() == child, "compiler callback was not entered in the actual child context");
		++compilations;
		return sgre::script::SquirrelNativeResult::ReturnValue(actual.CreateClosure(CompileSquirrelSource(actual, "return context_value;", "context-source")));
	});
	const auto compiled = vm.InvokeInContext(child, compile, arguments).Value;
	Require(compilations == 1 && vm.CurrentContext() == vm.RootContext(), "compile callback must restore its caller without executing the compiled main");
	Require(std::get<std::int32_t>(vm.InvokeInContext(child, compiled, arguments).Value) == 202 && std::get<std::int32_t>(vm.Invoke(compiled, arguments).Value) == 101, "free identifier fallback must use the actual invocation context's current root");
	vm.NewSlot(receiver, std::string("context_value"), std::int32_t{303});
	Require(std::get<std::int32_t>(vm.InvokeInContext(child, compiled, arguments).Value) == 303, "implicit receiver lookup must precede current-root fallback");
	vm.NewSlot(receiver, std::string("empty_member"), vm.CreateTable());
	RequireFailure([&] {
		static_cast<void>(Run(vm, "return this.empty_member.context_value;", receiver));
	},
	    "does not exist");
	vm.ReleaseContext(child);
}

static void TestLogicalConditionalAndRootExpressions() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	std::string trace;
	const auto callback = [&](std::string label, SquirrelValue result, const SquirrelValue& expected_receiver) {
		return vm.CreateNativeClosure(label, [&, label, result, expected_receiver](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			Require(arguments.size() == 1 && arguments[0] == expected_receiver, "compiled control expression lost its genuine method receiver");
			trace += label;
			return sgre::script::SquirrelNativeResult::ReturnValue(result);
		});
	};
	vm.NewSlot(receiver, std::string("yes"), callback("Y", true, receiver));
	vm.NewSlot(receiver, std::string("no"), callback("N", false, receiver));
	vm.NewSlot(receiver, std::string("value"), callback("V", std::string("selected"), receiver));
	vm.NewSlot(receiver, std::string("isSpecWin"), callback("W", true, receiver));
	Require(!std::get<bool>(Run(vm, "return !isSpecWin()", receiver)) && trace == "W", "actual generated title expression must negate the real method result once");
	trace.clear();
	Require(!std::get<bool>(Run(vm, "return no() && value();", receiver)) && trace == "N", "false && must return the left operand and never execute the right call");
	trace.clear();
	Require(std::get<bool>(Run(vm, "return yes() || value();", receiver)) && trace == "Y", "true || must return the left operand and never execute the right call");
	trace.clear();
	Require(sgre::script::StringText(Run(vm, "return yes() && value();", receiver)) == "selected" && trace == "YV", "true && must return the actual right value without boolean coercion");
	trace.clear();
	Require(sgre::script::StringText(Run(vm, "return no() || value();", receiver)) == "selected" && trace == "NV", "false || must evaluate the right value once");
	trace.clear();
	Require(std::get<bool>(Run(vm, "return yes() || no() && value();", receiver)) && trace == "Y", "logical precedence must keep && inside the skipped || operand");
	trace.clear();
	Require(!std::get<bool>(Run(vm, "return no() && yes() || no();", receiver)) && trace == "NN", "mixed logical operators must preserve real execution order and skipped calls");
	Require(std::get<bool>(Run(vm, "return !0;", receiver)) && !std::get<bool>(Run(vm, "return !\"\";", receiver)) && std::get<bool>(Run(vm, "return !null;", receiver)) && std::get<bool>(Run(vm, "return !!true;", receiver)), "generated operators must retain native numeric-zero, empty-string, null and boolean truth");
	trace.clear();
	Require(sgre::script::StringText(Run(vm, "return yes() ? value() : missing();", receiver)) == "selected" && trace == "YV", "conditional must not look up or call the unselected arm");
	trace.clear();
	Require(sgre::script::StringText(Run(vm, "return no() ? missing() : value();", receiver)) == "selected" && trace == "NV", "false conditional must select its own result destination");
	Require(std::get<std::int32_t>(Run(vm, "return false ? 11 : true ? 23 : 37;", receiver)) == 23 && std::get<std::int32_t>(Run(vm, "return true ? false ? 11 : 23 : 37;", receiver)) == 23, "nested conditionals must associate through each arm and patch branch targets independently");
	vm.NewSlot(vm.ConstantTable(), std::string("TARGET_SYSTEM"), std::string("WIN"));
	Require(std::get<std::int32_t>(Run(vm, "return TARGET_SYSTEM==\"VITA\"?1:0", receiver)) == 0, "actual platform conditional must compare the shared compile-time value");
	vm.NewSlot(vm.RootTable(), std::string("root_value"), std::int32_t{41});
	vm.NewSlot(receiver, std::string("root_value"), std::int32_t{99});
	vm.NewSlot(vm.ConstantTable(), std::string("root_value"), std::int32_t{77});
	Require(std::get<std::int32_t>(Run(vm, "return ::root_value;", receiver)) == 41, "explicit root lookup must bypass receiver and compiler constants");
	const auto child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.NewSlot(child_root, std::string("root_value"), std::int32_t{52});
	const auto root_closure = vm.CreateClosure(CompileSquirrelSource(vm, "return ::root_value;"));
	vm.SetRootTableInContext(child, child_root);
	const std::array<SquirrelValue, 1> arguments{receiver};
	Require(std::get<std::int32_t>(vm.InvokeInContext(child, root_closure, arguments).Value) == 52, "explicit root must use invocation context rather than the compilation context");
	vm.ReleaseContext(child);
	vm.NewSlot(vm.RootTable(), std::string("isCircleButtonOK"), callback("C", false, vm.RootTable()));
	vm.NewSlot(vm.RootTable(), std::string("toint"), vm.CreateNativeClosure("platform_bool_to_int", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] == vm.RootTable(), "explicit root call must preserve root receiver");
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{std::get<bool>(arguments[1]) ? 1 : 0});
	}));
	trace.clear();
	Require(std::get<std::int32_t>(Run(vm, "return ::toint(TARGET_SYSTEM==\"PS4\"&&!::isCircleButtonOK())", receiver)) == 0 && trace.empty(), "actual platform expression must skip the native circle query on another platform");
	vm.SetSlot(vm.ConstantTable(), std::string("TARGET_SYSTEM"), std::string("PS4"));
	Require(std::get<std::int32_t>(Run(vm, "return ::toint(TARGET_SYSTEM==\"PS4\"&&!::isCircleButtonOK())", receiver)) == 1 && trace == "C", "actual platform expression must call root query and preserve unary precedence");
	for (const auto source : {"return !;", "return true ? 1;", "return true ? : 1;", "return false ||;", "return ::;", "return ::this;"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "control-error"));
		},
		    "control-error");
	}
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, "return " + std::string(70, '!') + "true;", "unary-depth"));
	},
	    "depth limit");
	std::string chain = "return true";
	for (std::size_t index = 0; index != 70; ++index) {
		chain += " && true";
	}
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, chain, "logical-depth"));
	},
	    "depth limit");
}

static void TestCommaExpressionsAndArgumentBoundaries() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	std::string trace;
	for (const auto& [name, value] : std::array<std::pair<std::string, std::int32_t>, 3>{{{"first", 11}, {"second", 22}, {"last", 33}}}) {
		vm.NewSlot(receiver, name, vm.CreateNativeClosure(name, [&, name, value](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			Require(arguments.size() == 1 && arguments[0] == receiver, "comma call lost its original receiver");
			trace += name;
			return sgre::script::SquirrelNativeResult::ReturnValue(value);
		}));
	}
	Require(std::get<std::int32_t>(Run(vm, "return first(),second(),last();", receiver)) == 33 && trace == "firstsecondlast", "comma expression must evaluate every operand once in source order and return only the last value");
	trace.clear();
	Require(std::get<std::int32_t>(Run(vm, "first(),second(); return 47;", receiver)) == 47 && trace == "firstsecond", "comma expression statement must discard its result before the next statement");
	trace.clear();
	Require(std::get<std::int32_t>(Run(vm, "return false && first(),second();", receiver)) == 22 && trace == "second", "comma must bind below short-circuit conjunction");
	trace.clear();
	Require(std::get<std::int32_t>(Run(vm, "return false ? first() : second(),last();", receiver)) == 33 && trace == "secondlast", "comma must bind below both conditional arms");
	trace.clear();
	Require(std::get<std::int32_t>(Run(vm, "return true ? (first(),second()) : last();", receiver)) == 22 && trace == "firstsecond", "parentheses must allow an ordered comma expression within the selected conditional arm");
	vm.NewSlot(receiver, std::string("collect"), vm.CreateNativeClosure("collect", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 4 && arguments[0] == receiver && std::get<std::int32_t>(arguments[1]) == 11 && std::get<std::int32_t>(arguments[2]) == 22 && std::get<std::int32_t>(arguments[3]) == 33, "call commas must separate actual arguments while grouped commas retain one value");
		trace += "collect";
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{71});
	}));
	trace.clear();
	Require(std::get<std::int32_t>(Run(vm, "return collect(first(), (first(),second()), last());", receiver)) == 71 && trace == "firstfirstsecondlastcollect", "a grouped comma must preserve earlier live argument registers and argument order");
	trace.clear();
	RequireFailure([&] {
		static_cast<void>(Run(vm, "return missing,first();", receiver));
	},
	    "does not exist");
	Require(trace.empty(), "discarding a slot expression must retain its lookup failure before later comma operands");
	const auto tools = vm.CreateTable();
	vm.NewSlot(receiver, std::string("tools"), tools);
	vm.NewSlot(tools, std::string("finish"), vm.CreateNativeClosure("finish", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1, "grouped callable has unexpected arguments");
		return sgre::script::SquirrelNativeResult::ReturnValue(arguments[0] == tools);
	}));
	Require(std::get<bool>(Run(vm, "return first(),this.tools.finish();", receiver)), "direct final comma method must retain its object receiver");
	Require(!std::get<bool>(Run(vm, "return (first(),this.tools.finish)();", receiver)) && !std::get<bool>(Run(vm, "return (this.tools.finish)();", receiver)), "parenthesized function values must use the current this receiver instead of retaining an inner slot");
	vm.NewSlot(receiver, std::string("chosen"), std::int32_t{91});
	Require(std::get<std::int32_t>(Run(vm, "return this[(first(),\"chosen\")];", receiver)) == 91, "grouped index comma must select its final actual key");
	std::string long_sequence = "return ";
	for (std::size_t index = 0; index < 1000; ++index) {
		long_sequence += "17,";
	}
	long_sequence += "53;";
	Require(std::get<std::int32_t>(Run(vm, long_sequence, receiver)) == 53, "discarded comma operands must not exhaust the bounded VM register file");
	for (const auto source : {"return ,1;", "return 1,;", "return (1,);", "return collect(1,,2);", "return true ? 1,2 : 3;", "return this[1,2];"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "comma-error"));
		},
		    "comma-error");
	}
}

static void TestCommaActionSuspensionAndRegisteredCompilation() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto receiver = vm.CreateTable();
	std::string trace;
	vm.NewSlot(vm.RootTable(), std::string("wait"), vm.CreateNativeClosure("wait", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] == vm.RootTable() && std::get<std::int32_t>(arguments[1]) == 10, "ordered action must invoke the explicit root wait with its exact argument");
		trace += "wait";
		sgre::script::SquirrelNativeResult result;
		result.Value = std::string("waiting");
		result.Suspend = true;
		return result;
	}));
	vm.NewSlot(receiver, std::string("startContacts"), vm.CreateNativeClosure("startContacts", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1 && arguments[0] == receiver, "action continuation must retain its genuine panel receiver");
		trace += "contacts";
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{79});
	}));
	const auto compiler = vm.GetSlot(vm.RootTable(), std::string("compilestring"));
	const std::array<SquirrelValue, 2> compile_arguments{vm.RootTable(), std::string("return ::wait(10),startContacts()")};
	const auto action = vm.Invoke(compiler, compile_arguments).Value;
	Require(trace.empty() && !vm.HasBoundEnvironment(action), "registered comma action compilation must remain inert and unbound");
	const std::array<SquirrelValue, 1> arguments{receiver};
	const auto suspended = vm.Invoke(action, arguments);
	Require(suspended.State == sgre::script::SquirrelRunState::Suspended && sgre::script::StringText(suspended.Value) == "waiting" && trace == "wait", "comma continuation must wait at the original call rather than running its later action early");
	const auto resumed = vm.Resume(std::int32_t{97});
	Require(resumed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(resumed.Value) == 79 && trace == "waitcontacts" && !vm.IsSuspended(), "resume must discard only the left result and execute the original final action once");
	const auto factory = CompileSquirrelSource(vm, "return function() { return ::wait(10),startContacts(); };", "nested-comma-action");
	const auto& instructions = factory.MainFunction.Functions.front().Instructions;
	std::vector<std::uint8_t> calls;
	for (const auto& instruction : instructions) {
		if (instruction.Opcode == 5 || instruction.Opcode == 6) {
			calls.push_back(instruction.Opcode);
		}
	}
	Require(calls == std::vector<std::uint8_t>{6, 5}, "only the final returned comma call may become a nested tail call");
	const auto nested = Run(vm, "return function() { return ::wait(10),startContacts(); };", receiver);
	trace.clear();
	Require(vm.Invoke(nested, arguments).State == sgre::script::SquirrelRunState::Suspended && trace == "wait", "nested comma action must preserve the ordinary first-call suspension");
	Require(std::get<std::int32_t>(vm.Resume().Value) == 79 && trace == "waitcontacts", "nested tail-call continuation must run after the original wait resumes");
}

static void TestRelationalOperatorsAndNativePrecedence() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	const std::array<std::pair<std::string_view, bool>, 12> cases{{{"return 1<2;", true}, {"return 1>2;", false}, {"return 2<1;", false}, {"return 2>1;", true}, {"return 1<=2;", true}, {"return 1>=2;", false}, {"return 2<=1;", false}, {"return 2>=1;", true}, {"return 2<=2;", true}, {"return 2>=2;", true}, {"return 2<2;", false}, {"return 2>2;", false}}};
	for (const auto& [source, expected] : cases) {
		Require(std::get<bool>(Run(vm, source, receiver)) == expected, "relational expression differs at an original strict/inclusive boundary");
	}
	vm.NewSlot(receiver, std::string("maximum"), std::numeric_limits<std::int32_t>::max());
	vm.NewSlot(receiver, std::string("minimum"), std::numeric_limits<std::int32_t>::min());
	Require(std::get<bool>(Run(vm, "return maximum<minimum;", receiver)), "compiled relation must retain the VM's native wrapped integer difference");
	vm.NewSlot(receiver, std::string("fraction"), 1.5f);
	Require(std::get<bool>(Run(vm, "return fraction>1;", receiver)) && std::get<bool>(Run(vm, "return \"alpha\"<\"beta\";", receiver)), "compiled relation must preserve existing mixed numeric and string ordering");
	std::string trace;
	for (const auto& [name, value] : std::array<std::pair<std::string, std::int32_t>, 2>{{{"left", 7}, {"right", 6}}}) {
		vm.NewSlot(receiver, name, vm.CreateNativeClosure(name, [&, name, value](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			Require(arguments.size() == 1 && arguments[0] == receiver, "relational operand call lost its actual receiver");
			trace += name;
			return sgre::script::SquirrelNativeResult::ReturnValue(value);
		}));
	}
	Require(std::get<bool>(Run(vm, "return left()>=right();", receiver)) && trace == "leftright", "comparison operands must execute once in left-to-right order");
	for (const auto& [source, expected] : std::array<std::pair<std::string_view, std::vector<std::uint8_t>>, 3>{{{"return a==b<c;", {15, 40}}, {"return a<b==c;", {40, 15}}, {"return a<b>=c;", {40, 40}}}}) {
		const auto program = CompileSquirrelSource(vm, source, "native-comparison-precedence");
		std::vector<std::uint8_t> comparisons;
		for (const auto& instruction : program.MainFunction.Instructions) {
			if (instruction.Opcode == 15 || instruction.Opcode == 16 || instruction.Opcode == 40) {
				comparisons.push_back(instruction.Opcode);
			}
		}
		Require(comparisons == expected, "original equality and relational operators must share one left-associative level");
	}
	for (const auto source : {"return 1<;", "return 1>=;", "return 1<=>2;"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "relation-error"));
		},
		    "relation-error");
	}
}

static void TestActualDialogThresholdCompilation() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto receiver = vm.CreateTable();
	vm.NewSlot(receiver, std::string("isDialog"), false);
	const auto compiler = vm.GetSlot(vm.RootTable(), std::string("compilestring"));
	std::array<SquirrelValue, 2> conditions;
	for (std::size_t index = 0; index < conditions.size(); ++index) {
		const std::array<SquirrelValue, 2> compile_arguments{vm.RootTable(), std::string("return isDialog && choiceNum>=") + std::to_string(index + 1)};
		conditions[index] = vm.Invoke(compiler, compile_arguments).Value;
	}
	const std::array<SquirrelValue, 1> arguments{receiver};
	for (const auto& condition : conditions) {
		Require(!std::get<bool>(vm.Invoke(condition, arguments).Value), "inactive dialog must short-circuit before looking up an absent choice count");
	}
	vm.SetSlot(receiver, std::string("isDialog"), true);
	vm.NewSlot(receiver, std::string("choiceNum"), std::int32_t{0});
	for (std::int32_t count = 0; count <= 3; ++count) {
		vm.SetSlot(receiver, std::string("choiceNum"), count);
		for (std::size_t index = 0; index < conditions.size(); ++index) {
			Require(std::get<bool>(vm.Invoke(conditions[index], arguments).Value) == (count >= static_cast<std::int32_t>(index + 1)), "original dialog choice condition must observe the current receiver count at each inclusive threshold");
		}
	}
}

static void TestUnsupportedGrammarAndDiagnostics() {
	SquirrelVm vm;
	for (const auto source : {"return (1;", "return \"unterminated;", "return this.;", "return this[;", "return function(value,,other) { return value; };", "return function(value,value) { return value; };", "return function() :(outer) { return outer; };", "while (true) {}", "return function(value) { return value;", "return 2147483648;"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "rejected-source"));
		},
		    "rejected-source");
	}
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, "\nreturn this.;", "line-source"));
	},
	    "line-source:2: source compiler at byte");
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, "return this\n[\"key\"];", "bracket-newline-source"));
	},
	    "bracket-newline-source");
	const auto receiver = vm.CreateTable();
	vm.NewSlot(receiver, std::string("value"), std::int32_t{0});
	Require(std::get<std::int32_t>(Run(vm, "local value = 1; return value;", receiver)) == 1, "newly recovered local declaration must produce its independent value");
	Require(std::holds_alternative<std::monostate>(Run(vm, "this.value = 1;", receiver)) && std::get<std::int32_t>(vm.GetSlot(receiver, std::string("value"))) == 1, "newly recovered assignment must publish its effect and retain implicit null return");
	Require(std::get<std::int32_t>(Run(vm, "return 1 + 2;", receiver)) == 3, "newly recovered arithmetic must produce its VM addition result");
	Require(std::get<std::int32_t>(Run(vm, "return 29;", vm.RootTable())) == 29, "a rejected compile must not poison a later actual compilation/invocation");
}

static void TestNulBoundaryAndCompilerLimits() {
	SquirrelVm vm;
	std::string source = "return 7;";
	source.push_back('\0');
	source += "unsupported = input;";
	Require(std::get<std::int32_t>(Run(vm, source, vm.RootTable())) == 7, "native lexer NUL must terminate source before trailing unsupported bytes");
	Require(std::holds_alternative<std::monostate>(Run(vm, std::string(1, '\0'), vm.RootTable())), "initial NUL must behave as empty source");
	const std::string name("named\0ignored", 13);
	const auto named = CompileSquirrelSource(vm, "return null;", name);
	Require(std::get<std::string>(named.MainFunction.SourceName) == "named", "native source name must stop at its first NUL");
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, std::string(1024 * 1024 + 1, ' '), "oversized-source"));
	},
	    "source byte limit exceeded");
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, "return " + std::string(70, '(') + "1" + std::string(70, ')') + ";", "deep-source"));
	},
	    "depth limit exceeded");
	std::string parameters = "return function(";
	for (std::size_t index = 0; index < 255; ++index) {
		parameters += (index == 0 ? "" : ",") + std::string("arg") + std::to_string(index);
	}
	parameters += ") { return null; };";
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, parameters, "register-source"));
	},
	    "register limit exceeded");
	std::string statements;
	for (std::size_t index = 0; index < 16385; ++index) {
		statements += "17;";
	}
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, statements, "instruction-source"));
	},
	    "instruction limit exceeded");
	std::string literals;
	std::string functions;
	for (std::size_t index = 0; index < 4097; ++index) {
		literals += "\"literal_" + std::to_string(index) + "\";";
		functions += "function() {};";
	}
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, literals, "literal-source"));
	},
	    "literal limit exceeded");
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, functions, "function-source"));
	},
	    "function limit exceeded");
}

static SquirrelValue ClosureInformation(SquirrelVm& vm, const SquirrelValue& closure) {
	const std::array<SquirrelValue, 1> arguments{closure};
	return vm.Invoke(vm.GetSlot(closure, std::string("getinfos")), arguments).Value;
}

static void TestActualCompilestringRegistration() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto compiler = vm.GetSlot(vm.RootTable(), std::string("compilestring"));
	const auto information = ClosureInformation(vm, compiler);
	const auto types = vm.GetSlot(information, std::string("typecheck"));
	Require(vm.Type(compiler) == sgre::script::SquirrelValueType::NativeClosure && std::get<std::int32_t>(vm.GetSlot(information, std::string("paramscheck"))) == -2 && vm.Length(types) == 3 && std::get<std::int32_t>(vm.GetSlot(types, std::int32_t{0})) == -1 && std::get<std::int32_t>(vm.GetSlot(types, std::int32_t{1})) == 16 && std::get<std::int32_t>(vm.GetSlot(types, std::int32_t{2})) == 16, "compilestring must register its actual minimum-two .ss native signature");
	const auto compiler_receiver = vm.CreateTable();
	const auto call_receiver = vm.CreateTable();
	vm.NewSlot(compiler_receiver, std::string("mark"), std::int32_t{7});
	vm.NewSlot(call_receiver, std::string("mark"), std::int32_t{83});
	std::size_t calls = 0;
	vm.RegisterNative("later", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1 && arguments[0] == call_receiver, "registered compiled code must use its real invocation receiver");
		++calls;
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 2> defaults{compiler_receiver, std::string("later(); return this.mark;")};
	const auto closure = vm.Invoke(compiler, defaults).Value;
	Require(vm.Type(closure) == sgre::script::SquirrelValueType::Closure && !vm.HasBoundEnvironment(closure) && calls == 0 && sgre::script::StringText(vm.GetSlot(ClosureInformation(vm, closure), std::string("src"))) == "unnamedbuffer", "registered compilestring must return an unbound, unexecuted closure with the native default source name");
	const std::array<SquirrelValue, 1> actual_arguments{call_receiver};
	Require(std::get<std::int32_t>(vm.Invoke(closure, actual_arguments).Value) == 83 && calls == 1, "returned compiled closure must execute only when invoked with its actual receiver");
	const std::array<SquirrelValue, 5> extra{compiler_receiver, std::string("return this.mark;"), std::string("optional-source"), std::int32_t{99}, false};
	const auto named = vm.Invoke(compiler, extra).Value;
	Require(!vm.HasBoundEnvironment(named) && sgre::script::StringText(vm.GetSlot(ClosureInformation(vm, named), std::string("src"))) == "optional-source" && std::get<std::int32_t>(vm.Invoke(named, actual_arguments).Value) == 83, "optional source name must be physical argument three and later arguments must not alter compilation");
	RequireFailure([&] {
		const std::array<SquirrelValue, 1> missing{compiler_receiver};
		static_cast<void>(vm.Invoke(compiler, missing));
	},
	    "wrong number of parameters");
	std::size_t conversions = 0;
	const auto type = vm.CreateClass();
	vm.NewSlot(type, std::string("_tostring"), vm.CreateNativeClosure("not_a_source_string", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		++conversions;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("return 1;"));
	}));
	const auto object = vm.CreateInstance(type);
	for (const SquirrelValue& invalid : {SquirrelValue{}, SquirrelValue{std::int32_t{17}}, SquirrelValue{true}, object}) {
		RequireFailure([&] {
			const std::array<SquirrelValue, 2> arguments{compiler_receiver, invalid};
			static_cast<void>(vm.Invoke(compiler, arguments));
		},
		    "type mismatch");
		RequireFailure([&] {
			const std::array<SquirrelValue, 3> arguments{compiler_receiver, std::string("return 1;"), invalid};
			static_cast<void>(vm.Invoke(compiler, arguments));
		},
		    "type mismatch");
	}
	Require(conversions == 0 && calls == 1, "compilestring must not ToString a source/name or execute a rejected input");
	RequireFailure([&] {
		const std::array<SquirrelValue, 3> arguments{compiler_receiver, std::string("return this.;"), std::string("registered-error")};
		static_cast<void>(vm.Invoke(compiler, arguments));
	},
	    "registered-error");
}

static void TestQualifiedConstantMembersAndOwnedSnapshots() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	auto enumeration = vm.CreateTable();
	vm.NewSlot(vm.ConstantTable(), std::string("ENUM"), enumeration);
	vm.NewSlot(enumeration, std::string("Number"), std::int32_t{37});
	vm.NewSlot(enumeration, std::string("Negative"), std::int32_t{-17});
	vm.NewSlot(enumeration, std::string("Fraction"), 1.25f);
	vm.NewSlot(enumeration, std::string("Enabled"), true);
	vm.NewSlot(enumeration, std::string("Empty"), SquirrelValue{});
	vm.NewSlot(enumeration, std::string("Text"), std::string("owned primitive"));
	vm.NewSlot(enumeration, std::string("constructor"), std::int32_t{88});
	Require(std::get<std::int32_t>(Run(vm, "return ENUM.Number;", vm.RootTable())) == 37 && std::get<std::int32_t>(Run(vm, "return ENUM.Negative;", vm.RootTable())) == -17, "qualified constant integers must fold the selected primitive member");
	Require(std::get<float>(Run(vm, "return ENUM.Fraction;", vm.RootTable())) == 1.25f && std::get<bool>(Run(vm, "return ENUM.Enabled;", vm.RootTable())) && std::holds_alternative<std::monostate>(Run(vm, "return ENUM.Empty;", vm.RootTable())), "qualified constants changed existing primitive float/bool/null loading");
	Require(std::get<std::int32_t>(Run(vm, "return ENUM.constructor;", vm.RootTable())) == 88, "native identifier expectation must retain its constructor-name exception");
	const auto runtime_table = vm.CreateTable();
	vm.NewSlot(vm.RootTable(), std::string("ENUM"), runtime_table);
	vm.NewSlot(runtime_table, std::string("Number"), std::int32_t{91});
	Require(std::get<std::int32_t>(Run(vm, "return ::ENUM.Number;", vm.RootTable())) == 91 && std::get<std::int32_t>(Run(vm, "return ENUM.Number;", vm.RootTable())) == 37, "explicit root access and shared compile-time constants must remain distinct");
	const auto local = Run(vm, "return function(ENUM) { return ENUM.Number; };", vm.RootTable());
	const auto local_root = vm.Retain(local);
	const std::array<SquirrelValue, 2> local_arguments{vm.RootTable(), runtime_table};
	Require(std::get<std::int32_t>(vm.Invoke(local, local_arguments).Value) == 91, "a real local table parameter must shadow the shared enum constant");
	vm.SetSlot(runtime_table, std::string("Number"), std::int32_t{92});
	Require(std::get<std::int32_t>(vm.Invoke(local, local_arguments).Value) == 92, "local member access must remain a runtime read rather than a folded enum member");
	std::size_t delegated_reads = 0;
	const auto delegate = vm.CreateTable();
	const auto delegate_root = vm.Retain(delegate);
	vm.NewSlot(delegate, std::string("Inherited"), std::int32_t{74});
	vm.NewSlot(delegate, std::string("_get"), vm.CreateNativeClosure("forbidden_constant_get", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		++delegated_reads;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{75});
	}));
	sgre::script::BytecodeProgram attach_delegate;
	attach_delegate.MainFunction.Name = std::string("attach-enum-delegate");
	attach_delegate.MainFunction.SourceName = std::string("qualified-constant-fixture");
	attach_delegate.MainFunction.Parameters = {std::string("this"), std::string("object"), std::string("delegate")};
	attach_delegate.MainFunction.StackSize = 3;
	sgre::script::SquirrelInstruction delegation;
	delegation.Opcode = 53;
	delegation.Argument0 = 1;
	delegation.Argument1 = 1;
	delegation.Argument2 = 2;
	sgre::script::SquirrelInstruction returned;
	returned.Opcode = 19;
	returned.Argument0 = 1;
	returned.Argument1 = 1;
	attach_delegate.MainFunction.Instructions = {delegation, returned};
	Require(vm.Invoke(vm.CreateClosure(std::move(attach_delegate)), std::array<SquirrelValue, 3>{vm.RootTable(), enumeration, delegate}).Value == enumeration, "real delegate instruction must return the actual enum table");
	for (const auto source : {"return ENUM.Inherited;", "return ENUM.Missing;"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "raw-enum-source"));
		},
		    "invalid constant [ENUM.");
	}
	Require(delegated_reads == 0, "compile-time enum lookup must not traverse delegates or invoke _get");
	for (const auto source : {"return ENUM;", "return ENUM[\"Number\"];"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "enum-qualifier-source"));
		},
		    "constant table requires '.'");
	}
	for (const auto source : {"return ENUM.;", "return ENUM.this;", "return ENUM.\"Number\";"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "enum-member-source"));
		},
		    "constant member requires identifier");
	}
	vm.NewSlot(enumeration, std::string("Object"), vm.CreateTable());
	for (const auto& object : {vm.CreateTable(), vm.CreateArray(), vm.CreateNativeClosure("unrecovered_object_literal", [](SquirrelVm&, std::span<const SquirrelValue>) {
		                           return sgre::script::SquirrelNativeResult::NoValues();
	                           })}) {
		vm.SetSlot(enumeration, std::string("Object"), object);
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, "return ENUM.Object;", "object-enum-source"));
		},
		    "constant-table value requires unrecovered object constant semantics");
	}
	const auto program = CompileSquirrelSource(vm, "return ENUM.Text;", "enum-owned-source");
	Require(program.MainFunction.Literals == std::vector<sgre::script::SquirrelLiteral>{std::string("owned primitive")}, "folded enum text must own only the resolved primitive literal");
	const auto compiled = vm.CreateClosure(program);
	const auto compiled_root = vm.Retain(compiled);
	vm.SetSlot(enumeration, std::string("Text"), std::string("changed after compilation"));
	vm.SetConstantTable(vm.CreateTable());
	enumeration = {};
	vm.ResetContext(vm.RootContext());
	static_cast<void>(vm.CollectGarbage());
	Require(sgre::script::StringText(vm.Invoke(compiled, std::array<SquirrelValue, 1>{vm.RootTable()}).Value) == "owned primitive", "compiled member snapshot borrowed the retired compile-time table or its later string value");
}

static void TestActualRegisteredKeyMemberConditional() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::input::KeyMappings mappings;
	sgre::script::ScriptInputBindings inputs(vm, objects, mappings);
	const auto keys = vm.GetSlot(vm.ConstantTable(), std::string("KEY"));
	Require(std::get<std::int32_t>(vm.GetSlot(keys, std::string("B"))) == 2, "actual recovered input enum changed its B member");
	vm.NewSlot(vm.ConstantTable(), std::string("KEY_OK"), std::int32_t{2});
	bool nx = false;
	std::size_t calls = 0;
	const auto receiver = vm.CreateTable();
	vm.NewSlot(receiver, std::string("checkNX"), vm.CreateNativeClosure("actual_conditional_platform_query", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1 && arguments[0] == receiver, "generated platform conditional changed the actual method receiver");
		++calls;
		return sgre::script::SquirrelNativeResult::ReturnValue(nx);
	}));
	constexpr std::string_view kOriginalSource = "return checkNX()?2:KEY_OK==KEY.B?1:0";
	const auto program = CompileSquirrelSource(vm, kOriginalSource, "actual-key-member-source");
	Require(std::none_of(program.MainFunction.Literals.begin(), program.MainFunction.Literals.end(), [](const auto& literal) {
		const auto* text = std::get_if<std::string>(&literal);
		return text && (*text == "KEY" || *text == "B" || *text == "KEY_OK");
	}),
	    "generated KEY.B must compile its primitive value without a runtime enum-object lookup");
	const auto compiler = vm.GetSlot(vm.RootTable(), std::string("compilestring"));
	const std::array<SquirrelValue, 3> compile_arguments{vm.RootTable(), std::string(kOriginalSource), std::string("registered-key-member-source")};
	const auto closure = vm.Invoke(compiler, compile_arguments).Value;
	Require(calls == 0, "registered member compilation must not execute the generated platform query");
	const std::array<SquirrelValue, 1> arguments{receiver};
	Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 1 && calls == 1, "generated non-NX equal-key conditional must select its middle result");
	nx = true;
	Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 2 && calls == 2, "generated NX conditional must preserve its first-arm short circuit");
	nx = false;
	vm.SetSlot(vm.ConstantTable(), std::string("KEY_OK"), std::int32_t{1});
	Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 1 && calls == 3, "compiled key conditional changed after a shared constant mutation");
	const auto different = vm.Invoke(compiler, compile_arguments).Value;
	Require(calls == 3 && std::get<std::int32_t>(vm.Invoke(different, arguments).Value) == 0 && calls == 4, "fresh generated nonmatching-key conditional must select its final result");
	vm.SetSlot(keys, std::string("B"), std::int32_t{64});
	Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 1 && std::get<std::int32_t>(vm.Invoke(different, arguments).Value) == 0 && calls == 6, "registered bytecode retained a runtime dependency on the enum table");
}

static void TestHexadecimalWordsAndPrefixBoundaries() {
	SquirrelVm vm;
	const std::array<std::pair<std::string_view, std::int32_t>, 16> cases{{{"0x00000000", 0}, {"0X0", 0}, {"0x1", 1}, {"0X9", 9}, {"0xa", 10}, {"0xAbCdEf", 11259375}, {"0x1e2", 482}, {"0X7fFfFfFf", 2147483647}, {"0x80000000", std::numeric_limits<std::int32_t>::min()}, {"0x80000001", -2147483647}, {"0XFFFFFFFE", -2}, {"0xFFFFFFFF", -1}, {"0x89aBcDeF", -1985229329}, {"0x0000000F", 15}, {"0x", 0}, {"0X", 0}}};
	for (const auto& [literal, expected] : cases) {
		const auto source = std::string("return ") + std::string(literal) + ";";
		Require(std::get<std::int32_t>(Run(vm, source, vm.RootTable())) == expected, "hexadecimal prefix, digit or signed32 word differs");
		const auto program = CompileSquirrelSource(vm, source, "hex-word-emission");
		const auto& instructions = program.MainFunction.Instructions;
		if (expected < 0) {
			Require(program.MainFunction.Literals == std::vector<sgre::script::SquirrelLiteral>{expected} && !instructions.empty() && instructions.front().Opcode == 1, "negative original integer word must use its owned literal and LOAD instruction");
		} else {
			Require(program.MainFunction.Literals.empty() && !instructions.empty() && instructions.front().Opcode == 2 && instructions.front().Argument1 == expected, "nonnegative original integer word must use its immediate LOADINT instruction");
		}
	}
	Require(std::get<std::int32_t>(Run(vm, "return 0x", vm.RootTable())) == 0, "native empty hexadecimal prefix at EOF differs");
	Require(std::get<bool>(Run(vm, "return 0x80000000 < 0;", vm.RootTable())), "hexadecimal sign bit must participate in signed integer comparison");
	Require(std::get<bool>(Run(vm, "return 0xA == 10 && 0Xff == 255;", vm.RootTable())), "hexadecimal token consumed the following expression operator");
	Require(std::get<std::int32_t>(Run(vm, "return true ? 0x123 : 0Xffffffff;", vm.RootTable())) == 291, "hexadecimal token changed conditional arm parsing");
}

static void TestHexadecimalDigitLimitsAndDiagnostics() {
	SquirrelVm vm;
	for (const auto source : {"return 0x000000000;", "return 0X100000000;", "return 0xFFFFFFFFF;"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "hex-limit-source"));
		},
		    "too many digits for a hexadecimal integer");
	}
	for (const auto source : {"return 0xG;", "return 0x1g;", "return 0x1.0;", "return 0x 1;"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "hex-boundary-source"));
		},
		    "hex-boundary-source");
	}
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, "\nreturn 0x000000000;", "hex-line-source"));
	},
	    "hex-line-source:2: source compiler at byte 8");
}

static void TestRegisteredColorHexAndActualArgumentWords() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto compiler = vm.GetSlot(vm.RootTable(), std::string("compilestring"));
	const std::array<SquirrelValue, 3> compile_arguments{vm.RootTable(), std::string("return 0x00000000"), std::string("image-color-source")};
	const auto result = vm.Invoke(compiler, compile_arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed && vm.Type(result.Value) == sgre::script::SquirrelValueType::Closure, "registered compiler did not produce a real closure for the original image color expression");
	const std::array<SquirrelValue, 1> actual_arguments{vm.RootTable()};
	const auto color = vm.Invoke(result.Value, actual_arguments);
	Require(color.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(color.Value) == 0, "original transparent-black expression lost its zero word through registered compilation");
	const auto receiver = vm.CreateTable();
	std::size_t calls = 0;
	vm.NewSlot(receiver, std::string("accept"), vm.CreateNativeClosure("hexadecimal_arguments", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 4 && arguments.front() == receiver && std::get<std::int32_t>(arguments[1]) == -1985229329 && std::get<std::int32_t>(arguments[2]) == 0 && std::get<std::int32_t>(arguments[3]) == -1, "compiled hexadecimal values changed the real callback receiver or argument words");
		++calls;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{71});
	}));
	const auto closure = vm.CreateClosure(CompileSquirrelSource(vm, "return this.accept(0x89aBcDeF, 0x0, 0XFFFFFFFF);", "hex-call-source"));
	Require(calls == 0, "hexadecimal compilation executed its callback");
	const std::array<SquirrelValue, 1> receiver_arguments{receiver};
	Require(std::get<std::int32_t>(vm.Invoke(closure, receiver_arguments).Value) == 71 && calls == 1, "compiled hexadecimal callback changed its result or invocation count");
}

static void TestScopedLocalDeclarations() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	vm.NewSlot(receiver, std::string("inner"), std::int32_t{0});
	vm.NewSlot(receiver, std::string("scoped"), std::int32_t{61});
	vm.NewSlot(vm.ConstantTable(), std::string("TOKEN"), std::int32_t{90});
	Require(std::get<std::int32_t>(Run(vm, "local value = 5; { local value = value + 2; this.inner = value; local scoped = 99; } return value;", receiver)) == 5 && std::get<std::int32_t>(vm.GetSlot(receiver, std::string("inner"))) == 7, "block shadow initializer must read the old local, then restore its original register binding");
	Require(std::get<std::int32_t>(Run(vm, "{ local scoped = 17; } return scoped;", receiver)) == 61, "retired block local must resolve through the actual receiver rather than its stale register");
	Require(std::get<std::int32_t>(Run(vm, "local TOKEN = 7, other = TOKEN + 3; local TOKEN = other + 1; return TOKEN;", receiver)) == 11, "declarations must register names in order and resolve the last same-scope local before constants");
	Require(std::holds_alternative<std::monostate>(Run(vm, "local empty; return empty;", receiver)), "uninitialized local must receive native null");
	Require(std::get<std::int32_t>(Run(vm, "local value = 1\nvalue = value + 4\nreturn value", receiver)) == 5, "native statement LF must separate declarations and assignments without semicolons");
	Require(std::get<std::int32_t>(Run(vm, "local value = 1; return value + (value = 4);", receiver)) == 8, "binary evaluation must retain the original local register alias rather than invent an eager local copy");
}

static void TestContainerLiteralsAndEvaluationOrder() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	std::vector<std::int32_t> calls;
	vm.NewSlot(receiver, std::string("mark"), vm.CreateNativeClosure("mark", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] == receiver, "container callback lost its receiver");
		calls.push_back(std::get<std::int32_t>(arguments[1]));
		return sgre::script::SquirrelNativeResult::ReturnValue(arguments[1]);
	}));
	const auto result = Run(vm, "local index = 2; return { values = [mark(1), index++, index,] nested = { answer = mark(2) }, [\"dynamic\"] = mark(3), constructor = 19 };", receiver);
	const auto values = vm.GetSlot(result, std::string("values"));
	Require(vm.Type(result) == sgre::script::SquirrelValueType::Table && vm.Type(values) == sgre::script::SquirrelValueType::Array && calls == std::vector<std::int32_t>{1, 2, 3}, "container fields and nested elements must execute once in original order");
	Require(std::get<std::int32_t>(vm.GetSlot(values, std::int32_t{0})) == 1 && std::get<std::int32_t>(vm.GetSlot(values, std::int32_t{1})) == 2 && std::get<std::int32_t>(vm.GetSlot(values, std::int32_t{2})) == 3, "array append must publish each value before the following element changes its local");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.GetSlot(result, std::string("nested")), std::string("answer"))) == 2 && std::get<std::int32_t>(vm.GetSlot(result, std::string("dynamic"))) == 3 && std::get<std::int32_t>(vm.GetSlot(result, std::string("constructor"))) == 19, "literal identifier and evaluated table keys differ");
	RequireFailure([&] {
		static_cast<void>(vm.GetSlot(values, std::int32_t{3}));
	},
	    "index");
	calls.clear();
	RequireFailure([&] {
		static_cast<void>(Run(vm, "local index = 2; return { values = [mark(1), index++, index,] nested = { answer = mark(2) } [\"dynamic\"] = mark(3), constructor = 19 };", receiver));
	},
	    "Squirrel assignment to a missing slot");
	Require(calls == std::vector<std::int32_t>{1, 2, 3}, "without the field comma, native postfix must evaluate the preceding table, computed key and right value before its real missing-slot SET fails");
	const auto spaced = Run(vm, "return [11 17 23];", receiver);
	Require(std::get<std::int32_t>(vm.GetSlot(spaced, std::int32_t{2})) == 23, "native container parser permits omitted optional commas between complete expressions");
	Require(vm.Type(Run(vm, "return {};", receiver)) == sgre::script::SquirrelValueType::Table && vm.Type(Run(vm, "return [];", receiver)) == sgre::script::SquirrelValueType::Array, "empty literals must allocate actual distinct container types");
}

static void TestMutableCallArgumentsAndReceivers() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	std::vector<std::int32_t> values;
	vm.NewSlot(receiver, std::string("collect"), vm.CreateNativeClosure("collect", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 4 && arguments[0] == receiver, "local argument call lost its receiver or arity");
		for (std::size_t index = 1; index < arguments.size(); ++index) {
			values.push_back(std::get<std::int32_t>(arguments[index]));
		}
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{53});
	}));
	Require(std::get<std::int32_t>(Run(vm, "local value = 4; return collect(value, value++, value);", receiver)) == 53 && values == std::vector<std::int32_t>{4, 4, 5}, "each local argument must be copied before later argument mutation");
	values.clear();
	Require(std::get<std::int32_t>(Run(vm, "local value = 4; return collect((value), value++, value);", receiver)) == 53 && values == std::vector<std::int32_t>{4, 4, 5}, "parentheses erase assignment eligibility but must not bypass native local-argument copying");
	values.clear();
	Require(std::get<std::int32_t>(Run(vm, "local value = 3; return collect(value, value = 7, value);", receiver)) == 53 && values == std::vector<std::int32_t>{3, 7, 7}, "local assignment result must be frozen at its argument boundary");
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	vm.NewSlot(receiver, std::string("first"), first);
	vm.NewSlot(receiver, std::string("second"), second);
	std::size_t calls = 0;
	vm.NewSlot(first, std::string("method"), vm.CreateNativeClosure("method", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] == first && arguments[1] == second, "prepared method receiver must survive reassignment of the original local during arguments");
		++calls;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{71});
	}));
	Require(std::get<std::int32_t>(Run(vm, "local object = this.first; return object.method(object = this.second);", receiver)) == 71 && calls == 1, "method preparation must precede argument effects exactly once");
}

static void TestAssignmentAndPostIncrementEffects() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	vm.NewSlot(receiver, std::string("value"), std::int32_t{3});
	Require(std::get<std::int32_t>(Run(vm, "local left = 0, right = 0; left = right = 9; return left + right;", receiver)) == 18, "assignment must associate right-to-left through actual local registers");
	Require(std::get<std::int32_t>(Run(vm, "local old = this.value++; return old;", receiver)) == 3 && std::get<std::int32_t>(vm.GetSlot(receiver, std::string("value"))) == 4, "slot post-increment must return the old value and commit one addition");
	std::size_t key_calls = 0;
	vm.NewSlot(receiver, std::string("key"), vm.CreateNativeClosure("key", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1 && arguments[0] == receiver, "assignment key lost its receiver");
		++key_calls;
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("value"));
	}));
	Require(std::get<std::int32_t>(Run(vm, "return this[key()] = 27;", receiver)) == 27 && key_calls == 1 && std::get<std::int32_t>(vm.GetSlot(receiver, std::string("value"))) == 27, "indexed assignment must evaluate its key once and return the actual assigned value");
	const auto type = vm.CreateClass();
	std::int32_t stored = 12;
	std::string order;
	vm.NewSlot(type, std::string("_get"), vm.CreateNativeClosure("get", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && sgre::script::StringText(arguments[1]) == "count", "post-increment getter key differs");
		order += "G";
		return sgre::script::SquirrelNativeResult::ReturnValue(stored);
	}));
	vm.NewSlot(type, std::string("_set"), vm.CreateNativeClosure("set", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && sgre::script::StringText(arguments[1]) == "count", "post-increment setter key differs");
		order += "S";
		stored = std::get<std::int32_t>(arguments[2]);
		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	vm.NewSlot(receiver, std::string("proxy"), vm.CreateInstance(type));
	Require(std::get<std::int32_t>(Run(vm, "return this.proxy.count++;", receiver)) == 12 && stored == 13 && order == "GS", "compiled post-increment must use the VM getter/arithmetic/setter operation without duplicate source reads");
}

static void TestForLoopControlAndContainerEffects() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto receiver = vm.CreateTable();
	const auto result = Run(vm, "local result = []; for (local i = 0; i <= 3; i++) { local item = { number = i previous = i > 0 ? i - 1 : -1 next = i < 3 ? i + 1 : -1 }; result.append(item); } return result;", receiver);
	for (std::int32_t index = 0; index < 4; ++index) {
		const auto item = vm.GetSlot(result, index);
		Require(std::get<std::int32_t>(vm.GetSlot(item, std::string("number"))) == index && std::get<std::int32_t>(vm.GetSlot(item, std::string("previous"))) == (index == 0 ? -1 : index - 1) && std::get<std::int32_t>(vm.GetSlot(item, std::string("next"))) == (index == 3 ? -1 : index + 1), "loop must construct each independent container from the current induction value and selected arithmetic branch");
	}
	RequireFailure([&] {
		static_cast<void>(vm.GetSlot(result, std::int32_t{4}));
	},
	    "index");
	Require(std::get<std::int32_t>(Run(vm, "local sum = 0; for (local i = 0; i < 3; i++) for (local j = 0; j < 2; j++) { sum = sum + i + j; } return sum;", receiver)) == 9, "nested for iteration blocks and back jumps must preserve both loop-local registers");
	Require(std::get<std::int32_t>(Run(vm, "local value = 0; for (; value < 5; value = value + 1) ; return value;", receiver)) == 5, "empty loop body must still execute its iteration and condition");
	Require(std::get<std::int32_t>(Run(vm, "for (;;) return 37;", receiver)) == 37, "omitted condition must follow the real loop body without a fabricated completion");
	Require(std::get<std::int32_t>(Run(vm, "local value = 0; for (local i = 5; i < 0; i++) { value = 19; } return value;", receiver)) == 0, "false initial condition must skip both body and iteration");
	vm.NewSlot(receiver, std::string("i"), std::int32_t{83});
	Require(std::get<std::int32_t>(Run(vm, "for (local i = 0; i < 2; i++) {} return i;", receiver)) == 83, "loop-local scope must retire before following receiver lookup");
	Require(std::get<std::int32_t>(Run(vm, "local count = 0; for (local i = 0; i < 1000; i++) { local values = [i, i + 1]; count = count + 1; } return count;", receiver)) == 1000, "ordinary iterations must reuse bounded registers rather than allocate runtime compiler temporaries");
}

static void TestLoopSideEffectOrderAndSuspension() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	std::string order;
	vm.NewSlot(receiver, std::string("check"), vm.CreateNativeClosure("check", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		order += "C";
		return sgre::script::SquirrelNativeResult::ReturnValue(std::get<std::int32_t>(arguments[1]) < 2);
	}));
	vm.NewSlot(receiver, std::string("step"), vm.CreateNativeClosure("step", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		order += "I";
		return sgre::script::SquirrelNativeResult::ReturnValue(std::get<std::int32_t>(arguments[1]) + 1);
	}));
	vm.NewSlot(receiver, std::string("body"), vm.CreateNativeClosure("body", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		order += "B";
		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	Require(std::get<std::int32_t>(Run(vm, "local i = 0; for (; check(i); i = step(i)) body(); return i;", receiver)) == 2 && order == "CBICBIC", "compiled iteration must execute after each body although parsed before the body");
	std::size_t waits = 0;
	vm.NewSlot(receiver, std::string("wait"), vm.CreateNativeClosure("wait", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		++waits;
		return vm.RequestNativeSuspension();
	}));
	const auto closure = vm.CreateClosure(CompileSquirrelSource(vm, "local count = 0; for (local i = 0; i < 2; i++) { count = count + 1; wait(); } return count;", "suspended-loop"));
	const std::array<SquirrelValue, 1> arguments{receiver};
	Require(vm.Invoke(closure, arguments).State == sgre::script::SquirrelRunState::Suspended && waits == 1, "loop must suspend at its actual first callback");
	Require(vm.Resume().State == sgre::script::SquirrelRunState::Suspended && waits == 2, "resume must preserve induction and local state for exactly the next iteration");
	const auto completed = vm.Resume();
	Require(completed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(completed.Value) == 2 && waits == 2, "second resume must exit the original loop with retained local values");
}

static void TestLocalGrammarSafetyAndLimits() {
	SquirrelVm vm;
	vm.NewSlot(vm.ConstantTable(), std::string("IMMUTABLE"), std::int32_t{9});
	for (const auto source : {"local = 1;", "local value = ;", "for local i = 0;", "for (local i = 0 i < 2; i++) {}", "for (local i = 0; i < 2 i++) {}", "for (local i = 0; i < 2; i++ {}", "for (;;) break;", "for (;;) continue;", "return { value : 1 };", "return { [1 = 2 };", "return [1,;", "return { value = 1; };", "return 1 = 2;", "return 1++;", "return (1 + 2)++;", "local value = 1; return (value) = 3;", "IMMUTABLE = 7;", "return IMMUTABLE++;", "return ++this.value;", "return --this.value;", "return this.value--1;", "return function() :(value) { return value; };"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "local-grammar-error"));
		},
		    "local-grammar-error");
	}
	for (const auto source : {"IMMUTABLE.field = 7;", "IMMUTABLE[0] = 7;"}) {
		const auto program = CompileSquirrelSource(vm, source, "constant-slot-context");
		Require(std::count_if(program.MainFunction.Instructions.begin(), program.MainFunction.Instructions.end(), [](const auto& instruction) {
			return instruction.Opcode == 13;
		}) == 1,
		    "dereference must clear constant-binding modification status and leave actual slot/type validation to the VM");
	}
	std::string declarations;
	for (std::size_t index = 0; index < 255; ++index) {
		declarations += "local value" + std::to_string(index) + ";";
	}
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, declarations, "local-register-limit"));
	},
	    "register limit exceeded");
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, std::string(70, '{') + "17;" + std::string(70, '}'), "block-depth-limit"));
	},
	    "depth limit exceeded");
	RequireFailure([&] {
		static_cast<void>(CompileSquirrelSource(vm, "return " + std::string(70, '[') + "17" + std::string(70, ']') + ";", "container-depth-limit"));
	},
	    "depth limit exceeded");
	Require(std::get<std::int32_t>(Run(vm, "local value = 6; value++; return value + 1;", vm.RootTable())) == 8, "rejected syntax must leave compiler and VM available for a genuine later local program");
}

static void TestVariadicAndDefaultCallbacks() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	const auto program = CompileSquirrelSource(vm, "return function(value, ...) { return value + vargc + vargv[0]; };", "variadic-source");
	const auto& prototype = program.MainFunction.Functions.front();
	Require(prototype.HasVariableParameters && prototype.Parameters.size() == 2 && prototype.DefaultParameters.empty(), "variadic callback changed its fixed parameter/default prototype");
	const auto function = Run(vm, "return function(value, ...) { return value + vargc + vargv[0]; };", receiver);
	const std::array<SquirrelValue, 4> arguments{receiver, std::int32_t{10}, std::int32_t{7}, std::int32_t{99}};
	Require(std::get<std::int32_t>(vm.Invoke(function, arguments).Value) == 19, "variadic callback lost fixed argument, surplus count or indexed first argument");
	const auto empty = Run(vm, "return function(...) { return vargc; };", receiver);
	const std::array<SquirrelValue, 1> no_arguments{receiver};
	Require(std::get<std::int32_t>(vm.Invoke(empty, no_arguments).Value) == 0, "empty variadic call invented an argument");
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(function, no_arguments));
	},
	    "parameter");

	std::vector<std::int32_t> evaluated;
	vm.NewSlot(receiver, std::string("capture"), vm.CreateNativeClosure("capture_default", [&](SquirrelVm&, std::span<const SquirrelValue> actual) {
		Require(actual.size() == 2 && actual[0] == receiver, "default expression lost the enclosing receiver");
		const auto value = std::get<std::int32_t>(actual[1]);
		evaluated.push_back(value);
		return sgre::script::SquirrelNativeResult::ReturnValue(value);
	}));
	const auto defaults_program = CompileSquirrelSource(vm, "return function(required, first=capture(13), second=capture(17)) { return [required,first,second]; };", "default-source");
	Require(evaluated.empty() && defaults_program.MainFunction.Functions.front().DefaultParameters.size() == 2, "compilation executed defaults or lost their prototype capture slots");
	const auto factory = vm.CreateClosure(defaults_program);
	const auto defaults = vm.Invoke(factory, no_arguments).Value;
	Require(evaluated == std::vector<std::int32_t>{13, 17}, "defaults must execute once, in order, during closure creation");
	const std::array<SquirrelValue, 2> required{receiver, std::int32_t{5}};
	const auto omitted = vm.Invoke(defaults, required).Value;
	Require(std::get<std::int32_t>(vm.GetSlot(omitted, std::int32_t{0})) == 5 && std::get<std::int32_t>(vm.GetSlot(omitted, std::int32_t{1})) == 13 && std::get<std::int32_t>(vm.GetSlot(omitted, std::int32_t{2})) == 17, "omitted trailing parameters did not use the owned closure defaults");
	const std::array<SquirrelValue, 3> explicit_null{receiver, std::int32_t{8}, {}};
	const auto supplied = vm.Invoke(defaults, explicit_null).Value;
	Require(std::holds_alternative<std::monostate>(vm.GetSlot(supplied, std::int32_t{1})) && std::get<std::int32_t>(vm.GetSlot(supplied, std::int32_t{2})) == 17 && evaluated.size() == 2, "explicit null was treated as omission or defaults ran again on invocation");
	const auto local_default = Run(vm, "local outer = 23; return function(value=outer) { return value; };", receiver);
	Require(std::get<std::int32_t>(vm.Invoke(local_default, no_arguments).Value) == 23, "default did not capture the enclosing local through the genuine closure prototype");
	const auto false_default = Run(vm, "return function(enabled=false) { return enabled; };", receiver);
	Require(!std::get<bool>(vm.Invoke(false_default, no_arguments).Value), "boolean default did not retain its VM type/value");
	for (const auto source : {"return function(first=1,second) {};", "return function(first=1,...) {};", "return function(...,last) {};", "return function(first,...,last) {};", "return function(first=) {};"}) {
		RequireFailure([&] {
			static_cast<void>(CompileSquirrelSource(vm, source, "parameter-error"));
		},
		    "parameter-error");
	}
}

static void TestMenuArithmeticAndScopedConditionals() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	for (const auto& [source, expected] : std::array<std::pair<std::string_view, std::int32_t>, 9>{{{"return 2+3*4;", 14}, {"return 20/3*2;", 12}, {"return -7%3;", -1}, {"return 1|2^3&6;", 1}, {"return 1<<3+1;", 16}, {"return 8>>1+1;", 2}, {"return 0x80000000>>>31;", 1}, {"return -8>>2;", -2}, {"return ~0;", -1}}}) {
		Require(std::get<std::int32_t>(Run(vm, source, receiver)) == expected, "multiplicative/bitwise/shift operator precedence or native VM operation differs");
	}
	Require(std::get<bool>(Run(vm, "return (3&1)==1;", receiver)), "parenthesized flag comparison differs");
	Require(std::get<bool>(Run(vm, "return \"entry\" in {entry=9};", receiver)) && !std::get<bool>(Run(vm, "return \"absent\" in {entry=9};", receiver)), "compiled membership did not use original key/receiver ordering");
	Require(!std::get<bool>(Run(vm, "return false && \"entry\" in missing_container;", receiver)), "membership bypassed logical short circuit");
	Require(std::get<std::int32_t>(Run(vm, "local value=4; return value*(value=5);", receiver)) == 25, "multiplication incorrectly froze its native live local operand");
	Require(std::get<std::int32_t>(Run(vm, "if(false) return 1; else if(true) return 2; else return 3;", receiver)) == 2, "conditional branch selection or return patching differs");
	Require(std::get<std::int32_t>(Run(vm, "if(true) if(false) return 1; else return 2; return 3;", receiver)) == 2, "else did not bind to its nearest unpaired conditional");
	Require(std::get<std::int32_t>(Run(vm, "if(true) return 7 else return 9", receiver)) == 7, "original else statement boundary was rejected");
	vm.NewSlot(receiver, std::string("value"), std::int32_t{41});
	Require(std::get<std::int32_t>(Run(vm, "if(true) local value=3; else local value=8; return value;", receiver)) == 41, "unbraced conditional local escaped its native branch scope");
	Require(std::get<std::int32_t>(Run(vm, "local total=0; for(local i=0;i<5;i++){if(i%2==0) total=total+i; else total=total+10;} return total;", receiver)) == 26, "conditional/modulo loop control lost branch side effects");
	RequireFailure([&] {
		static_cast<void>(Run(vm, "return 7%0;", receiver));
	},
	    "zero");
}

int main() {
	try {
		TestLiteralsStatementsAndMainPrototype();
		TestCompileIsInertAndCallsPreserveReceivers();
		TestNestedFunctionsParametersAndEquality();
		TestVariadicAndDefaultCallbacks();
		TestMenuArithmeticAndScopedConditionals();
		TestConstantSnapshotsAndCurrentContextRoot();
		TestLogicalConditionalAndRootExpressions();
		TestCommaExpressionsAndArgumentBoundaries();
		TestCommaActionSuspensionAndRegisteredCompilation();
		TestRelationalOperatorsAndNativePrecedence();
		TestActualDialogThresholdCompilation();
		TestUnsupportedGrammarAndDiagnostics();
		TestNulBoundaryAndCompilerLimits();
		TestActualCompilestringRegistration();
		TestQualifiedConstantMembersAndOwnedSnapshots();
		TestActualRegisteredKeyMemberConditional();
		TestHexadecimalWordsAndPrefixBoundaries();
		TestHexadecimalDigitLimitsAndDiagnostics();
		TestRegisteredColorHexAndActualArgumentWords();
		TestScopedLocalDeclarations();
		TestContainerLiteralsAndEvaluationOrder();
		TestMutableCallArgumentsAndReceivers();
		TestAssignmentAndPostIncrementEffects();
		TestForLoopControlAndContainerEffects();
		TestLoopSideEffectOrderAndSuspension();
		TestLocalGrammarSafetyAndLimits();
		std::cout << "source compiler: real prototypes, receiver/context lookup, inert compilation, calls and grammar rejection passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
