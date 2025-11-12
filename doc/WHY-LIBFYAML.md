# Why libfyaml's Approach Matters Today

The requirements for YAML/JSON libraries have fundamentally changed. What worked in 2010 doesn't address the challenges of 2025.

## The Historical Model: A False Choice

Traditionally, YAML/JSON libraries offered a stark tradeoff:

**Event-based (libyaml)**: Fast streaming, minimal memory, but you're on your own for making sense of the data. You get events like "SCALAR_EVENT: value=42" but must build your own data structures.

**Node-based (yaml-cpp, json-c)**: Build a document tree for easy manipulation, but every value requires heap allocation and manual type checking. Want to know if a value is an integer? Call a function. Want to access an array? Different function. Everything is pointers and manual management.

This worked when YAML/JSON was primarily for **configuration files** with known, static schemas. Parse once, extract known fields, done.

## The Modern Challenge: Interoperability with Sum Types

Today's landscape is radically different. Modern languages have native support for **sum types** (values that can be "this OR that"):

**Rust**:
```rust
enum Message {
    Text(String),
    Image { url: String, alt: String },
    ToolCall { name: String, args: serde_json::Value },
}
```

**TypeScript**:
```typescript
type Response =
    | { type: 'success', data: any }
    | { type: 'error', code: number, message: string }
    | { type: 'pending' }
```

**Python** (with type hints):
```python
from typing import Union
Result = Union[Success, Error, Pending]
```

When these programs serialize to YAML/JSON, they produce documents with **runtime-variant types**. The same field might be an object, an array, or a scalar depending on runtime conditions.

**The problem**: Traditional C libraries have no natural way to represent this. You're forced to:
1. Parse to nodes
2. Manually check types at every access
3. Write verbose boilerplate for every possible variant
4. Hope you didn't miss a case

## Schema Evolution: 'Any' Types Are Everywhere

Modern API schemas embrace type flexibility:

**OpenAPI 3.x** (`oneOf`, `anyOf`):
```yaml
responseBody:
  oneOf:
    - type: object
      properties:
        data: { type: array }
    - type: object
      properties:
        error: { type: string }
    - type: string  # Simple error message
```

**GraphQL unions**:
```graphql
union SearchResult = User | Repository | Issue
```

**JSON Schema**:
```json
{
  "type": ["string", "number", "object"],
  "properties": { ... }
}
```

Code generators (openapi-generator, quicktype, etc.) now produce code with sum types. If you're consuming these APIs in C, you need runtime type flexibility.

## The AI API Explosion

AI APIs have made this even more critical. Consider OpenAI's chat completions API:

```json
{
  "choices": [{
    "message": {
      "role": "assistant",
      "content": "Hello!",          // Sometimes a string
      "tool_calls": [...]            // Sometimes an array
    }
  }],
  "usage": { ... }
}
```

Or Anthropic's Claude API with content blocks:

```json
{
  "content": [
    { "type": "text", "text": "Here's the analysis:" },
    { "type": "tool_use", "id": "...", "name": "...", "input": {...} }
  ]
}
```

**The pattern**: A `type` field discriminates between variants, and the structure changes based on type. Every major AI API (OpenAI, Anthropic, Google, Cohere) uses this pattern.

**The pain point**: Parsing these in C with traditional libraries means:
```c
// Traditional approach - painful!
struct json_object *content_array = json_object_object_get(response, "content");
size_t len = json_object_array_length(content_array);
for (size_t i = 0; i < len; i++) {
    struct json_object *block = json_object_array_get_idx(content_array, i);
    struct json_object *type_obj = json_object_object_get(block, "type");
    const char *type = json_object_get_string(type_obj);

    if (strcmp(type, "text") == 0) {
        struct json_object *text_obj = json_object_object_get(block, "text");
        const char *text = json_object_get_string(text_obj);
        // ... handle text
    } else if (strcmp(type, "tool_use") == 0) {
        struct json_object *id_obj = json_object_object_get(block, "id");
        // ... more manual extraction
    }
    // ... repeat for every field, every type
}
```

This is:
- Verbose (10+ lines for what should be simple)
- Error-prone (forgot null checks? segfault)
- Unnatural (C programmers want switch statements, not nested ifs)
- Unmaintainable (add a new message type? rewrite everything)

## Why Old Approaches Fail

**Event-based libraries**:
```c
// You get events, but what do they mean?
yaml_parser_parse(&parser, &event);
if (event.type == YAML_SCALAR_EVENT) {
    // Is this a type discriminator? A string value? An integer?
    // You have to track context manually
}
```

