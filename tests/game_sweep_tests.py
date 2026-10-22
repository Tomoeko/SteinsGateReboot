"""Independent crash/coverage/ownership checks for the headless game sweep."""

import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest


REPOSITORY = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("game_sweep", REPOSITORY / "tools/game_sweep.py")
SWEEP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SWEEP)


def case():
	return {"id": "fixture", "target": "fixture.ks:20", "storage": "fixture.ks", "label": "*entry", "line": 20, "point": 2, "record": 7, "jump_line": 20}


def begin():
	return {"identity": "a" * 64, "parent": "new-game", "headless": True, "hardware_audio": False, "width": 1280, "height": 720}


def event(observation, event_name, **fields):
	observation.event({"schema": 1, "sequence": observation.sequence + 1, "event": event_name, **fields})


def plan():
	return {"schema": 1, "entries": 2, "scenes": [{"storage": "fixture.ks", "label": "*entry", "terminal_exits": 0, "snapshots": [{"line": 10, "point": 1, "record": 0, "jump_min": 0, "jump_line": 10}, {"line": 20, "point": 2, "record": 7, "jump_min": 11, "jump_line": 20}, {"line": 20, "point": 3, "record": 8, "jump_min": None, "jump_line": None}], "exits": [{"storage": "next.ks", "label": "", "conditional": True}, {"storage": "missing.ks", "label": "*dynamic", "conditional": False}]}, {"storage": "next.ks", "label": "*first", "terminal_exits": 1, "snapshots": [], "exits": []}]}


