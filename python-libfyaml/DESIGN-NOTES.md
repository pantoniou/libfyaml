# Python Bindings Design Notes

## Mutation Strategy

### Decision: Shallow Materialization with Freeze/Thaw Cycle

When a user mutates a FyGeneric object, use **lazy shallow materialization** that only converts the mutation path, leaving siblings as FyGeneric. Add explicit freeze/thaw cycle for efficiency.

#### Basic Usage

```python
data = libfyaml.load('config.yaml')  # Frozen: FyGeneric (lazy, efficient)

# Read operations work on FyGeneric (fast)
host = data['server']['host']  # FyGeneric traversal, no materialization

# Mutation materializes only the path
data['server']['port'] = 3000  # Thawed state
# Materializes: data -> data['server'] -> 'port'
# Siblings unchanged: data['database'] stays FyGeneric

# Can freeze back to efficient FyGeneric
data = data.freeze()  # Convert back to pure FyGeneric
# All mutations preserved, efficient representation restored

# Continue working efficiently
for server in data['servers']:  # Fast FyGeneric iteration
    print(server['port'])
```

#### Advanced: Freeze/Thaw Cycles

```python
# Load: Frozen (FyGeneric)
config = libfyaml.load('large-config.yaml')  # Efficient

# Thaw: Mutations materialize on-demand
config['app']['debug'] = True        # Materializes: config -> app
config['database']['pool_size'] = 20 # Materializes: config -> database

# State now: Hybrid
#   config._py_value = {
#       'app': { 'debug': True, 'name': FyGeneric(...) },
#       'database': { 'pool_size': 20, 'host': FyGeneric(...) },
#       'servers': FyGeneric([...])  # Untouched, still FyGeneric!
#   }

# Freeze: Convert back to FyGeneric
config = config.freeze()
# Everything back to efficient FyGeneric representation
# Can now iterate 'servers' efficiently without materialization overhead

# Thaw again for more mutations
config['servers'][0]['enabled'] = False
```

### Implementation

#### Core Wrapper Class

