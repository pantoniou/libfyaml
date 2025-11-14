# Generic API Examples

This document provides real-world examples and language comparisons for libfyaml's generic type system.

## Real-World Example: Anthropic Messages API

### Parsing a Response

```c
#include <libfyaml.h>

void parse_anthropic_response(const char *json_response) {
    // Parse JSON to document
    struct fy_document *doc = fy_document_build_from_string(NULL, json_response, -1);
    fy_generic response = fy_document_to_generic(doc);

    // Extract top-level fields - clean and readable
    const char *id = fy_map_get(response, "id", "");
    const char *model = fy_map_get(response, "model", "");
    const char *role = fy_map_get(response, "role", "assistant");
    const char *stop_reason = fy_map_get(response, "stop_reason", "");

    printf("Message ID: %s\n", id);
    printf("Model: %s\n", model);
    printf("Role: %s\n", role);
    printf("Stop reason: %s\n", stop_reason);

    // Chained lookup with fy_map_empty - just like Python!
    int input_tokens = fy_map_get(
        fy_map_get(response, "usage", fy_map_empty),
        "input_tokens",
        0
    );
    int output_tokens = fy_map_get(
        fy_map_get(response, "usage", fy_map_empty),
        "output_tokens",
        0
    );

    printf("Tokens: %d input, %d output\n", input_tokens, output_tokens);

    // Get content array - use handles for type safety
    fy_seq_handle content = fy_map_get(response, "content", fy_seq_invalid);

    // Polymorphic operations: fy_len() and fy_get_item() work on handles!
    for (size_t i = 0; i < fy_len(content); i++) {
        fy_generic block = fy_get_item(content, i);

        const char *type = fy_map_get(block, "type", "unknown");

        if (strcmp(type, "text") == 0) {
            const char *text = fy_map_get(block, "text", "");
            printf("  [text] %s\n", text);

        } else if (strcmp(type, "tool_use") == 0) {
            const char *tool_id = fy_map_get(block, "id", "");
            const char *tool_name = fy_map_get(block, "name", "");

            printf("  [tool_use] %s (id: %s)\n", tool_name, tool_id);

            // Chained lookup into tool input
            const char *query = fy_map_get(
                fy_map_get(block, "input", fy_map_empty),
                "query",
                ""
            );

            if (query[0]) {
                printf("    Query: %s\n", query);
            }
        }
    }

    fy_document_destroy(doc);
}
```

### Building a Request

```c
struct fy_document *build_anthropic_request(struct fy_generic_builder *gb) {
    // Create message history with natural nesting
    fy_generic messages = fy_gb_sequence(gb,
        fy_mapping(
            "role", "user",
            "content", "What's the weather in San Francisco?"
        ),
        fy_mapping(
            "role", "assistant",
            "content", fy_sequence(
                fy_mapping(
                    "type", "text",
                    "text", "I'll check the weather for you."
                ),
                fy_mapping(
                    "type", "tool_use",
                    "id", "toolu_01A",
                    "name", "get_weather",
                    "input", fy_mapping("location", "San Francisco, CA")
                )
            )
        ),
        fy_mapping(
            "role", "user",
            "content", fy_sequence(
                fy_mapping(
                    "type", "tool_result",
                    "tool_use_id", "toolu_01A",
                    "content", "72°F and sunny"
                )
            )
        )
    );

    // Define available tools
    fy_generic tools = fy_gb_sequence(gb,
        fy_mapping(
            "name", "get_weather",
            "description", "Get current weather for a location",
            "input_schema", fy_mapping(
                "type", "object",
                "properties", fy_mapping(
                    "location", fy_mapping(
                        "type", "string",
                        "description", "City and state, e.g. San Francisco, CA"
                    )
                ),
                "required", fy_sequence("location")
            )
        )
    );

    // Build complete request
    fy_generic request = fy_gb_mapping(gb,
        "model", "claude-3-5-sonnet-20241022",
        "max_tokens", 1024,
        "messages", messages,
        "tools", tools
    );

    return fy_generic_to_document(NULL, request);
}
```

### Polymorphic Operations in Action

