# Operator continuity and truthful verdict — current-dev replay

This slice replays the still-missing presentation behavior from historical PR #54 onto current `dev` without changing the frozen Waveshare physical candidate or its evidence chain.

## Scope

- Preserve keyboard focus and `<details>` disclosure state when operator/alarm DOM subtrees are replaced by existing refresh loops.
- Add a cautious dashboard verdict using only the already-rendered controller status strip: `PLANT NORMAL`, `PLANT STATUS UNKNOWN`, or `ATTENTION REQUIRED`.
- Repair the current shell Theme menu bridge from retired `themeToggle` to the actual `themeToggleButton` control.
- Reduce duplicate top-bar Refresh/Theme controls on narrow screens while keeping both actions available in the existing controller menu.
- Embed the new presentation assets in the current C-composed `/app.js` and `/app.css` bundles.

## Ownership boundary

The new presentation modules do not fetch APIs, add polling, change authentication, derive source state from power sign, write configuration, enable control, or issue inverter commands. One scoped `MutationObserver` watches only existing operator/alarm render roots and the status strip; it does not observe the whole document or body.

This software slice does not alter or satisfy Waveshare physical acceptance, OTA physical qualification, generator transition bench qualification, production inverter-profile approval, FAT, endurance, or SAT gates.
