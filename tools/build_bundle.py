#!/usr/bin/env python3
"""Builds the two web bundles the firmware serves, pre-compressed.

WHY PRE-COMPRESSED, AND WHY THIS IS THE LARGEST SINGLE IMPROVEMENT AVAILABLE.

The controller served 873 KB of JavaScript uncompressed, over Wi-Fi, from an
ESP32. Measured from the board that is about 27 seconds before the interface
appears -- long enough that people conclude the controller has crashed, reload,
and make it worse. The same bytes gzipped are 216 KB: a quarter of the transfer,
for no loss of anything.

Compressed HERE rather than on the device, because an ESP32 compressing 873 KB
on every page load would spend CPU the control loop needs, on work whose answer
never changes between builds. The bundle is fixed the moment the firmware is
built, so this is the only sensible place for it.

BOTH FORMS ARE EMITTED. Every browser sends Accept-Encoding: gzip, but a
commissioning script written with plain curl does not, and serving it gzip bytes
it did not ask for would hand somebody a file of binary garbage during a site
visit. The uncompressed copy costs flash and removes that whole class of
surprise.

ORDER COMES FROM web/app.js.order AND web/app.css.order, which are the source of
truth. It used to live in three places -- the assets[] arrays in web_server.c,
CMakeLists.txt in a different order, and a JSON file for the preview -- and three
lists mean three chances to disagree, with the disagreement invisible until a
page quietly stops working.

    python tools/build_bundle.py <output-directory>
"""
import gzip
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEB = ROOT / "web"


def read_order(kind):
    """The files to concatenate, in order. Comments and blanks ignored."""
    path = WEB / f"app.{kind}.order"
    if not path.exists():
        raise SystemExit(f"build_bundle: {path} is missing; it is the source of truth "
                         f"for the {kind} serve order")
    names = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        names.append(line)
    if not names:
        raise SystemExit(f"build_bundle: {path} lists no files")
    return names


def build(kind, out_dir):
    names = read_order(kind)
    parts = []
    for name in names:
        source = WEB / name
        if not source.exists():
            # Fatal, not skipped. A missing module that the build shrugs off is
            # a page that loads and does nothing, with no error anywhere.
            raise SystemExit(f"build_bundle: {source} is listed in the {kind} order "
                             f"but does not exist")
        text = source.read_text(encoding="utf-8")
        # A banner per file. It costs a little and it is what makes a stack
        # trace from a customer's browser point at a file rather than at a line
        # number in an 800 KB blob.
        parts.append(f"/* --- {name} --- */\n{text}")

    # A newline between parts, because a file that ends mid-statement would
    # otherwise merge into the next one -- and in JavaScript that produces a
    # syntax error whose reported location is in a different module.
    bundle = "\n".join(parts).encode("utf-8")

    plain = out_dir / f"app_bundle.{kind}"
    compressed = out_dir / f"app_bundle.{kind}.gz"
    plain.write_bytes(bundle)
    # mtime=0 so an unchanged bundle produces byte-identical output and the
    # build does not relink because of a timestamp.
    compressed.write_bytes(gzip.compress(bundle, compresslevel=9, mtime=0))

    saved = 100 - (len(compressed.read_bytes()) * 100 // max(1, len(bundle)))
    print(f"build_bundle: {kind} {len(names)} files, "
          f"{len(bundle) // 1024} KB -> {len(compressed.read_bytes()) // 1024} KB gzip "
          f"({saved}% smaller)")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: build_bundle.py <output-directory>")
    out_dir = pathlib.Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    build("js", out_dir)
    build("css", out_dir)


if __name__ == "__main__":
    main()
