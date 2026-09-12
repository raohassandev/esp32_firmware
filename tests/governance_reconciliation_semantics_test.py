#!/usr/bin/env python3
"""Guard governance files against self-stale or duplicate release semantics.

A reconciliation change can know the integration parent it started from, but it
cannot know its own future GitHub merge commit. The authoritative documents must
therefore describe the known parent and require a live repository re-fetch.

The historical 87841ece Waveshare graph is also permanently evidence-only after
being superseded by PR #179/#174. Governance must not silently reintroduce that
old image as a second active release path or rewrite its incomplete soak as PASS.
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

    # Historical Waveshare must remain evidence-only and not become an active
    # release dependency again. Its short PASS is preserved, but final >=4 h
    # acceptance and post-soak matrices were never completed.
    assert "status: RETIRED_SUPERSEDED_HISTORICAL_EVIDENCE_ONLY" in execution
    assert "active_release_dependency: false" in execution
    assert "historical_truth: SHORT_PASS_FINAL_4H_BACKEND_PARITY_AND_PERSISTENCE_NOT_COMPLETED" in execution

    agent_registry = contents["AGENT_REGISTRY.yaml"]
    assert "role: RETIRED_EVIDENCE_ONLY" in agent_registry
    assert "release_dependency: false" in agent_registry

    assert "retired_historical_waveshare_evidence:" in gates
    assert "issues_closed_not_planned: [87, 24, 25, 26, 27]" in gates
    assert "prs_closed_unmerged: [20, 57, 67]" in gates
    assert "release_dependency: false" in gates

    todo = contents["TODO.md"]
    todo_lower = todo.lower()
    assert "## retired historical waveshare evidence" in todo_lower
    assert "issues #24/#25/#26/#27/#87 and prs #20/#57/#67 were closed" in todo_lower
    assert "- [ ] **l3 / #87/#27" not in todo_lower

    blockers = contents["BLOCKERS.md"]
    assert "## RETIRED — Historical Waveshare acceptance graph" in blockers
    assert "CLOSED NOT_PLANNED" in blockers
    assert "PRs CLOSED UNMERGED" in blockers

    board = contents["PROGRAM_BOARD.md"]
    assert "L3 | Historical Waveshare release | RETIRED / SUPERSEDED EVIDENCE ONLY" in board
    assert "PR #179 is the sole current Waveshare release candidate" in board

    requirements = contents["REQUIREMENTS_MATRIX.md"]
    assert "R-WAVE-02 | historical uninterrupted >=4 h / >=240 sample soak | NOT COMPLETED / RETIRED SUPERSEDED" in requirements
    assert "R-WAVE-03 | historical backend parity + persistence/ARM | NOT COMPLETED / RETIRED SUPERSEDED" in requirements

    evidence = contents["EVIDENCE_INDEX.md"]
    assert "## Retired historical Waveshare evidence" in evidence
    assert "uninterrupted >=4 h / >=240 run: NOT COMPLETED" in evidence
    assert "PRs #20/#57/#67: CLOSED UNMERGED" in evidence

    print("Governance reconciliation semantics contract passed")


if __name__ == "__main__":
    main()
