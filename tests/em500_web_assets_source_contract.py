from pathlib import Path
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
import bundle_membership as bundle

ROOT = Path(__file__).resolve().parents[1]
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
ASSETS = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


css_assets = [
    "web_assets_em500_css",
]
js_assets = [
    "web_assets_em500_utils_js",
    "web_assets_em500_core_js",
    "web_assets_em500_quality_js",
    "web_assets_em500_profiles_js",
    "web_assets_em500_plan_js",
]

# EVERY EM500 MODULE REACHES THE BROWSER.
#
# This used to be asserted five ways over -- a getter in the header, one in the
# source, a filename in CMakeLists, a symbol inside the css_handler body and
# another inside js_handler -- because under the old architecture all five had to
# agree. The bundle needs one fact, and five copies of a fact are five chances to
# disagree.
em500_files = [
    "em500.css",
    "em500-utils.js",
    "em500-core.js",
    "em500-quality.js",
    "em500-profiles.js",
    "em500-plan.js",
]
bundle.require_delivered(*em500_files)

# AND IN DEPENDENCY ORDER, which is the part that still needs asserting: core
# calls utils at load time, and the later modules call core. Bundled the other
# way round the calls find nothing defined and the EM500 pages simply do not
# appear, with no error anywhere.
em500_js = [name for name in em500_files if name.endswith(".js")]
positions = [bundle.position(name) for name in em500_js]
require(positions == sorted(positions),
        "EM500 modules must be bundled in utils, core, quality, profiles, plan "
        "order: each calls the ones before it while loading")
# Its delivery is covered by require_delivered above. The linker symbol it used
# to assert on no longer exists: the module is concatenated into the bundle at
# build time rather than embedded on its own.


print("EM500 web asset delivery source contract passed")