```python
class FyGenericWrapper:
    """
    Hybrid wrapper that can be in two states:
    - Frozen: Pure FyGeneric (efficient, immutable)
    - Thawed: Materialized Python containers with FyGeneric children (mutable)
    """

    def __init__(self, gb, fy_value=None, py_value=None):
        self._gb = gb                   # Generic builder
        self._fy_value = fy_value       # FyGeneric value (frozen state)
        self._py_value = py_value       # Python dict/list (thawed state)
        self._is_materialized = (py_value is not None)

    def __getitem__(self, key):
        """Get item - works on both frozen and thawed state."""
        if self._is_materialized:
            # Thawed: return from Python container
            child = self._py_value[key]
            if isinstance(child, FyGenericWrapper):
                return child
            else:
                # Plain Python value (was set directly)
                return child
        else:
            # Frozen: get from FyGeneric and wrap
            child_fy = fy_generic_op(self._gb, FYGBOPF_GET, self._fy_value, 1, [key])
            if fy_generic_is_invalid(child_fy):
                raise KeyError(key)
            return FyGenericWrapper(self._gb, fy_value=child_fy)

    def __setitem__(self, key, value):
        """
        Set item - materializes on first mutation.
        Only materializes THIS level, children stay FyGeneric.
        """
        if not self._is_materialized:
            self._materialize_shallow()

        # Convert Python value to wrapped FyGeneric if needed
        if isinstance(value, (dict, list)):
            # Convert to FyGeneric first
            fy_val = python_to_generic(self._gb, value)
            value = FyGenericWrapper(self._gb, fy_value=fy_val)
        elif not isinstance(value, FyGenericWrapper):
            # Scalar Python value - wrap it
            fy_val = python_to_generic(self._gb, value)
            value = FyGenericWrapper(self._gb, fy_value=fy_val)

        self._py_value[key] = value

    def _materialize_shallow(self):
        """
        Convert THIS level to Python dict/list.
        Children remain as FyGeneric wrappers (not materialized).
        """
        if fy_generic_is_mapping(self._fy_value):
            # Create Python dict with wrapped FyGeneric children
            self._py_value = {}

            # Get all keys from FyGeneric
            keys = fy_generic_op(self._gb, FYGBOPF_KEYS, self._fy_value, 0, None)
            for key in keys:
                child_fy = fy_generic_op(self._gb, FYGBOPF_GET, self._fy_value, 1, [key])
                # Wrap child - stays FyGeneric until accessed/mutated
                self._py_value[key] = FyGenericWrapper(self._gb, fy_value=child_fy)

            self._is_materialized = True
            self._fy_value = None  # Release FyGeneric memory

        elif fy_generic_is_sequence(self._fy_value):
            # Create Python list with wrapped FyGeneric children
            self._py_value = []
            length = fy_generic_sequence_length(self._fy_value)

            for i in range(length):
                child_fy = fy_generic_op(self._gb, FYGBOPF_GET_AT, self._fy_value, 1, [i])
                # Wrap child - stays FyGeneric until accessed/mutated
                self._py_value.append(FyGenericWrapper(self._gb, fy_value=child_fy))

            self._is_materialized = True
            self._fy_value = None  # Release FyGeneric memory
        else:
            # Scalar - nothing to materialize
            pass

    def freeze(self):
        """
        Convert back to pure FyGeneric representation.
        Recursively converts materialized Python containers and their children.
        Returns a new frozen FyGenericWrapper.
        """
        if not self._is_materialized:
            # Already frozen
            return self

        # Convert materialized structure back to FyGeneric
        frozen_fy = self._freeze_recursive(self._py_value)
        return FyGenericWrapper(self._gb, fy_value=frozen_fy)

    def _freeze_recursive(self, value):
        """Recursively convert Python/wrapped values to FyGeneric."""
        if isinstance(value, FyGenericWrapper):
            if value._is_materialized:
                # Recursively freeze materialized wrapper
                return value._freeze_recursive(value._py_value)
            else:
                # Already FyGeneric
                return value._fy_value

        elif isinstance(value, dict):
            # Convert Python dict to FyGeneric mapping
            pairs = []
            for k, v in value.items():
                key_fy = python_to_generic(self._gb, k)
                val_fy = self._freeze_recursive(v)
                pairs.extend([key_fy, val_fy])
            return fy_gb_mapping_create(self._gb, len(value), pairs)

        elif isinstance(value, list):
            # Convert Python list to FyGeneric sequence
            items = [self._freeze_recursive(item) for item in value]
            return fy_gb_sequence_create(self._gb, len(items), items)

        else:
            # Scalar Python value
            return python_to_generic(self._gb, value)

    def __iter__(self):
        """Iterate - works on both frozen and thawed state."""
        if self._is_materialized:
            # Thawed: iterate Python container
            if isinstance(self._py_value, dict):
                return iter(self._py_value.keys())
            elif isinstance(self._py_value, list):
                return iter(self._py_value)
        else:
            # Frozen: iterate FyGeneric
            if fy_generic_is_mapping(self._fy_value):
                keys = fy_generic_op(self._gb, FYGBOPF_KEYS, self._fy_value, 0, None)
                return iter(keys)
            elif fy_generic_is_sequence(self._fy_value):
                # Return iterator that yields wrapped items
                return FyGenericSequenceIterator(self)

    def keys(self):
        """Mapping keys."""
        if self._is_materialized:
            return self._py_value.keys()
        else:
            return fy_generic_op(self._gb, FYGBOPF_KEYS, self._fy_value, 0, None)

    def values(self):
        """Mapping values."""
        if self._is_materialized:
            return self._py_value.values()
        else:
            # Return wrapped values
            return [self[k] for k in self.keys()]

    def items(self):
        """Mapping items."""
        if self._is_materialized:
            return self._py_value.items()
        else:
            return [(k, self[k]) for k in self.keys()]

    def __len__(self):
        """Length."""
        if self._is_materialized:
            return len(self._py_value)
        else:
            if fy_generic_is_mapping(self._fy_value):
                return fy_generic_mapping_length(self._fy_value)
            elif fy_generic_is_sequence(self._fy_value):
                return fy_generic_sequence_length(self._fy_value)
            return 0
```

