"""Build recovery fixtures sequentially and retire their linked intermediates."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree


JOB_BYTES_LIMIT = 10_000_000_000
ADDITIONAL_BYTES_LIMIT = 10_000_000_000


def file_digest(path):
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(65536), b""):
			digest.update(block)
	return digest.hexdigest()


def directory_bytes(directory):
	return sum(storage_bytes(path) for path in directory.rglob("*") if not path.is_symlink() and path.is_file())


def storage_bytes(path):
	status = path.stat()
	return max(status.st_size, getattr(status, "st_blocks", 0) * 512)


def check_storage(repository, build):
	job_bytes = directory_bytes(build)
	additional_bytes = 0
	for path in (repository / "Files").iterdir():
		if path.name == "STEINS.GATE.REBOOT" or path.is_symlink():
			continue
		additional_bytes += directory_bytes(path) if path.is_dir() else storage_bytes(path)
	if job_bytes > JOB_BYTES_LIMIT or additional_bytes > ADDITIONAL_BYTES_LIMIT:
		raise RuntimeError("validation storage exceeds the configured research budget")
	return {"build_bytes": job_bytes, "additional_bytes": additional_bytes}


def configured_build(repository, requested):
	build = requested.resolve()
	try:
		build.relative_to(repository / "Files")
	except ValueError as error:
		raise RuntimeError("validation build must be inside the ignored Files directory") from error
	cache = build / "CMakeCache.txt"
	if not cache.is_file() or not (build / "build.ninja").is_file():
		raise RuntimeError("configure this repository with the Ninja generator first")
	home = next((line.split("=", 1)[1] for line in cache.read_text().splitlines() if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")), None)
	if home is None or Path(home).resolve() != repository:
		raise RuntimeError("build directory belongs to another source repository")
	return build


def fixture_targets(build):
	output = subprocess.check_output(["ninja", "-C", str(build), "-t", "targets", "all"], text=True)
	pattern = re.compile(r"^(?P<name>[A-Za-z0-9_]+_tests)(?:\.exe)?: (?:CXX|OBJCXX)_EXECUTABLE_LINKER")
	targets = []
	for line in output.splitlines():
		match = pattern.match(line)
		if match is not None and match.group("name") not in targets:
			targets.append(match.group("name"))
	if not targets:
		raise RuntimeError("this configuration has no recovery fixture executables")
	return targets


def build_targets(build, targets, parallel, log):
	command = ["cmake", "--build", str(build), "--target", *targets, "--parallel", str(parallel)]
	subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
	log.flush()


def retire_intermediates(repository, build, target):
	executable = build / target
	if not executable.is_file() and not executable.with_suffix(".exe").is_file():
		raise RuntimeError("successful build did not produce its fixture executable: " + target)
	directory = build / "CMakeFiles" / (target + ".dir")
	records = []
	for path in sorted(directory.rglob("*")):
		if path.is_symlink() or not path.is_file() or path.suffix not in (".o", ".obj"):
			continue
		records.append({"target": target, "path": str(path.relative_to(repository)), "bytes": path.stat().st_size, "sha256": file_digest(path)})
		path.unlink()
	return records



def configured_tests(build):
	command = ["ctest", "--test-dir", str(build), "--show-only=json-v1"]
	return json.loads(subprocess.check_output(command, text=True))["tests"]


def validate_target(build, target, log, allow_empty=False):
	# CTest omits commands for missing executables; query after this target links.
	tests = [test["name"] for test in configured_tests(build) if test.get("command") and Path(test["command"][0]).name in (target, target + ".exe")]
	if not tests:
		if allow_empty:
			return []
		raise RuntimeError("linked validation target has no configured CTest: " + target)
	selection = "^(" + "|".join(re.escape(name) for name in tests) + ")$"
	with tempfile.NamedTemporaryFile(prefix="sgre-ctest-", suffix=".xml", dir=build, delete=False) as report:
		report_path = Path(report.name)
	try:
		command = ["ctest", "--test-dir", str(build), "--output-on-failure", "--no-tests=error", "--timeout", "60", "-R", selection, "--output-junit", str(report_path)]
		subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
		log.flush()
		suite = ElementTree.parse(report_path).getroot()
		cases = suite.findall("testcase")
		if len(cases) != len(tests) or {case.attrib["name"] for case in cases} != set(tests):
			raise RuntimeError("CTest report does not contain the exact selected tests: " + target)
		if any(case.find("failure") is not None or case.find("error") is not None or case.find("skipped") is not None for case in cases):
			raise RuntimeError("CTest report contains failed or skipped checks: " + target)
		return [{"name": case.attrib["name"], "seconds": float(case.attrib.get("time", "0")), "target": target, "status": "PASS"} for case in cases]
	finally:
		report_path.unlink()


def retire_fixture(repository, build, target):
	executable = build / target
	if not executable.is_file():
		executable = executable.with_suffix(".exe")
	if not executable.is_file() or executable.is_symlink():
		raise RuntimeError("validated fixture executable is missing: " + target)
	record = {"target": target, "path": str(executable.relative_to(repository)), "bytes": executable.stat().st_size, "sha256": file_digest(executable)}
	executable.unlink()
	return record


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_directory", type=Path)
	parser.add_argument("--parallel", type=int, default=2)
	parser.add_argument("--validate-and-retire", action="store_true", help="run every configured CTest as its executable links, then retire verified fixture products")
	options = parser.parse_args()
	if not 1 <= options.parallel <= 4:
		parser.error("parallel must be between one and four")
	repository = Path(__file__).resolve().parent.parent
	build = configured_build(repository, options.build_directory)
	initial_storage = check_storage(repository, build)
	targets = fixture_targets(build)
	expected_tests = {test["name"] for test in configured_tests(build)} if options.validate_and_retire else set()
	passed_tests = []
	products = []
	records = []
	with (build / "bounded-build.log").open("w") as log:
		build_targets(build, ["sgre_resource", "sgre-inspect"], options.parallel, log)
		check_storage(repository, build)
		for index, target in enumerate(targets, 1):
			build_targets(build, [target], options.parallel, log)
			if options.validate_and_retire:
				passed_tests.extend(validate_target(build, target, log))
			# The executable remains present while linked-object retirement verifies it.
			records.extend(retire_intermediates(repository, build, target))
			if options.validate_and_retire:
				products.append(retire_fixture(repository, build, target))
			check_storage(repository, build)
			if index % 20 == 0 or index == len(targets):
				print("Built", index, "of", len(targets), "fixtures", flush=True)
		if options.validate_and_retire:
			passed_tests.extend(validate_target(build, "sgre-inspect", log, allow_empty=True))
			if len(passed_tests) != len(expected_tests) or {test["name"] for test in passed_tests} != expected_tests:
				raise RuntimeError("bounded validation did not execute every configured CTest exactly once")
	final_storage = check_storage(repository, build)
	receipt = {"targets": len(targets), "pruned_intermediate_bytes": sum(record["bytes"] for record in records), "initial_storage": initial_storage, "final_storage": final_storage, "objects": records, "validated_tests": passed_tests, "retired_products": products}
	(build / "bounded-build-validation.json").write_text(json.dumps(receipt, indent=2) + "\n")
	if options.validate_and_retire:
		print("All", len(passed_tests), "configured CTests passed; verified fixture executables retired.", flush=True)
	else:
		print("All fixture executables built; run CTest for validation.", flush=True)


if __name__ == "__main__":
	try:
		main()
	except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
		print(str(error), file=sys.stderr)
		sys.exit(1)
