#!/bin/sh
# End-to-end tests for the vly command line. Usage: test_cli.sh PATH_TO_VLY
set -u

VLY=$1
WORK=$(mktemp -d)
failures=0

# Process names come from the executable, so copies give us names that are
# unique to this run (and within the kernel's 15-character limit).
SLEEP_NAME="vlys$$"
PARENT_NAME="vlyp$$"
cp "$(command -v sleep)" "$WORK/$SLEEP_NAME"
cp "$(command -v sh)" "$WORK/$PARENT_NAME"

cleanup() {
    pkill -KILL -f "$WORK/" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $1" >&2
    failures=$((failures + 1))
}

# expect_status EXPECTED DESCRIPTION COMMAND...
expect_status() {
    expected=$1
    description=$2
    shift 2
    "$@" >"$WORK/out" 2>&1
    status=$?
    if [ "$status" -ne "$expected" ]; then
        fail "$description: exit $status, expected $expected"
        cat "$WORK/out" >&2
    fi
}

# Number of this run's processes with the given name.
count_running() {
    pgrep -c -x "$1" 2>/dev/null || true
}

# wait_until_count NAME COUNT: polls for up to 5 s instead of sleeping blindly.
wait_until_count() {
    tries=0
    while [ "$(count_running "$1")" -ne "$2" ] && [ $tries -lt 100 ]; do
        sleep 0.05
        tries=$((tries + 1))
    done
    [ "$(count_running "$1")" -eq "$2" ]
}

expect_status 0 "--version" "$VLY" --version
expect_status 0 "--help" "$VLY" --help
expect_status 1 "no command" "$VLY"
expect_status 1 "unknown command" "$VLY" frobnicate

expect_status 0 "list" "$VLY" list
grep -q "PID" "$WORK/out" || fail "list prints a header"
expect_status 1 "list with bad option" "$VLY" list --bogus

# kill by pid: a cooperative process exits on SIGTERM.
"$WORK/$SLEEP_NAME" 300 &
pid=$!
wait_until_count "$SLEEP_NAME" 1 || fail "test process did not start"
expect_status 0 "kill by pid" "$VLY" kill "$pid"
wait "$pid"
[ $? -eq 143 ] || fail "kill by pid should end the process with SIGTERM"

# kill by name: every process with the name goes.
"$WORK/$SLEEP_NAME" 300 &
"$WORK/$SLEEP_NAME" 300 &
wait_until_count "$SLEEP_NAME" 2 || fail "test processes did not start"
expect_status 0 "kill by name" "$VLY" kill -t 1 "$SLEEP_NAME"
wait
wait_until_count "$SLEEP_NAME" 0 || fail "kill by name leaves none running"

# kill-tree: parent and children all go.
"$WORK/$PARENT_NAME" -c "\"$WORK/$SLEEP_NAME\" 300 & \"$WORK/$SLEEP_NAME\" 300 & wait" &
wait_until_count "$SLEEP_NAME" 2 || fail "test tree did not start"
expect_status 0 "kill-tree by name" "$VLY" kill-tree -t 1 "$PARENT_NAME"
grep -q "+ 2 descendants" "$WORK/out" || fail "kill-tree reports 2 descendants"
wait_until_count "$PARENT_NAME" 0 || fail "kill-tree stops the parent"
wait_until_count "$SLEEP_NAME" 0 || fail "kill-tree stops the children"

# Refusals and misses.
expect_status 1 "kill pid 1" "$VLY" kill 1
expect_status 1 "kill missing name" "$VLY" kill vly-no-such-process
expect_status 1 "kill without target" "$VLY" kill
expect_status 1 "kill with bad timeout" "$VLY" kill -t abc 123

if [ "$failures" -ne 0 ]; then
    echo "$failures CLI check(s) failed" >&2
    exit 1
fi
