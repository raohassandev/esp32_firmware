#!/usr/bin/env python3
"""Guard governance files against self-stale reconciliation snapshots.

A reconciliation change can know the integration parent it started from, but it
cannot know its own future GitHub merge commit. The authoritative documents must
therefore describe the known parent and require a live repository re-fetch,
rather than labelling the pre-merge parent as the post-merge current head.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

MARKDOWN_FILES = (
    "TODO.md",
    "BLOCKERS.md",
    "PROGRAM_BOARD.md",
    "REQUIREMENTS_MATRIX.md",
    "EVIDENCE_INDEX.md",
)
YAML_FILES = (
    "EXECUTION_TREE.yaml",
    "AGENT_REGISTRY.yaml",
    "GATES.yaml",
)
ALL_FILES = MARKDOWN_FILES + YAML_FILES


def main() -> None:
    contents = {name: (ROOT / name).read_text(encoding="utf-8") for name in ALL_FILES}

    # Historical anti-pattern: a governance PR called its known parent the
    # "current/snapshot head", so the document became false as soon as it merged.
    forbidden = (
        "integration_head_at_snapshot:",
        "Snapshot baseline:",
        "snapshot baseline:",
    )
    for name, text in contents.items():
        for token in forbidden:
            assert token not in text, f"{name}: forbidden self-stale snapshot token {token!r}"

    execution = contents["EXECUTION_TREE.yaml"]
    assert "reconciliation_parent_before_this_revision:" in execution
    assert "snapshot_semantics:" in execution
    assert "live_repository_over_stale_docs" in execution

    for name in ("AGENT_REGISTRY.yaml", "GATES.yaml"):
        text = contents[name]
        assert "reconciliation_parent_before_this_revision:" in text, name
        assert "live_repository_over_stale_docs" in text, name

    # Human-readable authorities must explain both halves of the contract:
    # known reconciliation parent + live repository state after that point.
    for name in MARKDOWN_FILES:
        lower = contents[name].lower()
        assert "reconciliation parent" in lower, f"{name}: missing reconciliation-parent semantics"
        assert "live" in lower and "dev" in lower, f"{name}: missing live dev re-fetch/authority wording"

    # #174 evidence semantics must remain topology-correct in the authority set.
    gates = contents["GATES.yaml"]
    assert "transfer_ATS_if_configured_roundtrip_must_pass" in gates
    assert "transfer_ATS_if_not_configured_roundtrip_false_and_nonempty_reason" in gates
    assert "sync_if_configured_roundtrip_must_pass" in gates
    assert "sync_if_not_configured_roundtrip_false_and_nonempty_reason" in gates

    print("Governance reconciliation semantics contract passed")


if __name__ == "__main__":
    main()