#### Optimizations

**Lazy Child Wrapping**: Children are only wrapped when accessed, not during materialization:

```python
def __getitem__(self, key):
    if self._is_materialized:
        child = self._py_value[key]
        # If child is already wrapped, return it
        if isinstance(child, FyGenericWrapper):
            return child
        # If child is raw Python value (from mutation), return as-is
        return child
    else:
        # Lazy wrapping on access
        child_fy = fy_generic_get(self._fy_value, key)
        return FyGenericWrapper(self._gb, fy_value=child_fy)
```

**Memory Release**: After materialization, release FyGeneric reference:

```python
def _materialize_shallow(self):
    # ... create self._py_value ...
    self._is_materialized = True
    self._fy_value = None  # Allow GC of FyGeneric structure
```

**Freeze Optimization**: Skip freezing if nothing materialized:

```python
def freeze(self):
    if not self._is_materialized:
        return self  # Already frozen, no work needed
    # ... convert back to FyGeneric ...
```

### Rationale

**Why shallow materialization with freeze/thaw:**

1. **Efficient mutations**: Only materializes the mutation path, siblings stay FyGeneric
   - Mutating `data['server']['port']` doesn't touch `data['database']` subtree
   - Large config files: change one setting, millions of lines stay efficient

2. **Python semantics**: Natural mutable behavior
   - Mutations visible everywhere (shared Python containers)
   - No surprises for Python developers

3. **Best of both worlds**:
   - Frozen state: Fast reads, low memory (FyGeneric)
   - Thawed state: Mutable when needed (Python containers)
   - Freeze/thaw: Explicit control over representation

4. **Memory efficiency**:
   - Unmutated siblings stay as FyGeneric (no materialization overhead)
   - Can freeze back to compact FyGeneric representation
   - Only pay for what you use

5. **Use case optimization**:
   - Config loading: Read-only → stays frozen (fast!)
   - Config modification: Thaw, mutate specific paths, freeze
   - Batch mutations: Stay thawed during mutation phase, freeze when done

**Trade-offs:**

- ⚠️ Slight overhead: Materialized containers hold FyGenericWrapper objects
- ⚠️ Hybrid structure: Mix of Python containers and FyGeneric values
- ⚠️ Freezing cost: Converting back requires traversal (but only when needed)
- ✅ Efficient: Only materializes mutation paths, not entire tree
- ✅ Flexible: Can freeze/thaw multiple times as needed
- ✅ Memory efficient: Siblings stay FyGeneric, can release FyGeneric after materialization

**Comparison to alternatives:**

| Approach | Mutation Cost | Memory | Complexity |
|----------|---------------|--------|------------|
| Full materialization | High (entire tree) | High (full Python) | Low |
| Shallow materialization | Low (path only) | Medium (hybrid) | Medium |
| Path tracking + SET_AT_PATH | Low (persistent) | Low (structural sharing) | Very High |

**Decision**: Shallow materialization strikes the best balance of efficiency, simplicity, and Python semantics.

### Implementation Strategies

See [MUTATION-DESIGN.md](MUTATION-DESIGN.md) for detailed implementation specification with two strategies:

**Strategy A (Simple - Recommended)**: Global mutated state
- First mutation converts entire tree to native Python dict/list
- Simple structure, no parent tracking, no resolution logic
- Fast implementation (1-2 days)
- Optimal for small/medium trees (< 10k items) - typical config files

**Strategy B (Hybrid - Advanced)**: Shallow materialization with bitmap
- Only materializes mutation paths, siblings stay FyGeneric
- Can use same global tracking for frozen reference resolution
- Optimized for very large trees (> 10k items) with sparse mutations
- Higher implementation complexity (1-2 weeks)

**Key insight**: **Tree size matters!** For small trees, converting to Python is cheap and probably faster than hybrid overhead. The mutation strategy should adapt based on tree size.

