#!/usr/bin/env python3
from pathlib import Path

path = Path('tests/waveshare_screen_source_contract.py')
text = path.read_text(encoding='utf-8')
old = '    assert "source_detection_attributed_to(&source)" in product_provider\n'
new = '''    # The provider consumes the shared source-detection STATUS snapshot. The\n    # fail-closed attribution itself remains owned by source_detection_attributed_to()\n    # and is already asserted below; do not duplicate that decision in board code.\n    assert "source_detection_get_status(&source)" in product_provider\n    assert "source_attribution_available(&source)" in product_provider\n    assert "source.attributed_to" in product_provider\n'''
if text.count(old) != 1:
    raise SystemExit(f'expected one legacy direct-attribution assertion, found {text.count(old)}')
text = text.replace(old, new, 1)
path.write_text(text, encoding='utf-8')
print('Waveshare source attribution contract updated for shared status ownership')
