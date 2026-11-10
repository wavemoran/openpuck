#!/bin/sh
# Build and run the host unit tests for puck_core, with a coverage report.
#   tools/test.sh            run tests + coverage (needs gcovr: pip install gcovr)
#   tools/test.sh --no-cov   run tests only
# Coverage below COVERAGE_MIN % of lines fails the run.
set -e
cd "$(dirname "$0")/.."
COVERAGE_MIN=${COVERAGE_MIN:-90}
BUILD=build-host

coverage=ON
[ "$1" = "--no-cov" ] && coverage=OFF

cmake -S test/host -B "$BUILD" -DCOVERAGE=$coverage > /dev/null
cmake --build "$BUILD" --parallel
find "$BUILD" -name '*.gcda' -delete
ctest --test-dir "$BUILD" --output-on-failure
python3 test/test_flash_tool.py

[ "$coverage" = OFF ] && exit 0

# Apple clang writes LLVM-format coverage; gcovr reads it through llvm-cov.
gcov_tool=""
if [ "$(uname)" = "Darwin" ]; then
    gcov_tool="--gcov-executable=xcrun llvm-cov gcov"
fi
mkdir -p "$BUILD/coverage"
gcovr --root . --filter 'components/puck_core/' ${gcov_tool:+"$gcov_tool"} \
    --object-directory "$BUILD" \
    --txt --html-details "$BUILD/coverage/index.html" --xml "$BUILD/coverage/coverage.xml" \
    --fail-under-line "$COVERAGE_MIN"
echo "HTML report: $BUILD/coverage/index.html"
