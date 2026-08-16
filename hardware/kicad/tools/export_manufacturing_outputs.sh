#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
K="$ROOT/hardware/kicad"
PCB="$K/Automatrix_PVDG_RevA.kicad_pcb"
SCH="$K/Automatrix_PVDG_RevA.kicad_sch"
OUT="$K/manufacturing"
CLI="${KICAD_CLI:-kicad-cli}"
[[ -s "$PCB" ]] || { echo "H2 board missing: $PCB" >&2; exit 2; }
[[ -s "$SCH" ]] || { echo "schematic missing: $SCH" >&2; exit 2; }
[[ -f "$K/H2_ROUTING_COMPLETE" ]] || { echo "H2_ROUTING_COMPLETE missing; refusing manufacturing-final export" >&2; exit 2; }
# Canonical logical/electrical audits are independent of KiCad's generated
# footprint metadata/path parity, which is not a manufacturing connectivity gate.
python3 "$K/tools/validate_exported_netlist.py"
python3 "$K/tools/validate_hw_interface_contract.py"
python3 "$K/tools/validate_power_relay_budget.py"
rm -rf "$OUT"; mkdir -p "$OUT/gerber" "$OUT/drill" "$OUT/drawings"
"$CLI" pcb drc --severity-all --exit-code-violations --refill-zones --save-board --output "$OUT/DRC_FINAL.rpt" "$PCB"
"$CLI" sch erc --severity-all --exit-code-violations --output "$OUT/ERC_FINAL.rpt" "$SCH"
"$CLI" pcb export gerbers --output "$OUT/gerber" --layers "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts" --precision 6 --check-zones "$PCB"
"$CLI" pcb export drill --output "$OUT/drill" --format excellon --excellon-units mm --excellon-separate-th --generate-map --map-format pdf --generate-report --report-path "$OUT/drill/DRILL_REPORT.rpt" "$PCB"
"$CLI" pcb export pos --output "$OUT/Automatrix_PVDG_RevA-cpl.csv" --side both --format csv --units mm --exclude-dnp "$PCB"
"$CLI" pcb export ipcd356 --output "$OUT/Automatrix_PVDG_RevA.ipc" "$PCB"
"$CLI" sch export bom --output "$OUT/Automatrix_PVDG_RevA-bom.csv" "$SCH"
"$CLI" pcb export pdf --mode-single --black-and-white --sketch-pads-on-fab-layers --layers "F.Fab,Edge.Cuts" --output "$OUT/drawings/ASSEMBLY_TOP.pdf" "$PCB"
"$CLI" pcb export pdf --mode-single --black-and-white --mirror --sketch-pads-on-fab-layers --layers "B.Fab,Edge.Cuts" --output "$OUT/drawings/ASSEMBLY_BOTTOM.pdf" "$PCB"
"$CLI" pcb export step --force --no-dnp --output "$OUT/Automatrix_PVDG_RevA.step" "$PCB"
"$CLI" pcb export stats --format json --output "$OUT/Automatrix_PVDG_RevA-stats.json" "$PCB"
find "$OUT/gerber" -type f -size +0c | grep -q .
find "$OUT/drill" -type f -size +0c | grep -q .
for f in "$OUT/Automatrix_PVDG_RevA-cpl.csv" "$OUT/Automatrix_PVDG_RevA-bom.csv" "$OUT/Automatrix_PVDG_RevA.step" "$OUT/DRC_FINAL.rpt" "$OUT/ERC_FINAL.rpt"; do [[ -s "$f" ]]; done
echo "MANUFACTURING EXPORT PASS: $OUT"