class CoverageTests(unittest.TestCase):
	def test_inventory_keeps_unaddressable_and_unresolved_branches(self):
		cases = SWEEP.make_cases(plan())
		self.assertEqual(len(cases), 1)
		self.assertEqual(cases[0]["record"], 7)
		self.assertEqual(SWEEP.choose_cases(plan(), cases, ["FIXTURE.ks:20"])[0]["record"], 7)
		self.assertEqual(SWEEP.choose_cases(plan(), cases, ["fixture.ks:19"])[0]["record"], 7)
		with self.assertRaises(RuntimeError):
			SWEEP.choose_cases(plan(), cases, ["fixture.ks:21"])
		with self.assertRaises(RuntimeError):
			SWEEP.make_cases({"schema": 1, "scenes": plan()["scenes"] * 2})
		report = SWEEP.summarize(plan(), [])
		self.assertEqual(report["snapshot_records_total"], 3)
		self.assertEqual(report["unaddressable_scenes"], [("next.ks", "*first")])
		self.assertTrue(report["branches"][0]["resolved"])
		self.assertFalse(report["branches"][1]["resolved"])
		self.assertFalse(report["branches"][0]["predicate_outcome_verified"])

	def test_arrival_requires_exact_original_record_and_post_frames(self):
		observation = SWEEP.Observation(case())
		event(observation, "begin", **begin())
		event(observation, "seek", phase=3)
		event(observation, "snapshot", **case(), digest="b" * 64)
		event(observation, "frame", updates=4, draws=3)
		event(observation, "boundary", kind="observation_stopped", reason="fixture")
		self.assertEqual(observation.result(0, 1, ["fixture"])["status"], "reached")
		self.assertFalse(SWEEP.Observation(case()).result(0, 1, ["fixture"])["status"] == "reached")
		wrong = SWEEP.Observation(case())
		event(wrong, "begin", **begin())
		event(wrong, "snapshot", **{**case(), "record": 8}, digest="b" * 64)
		event(wrong, "seek", phase=3)
		event(wrong, "frame", updates=4, draws=3)
		event(wrong, "boundary", kind="observation_stopped", reason="fixture")
		self.assertEqual(wrong.result(0, 1, ["fixture"])["errors"][0]["kind"], "wrong_target")

	def test_caught_script_errors_fail_even_with_successful_exit(self):
		observation = SWEEP.Observation(case())
		event(observation, "begin", **begin())
		event(observation, "script", message="AN ERROR HAS OCCURRED [missing owner]", error=False)
		self.assertEqual(observation.result(0, 1, ["fixture"])["status"], "failed")
		observation.line(b"sgre-test {invalid}")
		self.assertEqual(observation.errors[-1]["kind"], "protocol")
		identity = SWEEP.Observation(case())
		identity.line(b"actualStartupBuildIdentity addressSanitizer=disabled undefinedBehaviorSanitizer=disabled")
		self.assertEqual(identity.errors, [])
		identity.line(b"ERROR: AddressSanitizer: heap-use-after-free")
		self.assertEqual(identity.errors[0]["kind"], "runtime")
		identity.line(b"Runtime Exception:original seek panel callback")
		self.assertEqual(identity.errors[-1]["kind"], "runtime")
		observation.line(b'sgre-test {"schema":1,"sequence":3,"event":"snapshot"}')
		self.assertEqual(observation.errors[-1]["kind"], "protocol")

	def test_genuine_input_block_is_not_branch_success(self):
		observation = SWEEP.Observation(case())
		event(observation, "begin", **begin())
		event(observation, "seek", phase=4, reason="original selection requires genuine route input")
		self.assertEqual(observation.result(0, 1, ["fixture"])["status"], "blocked")
		bad = SWEEP.Observation(case())
		event(bad, "begin", **begin())
		event(bad, "seek", phase=4, reason="original seek player retired before the requested snapshot")
		self.assertEqual(bad.result(0, 1, ["fixture"])["status"], "failed")

	def test_retirement_fault_remains_a_failed_check_with_runtime_result(self):
		observation = SWEEP.Observation(case())
		event(observation, "begin", **begin())
		event(observation, "snapshot", **case(), digest="b" * 64)
		event(observation, "seek", phase=3)
		event(observation, "frame", updates=4, draws=3)
		event(observation, "boundary", kind="observation_stopped", reason="fixture")
		observation.line(b"original startup failed: fixture teardown owner")
		result = observation.result(1, 1, ["fixture"])
		self.assertEqual(result["status"], "retirement_failed")
		self.assertTrue(result["runtime_reached"])
		self.assertEqual(result["errors"][0]["kind"], "retirement")

	def test_transition_counts_do_not_claim_predicate_coverage(self):
		result = {"case": case(), "status": "reached", "snapshots": [{**case(), "digest": "a" * 64}, {**case(), "storage": "next.ks", "label": "*first", "digest": "b" * 64}], "errors": [], "command": ["fixture"]}
		report = SWEEP.summarize(plan(), [result])
		self.assertEqual(report["transitions_observed"], 1)
		self.assertEqual(report["snapshot_records_observed"], 1)
		self.assertEqual(len(report["unrecognized_snapshot_receipts"]), 1)
		self.assertTrue(report["branches"][0]["transition_observed"])
		self.assertFalse(report["branches"][0]["predicate_outcome_verified"])

	def test_largest_scenario_family_runs_first_without_name_rules(self):
		inventory = plan()
		extra = {"storage": "auxiliary.ks", "label": "*isolated", "snapshots": [{"line": 1, "point": 1, "record": 0, "jump_line": 1, "jump_min": 0}], "exits": []}
		inventory["scenes"].insert(0, extra)
		cases = SWEEP.make_cases(inventory)
		self.assertEqual(cases[0]["storage"], "fixture.ks")
		self.assertEqual(cases[-1]["storage"], "auxiliary.ks")

	def test_output_guard_rejects_preserved_installation_and_user_state(self):
		installation = REPOSITORY / "Files/STEINS.GATE.REBOOT"
		for output in (REPOSITORY / "Files", installation / "captures", REPOSITORY / "Files/saves/test", REPOSITORY / "Files/checkpoints/test", REPOSITORY / "Files/build/test", REPOSITORY / "public-output"):
			with self.assertRaises(RuntimeError):
				SWEEP.require_output(REPOSITORY, output, installation)
		self.assertEqual(SWEEP.require_output(REPOSITORY, REPOSITORY / "Files/testing/owned", installation), REPOSITORY / "Files/testing/owned")

	def test_replay_pins_original_identity_and_route_ancestry(self):
		root = REPOSITORY / "Files/testing/sweep-unit"
		root.mkdir(parents=True, exist_ok=True)
		with tempfile.TemporaryDirectory(dir=root) as temporary:
			path = Path(temporary) / "result.json"
			record = {"case": case(), "identity": "a" * 64, "parent": "new-game", "status": "failed"}
			path.write_text(json.dumps(record))
			options = SimpleNamespace(replay=path, retry_from=None, target=[])
			targets, parents = SWEEP.prior_queue(options, {"resource_identity": "a" * 64})
			self.assertEqual(targets, ["fixture.ks:20"])
			self.assertEqual(parents["fixture"], "new-game")
			with self.assertRaises(RuntimeError):
				SWEEP.prior_queue(options, {"resource_identity": "b" * 64})
			record["parent"] = None
			path.write_text(json.dumps(record))
			with self.assertRaises(RuntimeError):
				SWEEP.prior_queue(options, {"resource_identity": "a" * 64})


