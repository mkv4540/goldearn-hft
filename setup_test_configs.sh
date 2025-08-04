#!/bin/bash

# Create test configuration directory
TEST_DIR="/tmp/goldearn_test_configs"
mkdir -p "$TEST_DIR"

# Create various test JSON files
echo '{"key": "value", "number": 42, "nested": {"inner": "data"}}' > "$TEST_DIR/valid.json"

echo '{"level1": {"level2": {"level3": {"level4": "deep"}}}}' > "$TEST_DIR/nested.json"

echo '{"items": [1, 2, 3], "strings": ["a", "b", "c"], "mixed": [1, "two", 3.0]}' > "$TEST_DIR/arrays.json"

echo '{"flag1": true, "flag2": false, "flag3": true}' > "$TEST_DIR/booleans.json"

echo '{"integer": 42, "float": 3.14, "negative": -100, "zero": 0}' > "$TEST_DIR/numbers.json"

echo '{"special": "line1\nline2", "tab": "col1\tcol2", "quote": "He said \"hello\""}' > "$TEST_DIR/special_chars.json"

echo '{"value": null, "empty": "", "array_with_null": [1, null, 3]}' > "$TEST_DIR/nulls.json"

echo '{"comment": "This is valid JSON", "no_comments": "JSON doesnt support comments"}' > "$TEST_DIR/comments.json"

echo '{"escaped": "\\\"quoted\\\"", "backslash": "path\\\\to\\\\file", "unicode": "\\u0048\\u0065\\u006C\\u006C\\u006F"}' > "$TEST_DIR/escaped.json"

echo '{"emoji": "😀", "chinese": "你好", "arabic": "مرحبا"}' > "$TEST_DIR/unicode.json"

echo '{"big_int": 9223372036854775807, "big_float": 1.7976931348623157e+308}' > "$TEST_DIR/large_numbers.json"

echo '{"sci_notation": 1.23e-4, "positive_exp": 5.67e+8}' > "$TEST_DIR/scientific.json"

echo '{"string": "text", "number": 123, "boolean": true, "null": null, "array": [1,2,3], "object": {"a": 1}}' > "$TEST_DIR/mixed.json"

echo '{"empty_obj": {}, "empty_arr": [], "nested_empty": {"obj": {}, "arr": []}}' > "$TEST_DIR/empty_objects.json"

echo "Created test configuration files in $TEST_DIR"
ls -la "$TEST_DIR"