#!/usr/bin/env python3
"""Low-brightness smoke test for one 64x32 HUB75 matrix on Raspberry Pi."""

from __future__ import annotations

import argparse
import os
import sys
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Cycle a HUB75 panel through red, green, blue, and white.",
    )
    parser.add_argument("--rows", type=int, default=32)
    parser.add_argument("--cols", type=int, default=64)
    parser.add_argument("--brightness", type=int, default=10, choices=range(1, 101))
    parser.add_argument("--slowdown", type=int, default=4)
    parser.add_argument("--seconds", type=float, default=1.0)
    parser.add_argument(
        "--mapping",
        default="regular",
        help=(
            "GPIO mapping (default: regular). Try adafruit-hat if the WT board "
            "uses Adafruit-compatible pin routing."
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.seconds <= 0:
        print("--seconds must be greater than zero", file=sys.stderr)
        return 2

    try:
        from rgbmatrix import RGBMatrix, RGBMatrixOptions
    except ImportError:
        print(
            "Missing the rpi-rgb-led-matrix Python bindings.\n"
            "Install prerequisites and the maintained bindings with:\n"
            "  sudo apt install python3-dev python3-pil cython3\n"
            "  python3 -m pip install git+https://github.com/hzeller/"
            "rpi-rgb-led-matrix",
            file=sys.stderr,
        )
        return 1

    if hasattr(os, "geteuid") and os.geteuid() != 0:
        print(
            "Warning: run with sudo for reliable GPIO timing and access.",
            file=sys.stderr,
        )

    options = RGBMatrixOptions()
    options.rows = args.rows
    options.cols = args.cols
    options.chain_length = 1
    options.parallel = 1
    options.hardware_mapping = args.mapping
    options.gpio_slowdown = args.slowdown
    options.brightness = args.brightness

    print(
        f"Starting {args.cols}x{args.rows} HUB75 test: "
        f"mapping={args.mapping}, slowdown={args.slowdown}, "
        f"brightness={args.brightness}%"
    )

    matrix = None
    try:
        matrix = RGBMatrix(options=options)
        colors = (
            ("RED", 255, 0, 0),
            ("GREEN", 0, 255, 0),
            ("BLUE", 0, 0, 255),
            ("WHITE", 255, 255, 255),
        )

        for name, red, green, blue in colors:
            print(name, flush=True)
            matrix.Fill(red, green, blue)
            time.sleep(args.seconds)
            matrix.Clear()
            time.sleep(0.15)

        print("Smoke test complete; clearing panel.")
        return 0
    except KeyboardInterrupt:
        print("\nInterrupted; clearing panel.")
        return 130
    finally:
        if matrix is not None:
            matrix.Clear()


if __name__ == "__main__":
    raise SystemExit(main())
