from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
experience = (ROOT / 'web/product-experience-v2.js').read_text(encoding='utf-8')
shell = (ROOT / 'web/product-shell-v2.js').read_text(encoding='utf-8')
mode = (ROOT / 'web/product-mode.js').read_text(encoding='utf-8')
errors = (ROOT / 'web/engineering-errors.js').read_text(encoding='utf-8')

# Large meter/inverter workspaces update their own descendants frequently, and
# four modules each installed a MutationObserver on #mainContent to re-derive
# the same fact -- "did a page appear?" -- from the same records. There is now
# exactly one observer on that node, in the access module, republished as
# onContentChange. The shell and experience layers must subscribe, not observe.
assert "new MutationObserver" not in experience, "product-experience-v2 must not observe #mainContent"
assert "new MutationObserver" not in shell, "product-shell-v2 must not observe #mainContent"
assert "onContentChange(" in experience and "onContentChange(" in shell
assert mode.count("new MutationObserver") == 1, "there must be exactly one #mainContent observer"
assert "composeQueued" in experience and "scheduleCompose" in experience

# The shared observer must not react to its own subscribers' writes. Records
# generated while subscribers run are drained and discarded, so a subscriber
# that starts adding children to #mainContent still cannot start a feedback
# loop. This is a property of the notifier, not of what subscribers happen to do.
notify = mode[mode.index("function notifyContentChange"):mode.index("async function renewEngineeringSession")]
assert "notifying" in notify, "the shared notifier must guard against re-entry"
assert "takeRecords()" in notify, "the shared notifier must discard records its subscribers caused"
# Only a page appearing or disappearing wakes the default subscribers; a
# subscriber that needs descendant changes must ask for them explicitly.
assert "record.target === document.getElementById('mainContent')" in notify
assert "entry.deep" in notify, "descendant delivery must be opt-in per subscriber"

# The access layer is allowed to maintain visibility, but must not perform route
# composition or navigation regrouping from its DOM observer.
observer_tail = mode[mode.find('new MutationObserver'):]
assert 'groupNavigation' not in observer_tail
assert 'location.hash' not in observer_tail

# Friendly error translation must be idempotent. Generated descendants may not
# contain a fresh raw ESP_ERR_* token or be reconsidered by the observer.
assert "data.diagnosticCode" not in errors
assert "node.dataset.diagnosticCode = translated.code" in errors
assert "detail.textContent = 'Technical diagnostic recorded.'" in errors
assert "detail.textContent = `Diagnostic code: ${translated.code}`" not in errors
assert "!item.closest('.engineering-friendly-error')" in errors
assert "characterData: true" not in errors
assert "node.matches(ERROR_SELECTOR)" in errors

print('UI runtime performance source contract: PASS')