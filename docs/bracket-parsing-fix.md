# Proper Nested Bracket Handling

## Current Implementation (Naive)

```c
// Only checks first and last character
if (value[0] == '[') {
    size_t len = strlen(value);
    if (len > 2 && value[len-1] == ']') {
        parent_config_str = strndup(value + 1, len - 2);
    }
}
```

**Problem**: Doesn't match brackets - just assumes first '[' matches last ']'

## Proper Implementation (Bracket Depth Tracking)

```c
static char *extract_bracketed_content(const char *value)
{
    size_t len, i;
    int depth = 0;
    int start = -1, end = -1;

    if (!value || value[0] != '[')
        return NULL;

    len = strlen(value);

    /* Find matching closing bracket */
    for (i = 0; i < len; i++) {
        if (value[i] == '[') {
            if (depth == 0)
                start = i;
            depth++;
        } else if (value[i] == ']') {
            depth--;
            if (depth == 0) {
                end = i;
                break;  /* Found matching bracket */
            }
        }
    }

    /* Validate matching brackets */
    if (start != 0 || end != len - 1 || depth != 0) {
        fprintf(stderr, "Unmatched brackets in config\n");
        return NULL;
    }

    /* Extract content between brackets */
    if (end - start <= 1)
        return strdup("");  /* Empty brackets */

    return strndup(value + start + 1, end - start - 1);
}
```

## Test Cases

### Test 1: Simple Brackets
```
Input:  "[linear:size=16M]"
Depth:   123333333333332221  (1 at [, 2 at content, 1 at ])
Output: "linear:size=16M"  ✅
```

### Test 2: Nested Brackets
```
Input:  "[dedup:parent=[linear:size=16M]]"
Depth:   1234444444444445555555555544321
         ^                              ^
         Start at 0                End at last
Output: "dedup:parent=[linear:size=16M]"  ✅
```

### Test 3: Multiple Nested
```
Input:  "[mremap:a=[x,y],b=[p,q]]"
Depth:   1233333323222233223221
         ^                     ^
Output: "mremap:a=[x,y],b=[p,q]"  ✅
```

### Test 4: Unmatched Brackets (Error)
```
Input:  "[mremap:a=[x,y]"
Depth:   12333333233332
         ^            ^ (depth = 1, not 0!)
Output: NULL (error)  ✅
```

### Test 5: Extra Content After Bracket
```
Input:  "[mremap:a=1]extra"
                    ^--- end should be at last char, but it's not!
Output: NULL (error)  ✅
```

## Usage

```c
if (strcmp(key, "parent") == 0) {
    if (value[0] == '[') {
        /* Bracket syntax */
        parent_config_str = extract_bracketed_content(value);
        if (!parent_config_str) {
            fprintf(stderr, "dedup: Invalid bracket syntax in parent config\n");
            goto err_out;
        }
    } else {
        /* No brackets */
        parent_config_str = strdup(value);
        if (!parent_config_str)
            goto err_out;
    }
}
```

## Recursive Bracket Support

For full nested support, **all allocators** need bracket handling, not just dedup:

```c
// In mremap, linear, auto parsers:
if (value[0] == '[') {
    value = extract_bracketed_content(value);
    if (!value)
        goto err_out;
    /* ... parse value ... */
    free(value);
}
```

This allows deep nesting:
```bash
dedup:parent=[dedup:parent=[linear:size=16M],threshold=32],threshold=64
```

Parsing flow:
1. Outer dedup extracts: `dedup:parent=[linear:size=16M],threshold=32`
2. Inner dedup (recursive) extracts: `linear:size=16M`
3. Linear parses: `size=16M`
