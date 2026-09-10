#!/bin/bash
# Build every commit in a range, not just the tip.
#
#   ./tools/bisect-build.sh [base] [branch]
#
# Zephyr requires bisectability -- doc/contribute/contributor_expectations.rst:
# "Every commit in the pull request must build successfully and pass all
# relevant tests." Upstream enforces it with twister across the series.
#
# This exists because a tip-only build hid a real defect once: PR A referenced
# cpu@400/cpu@500 while the commit adding those nodes lived in PR B, so PR A
# failed dtc on its own even though the full branch built cleanly. Any run that
# validates only the last commit cannot see that class of bug.
#
# Each commit is built for mt8195//adsp -- a pre-existing MediaTek board that
# must never regress -- plus each Genio board from the commit that adds it.
# Assumes the range does not touch west.yml; if it does, modules need syncing
# per commit and this script is not enough.
set -u

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$TESTS_DIR/lib/common.sh"

BASE="${1:-upstream-zephyr/main}"
BRANCH="${2:-HEAD}"
BUILD="${BUILD_ROOT:-$ZEPHYR_BASE/build/tests}/bisect"
LOG="${LOG_DIR:-$TESTS_DIR/logs}/bisect-build.log"

G700=mt8390_genio_700_evk/mt8188/a55
G510=mt8370_genio_510_evk/mt8188/a55
ADSP=mt8195//adsp

cd "$ZEPHYR_BASE" || exit 1
# shellcheck disable=SC1091
. "$VENV/bin/activate" || exit 1

mkdir -p "$(dirname "$LOG")"
: > "$LOG"

start="$(git rev-parse --abbrev-ref HEAD)"
[ "$start" = HEAD ] && start="$(git rev-parse HEAD)"
restore() { git checkout -q "$start" 2>/dev/null; }
trap restore EXIT

fails=0
total=0

build() {
	total=$((total + 1))
	rm -rf "$BUILD"
	west build -p always -b "$1" -d "$BUILD" samples/hello_world >>"$LOG" 2>&1 && return 0
	fails=$((fails + 1))
	echo "### FAILED $2  board=$1" >> "$LOG"
	return 1
}

mapfile -t COMMITS < <(git rev-list --reverse "$BASE..$BRANCH")
[ "${#COMMITS[@]}" -gt 0 ] || { echo "no commits in $BASE..$BRANCH"; exit 1; }
echo "sweeping ${#COMMITS[@]} commits from $BASE"
echo

i=0
for c in "${COMMITS[@]}"; do
	i=$((i + 1))
	subj="$(git log -1 --format=%s "$c")"
	git checkout -q --detach "$c" || { echo "checkout failed: $c"; exit 1; }

	res=""
	build "$ADSP" "${c:0:11}" && res="$res adsp:ok" || res="$res adsp:FAIL"
	[ -d boards/mediatek/mt8390_genio_700_evk ] && \
		{ build "$G700" "${c:0:11}" && res="$res g700:ok" || res="$res g700:FAIL"; }
	[ -d boards/mediatek/mt8370_genio_510_evk ] && \
		{ build "$G510" "${c:0:11}" && res="$res g510:ok" || res="$res g510:FAIL"; }

	printf "%2d  %s  %-58.58s %s\n" "$i" "${c:0:11}" "$subj" "$res"
done

echo
echo "=== $((total - fails))/$total builds passed ==="
if [ "$fails" -eq 0 ]; then
	echo "BISECTABLE"
else
	echo "NOT BISECTABLE: $fails failure(s), see $LOG"
	exit 1
fi
