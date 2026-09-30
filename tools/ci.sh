#!/bin/sh
# Run the CI stages locally, so a red pipeline is reproducible on the machine
# that caused it.
#
#   tools/ci.sh                  every stage that this machine can run
#   tools/ci.sh lint tests       only those
#   tools/ci.sh --strict         a missing tool is a failure, not a SKIP
#
# Stages: lint arduino-lint tests sanitize docs
#
# .gitlab-ci.yml calls this rather than the tools directly. Two implementations
# of the same check drift, and the one that drifts is always the one nobody runs.
set -eu

STAGES_ALL="lint arduino-lint tests sanitize docs"
STRICT=0
STAGES=""

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$REPO"

B=$(printf '\033[1m'); N=$(printf '\033[0m')
banner() { printf '\n%s── %s %s\n' "$B" "$1" "$N"; }
have() { command -v "$1" >/dev/null 2>&1; }

# A stage that cannot run says so and does not fail the run -- unless --strict,
# which is what CI uses, because a silently skipped check is worth nothing there.
skip() {
  if [ "$STRICT" -eq 1 ]; then
    echo "  FAIL  $1 (missing: $2)"
    return 1
  fi
  echo "  SKIP  $1 (missing: $2)"
  return 0
}

usage() {
  echo "usage: $0 [--strict] [stage ...]"
  echo "stages: $STAGES_ALL"
  exit "${1:-0}"
}

for arg in "$@"; do
  case "$arg" in
    --strict) STRICT=1 ;;
    -h|--help) usage 0 ;;
    -*) echo "unknown option: $arg" >&2; usage 2 ;;
    *) STAGES="$STAGES $arg" ;;
  esac
done
[ -n "$STAGES" ] || STAGES=$STAGES_ALL

# --- lint ---------------------------------------------------------------------

# Every source this repository wrote. There is nothing vendored here, so the
# list needs no exclusions.
sources() {
  git ls-files '*.c' '*.cpp' '*.h' '*.ino'
}

# Checked against the index, not the working tree: with core.autocrlf=true every
# file on disk has CRLF even though the repository holds LF, so a check over the
# working tree would fire on the machine this is developed on.
lint_hygiene() {
  rc=0
  bad=$(git ls-files --eol -- '*.c' '*.cpp' '*.h' '*.ino' '*.md' '*.yml' '*.py' '*.sh' \
        | grep -v '^i/lf' || true)
  if [ -n "$bad" ]; then
    echo "  files stored with the wrong line endings:"
    printf '%s\n' "$bad" | sed 's/^/    /'
    rc=1
  fi

  # A literal tab rather than grep -P '\t': -P refuses to run outside a UTF-8
  # locale and exits non-zero, which this `if` would read as "no tabs found".
  tab=$(printf '\t')
  for f in $(sources); do
    if grep -n "$tab" "$f" >/dev/null 2>&1; then
      echo "  tab character in $f"; rc=1
    fi
    if grep -n ' $' "$f" >/dev/null 2>&1; then
      echo "  trailing whitespace in $f"; rc=1
    fi
  done

  # The opposite case: the Arduino IDE splits keywords.txt on tabs and silently
  # ignores any line using spaces, so a "tidied" file highlights nothing.
  if [ -f keywords.txt ]; then
    if grep -nE '^[A-Za-z_][A-Za-z0-9_]* +(KEYWORD|LITERAL)' keywords.txt >/dev/null 2>&1; then
      echo "  keywords.txt has entries separated by spaces instead of a tab"
      rc=1
    fi
  fi
  return $rc
}

# The version is written in three places and they must agree. The Doxyfile is
# one of them: that is where esp_crsf's version sat unnoticed at 0.3.0.
lint_versions() {
  p=$(sed -n 's/^version=//p' library.properties)
  j=$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' library.json)
  d=$(sed -n 's/^PROJECT_NUMBER *= *//p' Doxyfile | tr -d '"[:space:]')
  rc=0
  echo "  library.properties=$p  library.json=$j  Doxyfile=$d"
  [ "$p" = "$j" ] || { echo "  library.json disagrees"; rc=1; }
  [ "$p" = "$d" ] || { echo "  Doxyfile disagrees"; rc=1; }
  return $rc
}