```c
void demonstrate_polymorphic_ops(fy_generic data) {
    // Extract both sequences and mappings using handles
    fy_seq_handle messages = fy_map_get(data, "messages", fy_seq_invalid);
    fy_map_handle config = fy_map_get(data, "config", fy_map_invalid);
    const char *api_key = fy_map_get(data, "api_key", "");

    // fy_len() works polymorphically on all types!
    printf("Messages: %zu\n", fy_len(messages));    // sequence
    printf("Config: %zu\n", fy_len(config));         // mapping
    printf("API Key: %zu chars\n", fy_len(api_key)); // string

    // fy_get_item() works on both sequences (by index) and mappings (by key)
    if (fy_is_valid(messages)) {
        for (size_t i = 0; i < fy_len(messages); i++) {
            fy_generic msg = fy_get_item(messages, i);  // sequence indexing
            const char *role = fy_map_get(msg, "role", "");
            printf("  Message %zu: %s\n", i, role);
        }
    }

    if (fy_is_valid(config)) {
        // fy_get_item() also works on mappings!
        fy_generic host = fy_get_item(config, "host");
        fy_generic port = fy_get_item(config, "port");

        printf("  Host: %s\n", fy_get(host, "localhost"));
        printf("  Port: %d\n", fy_get(port, 8080));
    }
}
```

### Pattern Matching on Sum Types

```c
void handle_content_block(fy_generic block) {
    const char *type = fy_map_get(block, "type", "");

    if (strcmp(type, "text") == 0) {
        const char *text = fy_map_get(block, "text", "");
        printf("Text: %s\n", text);

    } else if (strcmp(type, "image") == 0) {
        fy_generic source = fy_map_get(block, "source", fy_map_empty);
        const char *source_type = fy_map_get(source, "type", "");
        const char *media_type = fy_map_get(source, "media_type", "");

        printf("Image: %s (%s)\n", source_type, media_type);

    } else if (strcmp(type, "tool_use") == 0) {
        const char *id = fy_map_get(block, "id", "");
        const char *name = fy_map_get(block, "name", "");

        // Extract input - could be mapping, sequence, or scalar
        fy_map_handle input_map = fy_map_get(block, "input", fy_map_invalid);
        fy_seq_handle input_seq = fy_map_get(block, "input", fy_seq_invalid);

        printf("Tool: %s (id=%s)\n", name, id);

        // fy_len() works polymorphically!
        if (fy_is_valid(input_map)) {
            printf("  Parameters: %zu fields\n", fy_len(input_map));
        } else if (fy_is_valid(input_seq)) {
            printf("  Parameters: %zu items\n", fy_len(input_seq));
        }
    }
}
```

## Language Comparisons

### Simple Lookup with Defaults

**Python**:
```python
role = message.get("role", "assistant")
port = config.get("port", 8080)
enabled = settings.get("enabled", True)
```

**TypeScript**:
```typescript
const role = message.role ?? "assistant";
const port = config.port ?? 8080;
const enabled = settings.enabled ?? true;
```

**Rust**:
```rust
let role = message.get("role").unwrap_or("assistant");
let port = config.get("port").unwrap_or(8080);
let enabled = settings.get("enabled").unwrap_or(true);
```

**libfyaml**:
```c
const char *role = fy_map_get(message, "role", "assistant");
int port = fy_map_get(config, "port", 8080);
bool enabled = fy_map_get(settings, "enabled", true);
```

### Nested Navigation

**Python**:
```python
port = root.get("server", {}).get("config", {}).get("port", 8080)
```

**TypeScript**:
```typescript
const port = root.server?.config?.port ?? 8080;
```

**Rust**:
```rust
let port = root
    .get("server").and_then(|s| s.as_object())
    .and_then(|s| s.get("config")).and_then(|c| c.as_object())
    .and_then(|c| c.get("port")).and_then(|p| p.as_i64())
    .unwrap_or(8080);
```

**libfyaml**:
```c
int port = fy_map_get(
    fy_map_get(
        fy_map_get(root, "server", fy_map_empty),
        "config",
        fy_map_empty
    ),
    "port",
    8080
);
```

*Note: TypeScript has the cleanest syntax with optional chaining, but libfyaml's approach is nearly as concise and more explicit about defaults.*

### Polymorphic Operations (len/length)

**Python**:
```python
users = data.get("users", [])
config = data.get("config", {})
name = "example"

print(f"Users: {len(users)}")
print(f"Config: {len(config)}")
print(f"Name: {len(name)}")
```

