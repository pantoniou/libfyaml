#!/usr/bin/env python3
"""
Clean YAML file by removing/replacing problematic Unicode characters.

Finds and replaces characters that PyYAML/ruamel.yaml reject but are
technically valid UTF-8.
"""

import sys
import unicodedata

def analyze_file(input_path):
    """Analyze file for problematic characters."""
    print(f"Analyzing {input_path}...")

    with open(input_path, 'rb') as f:
        data = f.read()

    print(f"File size: {len(data)} bytes")

    # Find all control characters
    problematic = {}
    for i, byte in enumerate(data):
        # C0 and C1 control characters (except tab, newline, carriage return)
        if byte < 0x20 and byte not in (0x09, 0x0a, 0x0d):  # C0 controls
            problematic.setdefault(byte, []).append(i)
        elif 0x80 <= byte <= 0x9f:  # C1 controls
            problematic.setdefault(byte, []).append(i)

    if problematic:
        print(f"\nFound {sum(len(v) for v in problematic.values())} problematic bytes:")
        for byte_val in sorted(problematic.keys()):
            positions = problematic[byte_val]
            print(f"  0x{byte_val:02x}: {len(positions)} occurrences")
            if len(positions) <= 5:
                print(f"    Positions: {positions}")
            else:
                print(f"    First 5 positions: {positions[:5]}")
    else:
        print("No problematic bytes found!")

    return problematic


def clean_file(input_path, output_path):
    """Clean file by replacing problematic characters."""
    print(f"\nCleaning {input_path} -> {output_path}...")

    with open(input_path, 'rb') as f:
        data = f.read()

    # Decode as UTF-8
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError as e:
        print(f"ERROR: Failed to decode as UTF-8: {e}")
        return False

    # Count replacements
    original_len = len(text)
    replacements = 0

    # Clean the text
    cleaned_chars = []
    for char in text:
        code = ord(char)
        # Keep only valid YAML characters:
        # - Printable ASCII (0x20-0x7E)
        # - Tab (0x09), newline (0x0A), carriage return (0x0D)
        # - Unicode characters > 0x9F
        if code == 0x09 or code == 0x0A or code == 0x0D:  # Tab, LF, CR
            cleaned_chars.append(char)
        elif code >= 0x20 and code <= 0x7E:  # Printable ASCII
            cleaned_chars.append(char)
        elif code > 0x9F:  # Valid Unicode
            cleaned_chars.append(char)
        else:
            # Replace control characters with space or remove
            if code < 0x20 or (0x80 <= code <= 0x9F):
                cleaned_chars.append(' ')  # Replace with space
                replacements += 1

    cleaned_text = ''.join(cleaned_chars)

    print(f"Replaced {replacements} problematic characters")
    print(f"Original length: {original_len} characters")
    print(f"Cleaned length: {len(cleaned_text)} characters")

    # Write cleaned file
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(cleaned_text)

    print(f"✅ Cleaned file saved to {output_path}")
    return True


def show_context(input_path, position, context=50):
    """Show context around a problematic position."""
    with open(input_path, 'rb') as f:
        data = f.read()

    start = max(0, position - context)
    end = min(len(data), position + context)

    print(f"\nContext around position {position}:")
    print(f"Bytes: {data[start:end]}")
    print(f"       {' ' * (position - start)}^ (position {position})")

    # Try to decode as UTF-8
    try:
        text = data[start:end].decode('utf-8', errors='replace')
        print(f"Text: {repr(text)}")
    except:
        print("Could not decode as UTF-8")


def main():
    input_file = "AtomicCards-2.yaml"
    output_file = "AtomicCards-2-cleaned.yaml"

    # Analyze
    problematic = analyze_file(input_file)

    if problematic:
        # Show context for first occurrence of most common problem
        most_common = max(problematic.items(), key=lambda x: len(x[1]))
        byte_val, positions = most_common
        print(f"\nShowing context for 0x{byte_val:02x} at position {positions[0]}:")
        show_context(input_file, positions[0])

    # Clean
    if clean_file(input_file, output_file):
        print("\n" + "="*70)
        print("SUCCESS! Testing cleaned file...")
        print("="*70)

        # Test with PyYAML
        try:
            import yaml
            print("\nTesting with PyYAML...")
            with open(output_file, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f)
            print("✅ PyYAML successfully parsed cleaned file!")
            print(f"   Top-level keys: {list(data.keys())}")
        except Exception as e:
            print(f"❌ PyYAML failed: {e}")

        # Test with ruamel.yaml
        try:
            from ruamel.yaml import YAML
            print("\nTesting with ruamel.yaml...")
            yaml = YAML()
            with open(output_file, 'r', encoding='utf-8') as f:
                data = yaml.load(f)
            print("✅ ruamel.yaml successfully parsed cleaned file!")
            print(f"   Top-level keys: {list(data.keys())}")
        except Exception as e:
            print(f"❌ ruamel.yaml failed: {e}")

        # Test with libfyaml
        try:
            sys.path.insert(0, '.')
            import libfyaml
            print("\nTesting with libfyaml...")
            data = libfyaml.load(output_file)
            print("✅ libfyaml successfully parsed cleaned file!")
            print(f"   Top-level keys: {list(data.keys())}")
        except Exception as e:
            print(f"❌ libfyaml failed: {e}")


if __name__ == '__main__':
    main()