# examples/<Name>/<Name>.ino: a sketch whose folder and file disagree is
# silently missing from the IDE's menu. arduino-lint catches it too, but this
# needs no download.
lint_examples() {
  rc=0
  for d in examples/*/; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    if [ ! -f "$d$name.ino" ]; then
      echo "  $d has no $name.ino"; rc=1
    fi
  done
  [ $rc -eq 0 ] && echo "  every example folder holds a sketch of the same name"
  return $rc
}

# Passed explicitly rather than left to HeaderFilterRegex in .clang-tidy: at
# least one clang-tidy build loads that value and then does not apply it, which
# made every finding in this library's headers invisible locally while firing in
# CI. Read from .clang-tidy so there is still one source of truth.
tidy_header_filter() {
  hf=$(sed -n "s/^HeaderFilterRegex:[[:space:]]*'\(.*\)'[[:space:]]*$/\1/p" .clang-tidy)
  if [ -z "$hf" ]; then
    echo "  cannot read HeaderFilterRegex from .clang-tidy" >&2
    return 1
  fi
  printf '%s' "$hf"
}

# Run clang-tidy over one file and, when it is unhappy, say what it said. The
# diagnostics go to stdout and only "N warnings generated." to stderr, so
# discarding stdout throws away the whole content of a failure. The command is
# echoed too, so a failure can be re-run by hand.
#
# $1 is the file, everything after it is passed to the compiler.
tidy() {
  f=$1
  shift
  if out=$(clang-tidy --quiet --header-filter="$HEADER_FILTER" "$f" -- "$@" 2>&1); then
    return 0
  fi
  echo "  clang-tidy is unhappy with $f"
  echo "    clang-tidy $f -- $*"
  printf '%s\n' "$out" | sed 's/^/    /'
  return 1
}

stage_lint() {
  rc=0
  banner "lint: hygiene"
  lint_hygiene || rc=1

  banner "lint: versions"
  lint_versions || rc=1

  banner "lint: examples"
  lint_examples || rc=1

  banner "lint: clang-format"
  if have clang-format; then
    for f in $(sources); do
      if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
        echo "  not formatted: $f"; rc=1
      fi
    done
    [ $rc -eq 0 ] && echo "  every file matches .clang-format"
  else
    skip "clang-format" "clang-format" || rc=1
  fi

  banner "lint: clang-tidy"
  if have clang-tidy; then
    HEADER_FILTER=$(tidy_header_filter) || return 1
    echo "  header filter: $HEADER_FILTER"
    # The core as C, the wrapper and the mock runtime as C++ -- the same split
    # the Arduino build and tests/Makefile use. Checking the core as C++ would
    # report C idioms that do not apply to it.
    tidy src/rclights_core.c -std=c11 -Isrc || rc=1
    tidy src/RcLights.cpp -std=c++11 -Isrc -Itests/arduino_stubs || rc=1
    tidy tests/arduino_stubs/arduino_stubs.cpp -std=c++11 \
        -Isrc -Itests -Itests/arduino_stubs || rc=1
    tidy tests/test_arduino_port.cpp -std=c++11 \
        -Isrc -Itests -Itests/arduino_stubs || rc=1
    tidy tests/test_example_defaults.cpp -std=c++11 \
        -Isrc -Itests -Itests/arduino_stubs -Iexamples/RcLightsCar || rc=1
    [ $rc -eq 0 ] && echo "  clang-tidy is happy"
  else
    skip "clang-tidy" "clang-tidy" || rc=1
  fi
  return $rc
}

# --- arduino-lint -------------------------------------------------------------

# The Arduino library specification, checked with Arduino's own tool.
#
# Run against a copy of the tracked files, not the working tree: rule LS007
# fails on any .exe in the library, and on Windows the host test binaries in
# build_tests/ are .exe files -- git-ignored, and invisible to the registry,
# which judges a tag. Working-tree content, so uncommitted work is checked.
#
# LIBRARY_MANAGER_MODE mirrors the variable in .gitlab-ci.yml and must hold the
# same value: the two modes check opposite things, so a default of submit here
# would fail on LP017 against our own index entry while CI passed.
stage_arduino_lint() {
  banner "arduino-lint"
  if ! have arduino-lint; then
    skip "arduino-lint" "arduino-lint"
    return
  fi

  mode=${LIBRARY_MANAGER_MODE:-update}
  name=$(sed -n 's/^name=//p' library.properties)
  if [ -z "$name" ]; then
    echo "  cannot read name= from library.properties"
    return 1
  fi

  tmp=$(mktemp -d)
  mkdir -p "$tmp/$name"
  git ls-files -z | tar --null -T - -cf - | (cd "$tmp/$name" && tar -xf -)

  echo "  checking the tracked files as $name/, library-manager=$mode"
  rc_al=0
  (cd "$tmp/$name" && arduino-lint --compliance strict --library-manager "$mode" --recursive) || rc_al=1
  rm -rf "$tmp"
  return $rc_al
}

# --- the rest -----------------------------------------------------------------

stage_tests()    { banner "tests";    make -C tests; }
stage_sanitize() {
  banner "sanitize"
  # Cygwin's gcc ships without libasan, which is why this is not part of `tests`.
  if printf 'int main(void){return 0;}' | \
     ${CC:-gcc} -fsanitize=address -x c - -o /dev/null >/dev/null 2>&1; then
    make -C tests sanitize
  else
    skip "sanitize" "libasan"
  fi
}
stage_docs()     {
  banner "docs"
  if have doxygen; then
    doxygen Doxyfile
    echo "  documentation built with no warnings"
  else
    skip "docs" "doxygen"
  fi
}

rc=0
for s in $STAGES; do
  case "$s" in
    lint)         stage_lint         || rc=1 ;;
    arduino-lint) stage_arduino_lint || rc=1 ;;
    tests)        stage_tests        || rc=1 ;;
    sanitize)     stage_sanitize     || rc=1 ;;
    docs)         stage_docs         || rc=1 ;;
    *) echo "unknown stage: $s" >&2; usage 2 ;;
  esac
done

banner "result"
if [ $rc -eq 0 ]; then
  echo "  everything that could run, passed"
else
  echo "  something failed; see above"
fi
exit $rc