**Recommendation**:
- Start with Strategy A for all trees (initial Python bindings release)
- Optionally add adaptive selection later: Strategy A for < 10k items, Strategy B for larger
- Only implement Strategy B if profiling shows it's needed

### Alternative Considered: Path-Tracking with SET_AT_PATH

The C library has `FYGBOPF_SET_AT_PATH` which creates a new FyGeneric root with structural sharing (persistent data structures, like Clojure). Could implement:

```python
class FyGenericWrapper:
    def __init__(self, root_holder, path):
        self._root_holder = root_holder  # Shared root reference
        self._path = path                 # Path from root

    def __setitem__(self, key, value):
        # Call SET_AT_PATH, update shared root
        new_root = fy_generic_set_at_path(
            self._root_holder.root,
            self._path + [key],
            value
        )
        self._root_holder.root = new_root  # Visible to all wrappers
```

**Why not this:**

- ❌ Complex: Every wrapper needs root holder + path
- ❌ Read overhead: Every access traverses from root
- ❌ Memory: Each wrapper stores path tuple
- ❌ Caching complexity: Need to invalidate caches on mutations
- ✅ Would give persistent data structures (cool!)
- ✅ Structural sharing (memory efficient for large docs)

**Verdict**: Path-tracking is technically interesting but over-engineered for Python bindings. The simpler convert-on-mutation approach (Strategy A) is more pragmatic and Pythonic.

### Practical Example: Large Config with Sparse Mutations

```python
# Load large config file (10MB, 100k keys)
config = libfyaml.load('large-config.yaml')
# State: Frozen, all FyGeneric, ~10MB memory

# Read configuration (fast, no materialization)
db_host = config['database']['host']        # FyGeneric access
api_key = config['services']['api']['key']  # FyGeneric access
# State: Still frozen, no materialization

# Mutate two specific settings
config['app']['debug'] = True
# Materializes: config (dict) -> config['app'] (dict)
# Siblings: config['database'], config['services'], etc. stay FyGeneric

config['database']['pool_size'] = 50
# Materializes: config['database'] (dict)
# Siblings: config['services'], config['logging'], etc. stay FyGeneric

# State now:
# - config: Python dict (thawed)
# - config['app']: Python dict (materialized)
# - config['database']: Python dict (materialized)
# - config['services']: FyGeneric (untouched!)
# - config['logging']: FyGeneric (untouched!)
# - config['monitoring']: FyGeneric (untouched!)
# - ... 99,995 other keys: FyGeneric (untouched!)
# Memory: ~10MB (FyGeneric) + ~1KB (two materialized dicts)

# Freeze back to efficient representation
config = config.freeze()
# Converts back to pure FyGeneric
# Memory: ~10MB (back to compact FyGeneric)
# All mutations preserved!

# Save to file
libfyaml.dump('config-modified.yaml', config)
```

**Performance comparison:**

| Operation | Full Materialization | Shallow Materialization |
|-----------|---------------------|------------------------|
| Load 10MB | 10MB FyGeneric | 10MB FyGeneric |
| Read values | Fast | Fast |
| Mutate 2 keys | Convert all 100k keys<br>(~100ms, 50MB memory) | Convert 2 paths<br>(~0.1ms, +1KB memory) |
| Freeze | N/A (already Python) | Convert modified paths<br>(~0.1ms) |
| Final memory | 50MB (full Python) | 10MB (FyGeneric) |

**Speedup for sparse mutations: ~1000x faster, ~5x less memory!**

### Implementation Priority

1. **Current (v0.2.0)**: FyGeneric is immutable, users call `.to_python()` manually
2. **Next (v0.3.0)**: Shallow materialization with freeze/thaw (this design)
3. **Future**: Optional persistent data structure API for advanced users who want immutability

### Related Design Decisions

- **Multi-document support**: C library has it, needs Python exposure
- **Streaming API**: Could add `load_iter()` for large multi-doc files
- **Custom tags**: Could support via post-processing hooks

---

**Date**: 2025-12-29
**Author**: Claude Code
**Status**: Design note for future implementation
