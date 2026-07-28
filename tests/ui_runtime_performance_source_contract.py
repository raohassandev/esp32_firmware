from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
experience = (ROOT / 'web/product-experience-v2.js').read_text(encoding='utf-8')
shell = (ROOT / 'web/product-shell-v2.js').read_text(encoding='utf-8')
mode = (ROOT / 'web/product-mode.js').read_text(encoding='utf-8')
errors = (ROOT / 'web/engineering-errors.js').read_text(encoding='utf-8')

# Large meter/inverter workspaces update their own descendants frequently. Global
# shell/layout observers must never watch the complete main-content subtree.
assert ".observe(main, { childList: true, subtree: true })" not in experience
assert ".observe(main, { childList: true, subtree: true })" not in shell
assert "composeQueued" in experience and "scheduleCompose" in experience
assert "record.target === main" in experience
assert "record.target === main" in shell

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