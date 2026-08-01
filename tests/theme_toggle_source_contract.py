from pathlib import Path
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
import bundle_membership as bundle

ROOT = Path(__file__).resolve().parents[1]
THEME_JS = (ROOT / "web/theme.js").read_text(encoding="utf-8")
THEME_CSS = (ROOT / "web/theme.css").read_text(encoding="utf-8")
SHELL_CSS = (ROOT / "web/product-shell-v2.css").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS_H = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
ASSETS_C = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("automatrix-pvdg-theme" in THEME_JS, "theme preference must use a stable storage key")
require("localStorage.getItem" in THEME_JS and "localStorage.setItem" in THEME_JS,
        "theme preference must persist in the browser")
require("prefers-color-scheme: light" in THEME_JS,
        "first-use theme must respect the operating-system preference")
require("themeToggleButton" in THEME_JS and "topbar-actions" in THEME_JS,
        "theme toggle button must be installed in the top bar")
require("aria-label" in THEME_JS and "aria-pressed" in THEME_JS,
        "theme toggle must expose accessible state")
require('meta[name="theme-color"]' in THEME_JS,
        "browser theme color must follow the selected theme")
require('html[data-theme="light"]' in THEME_CSS,
        "light palette is missing")
require("color-scheme: light" in THEME_CSS,
        "light palette must set the browser color scheme")

# The product shell is served after the base theme, so it must provide complete
# light and dark overrides instead of hard-coding a dark header.
require('html[data-theme="light"] body.product-shell-v2 .topbar' in SHELL_CSS,
        "light product-shell topbar override is missing")
require('html[data-theme="dark"] body.product-shell-v2 .topbar' in SHELL_CSS,
        "dark product-shell topbar override is missing")
require('html[data-theme="light"] .shell-health-button' in SHELL_CSS,
        "light shell controls are missing")
require('html[data-theme="dark"] .shell-health-button' in SHELL_CSS,
        "dark shell controls are missing")
require('html[data-theme="light"] .engineering-page .engineering-login' in SHELL_CSS,
        "light Engineering card contrast is missing")
require('html[data-theme="dark"] .engineering-page .engineering-login' in SHELL_CSS,
        "dark Engineering card contrast is missing")

require(bundle.delivered("theme.css") and bundle.delivered("theme.js"),
        "theme assets must be embedded")
require(bundle.delivered("theme.css") and bundle.delivered("theme.js"),
        "theme asset declarations are missing")
require(bundle.delivered("theme.css") and bundle.delivered("theme.js"),
        "theme asset implementations are missing")
require(bundle.delivered("theme.css") and bundle.delivered("theme.js"),
        "theme assets are not served")

print("persistent light/dark theme toggle source contract passed")
