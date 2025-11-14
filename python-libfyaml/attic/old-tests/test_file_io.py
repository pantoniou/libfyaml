#!/usr/bin/env python3
"""
Test file I/O helpers (load and dump).
"""

import sys
import os
import tempfile
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

print("=" * 70)
print("FILE I/O HELPERS TEST")
print("=" * 70)

# Test data
test_data = {
    "name": "Alice",
    "age": 30,
    "scores": [95, 88, 92],
    "config": {
        "theme": "dark",
        "notifications": True
    }
}

# Create temporary directory for tests
with tempfile.TemporaryDirectory() as tmpdir:
    yaml_path = os.path.join(tmpdir, "test.yaml")

    print("\n### Test 1: dump() to file path ###")
    libfyaml.dump(yaml_path, test_data)
    print(f"✅ Wrote to {yaml_path}")

    # Read back and verify
    with open(yaml_path, 'r') as f:
        content = f.read()
    print(f"File contents:\n{content}")

    print("\n### Test 2: load() from file path ###")
    loaded = libfyaml.load(yaml_path)
    print(f"✅ Loaded from {yaml_path}")
    print(f"Loaded data: {loaded.to_python()}")

    # Verify data matches
    assert loaded['name'] == "Alice"
    assert loaded['age'] + 0 == 30
    assert len(loaded['scores']) == 3
    print("✅ Data matches original!")

    print("\n### Test 3: dump() to file object ###")
    with open(yaml_path, 'w') as f:
        libfyaml.dump(f, test_data)
    print("✅ Wrote to file object")

    print("\n### Test 4: load() from file object ###")
    with open(yaml_path, 'r') as f:
        loaded2 = libfyaml.load(f)
    print("✅ Loaded from file object")
    print(f"Loaded data: {loaded2.to_python()}")

    print("\n### Test 5: Round-trip verification ###")
    original = {"users": ["alice", "bob"], "count": 42}
    libfyaml.dump(yaml_path, original)
    restored = libfyaml.load(yaml_path)
    restored_py = restored.to_python()

    print(f"Original: {original}")
    print(f"Restored: {restored_py}")
    assert restored_py == original
    print("✅ Round-trip successful!")

    print("\n### Test 6: Compact mode ###")
    compact_path = os.path.join(tmpdir, "compact.yaml")
    libfyaml.dump(compact_path, test_data, compact=True)

    with open(compact_path, 'r') as f:
        compact_content = f.read()
    print(f"Compact YAML:\n{compact_content}")

    print("\n### Test 7: JSON mode ###")
    json_path = os.path.join(tmpdir, "test.json")
    libfyaml.dump(json_path, test_data, mode="json")

    with open(json_path, 'r') as f:
        json_content = f.read()
    print(f"JSON output:\n{json_content}")

    loaded_json = libfyaml.load(json_path, mode="json")
    print(f"Loaded JSON: {loaded_json.to_python()}")

print("\n" + "=" * 70)
print("✅ ALL FILE I/O TESTS PASSED!")
print("=" * 70)
print("""
New API functions work:
  - libfyaml.load(path) or load(file_object)
  - libfyaml.dump(path, obj) or dump(file_object, obj)
  - Both support mode='yaml' or 'json'
  - dump() supports compact=True
""")