**TypeScript**:
```typescript
const users = data.users ?? [];
const config = data.config ?? {};
const name = "example";

console.log(`Users: ${users.length}`);
console.log(`Config: ${Object.keys(config).length}`);
console.log(`Name: ${name.length}`);
```

**Rust**:
```rust
let users = data.get("users").and_then(|v| v.as_array()).unwrap_or(&vec![]);
let config = data.get("config").and_then(|v| v.as_object()).unwrap_or(&Map::new());
let name = "example";

println!("Users: {}", users.len());
println!("Config: {}", config.len());
println!("Name: {}", name.len());
```

**libfyaml**:
```c
fy_seq_handle users = fy_map_get(data, "users", fy_seq_invalid);
fy_map_handle config = fy_map_get(data, "config", fy_map_invalid);
const char *name = "example";

printf("Users: %zu\n", fy_len(users));
printf("Config: %zu\n", fy_len(config));
printf("Name: %zu\n", fy_len(name));
```

*All four languages support polymorphic length operations. libfyaml's `fy_len()` matches Python's `len()` and Rust's `.len()` semantics.*

### Iteration

**Python**:
```python
users = data.get("users", [])
for user in users:
    name = user.get("name", "anonymous")
    print(name)

# Or with index
for i in range(len(users)):
    user = users[i]
    print(user.get("name", "anonymous"))
```

**TypeScript**:
```typescript
const users = data.users ?? [];
for (const user of users) {
    const name = user.name ?? "anonymous";
    console.log(name);
}

// Or with index
for (let i = 0; i < users.length; i++) {
    const user = users[i];
    console.log(user.name ?? "anonymous");
}
```

**Rust**:
```rust
let users = data.get("users").and_then(|v| v.as_array()).unwrap_or(&vec![]);
for user in users {
    let name = user.get("name")
        .and_then(|v| v.as_str())
        .unwrap_or("anonymous");
    println!("{}", name);
}

// Or with index
for i in 0..users.len() {
    let user = &users[i];
    let name = user.get("name")
        .and_then(|v| v.as_str())
        .unwrap_or("anonymous");
    println!("{}", name);
}
```

**libfyaml**:
```c
fy_seq_handle users = fy_map_get(data, "users", fy_seq_invalid);
for (size_t i = 0; i < fy_len(users); i++) {
    fy_generic user = fy_get_item(users, i);
    const char *name = fy_get(user, "anonymous");
    printf("%s\n", name);
}
```

*libfyaml's iteration is nearly identical to Rust's indexed approach and comparable to Python/TypeScript.*

### Building Data Structures

**Python**:
```python
config = {
    "host": "localhost",
    "port": 8080,
    "users": ["alice", "bob", "charlie"],
    "enabled": True
}
```

**TypeScript**:
```typescript
const config = {
    host: "localhost",
    port: 8080,
    users: ["alice", "bob", "charlie"],
    enabled: true
};
```

**Rust (with serde_json)**:
```rust
let config = json!({
    "host": "localhost",
    "port": 8080,
    "users": ["alice", "bob", "charlie"],
    "enabled": true
});
```

**libfyaml**:
```c
fy_generic config = fy_mapping(
    "host", "localhost",
    "port", 8080,
    "users", fy_sequence("alice", "bob", "charlie"),
    "enabled", true
);
```

*All four languages support concise literal syntax for building data structures.*

### Type Checking and Pattern Matching

**Python**:
```python
if isinstance(value, dict):
    print(f"Map with {len(value)} entries")
elif isinstance(value, list):
    print(f"List with {len(value)} items")
elif isinstance(value, str):
    print(f"String: {value}")
else:
    print(f"Other: {value}")
```

**TypeScript**:
```typescript
if (typeof value === "object" && !Array.isArray(value)) {
    console.log(`Map with ${Object.keys(value).length} entries`);
} else if (Array.isArray(value)) {
    console.log(`List with ${value.length} items`);
} else if (typeof value === "string") {
    console.log(`String: ${value}`);
} else {
    console.log(`Other: ${value}`);
}
```

**Rust (with serde_json::Value)**:
```rust
match value {
    Value::Object(map) => println!("Map with {} entries", map.len()),
    Value::Array(arr) => println!("List with {} items", arr.len()),
    Value::String(s) => println!("String: {}", s),
    _ => println!("Other: {:?}", value),
}
```

