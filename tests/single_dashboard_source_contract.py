"""One plant overview, whatever the access level.

Unlocking Engineering used to reveal a second, older dashboard underneath the
product view: its own grid power card, its own source banner, its own health
list. The screen a site is run from changed shape at the moment an engineer
signed in, and the two halves stated the same quantities in different words --
"Grid power" beside "Generator power" on a plant running on its generator.

An engineer needs MORE DETAIL than an operator, not a different page.

This contract holds two things:

  1. The legacy dashboard is hidden for everybody, not only for operators. A
     scope test in that function is what produced the second page.

  2. The three facts that lived ONLY on the legacy dashboard survive the
     removal. Hiding a page that is the sole renderer of a value deletes the
     value, and requested PV, applied PV and the meter tariff are exactly the
     evidence an engineer opens the overview to read -- the owner found a plant
     reading tariff 2 while every screen said grid, which is undiagnosable when
     only the verdict is on screen.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
VIEW = ROOT / 'web' / 'operator-view.js'
APP = ROOT / 'web' / 'app.js'

failures = []


def read(path):
    return path.read_text(encoding='utf-8', errors='replace').replace('\r\n', '\n')


view = read(VIEW)
app = read(APP)

# ---------------------------------------------------------------- 1. hidden
match = re.search(r'function hideLegacyOperatorContent\(\)\s*\{(.*?)\n    \}\n',
                  view, re.S)
if not match:
    failures.append('hideLegacyOperatorContent() could not be located')
else:
    body = match.group(1)
    # The dashboard branch must come BEFORE any access-level test, or engineering
    # keeps the second page. Locating it by the guard rather than by a comment:
    # a comment can say the right thing while the code does the opposite.
    guard = body.find('if (!isOperator()) return;')
    # The sweep now names several pages in one list rather than one by hand.
    dashboard = body.find("'dashboard'")
    if dashboard < 0:
        failures.append('the plant overview is no longer hidden by name; if it '
                        'is back in the scope-gated list, engineering sees two '
                        'dashboards again')
    elif guard >= 0 and dashboard > guard:
        failures.append('the plant overview is hidden only after the operator '
                        'guard, so unlocking Engineering reveals the old '
                        'dashboard underneath the product view again')
    # It must not appear in the SECOND list, the one behind the operator guard.
    if guard >= 0 and "'dashboard'" in body[guard:]:
        failures.append("'dashboard' is back in the scope-gated page list")

# ------------------------------------------------- 2. the detail is not lost
#
# Anchored to the engineering evidence block, not to the file: these strings
# appearing anywhere in a 1700-line module would pass while the block itself
# rendered none of them.
#
# Anchored to the PLANT OVERVIEW's block specifically. The first attempt put
# these lines in renderControl(), whose block has the same shape -- the contract
# passed while the plant overview showed none of them, because the content was
# removed from one page and restored on another.
block = re.search(r"const overviewDetail = node\('div', 'op-more-body'\);(.*?)"
                  r"view\.append\(details\('engineering'", view, re.S)
if not block:
    failures.append("the plant overview's engineering detail block could not be "
                    'located; if these lines moved to another page, the content '
                    'removed from the overview is not reachable from it')
else:
    evidence = block.group(1)
    for field, label in (('requested_pv_kw', 'Requested PV'),
                         ('applied_pv_kw', 'Applied PV')):
        if field not in evidence:
            failures.append(f'{label} was on the legacy dashboard and is now '
                            f'rendered nowhere: the controller\'s request and '
                            f'what it actually applied differ during a ramp, '
                            f'and that gap is the first thing an engineer '
                            f'checks when solar did not move as expected')
    if 'tariffLabel' not in evidence:
        failures.append('the meter tariff was on the legacy dashboard and is '
                        'now rendered nowhere: it is the EVIDENCE behind the '
                        'source verdict, and without it a plant reading tariff '
                        '2 while the screen says grid cannot be diagnosed')
    if 'qualityLabel' not in evidence:
        failures.append('the source evidence quality is now rendered nowhere')

# --------------------------------------------- 3. read from cache, not refetched
if 'AutomatrixSourceDetectionCache' not in view:
    failures.append('the product view no longer reads the published source '
                    'detection')
if 'AutomatrixSourceDetectionCache' not in app:
    failures.append('app.js no longer publishes the source detection, so the '
                    'plant overview has nothing to read')
else:
    # Published where it is FETCHED. A cache filled while drawing a banner the
    # overview hides is empty exactly when the overview asks for it.
    fetch = app.find("await api('/api/source-detection')")
    publish = app.find('window.AutomatrixSourceDetectionCache =')
    banner = app.find('function renderSourceBanner()')
    if fetch < 0 or publish < 0:
        failures.append('the source-detection fetch or its publication moved')
    elif banner >= 0 and publish > banner:
        failures.append('the source detection is published inside the banner '
                        'renderer, whose element the plant overview hides — so '
                        'the cache is empty exactly when the overview reads it')

# ---------------------------------------------------- 4. the CSS actually hides
#
# The class was applied and nothing happened: the rule that acts on it was
# scoped to html[data-access="operator"], so under engineering the legacy
# dashboard kept its space on screen and the product view was hidden instead.
# A contract that only checked the JavaScript passed while the page was still
# doubled -- which is why this reads the stylesheet too.
CSS = ROOT / 'web' / 'product-mode.css'
css = read(CSS)

_hide = re.search(r'\[data-page="dashboard"\][^{}]*\.operator-legacy-hidden[^{}]*\{[^{}]*display:\s*none', css)
if not _hide:
    failures.append('nothing hides .operator-legacy-hidden on the plant '
                    'overview at every access level; if the only rule is the '
                    'operator-scoped one, an engineer sees the old dashboard '
                    'occupying the page again')

_show = re.search(r'html\[data-access="engineering"\][^{}]*\[data-page="dashboard"\][^{}]*\.operator-product-view[^{}]*\{[^{}]*display:\s*grid\s*!important', css)
if not _show:
    failures.append('the product view is not restored on the plant overview '
                    'under engineering, so signing in leaves the page with the '
                    'legacy content hidden and nothing to replace it')

if failures:
    print('Single-dashboard contract FAILED:')
    for line in failures:
        print(f'  - {line}')
    sys.exit(1)

print('Single-dashboard contract passed '
      '(one plant overview; requested PV, applied PV and the meter tariff '
      'survive the removal; source detection read from the published cache)')
