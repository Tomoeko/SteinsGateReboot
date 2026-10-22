"""Run resumable, isolated headless checks over the original scenario graph."""

import argparse
from collections import Counter
import fcntl
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shlex
import signal
import subprocess
import sys
import time


PREFIX = b"sgre-test "
MAX_LOG_BYTES = 64 * 1024 * 1024
MAX_LINE_BYTES = 2 * 1024 * 1024
ERROR_TEXT = re.compile(r"AN ERROR HAS OCCURRED|unhandled Squirrel exception|Runtime Exception:|Script ERROR|script error:|actualThreadCompletion .*nativeResult=-1|(?:ERROR|SUMMARY): AddressSanitizer|UndefinedBehaviorSanitizer:|runtime error:", re.I)


def digest_file(path):
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def json_bytes(value):
	return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def write_json(path, value):
	temporary = path.with_suffix(path.suffix + ".pending")
	with temporary.open("x", encoding="utf-8") as stream:
		json.dump(value, stream, indent=2, ensure_ascii=False)
		stream.write("\n")
	os.replace(temporary, path)


def location_key(value):
	return (value["storage"], value["label"], value["line"], value["point"], value["record"])


def scene_key(value):
	return (value["storage"], value["label"])


def make_cases(plan):
	if plan.get("schema") != 1 or not isinstance(plan.get("scenes"), list):
		raise RuntimeError("unsupported or incomplete original scenario test plan")
	cases = []
	seen = set()
	for scene in plan["scenes"]:
		identity = scene_key(scene)
		if identity in seen:
			raise RuntimeError("duplicate original scene identity in test plan")
		seen.add(identity)
		addressable = [row for row in scene["snapshots"] if row["jump_line"] is not None]
		if not addressable:
			continue
		# One final addressable record exercises the path toward each scene's exit;
		# all intermediate snapshot receipts remain in the coverage inventory.
		row = addressable[-1]
		case = {**row, "storage": scene["storage"], "label": scene["label"]}
		case["target"] = scene["storage"] + ":" + str(row["jump_line"])
		case["id"] = hashlib.sha256(json_bytes(location_key(case))).hexdigest()[:20]
		cases.append(case)
	# Run the largest connected scenario family first, retaining archive order
	# within it. Auxiliary registration/sample resources remain in the queue.
	adjacent = {scene_key(scene): set() for scene in plan["scenes"]}
	first = {}
	for scene in plan["scenes"]:
		first.setdefault(scene["storage"], scene_key(scene))
	for scene in plan["scenes"]:
		for exit in scene["exits"]:
			destination = (exit["storage"], exit["label"]) if exit["label"] else first.get(exit["storage"])
			if destination in adjacent:
				adjacent[scene_key(scene)].add(destination)
				adjacent[destination].add(scene_key(scene))
	component_size = {}
	for identity in adjacent:
		if identity in component_size:
			continue
		pending = [identity]
		component = set()
		while pending:
			current = pending.pop()
			if current not in component:
				component.add(current)
				pending.extend(adjacent[current] - component)
		component_size.update({member: len(component) for member in component})
	return sorted(cases, key=lambda case: -component_size[scene_key(case)])


def choose_cases(plan, cases, targets):
	if not targets:
		return cases
	selected = []
	for target in targets:
		storage, separator, line_text = target.rpartition(":")
		if not separator or not line_text.isdecimal():
			raise RuntimeError("test target requires SCENE.ks:LINE")
		line = int(line_text)
		# These intervals invert the original line resolver, including boundaries
		# and records whose authored line repeats or decreases.
		matches = []
		for scene in plan["scenes"]:
			if scene["storage"].lower() == storage.lower():
				for row in scene["snapshots"]:
					if row["jump_line"] is not None and row["jump_min"] <= line <= row["jump_line"]:
						matches.append({**row, "storage": scene["storage"], "label": scene["label"]})
		if len(matches) != 1:
			raise RuntimeError("--target does not resolve to an addressable original snapshot: " + target)
		case = matches[0]
		case["target"] = case["storage"] + ":" + str(line)
		case["id"] = hashlib.sha256(json_bytes(location_key(case))).hexdigest()[:20]
		selected.append(case)
	return list({case["id"]: case for case in selected}.values())