**libfyaml**:
```c
switch (fy_type(value)) {
    case FYGT_MAPPING:
        printf("Map with %zu entries\n", fy_map_count(value));
        break;
    case FYGT_SEQUENCE:
        printf("List with %zu items\n", fy_seq_count(value));
        break;
    case FYGT_STRING:
        printf("String: %s\n", fy_string_get(value));
        break;
    default:
        printf("Other type\n");
        break;
}
```

*Rust's pattern matching is most concise. libfyaml's switch statement is comparable and more concise than Python/TypeScript.*

### Complex Example: Processing API Response

**Python**:
```python
def process_response(data):
    users = data.get("users", [])
    total = 0

    for user in users:
        if user.get("active", False):
            age = user.get("age", 0)
            total += age
            profile = user.get("profile", {})
            name = profile.get("name", "unknown")
            print(f"{name}: {age}")

    return total / len(users) if len(users) > 0 else 0
```

**TypeScript**:
```typescript
function processResponse(data: any): number {
    const users = data.users ?? [];
    let total = 0;

    for (const user of users) {
        if (user.active ?? false) {
            const age = user.age ?? 0;
            total += age;
            const profile = user.profile ?? {};
            const name = profile.name ?? "unknown";
            console.log(`${name}: ${age}`);
        }
    }

    return users.length > 0 ? total / users.length : 0;
}
```

**Rust**:
```rust
fn process_response(data: &Value) -> f64 {
    let users = data.get("users")
        .and_then(|v| v.as_array())
        .unwrap_or(&vec![]);
    let mut total = 0;

    for user in users {
        if user.get("active").and_then(|v| v.as_bool()).unwrap_or(false) {
            let age = user.get("age").and_then(|v| v.as_i64()).unwrap_or(0);
            total += age;
            let profile = user.get("profile").and_then(|v| v.as_object()).unwrap_or(&Map::new());
            let name = profile.get("name").and_then(|v| v.as_str()).unwrap_or("unknown");
            println!("{}: {}", name, age);
        }
    }

    if users.len() > 0 { total as f64 / users.len() as f64 } else { 0.0 }
}
```

**libfyaml**:
```c
double process_response(fy_generic data) {
    fy_seq_handle users = fy_map_get(data, "users", fy_seq_invalid);
    int total = 0;

    for (size_t i = 0; i < fy_len(users); i++) {
        fy_generic user = fy_get_item(users, i);

        if (fy_map_get(user, "active", false)) {
            int age = fy_map_get(user, "age", 0);
            total += age;

            fy_generic profile = fy_map_get(user, "profile", fy_map_empty);
            const char *name = fy_map_get(profile, "name", "unknown");

            printf("%s: %d\n", name, age);
        }
    }

    return fy_len(users) > 0 ? (double)total / fy_len(users) : 0.0;
}
```

### Key Observations

1. **Python vs libfyaml**: Nearly identical ergonomics. libfyaml requires type prefixes (`fy_`) and explicit length in loops, but otherwise matches Python's conciseness.

2. **TypeScript vs libfyaml**: TypeScript's optional chaining (`?.`) is more concise for nested access, but libfyaml's explicit defaults are clearer. Both have similar iteration patterns.

3. **Rust vs libfyaml**: Rust's type safety requires more verbose unwrapping. libfyaml achieves similar safety through `_Generic` dispatch but with less ceremony. Rust's pattern matching is more powerful, but libfyaml's switch statements are competitive.

4. **Unified operations**: libfyaml's `fy_len()` provides the same polymorphism as Python's `len()`, Rust's `.len()`, and TypeScript's `.length`, working across sequences, mappings, and strings.

5. **Performance**: libfyaml matches or exceeds all three languages:
   - **vs Python**: Zero-copy, inline storage, no GC pauses
   - **vs TypeScript**: No V8 overhead, direct memory access
   - **vs Rust**: Comparable performance with simpler syntax

### Summary

libfyaml achieves **Python-level ergonomics** while maintaining **C-level performance**:
- Concise syntax through `_Generic` dispatch
- Natural defaults with `fy_map_empty` and `fy_seq_empty`
- Polymorphic operations (`fy_len`, `fy_get_item`)
- Type safety at compile time
- Zero runtime overhead

The API proves that systems languages can match the ergonomics of high-level languages without sacrificing performance or safety.
