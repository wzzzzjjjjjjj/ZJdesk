"""Generate and compare a deterministic RGB codec fixture; no desktop capture."""

import argparse
import json
import pathlib
import re

WIDTH, HEIGHT = 1920, 1080
REGIONS = {
    "gradient": (0, 0, WIDTH, 128),
    "thin_shapes": (0, 200, WIDTH, 400),
    "single_pixel_columns": (0, 1000, 1800, 32),
}
COLORS = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255), (0, 0, 0)]


def generate(path):
    image = bytearray()
    for y in range(HEIGHT):
        row = bytearray()
        for x in range(WIDTH):
            color = (24, 28, 32)
            if y < 128:
                level = x * 255 // (WIDTH - 1)
                color = (level, level, level) if y < 64 else (level, 255 - level, level // 2)
            elif 200 <= y < 600:
                if x % 32 == 0 or (y - 200) % 32 == 0:
                    color = COLORS[(x // 32 + y // 32) % len(COLORS)]
            elif 1000 <= y < 1032 and x < 1800:
                color = COLORS[x % 3]
            row.extend(color)
        image.extend(row)
    path.write_bytes(image)
    print(json.dumps({"width": WIDTH, "height": HEIGHT, "format": "RGB", "bytes": len(image)}))


def compare(original_path, decoded_path):
    original, decoded = original_path.read_bytes(), decoded_path.read_bytes()
    expected_size = WIDTH * HEIGHT * 3
    if len(original) != expected_size or len(decoded) != expected_size:
        raise ValueError("Expected exactly one complete 1920x1080 RGB frame in each file")
    test_source = (pathlib.Path(__file__).resolve().parents[2] / "tests/ui/tst_window.cpp").read_text(encoding="utf-8")
    mean_limit = float(re.search(r"MaximumDesktopMeanError\s*=\s*([\d.]+)", test_source)[1])
    channel_limit = int(re.search(r"MaximumDesktopChannelError\s*=\s*(\d+)", test_source)[1])
    report = {"thresholds": {"meanErrorExclusive": mean_limit, "maximumErrorInclusive": channel_limit}, "regions": {}}
    for name, (left, top, width, height) in {"full_frame": (0, 0, WIDTH, HEIGHT), **REGIONS}.items():
        total, maximum = 0, 0
        for y in range(top, top + height):
            start = (y * WIDTH + left) * 3
            end = start + width * 3
            for expected, actual in zip(original[start:end], decoded[start:end]):
                error = abs(expected - actual)
                total += error
                maximum = max(maximum, error)
        mean = total / (width * height * 3)
        report["regions"][name] = {"meanError": mean, "maximumError": maximum,
            "passed": mean < mean_limit and maximum <= channel_limit}
    report["passed"] = all(region["passed"] for region in report["regions"].values())
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    source = commands.add_parser("generate")
    source.add_argument("output", type=pathlib.Path)
    comparison = commands.add_parser("compare")
    comparison.add_argument("original", type=pathlib.Path)
    comparison.add_argument("decoded", type=pathlib.Path)
    args = parser.parse_args()
    if args.command == "generate":
        generate(args.output)
        return 0
    return compare(args.original, args.decoded)


if __name__ == "__main__":
    raise SystemExit(main())