class Observation:
	def __init__(self, case):
		self.case = case
		self.sequence = 0
		self.started = False
		self.target_seen = False
		self.arrived = False
		self.arrival_time = None
		self.frame_before_arrival = 0
		self.draws = 0
		self.post_draws = 0
		self.snapshots = []
		self.errors = []
		self.blocked = None
		self.boundary = None
		self.retirement = []
		self.last_progress = time.monotonic()

	def fail(self, kind, reason):
		if not any(error["kind"] == kind and error["reason"] == reason for error in self.errors):
			self.errors.append({"kind": kind, "reason": reason[:4096]})

	def line(self, line):
		if line.startswith(PREFIX):
			try:
				event = json.loads(line[len(PREFIX):])
			except (UnicodeError, ValueError) as error:
				self.fail("protocol", "invalid JSON test receipt: " + str(error))
				return
			try:
				self.event(event)
			except (KeyError, TypeError, ValueError) as error:
				self.fail("protocol", "malformed test receipt fields: " + str(error))
			return
		text = line.decode("utf-8", errors="replace").strip()
		if "original script retirement:" in text:
			self.retirement.append(text[:4096])
			self.fail("retirement", text)
		elif "original startup failed:" in text and self.boundary and self.boundary.get("kind") == "observation_stopped":
			self.fail("retirement", text)
		elif ERROR_TEXT.search(text) or "original startup failed:" in text:
			self.fail("runtime", text)

	def event(self, event):
		if event.get("schema") != 1 or event.get("sequence") != self.sequence + 1:
			self.fail("protocol", "lost, duplicated or unsupported test receipt")
			return
		self.sequence += 1
		kind = event.get("event")
		if kind == "begin":
			if self.started or not event.get("headless") or event.get("hardware_audio") is not False or (event.get("width"), event.get("height")) != (1280, 720):
				self.fail("protocol", "test receipt lacks exclusive hidden/silent 720p policy")
			self.started = True
			self.identity = event["identity"]
			self.parent = event["parent"]
		elif not self.started:
			self.fail("protocol", "test activity preceded its begin receipt")
		elif kind == "snapshot":
			try:
				key = location_key(event)
				if not re.fullmatch(r"[0-9a-f]{64}", event["digest"]):
					raise ValueError("invalid checkpoint digest")
			except (KeyError, TypeError, ValueError) as error:
				self.fail("protocol", "invalid original snapshot receipt: " + str(error))
				return
			self.snapshots.append(event)
			self.target_seen |= key == location_key(self.case)
			self.last_progress = time.monotonic()
		elif kind == "seek":
			phase = event.get("phase")
			if phase == 3:
				self.arrived = True
				self.arrival_time = time.monotonic()
				self.frame_before_arrival = self.draws
			elif phase == 4:
				self.blocked = event
				if "player was replaced" in event.get("reason", "") or "player retired" in event.get("reason", ""):
					self.fail("unexpected_player_exit", event["reason"])
			elif phase in (5, 6):
				self.fail("seek", event.get("reason", "original seek failed or cancelled"))
		elif kind == "frame":
			self.draws = event["draws"]
			if self.arrived:
				self.post_draws = self.draws - self.frame_before_arrival
		elif kind == "script":
			message = event.get("message", "")
			if event.get("error") or ERROR_TEXT.search(message):
				self.fail("script", message)
		elif kind == "boundary":
			self.boundary = event
			if event.get("kind") == "runtime_boundary":
				self.fail("runtime", event.get("reason", "runtime boundary"))
		else:
			self.fail("protocol", "unknown test receipt kind: " + str(kind))

	def result(self, returncode, elapsed, command):
		if self.arrived and not self.target_seen:
			self.fail("wrong_target", "seek arrival lacks the exact storage/label/line/point/record receipt")
		runtime_reached = self.arrived and self.target_seen and self.post_draws > 0 and not any(error["kind"] != "retirement" for error in self.errors)
		if self.errors and all(error["kind"] == "retirement" for error in self.errors):
			status = "retirement_failed"
		elif self.errors:
			status = "failed"
		elif self.blocked:
			status = "blocked"
		elif returncode != 0:
			status = "crashed" if returncode is not None and returncode < 0 else "failed"
		elif not self.started or not self.boundary:
			status = "incomplete"
		elif not self.arrived or not self.target_seen or self.post_draws <= 0:
			status = "incomplete"
		else:
			status = "reached"
		return {"case": self.case, "status": status, "runtime_reached": runtime_reached, "seconds": round(elapsed, 3), "returncode": returncode, "command": command, "identity": getattr(self, "identity", None), "parent": getattr(self, "parent", None), "target_seen": self.target_seen, "arrived": self.arrived, "post_draws": self.post_draws, "snapshots": self.snapshots, "errors": self.errors, "blocked": self.blocked, "boundary": self.boundary, "retirement_warnings": self.retirement}