**Node-based libraries**:
```c
// Manual type checking at every step
if (json_object_is_type(obj, json_type_string)) {
    // handle string
} else if (json_object_is_type(obj, json_type_array)) {
    // handle array
} else if (json_object_is_type(obj, json_type_object)) {
    // handle object
    // Now check nested types...
}
```

Neither gives you a natural **sum type** in C.

## How libfyaml's fy_generic Solves This

libfyaml's generic type system provides **C-native sum types** with clean, expressive APIs:

```c
// Parse AI API response
struct fy_document *doc = fy_document_build_from_string(NULL, response_json, -1);
fy_generic response = fy_document_to_generic(doc);

// Direct lookup with type checking and defaults - one line!
const char *role = fy_generic_get_string_default(
    fy_generic_mapping_lookup(response, fy_to_generic("role")),
    "user"  // default if missing
);

// Get content array
fy_generic content = fy_generic_mapping_lookup(response, fy_to_generic("content"));

// Type checking is simple and clear
if (fy_generic_is_valid(content) && fy_generic_is_sequence(content)) {
    size_t count = fy_generic_sequence_get_item_count(content);
    for (size_t i = 0; i < count; i++) {
        fy_generic block = fy_generic_sequence_get_item(content, i);

        // Direct string extraction with default
        const char *type = fy_generic_get_string_default(
            fy_generic_mapping_lookup(block, fy_to_generic("type")),
            "unknown"
        );

        // Switch on type - natural C pattern matching!
        if (strcmp(type, "text") == 0) {
            const char *text = fy_generic_get_string_default(
                fy_generic_mapping_lookup(block, fy_to_generic("text")),
                ""
            );
            printf("Text: %s\n", text);
        } else if (strcmp(type, "tool_use") == 0) {
            // Access tool fields - same pattern
            const char *tool_name = fy_generic_get_string_default(
                fy_generic_mapping_lookup(block, fy_to_generic("name")),
                "unknown_tool"
            );
            printf("Tool: %s\n", tool_name);
        }
    }
}

// Or switch on the generic type itself for variant handling
// No need for explicit validity check - FYGT_INVALID is a case!
fy_generic value = fy_generic_mapping_lookup(obj, fy_to_generic("data"));
switch (fy_generic_get_type(value)) {
    case FYGT_STRING:
        printf("String: %s\n", fy_generic_get_string(value));
        break;
    case FYGT_INT:
        printf("Int: %lld\n", fy_generic_get_int(value));
        break;
    case FYGT_SEQUENCE:
        printf("Array with %zu items\n",
               fy_generic_sequence_get_item_count(value));
        break;
    case FYGT_MAPPING:
        printf("Object with %zu pairs\n",
               fy_generic_mapping_get_pair_count(value));
        break;
    case FYGT_INVALID:
        printf("Field not found or invalid\n");
        break;
    default:
        printf("Other type\n");
}
```

**Why this works better**:

1. **One type for all values**: `fy_generic` can hold anything (int, string, array, object)
2. **Automatic type detection**: `fy_to_generic(42)` just works
3. **Simple type checking**: `fy_generic_is_string(v)`, `fy_generic_is_sequence(v)`, etc.
4. **Natural pattern matching**: Switch on `fy_generic_get_type()` with `FYGT_INVALID` case for missing fields
5. **No redundant checks**: When using switch, `FYGT_INVALID` handles missing/invalid - no separate validity check needed
6. **Wrapped extraction**: Combine lookup + cast + default in one expression
7. **Value semantics**: Lookups return values, not pointers
8. **Inline storage**: Small values (integers, short strings) have zero allocation overhead
9. **Immutable by design**: Operations create new values—inherent thread safety without locks
10. **Idempotent operations**: Reading same generic always returns same result—perfect for concurrent processing
11. **Works with generated code**: Code generators can target `fy_generic` as a universal type

**Coming soon**: Even cleaner syntax with upcoming `fy_mapping_lookup(map, key_type)` extension for automatic type conversion.

## Real-World Example: AI Chat API

Complete example showing libfyaml handling OpenAI-style responses with clean, modern patterns:

