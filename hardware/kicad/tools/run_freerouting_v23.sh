#!/usr/bin/env bash
set -euo pipefail
PCB='hardware/kicad/Automatrix_PVDG_RevA.kicad_pcb'
PLACED='hardware/kicad/Automatrix_PVDG_RevA-placed.kicad_pcb'
DSN='hardware/kicad/Automatrix_PVDG_RevA.dsn'
SES='hardware/kicad/Automatrix_PVDG_RevA.ses'
PRE_DRC='hardware/kicad/Automatrix_PVDG_RevA-pre-route-drc.rpt'
PRE_SI='hardware/kicad/Automatrix_PVDG_RevA-pre-route-si.rpt'
KPY="docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/work -w /work kicad/kicad:10.0.5 python3"
K="docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/work -w /work kicad/kicad:10.0.5 kicad-cli"

: > hardware/kicad/freerouting.log

# Establish the dedicated L2 GND plane and reserve the proven dense-pad GND
# escape vias before any signal router can occupy their vertical clearance.
$KPY hardware/kicad/tools/post_route_finalize.py "$PCB"

# USB and W5500 MDI topology are release-owned, not generic-autorouter-owned.
# Route and lock them before Specctra export. This is required by the frozen SI
# contract: Run #24's generic Ethernet route was 64..81 mm with 3..5 vias and
# B.Cu usage, while the contract is <=45 mm, <=1 via and F.Cu/In2.Cu only.
$KPY hardware/kicad/tools/pre_route_critical_nets_release.py "$PCB"

# Refill L2 and prove the controlled critical geometry is DRC-clean before
# spending the Freerouting budget. Unconnected low-speed nets are expected here,
# so inspect the explicit DRC-violation count instead of using KiCad's exit code.
$K pcb drc --severity-error --refill-zones --save-board --output "$PRE_DRC" "$PCB" || true
if ! grep -Fq '** Found 0 DRC violations **' "$PRE_DRC"; then
  cat "$PRE_DRC"
  echo 'CRITICAL_PREROUTE_DRC=FAIL' >&2
  exit 1
fi
echo 'CRITICAL_PREROUTE_DRC=PASS violations=0'

# The same release SI validator used after full routing must already pass here.
# This makes a bad fixed high-speed topology fail in seconds, before the generic
# router can hide it behind a later connectivity failure.
$KPY hardware/kicad/tools/validate_signal_integrity.py "$PCB" "$PRE_SI"
echo 'CRITICAL_PREROUTE_SI=PASS'

cp "$PCB" "$PLACED"
$KPY hardware/kicad/tools/route_reva_freerouting.py export "$PLACED" "$DSN"
python3 hardware/kicad/tools/prepare_dsn_plane_classes.py "$DSN"

curl -fL --retry 3 --retry-delay 2 -o /tmp/freerouting.jar \
  https://github.com/freerouting/freerouting/releases/download/v2.3.0/freerouting-2.3.0.jar

# Freerouting's -inc option intentionally ignores GND_PLANE. Those ignored GND
# ratsnest items remain visible in Freerouting's "unrouted" score even though
# they are closed later by KiCad surface copper + L2 stitching. Critical USB and
# Ethernet routes are already present/locked in the exported DSN; generic routing
# owns only the remaining low-speed nets. KiCad remains final DRC/connectivity
# authority.
#
# Run #19 proved the former 420 s shell timeout was too tight for the same
# bounded 10-pass job: pass #8 completed successfully, then timeout killed Java
# with exit 124 before an SES could be written. Keep -mp 10 and deterministic
# headroom; the enclosing Actions job still has its own 35 minute cap.
#
# Run 32339166203 proved fanout=true is not a valid fallback on this board: it
# generated 72 x 0.15 mm tracks below the 0.20 mm board minimum.
success=0
attempt=1
OUT="hardware/kicad/route-attempt-${attempt}.ses"
rm -f "$OUT"; cp "$PLACED" "$PCB"
LIMIT=600
FANOUT=false
OPTS='-mp 10 -mt 3 -oit 0.15 -is prioritized -us greedy'
echo "=== FREEROUTING 2.3 ATTEMPT $attempt limit=${LIMIT}s fanout=${FANOUT} GND=IGNORED_PLANE ===" | tee -a hardware/kicad/freerouting.log
set +e
timeout "${LIMIT}s" java -Xmx5g -jar /tmp/freerouting.jar --gui.enabled=false \
  --router.automatic_neckdown=false --router.fanout.enabled="$FANOUT" \
  -de "$DSN" -do "$OUT" -inc GND_PLANE $OPTS 2>&1 | tee -a hardware/kicad/freerouting.log
ROUTER_RC=${PIPESTATUS[0]}
set -e
echo "FREEROUTING_ATTEMPT_${attempt}_EXIT=$ROUTER_RC" | tee -a hardware/kicad/freerouting.log
test "$ROUTER_RC" = 0
test -s "$OUT"

$KPY hardware/kicad/tools/route_reva_freerouting.py import "$PCB" "$OUT"

# Keep Freerouting's native SD_SCLK route. Run #147 proved the former
# deterministic replacement was the source of the final 12 DRC collisions;
# the imported SES route itself must be validated by KiCad DRC.
echo 'SD_SCLK_ROUTE_SOURCE=FREEROUTING_SES' | tee -a hardware/kicad/freerouting.log

# Add surface copper, fill once so islands are measurable, stitch every safe
# F.Cu GND island to L2, then remove any essentially coincident same-net stitch
# via that landed on a pre-reserved escape before authoritative DRC/audit.
$KPY hardware/kicad/tools/add_surface_ground.py "$PCB"
$K pcb drc --severity-error --refill-zones --save-board \
  --output "hardware/kicad/route-attempt-${attempt}-pre-stitch-drc.rpt" "$PCB" || true
$KPY hardware/kicad/tools/stitch_ground_islands.py "$PCB" | tee -a hardware/kicad/freerouting.log
$KPY hardware/kicad/tools/dedupe_gnd_vias.py "$PCB" | tee -a hardware/kicad/freerouting.log

set +e
$K pcb drc --severity-error --exit-code-violations --refill-zones --save-board \
  --output "hardware/kicad/route-attempt-${attempt}-drc.rpt" "$PCB"
DRC_RC=$?
$KPY hardware/kicad/tools/route_reva_freerouting.py audit "$PCB" 2>&1 | tee -a hardware/kicad/freerouting.log
AUDIT_RC=${PIPESTATUS[0]}
set -e
echo "POST_ROUTE_DRC_EXIT=$DRC_RC AUDIT_EXIT=$AUDIT_RC" | tee -a hardware/kicad/freerouting.log

if [ "$DRC_RC" = 0 ] && [ "$AUDIT_RC" = 0 ]; then
  cp "$OUT" "$SES"; success=1
  echo "ROUTING_COMPLETE_ATTEMPT=$attempt" | tee -a hardware/kicad/freerouting.log
fi

test "$success" = 1
$KPY hardware/kicad/tools/route_reva_freerouting.py audit "$PCB"