def stop_process(process):
	# Only the explicitly created process group is signalled, including owned workers.
	try:
		os.killpg(process.pid, signal.SIGTERM)
	except ProcessLookupError:
		pass
	try:
		process.wait(timeout=3)
	except subprocess.TimeoutExpired:
		try:
			os.killpg(process.pid, signal.SIGKILL)
		except ProcessLookupError:
			pass
		process.wait(timeout=3)


def interrupt_runner(_signal, _frame):
	# Raise through the observation's finally block so its owned group is reaped.
	raise KeyboardInterrupt


def observe(command, case, directory, timeout, post_seconds):
	observation = Observation(case)
	started = time.monotonic()
	process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True, bufsize=0)
	selector = selectors.DefaultSelector()
	selector.register(process.stdout, selectors.EVENT_READ)
	pending = bytearray()
	log_bytes = 0
	stop_sent = None
	next_return = started
	last_flush = started
	try:
		with gzip.open(directory / "runtime.log.gz", "xb") as log:
			while selector.get_map():
				now = time.monotonic()
				if now - started > timeout:
					observation.fail("timeout", "owned test exceeded its wall limit before a complete receipt")
					break
				want_stop = bool(observation.errors or observation.blocked)
				if observation.arrival_time is not None:
					want_stop |= now - observation.arrival_time >= post_seconds
				if want_stop and stop_sent is None and process.poll() is None:
					process.stdin.write(b"t")
					stop_sent = now
				elif stop_sent is not None and now - stop_sent > 8:
					observation.fail("shutdown", "owned test did not acknowledge stop/finish retirement within eight seconds")
					break
				elif observation.arrived and stop_sent is None and now >= next_return and process.poll() is None:
					process.stdin.write(b"r")
					next_return = now + 0.35
				for key, _ in selector.select(timeout=0.1):
					block = os.read(key.fd, 65536)
					if not block:
						selector.unregister(key.fileobj)
						continue
					log_bytes += len(block)
					if log_bytes > MAX_LOG_BYTES:
						observation.fail("output_limit", "owned test exceeded its retained log limit")
						break
					log.write(block)
					if now - last_flush >= 1:
						log.flush()
						last_flush = now
					pending.extend(block)
					while b"\n" in pending:
						line, _, remainder = pending.partition(b"\n")
						pending = remainder
						observation.line(line)
					if len(pending) > MAX_LINE_BYTES:
						observation.fail("output_limit", "owned test produced an unbounded partial log line")
						break
				if log_bytes > MAX_LOG_BYTES or len(pending) > MAX_LINE_BYTES:
					break
			if pending and len(pending) <= MAX_LINE_BYTES:
				observation.line(pending)
	except BrokenPipeError:
		observation.fail("transport", "owned game exited before accepting the queued test input")
	finally:
		if process.poll() is None:
			stop_process(process)
		else:
			# A child that exits its parent must not outlive this test's process group.
			stop_process(process)
		selector.close()
		process.stdin.close()
		process.stdout.close()
	return observation.result(process.returncode, time.monotonic() - started, command)