```c
// Response from AI API
const char *response_json = "{"
    "\"choices\": [{"
        "\"message\": {"
            "\"role\": \"assistant\","
            "\"content\": \"I'll help with that.\","
            "\"tool_calls\": ["
                "{\"id\": \"call_abc\", \"function\": {\"name\": \"search\", \"arguments\": \"{}\"}}"
            "]"
        "}"
    "}]"
"}";

// Parse with libfyaml
struct fy_document *doc = fy_document_build_from_string(NULL, response_json, -1);
fy_generic response = fy_document_to_generic(doc);

// Direct access with type checking - clean and safe
fy_generic choices = fy_generic_mapping_lookup(response, fy_to_generic("choices"));

if (fy_generic_is_valid(choices) && fy_generic_is_sequence(choices)) {
    fy_generic first_choice = fy_generic_sequence_get_item(choices, 0);
    fy_generic message = fy_generic_mapping_lookup(first_choice, fy_to_generic("message"));

    if (fy_generic_is_valid(message) && fy_generic_is_mapping(message)) {
        // Extract role with default - one line!
        const char *role = fy_generic_get_string_default(
            fy_generic_mapping_lookup(message, fy_to_generic("role")),
            "assistant"
        );
        printf("Role: %s\n", role);

        // Get content - handles null/missing gracefully
        const char *content = fy_generic_get_string_default(
            fy_generic_mapping_lookup(message, fy_to_generic("content")),
            ""  // Empty string if null/missing
        );
        if (content[0]) {  // Only print if non-empty
            printf("Content: %s\n", content);
        }

        // Check for tool calls
        fy_generic tool_calls = fy_generic_mapping_lookup(message, fy_to_generic("tool_calls"));

        if (fy_generic_is_valid(tool_calls) && fy_generic_is_sequence(tool_calls)) {
            size_t n_tools = fy_generic_sequence_get_item_count(tool_calls);
            printf("Tool calls: %zu\n", n_tools);

            // Process each tool call
            for (size_t i = 0; i < n_tools; i++) {
                fy_generic tool_call = fy_generic_sequence_get_item(tool_calls, i);

                // Extract tool ID and function name with defaults
                const char *id = fy_generic_get_string_default(
                    fy_generic_mapping_lookup(tool_call, fy_to_generic("id")),
                    "unknown"
                );

                fy_generic func = fy_generic_mapping_lookup(tool_call, fy_to_generic("function"));

                if (fy_generic_is_valid(func) && fy_generic_is_mapping(func)) {
                    const char *name = fy_generic_get_string_default(
                        fy_generic_mapping_lookup(func, fy_to_generic("name")),
                        "unknown_function"
                    );

                    printf("  Tool %zu: %s (id=%s)\n", i, name, id);

                    // Arguments might be string (JSON) or object
                    fy_generic args = fy_generic_mapping_lookup(func, fy_to_generic("arguments"));

                    // Switch handles invalid case naturally
                    switch (fy_generic_get_type(args)) {
                        case FYGT_STRING:
                            printf("    Args (JSON string): %s\n",
                                   fy_generic_get_string(args));
                            break;
                        case FYGT_MAPPING:
                            printf("    Args (object): %zu fields\n",
                                   fy_generic_mapping_get_pair_count(args));
                            break;
                        case FYGT_INVALID:
                            printf("    No arguments\n");
                            break;
                        default:
                            printf("    Args: unexpected type\n");
                    }
                }
            }
        }
    }
}

// Cleanup
fy_document_destroy(doc);
```

**Key advantages**:
- **Handles optional fields naturally**: `tool_calls` might not exist - `fy_generic_is_valid()` checks or `FYGT_INVALID` case
- **Handles type variants**: `arguments` could be string or object - `switch` with `FYGT_INVALID` case handles all possibilities
- **No redundant checks**: Switch on type includes `FYGT_INVALID` - missing fields handled naturally
- **One-line extraction**: `fy_generic_get_string_default()` combines lookup + cast + default
- **Simple type checking**: `fy_generic_is_sequence()`, `fy_generic_is_mapping()`
- **Value semantics**: No pointers to manage, values returned directly
- **No manual memory management**: Document owns everything
- **Clean, readable code**: Compares favorably with Python/Rust
- **Natural pattern matching**: C `switch` statement on `fy_generic_get_type()`

This is significantly cleaner and safer than traditional C JSON libraries, while maintaining full performance.

## The Shift in Requirements

| Era | Use Case | Library Model | Limitation |
|-----|----------|---------------|------------|
| **2000s** | Config files | Event-based (libyaml) | No data structures |
| **2010s** | Document manipulation | Node-based (yaml-cpp) | Verbose, heap-heavy |
| **2020s** | API interop, AI, schemas | **Generic-based (libfyaml)** | Sum types native to C |

Modern software needs to:
- Consume APIs designed in Rust/TypeScript/Python
- Handle schemas with `oneOf`/`anyOf`/unions
- Process AI API responses with type discriminators
- Work with code generators that produce sum types
- Do all this in **C**, for performance/compatibility

libfyaml's `fy_generic` is designed for this modern landscape. It brings **sum types to C** in a natural, performant way.
