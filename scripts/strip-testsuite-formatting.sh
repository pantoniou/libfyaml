#!/usr/bin/env bash
#
# strip-testsuite-formatting.sh
#
# Strips formatting information from YAML test suite event output to allow
# comparison between standard and generic test suite outputs.
#
# The generic test suite strips styling information (scalar styles, flow markers,
# anchors, tags, etc.) because generics don't preserve these formatting details.
#
# This script normalizes test.event format by:
# - Removing scalar style indicators (:, ', ", |, >, etc.)
# - Removing flow markers ([], {})
# - Removing anchor definitions (&anchor)
# - Removing tags (!!type)
# - Converting all scalars to double-quoted style for consistency
#
# Usage:
#   strip-testsuite-formatting.sh <input-file> [output-file]
#   cat test.event | strip-testsuite-formatting.sh
#

set -e

input_file="${1:--}"
output_file="${2:--}"

# Process the event stream
process_events() {
    local line
    while IFS= read -r line; do
        # Handle =VAL (scalar values)
        if [[ "$line" =~ ^=VAL ]]; then
            # Remove everything after =VAL and before the actual content
            # Formats:
            #   =VAL :value          -> plain style
            #   =VAL 'value          -> single quoted
            #   =VAL "value          -> double quoted
            #   =VAL |value          -> literal block
            #   =VAL >value          -> folded block
            #   =VAL &anchor :value  -> with anchor
            #   =VAL <tag> :value    -> with tag

            # Strip the =VAL prefix
            content="${line#=VAL }"

            # Remove explicit type tags like <tag:yaml.org,2002:str> or <!mytag>
            # These appear BEFORE the value and are enclosed in < >
            content=$(echo "$content" | sed 's/<[^>]*> //')

            # Remove anchor definitions (&anchor) that appear before the value
            # Anchors can contain any characters including unicode and special chars
            # Match & followed by any non-space characters
            content=$(echo "$content" | sed 's/&[^ ]* //')

            # Remove escaped tags (:\!\!str etc)
            content=$(echo "$content" | sed 's/:\\\\!\\\\![a-zA-Z0-9_-]* //')

            # Now handle the style indicator and value
            # Standard format uses style indicators: :, ', ", |, >
            # Generic format uses: !value

            if [[ ${#content} -gt 0 ]]; then
                first_char="${content:0:1}"

                # If it starts with a style indicator, strip it
                if [[ "$first_char" =~ [:\'\"|\>] ]]; then
                    value="${content:1}"
                    # Normalize "null" to empty
                    if [[ "$value" == "null" ]]; then
                        value=""
                    fi
                    # Normalize floating point values
                    if [[ "$value" =~ ^-?0*[0-9]+\.[0-9]+$ ]]; then
                        # Use awk to normalize float precision (handles leading zeros)
                        value=$(echo "$value" | awk '{printf "%.15g", $0}')
                    fi
                    echo "=VAL !${value}"
                elif [[ "$first_char" == "!" ]]; then
                    # Already in generic format (!value)
                    # Just normalize null
                    value="${content:1}"
                    if [[ "$value" == "null" ]]; then
                        echo "=VAL !"
                    else
                        # Normalize floating point values
                        if [[ "$value" =~ ^-?0*[0-9]+\.[0-9]+$ ]]; then
                            value=$(echo "$value" | awk '{printf "%.15g", $0}')
                        fi
                        echo "=VAL !${value}"
                    fi
                else
                    # No recognized prefix, add !
                    # Normalize floating point values
                    if [[ "$content" =~ ^-?0*[0-9]+\.[0-9]+$ ]]; then
                        content=$(echo "$content" | awk '{printf "%.15g", $0}')
                    fi
                    echo "=VAL !${content}"
                fi
            else
                # Empty content
                echo "=VAL !"
            fi

        # Handle =ALI (aliases) - these reference anchors
        elif [[ "$line" =~ ^=ALI ]]; then
            # Keep aliases as-is for now
            echo "$line"

        # Handle +SEQ and +MAP with flow markers and anchors
        elif [[ "$line" =~ ^\+SEQ ]]; then
            # Remove flow markers [] and anchors &anchor
            echo "+SEQ"
        elif [[ "$line" =~ ^\+MAP ]]; then
            # Remove flow markers {} and anchors &anchor
            echo "+MAP"

        # Handle +DOC with document markers
        elif [[ "$line" =~ ^\+DOC ]]; then
            # Remove document markers like ---
            echo "+DOC"

        # Handle -DOC with document end markers
        elif [[ "$line" =~ ^-DOC ]]; then
            # Remove document end markers like ...
            echo "-DOC"

        # Handle all other event types unchanged
        else
            echo "$line"
        fi
    done
}

# Main processing
if [[ "$input_file" == "-" ]]; then
    process_events
else
    if [[ ! -f "$input_file" ]]; then
        echo "Error: Input file '$input_file' not found" >&2
        exit 1
    fi

    if [[ "$output_file" == "-" ]]; then
        process_events < "$input_file"
    else
        process_events < "$input_file" > "$output_file"
    fi
fi
