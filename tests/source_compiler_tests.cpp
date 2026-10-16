#include "sgre/script/source_compiler.hpp"

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
	for (const auto source : {"return !;", "return true ? 1;", "return true ? : 1;", "return false ||;", "return ::;", "return ::this;", "return 1 & 2;", "return 1 | 2;"}) {
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
	for (const auto source : {"return 1<;", "return 1>=;", "return 1<<2;", "return 1>>2;", "return 1<=>2;"}) {
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
	for (const auto source : {"return (1;", "return \"unterminated;", "return this.;", "return this[;", "return function(value,,other) { return value; };", "return function(value,value) { return value; };", "return function() :(outer) { return outer; };", "local value = 1; return value;", "this.value = 1;", "return 1 + 2;", "if (true) return 1;", "while (true) {}", "return function(value) { return value;", "return 2147483648;"}) {
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

int main() {
	try {
		TestLiteralsStatementsAndMainPrototype();
		TestCompileIsInertAndCallsPreserveReceivers();
		TestNestedFunctionsParametersAndEquality();
		TestConstantSnapshotsAndCurrentContextRoot();
		TestLogicalConditionalAndRootExpressions();
		TestCommaExpressionsAndArgumentBoundaries();
		TestCommaActionSuspensionAndRegisteredCompilation();
		TestRelationalOperatorsAndNativePrecedence();
		TestActualDialogThresholdCompilation();
		TestUnsupportedGrammarAndDiagnostics();
		TestNulBoundaryAndCompilerLimits();
		TestActualCompilestringRegistration();
		std::cout << "source compiler: real prototypes, receiver/context lookup, inert compilation, calls and grammar rejection passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