def summarize(plan, results):
	snapshots = set()
	edges = set()
	groups = {}
	for result in results:
		previous = None
		for snapshot in result["snapshots"]:
			snapshots.add(location_key(snapshot))
			current = scene_key(snapshot)
			if previous is not None and previous != current:
				edges.add((previous, current))
			previous = current
		for error in result["errors"]:
			# Retain script source/PC; normalize volatile native addresses only.
			reason = re.sub(r"0x[0-9a-fA-F]{8,16}", "<address>", error["reason"])
			key = hashlib.sha256(json_bytes((error["kind"], reason))).hexdigest()[:16]
			group = groups.setdefault(key, {"kind": error["kind"], "reason": reason, "cases": [], "reproduce": shlex.join(result.get("reproduce", result["command"]))})
			group["cases"].append(result["case"]["target"])
	inventory = {location_key({**row, "storage": scene["storage"], "label": scene["label"]}) for scene in plan["scenes"] for row in scene["snapshots"]}
	unknown_snapshots = sorted(snapshots - inventory)
	snapshots &= inventory
	known_scenes = {scene_key(scene) for scene in plan["scenes"]}
	first_scenes = {}
	for scene in plan["scenes"]:
		first_scenes.setdefault(scene["storage"], scene_key(scene))
	branch_edges = []
	for scene in plan["scenes"]:
		for index, exit in enumerate(scene["exits"]):
			destination = (exit["storage"], exit["label"]) if exit["label"] else first_scenes.get(exit["storage"])
			branch_edges.append({"source": scene_key(scene), "ordinal": index, "destination": destination, "conditional": exit["conditional"], "resolved": destination in known_scenes, "transition_observed": (scene_key(scene), destination) in edges, "predicate_outcome_verified": False})
	planned = {case["id"] for case in make_cases(plan)}
	completed = {result["case"]["id"] for result in results}
	return {"schema": 1, "cases": len(results), "statuses": dict(Counter(result["status"] for result in results)), "planned_scene_checks": len(planned), "remaining_scene_checks": len(planned - completed), "runtime_reached_cases": sum(result.get("runtime_reached", False) for result in results), "snapshot_records_observed": len(snapshots), "snapshot_records_total": len(inventory), "unrecognized_snapshot_receipts": unknown_snapshots, "scene_ids_observed": len({key[:2] for key in snapshots}), "scene_ids_total": len(plan["scenes"]), "transitions_observed": len(edges), "branches": branch_edges, "failures": groups, "unaddressable_scenes": [scene_key(scene) for scene in plan["scenes"] if not any(row["jump_line"] is not None for row in scene["snapshots"])], "qualification": "Original sequential skip-path execution plus a short normal-frame probe after arrival. Counts do not prove alternate route states, predicate truth, menu coverage, normal timing, pixels, audio fidelity or complete game recovery."}


def require_output(repository, output, installation):
	output = output.resolve()
	files = (repository / "Files").resolve()
	if not output.is_relative_to(files) or output == files or output.is_relative_to(installation) or installation.is_relative_to(output):
		raise RuntimeError("sweep output must be a dedicated directory under ignored Files outside the installation")
	for protected in (files / "saves", files / "checkpoints", files / "build", files / "tools"):
		if output.is_relative_to(protected) or protected.is_relative_to(output):
			raise RuntimeError("sweep output overlaps an existing state/build namespace")
	return output


def protected_state(repository, user_saves):
	files = repository / "Files"
	state = {}
	for name in ("config.json", "config.json.last-good"):
		path = files / name
		state[name] = (digest_file(path), path.stat().st_mtime_ns) if path.exists() else None
	for name, directory, pattern in (("saves", user_saves, "*.bin"), ("checkpoints", files / "checkpoints", "*.sgcp")):
		entries = []
		for path in sorted(directory.rglob(pattern)):
			if path.is_file():
				status = path.stat()
				entries.append((str(path.relative_to(directory)), status.st_size, status.st_mtime_ns))
		state[name] = hashlib.sha256(json_bytes(entries)).hexdigest()
	return state


def check_storage(repository):
	additional = 0
	for root, directories, files in os.walk(repository / "Files", followlinks=False):
		directories[:] = [name for name in directories if not (Path(root) / name).is_symlink() and Path(root) / name != repository / "Files/STEINS.GATE.REBOOT"]
		for name in files:
			path = Path(root) / name
			if not path.is_symlink():
				status = path.stat()
				additional += max(status.st_size, getattr(status, "st_blocks", 0) * 512)
	if additional > 10_000_000_000:
		raise RuntimeError("generated build/research footprint exceeds the configured 10GB cap")
	return additional


def suite_pins(repository, build, installation, plan, options):
	paths = [build / "sgre-game", build / "sgre-inspect", Path(__file__).resolve()]
	paths += sorted(build.glob("*.dylib"))
	paths += sorted((repository / "shaders/metal").glob("*.metal"))
	paths += [installation / name for name in ("sgre_steam.exe", "wind3d11data/script_body.bin", "wind3d11data/scenario_body.bin")]
	paths += sorted((installation / "wind3d11data").glob("*_info.psb.m"))
	paths.append(repository / "Files/config.json")
	paths.append(repository / "tools/limit_file_output.py")
	identity = hashlib.sha256()
	for name in ("sgre_steam.exe", "wind3d11data/script_info.psb.m", "wind3d11data/script_body.bin", "wind3d11data/scenario_info.psb.m", "wind3d11data/scenario_body.bin"):
		identity.update(name.encode() + b"\0" + digest_file(installation / name).encode())
	return {"schema": 1, "pins": {str(path.relative_to(repository)) if path.is_relative_to(repository) else str(path): digest_file(path) for path in paths}, "resource_identity": identity.hexdigest(), "plan": hashlib.sha256(json_bytes(plan)).hexdigest(), "timeout": options.timeout, "post_seconds": options.post_seconds, "cold": options.cold}