class ProcessTests(unittest.TestCase):
	def test_runner_termination_reaps_its_owned_child(self):
		root = REPOSITORY / "Files/testing/sweep-unit"
		root.mkdir(parents=True, exist_ok=True)
		with tempfile.TemporaryDirectory(dir=root) as temporary:
			directory = Path(temporary)
			pid_path = directory / "owned-child.pid"
			child = [sys.executable, "-c", "import os,pathlib,sys,time;pathlib.Path(sys.argv[1]).write_text(str(os.getpid()));time.sleep(60)", str(pid_path)]
			code = "import importlib.util,json,pathlib,signal,sys\ns=importlib.util.spec_from_file_location('sweep',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)\nsignal.signal(signal.SIGTERM,m.interrupt_runner)\ntry:m.observe(json.loads(sys.argv[2]),json.loads(sys.argv[3]),pathlib.Path(sys.argv[4]),70,1)\nexcept KeyboardInterrupt:sys.exit(130)\n"
			process = subprocess.Popen([sys.executable, "-c", code, str(REPOSITORY / "tools/game_sweep.py"), json.dumps(child), json.dumps(case()), str(directory)])
			try:
				deadline = time.monotonic() + 3
				while not pid_path.exists() and time.monotonic() < deadline:
					time.sleep(0.01)
				self.assertTrue(pid_path.exists(), "owned fixture child did not start")
				child_pid = int(pid_path.read_text())
				process.send_signal(signal.SIGTERM)
				self.assertEqual(process.wait(timeout=3), 130)
				with self.assertRaises(ProcessLookupError):
					os.kill(child_pid, 0)
			finally:
				if process.poll() is None:
					process.kill()
					process.wait()

	def test_actual_owned_pipe_lifecycle(self):
		root = REPOSITORY / "Files/testing/sweep-unit"
		root.mkdir(parents=True, exist_ok=True)
		with tempfile.TemporaryDirectory(dir=root) as temporary:
			directory = Path(temporary)
			prefix = [{"event": "begin", **begin()}, {"event": "seek", "phase": 3}, {"event": "snapshot", **case(), "digest": "c" * 64}, {"event": "frame", "updates": 5, "draws": 5}]
			script = "import json,sys\nevents=json.loads(sys.argv[1])\nsequence=0\nfor event in events:\n sequence+=1; print('sgre-test '+json.dumps(dict(schema=1,sequence=sequence,**event)),flush=True)\nwhile sys.stdin.read(1)!='t': pass\nsequence+=1; print('sgre-test '+json.dumps(dict(schema=1,sequence=sequence,event='boundary',kind='observation_stopped',reason='fixture')),flush=True)\n"
			command = [sys.executable, "-c", script, json.dumps(prefix)]
			result = SWEEP.observe(command, case(), directory, 3, 0.1)
			self.assertEqual(result["status"], "reached")
			self.assertEqual(result["returncode"], 0)
			self.assertTrue((directory / "runtime.log.gz").is_file())

	def test_owned_crash_and_timeout_never_pass(self):
		root = REPOSITORY / "Files/testing/sweep-unit"
		root.mkdir(parents=True, exist_ok=True)
		with tempfile.TemporaryDirectory(dir=root) as temporary:
			directory = Path(temporary)
			crash = directory / "crash"
			crash.mkdir()
			result = SWEEP.observe([sys.executable, "-c", "import os,signal;os.kill(os.getpid(),signal.SIGKILL)"], case(), crash, 2, 0.1)
			self.assertEqual(result["status"], "crashed")
			hang = directory / "hang"
			hang.mkdir()
			result = SWEEP.observe([sys.executable, "-c", "import time;time.sleep(60)"], case(), hang, 0.2, 0.1)
			self.assertEqual(result["status"], "failed")
			self.assertEqual(result["errors"][0]["kind"], "timeout")


if __name__ == "__main__":
	unittest.main()
