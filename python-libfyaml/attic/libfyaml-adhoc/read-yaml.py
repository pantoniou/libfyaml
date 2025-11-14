#!/usr/bin/env python3
import sys
import yaml

def main(path):
    with open(path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    yaml.safe_dump(
        data,
        sys.stdout,
        default_flow_style=False,
        allow_unicode=True,
    )

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <file.yaml>", file=sys.stderr)
        sys.exit(1)

    main(sys.argv[1])