def prior_queue(options, manifest):
	if not options.retry_from and not options.replay:
		return options.target, {}
	if options.replay:
		records = [json.loads(options.replay.read_text())]
	else:
		prior = options.retry_from.resolve()
		if json.loads((prior / "suite.json").read_text())["plan"] != manifest["plan"]:
			raise RuntimeError("prior failure inventory differs from the current original scenario plan")
		records = [json.loads(path.read_text()) for path in sorted(prior.glob("cases/*/result.json"))]
	latest = {result["case"]["id"]: result for result in records}
	selected = [result for result in latest.values() if options.replay or result["status"] != "reached"]
	for result in selected:
		if result["identity"] != manifest["resource_identity"] or result["parent"] is None:
			raise RuntimeError("retained failure lacks matching original resource identity and authentic predecessor; use --target for a fresh observation")
	return [result["case"]["target"] for result in selected], {result["case"]["id"]: result["parent"] for result in selected}


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build", type=Path, default=Path("Files/build/game-release"))
	parser.add_argument("--game-root", type=Path)
	parser.add_argument("--output", type=Path, default=Path("Files/testing/game-sweep"))
	parser.add_argument("--target", action="append", default=[], help="original SCENE.ks:LINE; may repeat")
	parser.add_argument("--limit", type=int, help="bound the number of queued scene checks this invocation")
	parser.add_argument("--timeout", type=float, default=180)
	parser.add_argument("--post-seconds", type=float, default=2)
	parser.add_argument("--plan-only", action="store_true")
	parser.add_argument("--retry-failures", action="store_true")
	parser.add_argument("--retry-from", type=Path, help="check the failures from an older build's suite in this fresh output directory")
	parser.add_argument("--replay", type=Path, help="reproduce one retained result with its pinned original predecessor and fresh isolated saves")
	parser.add_argument("--cold", action="store_true", help="start from the original new game instead of using a retained predecessor; checks full traversal lifetimes")
	parser.add_argument("--keep-going", action="store_true", help="continue past a runtime failure instead of stopping at its first reproducible cause")
	options = parser.parse_args()
	if not 5 <= options.timeout <= 900 or not 0.5 <= options.post_seconds <= 60 or options.post_seconds >= options.timeout - 2 or (options.limit is not None and options.limit < 1):
		parser.error("timeout requires5..900 seconds, post-seconds0.5..60 below timeout-2, limit must be positive")
	if sum(bool(value) for value in (options.retry_from, options.replay, options.target)) > 1:
		parser.error("--retry-from, --replay and --target select different queues; use one")
	if options.cold and (options.retry_from or options.replay):
		parser.error("--cold selects new-game ancestry; replay and retry-from retain the earlier recorded ancestry")
	repository = Path(__file__).resolve().parent.parent
	os.chdir(repository)
	build = options.build.resolve()
	if not build.is_relative_to(repository / "Files"):
		raise RuntimeError("build must be inside this repository's ignored Files directory")
	configuration = json.loads((repository / "Files/config.json").read_text())
	installation = (options.game_root or Path(configuration["installation"])).resolve()
	output = require_output(repository, options.output, installation)
	user_saves = (repository / configuration["save_directory"]).resolve()
	if output.is_relative_to(user_saves) or user_saves.is_relative_to(output):
		raise RuntimeError("sweep output overlaps the user's configured saves")
	command = [str(build / "sgre-inspect"), "test-plan", "--game-root", str(installation)]
	plan = json.loads(subprocess.check_output(command, timeout=60))
	manifest = suite_pins(repository, build, installation, plan, options)
	if output.exists():
		if not (output / "suite.json").is_file() or json.loads((output / "suite.json").read_text()) != manifest:
			raise RuntimeError("existing suite has different inputs/settings; choose a new --output directory")
	else:
		output.mkdir(parents=True)
		write_json(output / "suite.json", manifest)
		write_json(output / "plan.json", plan)
	targets, predecessors = prior_queue(options, manifest)
	cases = choose_cases(plan, make_cases(plan), targets) if targets or not options.retry_from else []
	if options.cold:
		predecessors = {case["id"]: "new-game" for case in cases}
	if options.plan_only:
		print(str(len(plan["scenes"])) + " scenes, " + str(sum(len(scene["snapshots"]) for scene in plan["scenes"])) + " snapshot records, " + str(len(cases)) + " queued scene targets")
		return 0
	check_storage(repository)
	with (output / ".suite.lock").open("a") as lease:
		try:
			fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
		except BlockingIOError as error:
			raise RuntimeError("another runner already owns this test suite") from error
		return run_queue(repository, build, installation, output, user_saves, plan, cases, options, predecessors)


