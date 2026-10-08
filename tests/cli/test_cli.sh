#!/bin/sh
# End-to-end tests for the vly command line. Usage: test_cli.sh PATH_TO_VLY
set -u

VLY=$1
WORK=$(mktemp -d)
failures=0

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

is_running() {
    pgrep -x "$1" >/dev/null
}

# Process names come from the executable, so copies give us unique names.
cp "$(command -v sleep)" "$WORK/vlytestsleep"
cp "$(command -v sh)" "$WORK/vlytestparent"

expect_status 0 "--version" "$VLY" --version
expect_status 0 "--help" "$VLY" --help
expect_status 1 "no command" "$VLY"
expect_status 1 "unknown command" "$VLY" frobnicate

expect_status 0 "list" "$VLY" list
grep -q "PID" "$WORK/out" || fail "list prints a header"
expect_status 1 "list with bad option" "$VLY" list --bogus

# kill by pid: a cooperative process exits on SIGTERM.
"$WORK/vlytestsleep" 300 &
pid=$!
sleep 0.2
expect_status 0 "kill by pid" "$VLY" kill "$pid"
wait "$pid"
[ $? -eq 143 ] || fail "kill by pid should end the process with SIGTERM"

# kill by name: every process with the name goes.
"$WORK/vlytestsleep" 300 &
"$WORK/vlytestsleep" 300 &
sleep 0.2
expect_status 0 "kill by name" "$VLY" kill -t 1 vlytestsleep
wait
is_running vlytestsleep && fail "kill by name leaves no vlytestsleep running"

# kill-tree: parent and children all go.
"$WORK/vlytestparent" -c "\"$WORK/vlytestsleep\" 300 & \"$WORK/vlytestsleep\" 300 & wait" &
sleep 0.3
expect_status 0 "kill-tree by name" "$VLY" kill-tree -t 1 vlytestparent
grep -q "+ 2 descendants" "$WORK/out" || fail "kill-tree reports 2 descendants"
sleep 0.2
is_running vlytestparent && fail "kill-tree stops the parent"
is_running vlytestsleep && fail "kill-tree stops the children"

# Refusals and misses.
expect_status 1 "kill pid 1" "$VLY" kill 1
expect_status 1 "kill missing name" "$VLY" kill vly-no-such-process
expect_status 1 "kill without target" "$VLY" kill
expect_status 1 "kill with bad timeout" "$VLY" kill -t abc 123

if [ "$failures" -ne 0 ]; then
    echo "$failures CLI check(s) failed" >&2
    exit 1
fi
