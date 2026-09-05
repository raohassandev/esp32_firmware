from pathlib import Path

path = Path("boards/waveshare_esp32_s3_touch_lcd_5/screen/pages/commissioning_screen.c")
text = path.read_text()
old = "Declare installed equipment. Unknown or out-of-scope equipment stays stored but the Core commissioning gate refuses command authority."
new = "Declare installed equipment. Unknown or unsupported equipment remains non-commandable; current Core runtime evidence and profile checks determine command authority."
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected exactly one stale commissioning-gate sentence, found {count}")
path.write_text(text.replace(old, new, 1))

Path(".github/workflows/waveshare-final-wording-patch.yml").unlink()
Path(".github/scripts/waveshare_final_wording_patch.py").unlink()