def run_queue(repository, build, installation, output, user_saves, plan, cases, options, predecessors):
	inventory = {location_key({**row, "storage": scene["storage"], "label": scene["label"]}) for scene in plan["scenes"] for row in scene["snapshots"]}
	results = [json.loads(path.read_text()) for path in sorted(output.glob("cases/*/result.json"))]
	latest = {result["case"]["id"]: result for result in results}
	queue = [case for case in cases if case["id"] not in latest or (options.retry_failures and latest[case["id"]]["status"] != "reached")]
	if options.limit:
		queue = queue[:options.limit]
	for index, case in enumerate(queue, 1):
		directory = output / "cases" / (case["id"] + "-" + str(time.time_ns()))
		directory.mkdir(parents=True)
		run = [str(build / "sgre-game"), str(installation), "--checkpoint-worker", case["target"], "--headless", "--test-events", "--maximum-milliseconds", str(min(900000, int(options.timeout * 1000))), "--maximum-iterations", "100000", "--save-root", str(directory / "saves")]
		if case["id"] in predecessors:
			run.extend(["--test-parent", predecessors[case["id"]]])
		before = protected_state(repository, user_saves)
		bounded_run = [sys.executable, str(repository / "tools/limit_file_output.py"), "10000000000", *run]
		result = observe(bounded_run, case, directory, options.timeout + 3, options.post_seconds)
		result["reproduce"] = [sys.executable, str(Path(__file__).resolve()), "--build", str(build), "--game-root", str(installation), "--replay", str(directory / "result.json"), "--output", str(directory / "replay"), "--retry-failures", "--timeout", str(options.timeout), "--post-seconds", str(options.post_seconds)]
		if any(location_key(receipt) not in inventory for receipt in result["snapshots"]):
			result["errors"].append({"kind": "invalid_snapshot", "reason": "original runtime receipt does not match the supplied storage/label/line/point/record inventory"})
			result["status"] = "failed"
		result["protected_state_unchanged"] = before == protected_state(repository, user_saves)
		if not result["protected_state_unchanged"]:
			result["errors"].append({"kind": "state_change", "reason": "configuration, user saves or shared checkpoint namespace changed during the isolated observation; external writes cannot be attributed automatically"})
			result["status"] = "failed"
		write_json(directory / "result.json", result)
		latest[case["id"]] = result
		summary = summarize(plan, list(latest.values()))
		write_json(output / "summary.json", summary)
		print(f"[{index}/{len(queue)}] {case['target']}: {result['status']} ({result['seconds']}s); {summary['snapshot_records_observed']}/{summary['snapshot_records_total']} snapshot records observed", flush=True)
		if not result["protected_state_unchanged"]:
			raise RuntimeError("protected state changed; test queue stopped after retaining the result")
		if index % 20 == 0:
			check_storage(repository)
		if not options.keep_going and any(error["kind"] != "retirement" for error in result["errors"]):
			print("Stopped at the first runtime failure; remaining checks stay queued. Fix its cause, then use --retry-failures.", flush=True)
			break
	if not queue:
		write_json(output / "summary.json", summarize(plan, list(latest.values())))
	print("Report: " + str((output / "summary.json").relative_to(repository)))
	check_storage(repository)
	if any(result["status"] not in ("reached", "blocked") for result in latest.values()):
		return 1
	return 2 if any(result["status"] == "blocked" for result in latest.values()) else 0


if __name__ == "__main__":
	signal.signal(signal.SIGTERM, interrupt_runner)
	try:
		sys.exit(main())
	except KeyboardInterrupt:
		print("game sweep: interrupted; owned observation processes stopped, unfinished checks remain queued", file=sys.stderr)
		sys.exit(130)
	except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
		print("game sweep: " + str(error), file=sys.stderr)
		sys.exit(1)
