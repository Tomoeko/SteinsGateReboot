"""Apply the user-authorized generated-file cap to a child command."""

import os
import resource
import sys


FILE_BYTES_LIMIT = 10_000_000_000


def main():
	if len(sys.argv) < 3 or not sys.argv[1].isdigit():
		raise SystemExit("usage: limit_file_output.py <maximum bytes> <absolute command> [arguments ...]")
	limit = int(sys.argv[1])
	if not 0 < limit <= FILE_BYTES_LIMIT:
		raise SystemExit("file limit must be positive and at most 10 GB")
	if not os.path.isabs(sys.argv[2]):
		raise SystemExit("child command must use an absolute path")
	resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))
	resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
	os.execv(sys.argv[2], sys.argv[2:])


if __name__ == "__main__":
	main()
