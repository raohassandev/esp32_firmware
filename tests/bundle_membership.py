"""Does this web module actually reach the browser?

Every contract that used to ask this asked it four times over -- is the file in
CMakeLists, is there a getter in web_assets.h, one in web_assets.c, and is the
getter named in web_server.c -- because under the old architecture all four had
to be true and any one of them could be forgotten.

The bundle replaced all four with one fact: the file is listed in
web/app.js.order or web/app.css.order. The build concatenates exactly that list
and the firmware serves exactly what the build produced, so membership in the
list IS delivery. There is nothing else to check and nothing left to fall out of
step.

This helper exists so the answer has one implementation. Nineteen contracts
asked the question; nineteen copies of the answer is how the next architecture
change becomes a day of edits instead of one.
"""
import pathlib

WEB = pathlib.Path(__file__).resolve().parent.parent / "web"


def _order(kind):
    path = WEB / f"app.{kind}.order"
    if not path.exists():
        raise AssertionError(
            f"web/app.{kind}.order is missing. It is the source of truth for what "
            f"the browser receives; without it nothing is delivered.")
    names = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            names.append(line)
    return names


def delivered(filename):
    """True when web/<filename> is bundled into what the browser downloads."""
    kind = "css" if filename.endswith(".css") else "js"
    return filename in _order(kind)


def require_delivered(*filenames):
    """Raises with a useful message naming every file that is not delivered."""
    missing = [name for name in filenames if not delivered(name)]
    if missing:
        raise AssertionError(
            f"not delivered to the browser: {', '.join(missing)}. "
            f"A module that exists in web/ but is absent from web/app.js.order or "
            f"web/app.css.order is never sent, so the page that needs it silently "
            f"does nothing.")


def position(filename):
    """Index within its bundle, for contracts that assert cascade or load order."""
    kind = "css" if filename.endswith(".css") else "js"
    order = _order(kind)
    if filename not in order:
        raise AssertionError(f"{filename} is not bundled, so it has no position")
    return order.index(filename)
