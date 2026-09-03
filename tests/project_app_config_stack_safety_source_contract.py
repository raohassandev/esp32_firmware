#!/usr/bin/env python3
"""Prevent whole app_config_t automatic variables from returning to runtime sources."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
COMPONENTS = ROOT / "components"
MAIN = ROOT / "main"
RUNTIME_ROOTS = (COMPONENTS, MAIN)
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

for runtime_root in RUNTIME_ROOTS:
    if not runtime_root.is_dir():
        raise AssertionError(f"runtime source root is missing: {runtime_root.relative_to(ROOT)}")

# The historical failure class is an automatic whole app_config_t object (~2.5 kB)
# placed on a task stack. Pointers are allowed; static storage is allowed. Runtime
# sources should use heap/static snapshots instead of large automatic frames.
# Arrays and multi-declarator statements are also whole-object stack allocations
# and must not provide an escape hatch around this source contract.
declaration = re.compile(
    r"(?m)^[ \t]*"
    r"((?:(?:static|register|const|volatile)[ \t]+)*)"
    r"app_config_t[ \t]+(?:(?:const|volatile)[ \t]+)*"
    r"([A-Za-z_]\w*)[ \t]*(?:\[[^\]\n]*\][ \t]*)?(?:=[^;\n]*)?[,;]"
)


def is_forbidden_stack_object(match: re.Match) -> bool:
    """Static whole objects are allowed; every other matched whole object is not."""
    return "static" not in match.group(1).split()


positive_contract_cases = (
    "app_config_t cfg;",
    "const app_config_t cfg = {0};",
    "app_config_t const cfg = {0};",
    "register volatile app_config_t cfg;",
    "volatile app_config_t cfg[2];",
    "app_config_t first, second;",
)
negative_contract_cases = (
    "static app_config_t cfg;",
    "const static app_config_t cfg = {0};",
    "static const app_config_t cfg = {0};",
    "app_config_t *cfg;",
    "const app_config_t *cfg;",
    "app_config_t **cfg;",
)
for source in positive_contract_cases:
    match = declaration.search(source)
    if match is None or not is_forbidden_stack_object(match):
        raise AssertionError(f"stack-object detector missed guarded declaration: {source}")
for source in negative_contract_cases:
    match = declaration.search(source)
    if match is not None and is_forbidden_stack_object(match):
        raise AssertionError(f"stack-object detector rejected allowed declaration: {source}")

violations = []
for runtime_root in RUNTIME_ROOTS:
    for path in sorted(runtime_root.rglob("*.c")):
        if path == LEGACY_ALIAS:
            continue
        text = path.read_text(encoding="utf-8")
        for match in declaration.finditer(text):
            if not is_forbidden_stack_object(match):
                continue
            line = text.count("\n", 0, match.start()) + 1
            violations.append(f"{path.relative_to(ROOT)}:{line}: app_config_t {match.group(2)}")

if violations:
    raise AssertionError(
        "whole app_config_t task-stack objects are forbidden; use heap/static storage:\n"
        + "\n".join(violations)
    )

safe = (COMPONENTS / "config_manager/config_manager_stack_safe.c").read_text(encoding="utf-8")
if "malloc(sizeof(*snapshot))" not in safe:
    raise AssertionError("heap-backed public configuration adapter is missing")

print("project-wide app_config_t stack safety contract passed")
