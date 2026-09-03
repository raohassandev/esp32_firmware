#!/usr/bin/env python3
"""Prevent whole app_config_t automatic variables from returning to runtime components."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ROOT / "components"
CMAKE = (COMPONENTS / "config_manager/CMakeLists.txt").read_text(encoding="utf-8")

# config_manager_v6.c is intentionally retained only behind non-public compatibility
# aliases. Its public init/import/export symbols are owned by the heap-backed
# config_manager_stack_safe.c adapter. Keep that exception explicit and guarded.
LEGACY_ALIAS = COMPONENTS / "config_manager/config_manager_v6.c"
for token in (
    "config_manager_init=config_manager_init_v6_stack_legacy",
    "config_manager_import_json=config_manager_import_json_v6_stack_legacy",
    "config_manager_export_json=config_manager_export_json_v6_stack_legacy",
):
    if token not in CMAKE:
        raise AssertionError(f"legacy config stack exception lost its non-public alias: {token}")

# The historical failure class is an automatic whole app_config_t object (~2.5 kB)
# placed on a task stack. Pointers are allowed; static storage is allowed. Runtime
# components should use heap/static snapshots instead of large automatic frames.
stack_object = re.compile(
    r"(?m)^[ \t]*(?!static\b)(?:const[ \t]+)?app_config_t[ \t]+"
    r"([A-Za-z_]\w*)[ \t]*(?:=[^;\n]*)?;"
)

violations = []
for path in sorted(COMPONENTS.rglob("*.c")):
    if path == LEGACY_ALIAS:
        continue
    text = path.read_text(encoding="utf-8")
    for match in stack_object.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        violations.append(f"{path.relative_to(ROOT)}:{line}: app_config_t {match.group(1)}")

if violations:
    raise AssertionError(
        "whole app_config_t task-stack objects are forbidden; use heap/static storage:\n"
        + "\n".join(violations)
    )

safe = (COMPONENTS / "config_manager/config_manager_stack_safe.c").read_text(encoding="utf-8")
if "malloc(sizeof(*snapshot))" not in safe:
    raise AssertionError("heap-backed public configuration adapter is missing")

print("project-wide app_config_t stack safety contract passed")
