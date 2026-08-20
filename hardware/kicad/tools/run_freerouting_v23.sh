#!/usr/bin/env bash
set -euo pipefail
PCB='hardware/kicad/Automatrix_PVDG_RevA.kicad_pcb'
PLACED='hardware/kicad/Automatrix_PVDG_RevA-placed.kicad_pcb'
DSN='hardware/kicad/Automatrix_PVDG_RevA.dsn'
SES='hardware/kicad/Automatrix_PVDG_RevA.ses'
KPY="docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/work -w /work kicad/kicad:10.0.5 python3"
K="docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/work -w /work kicad/kicad:10.0.5 kicad-cli"

: > hardware/kicad/freerouting.log

# Dedicated L2 GND plane is established before Specctra export. Generic routing
# owns signals; GND remains a plane/pour problem and is stitched after fill.
$KPY hardware/kicad/tools/post_route_finalize.py "$PCB"
$K pcb drc --severity-error --refill-zones --save-board \
  --output hardware/kicad/Automatrix_PVDG_RevA-pre-route-drc.rpt "$PCB" || true
cp "$PCB" "$PLACED"
$KPY hardware/kicad/tools/route_reva_freerouting.py export "$PLACED" "$DSN"
python3 hardware/kicad/tools/prepare_dsn_plane_classes.py "$DSN"

curl -fL --retry 3 --retry-delay 2 -o /tmp/freerouting.jar \
  https://github.com/freerouting/freerouting/releases/download/v2.3.0/freerouting-2.3.0.jar

# Freerouting's -inc option intentionally ignores GND_PLANE. Those ignored GND
# ratsnest items remain visible in Freerouting's "unrouted" score even though
# they are closed later by KiCad surface copper + L2 stitching. The pass count
# remains bounded at 10 so Freerouting exits normally and writes the SES; KiCad,
# not the router score, is the final connectivity/DRC authority.
#
# Run #19 proved the former 420 s shell timeout was too tight for the same
# bounded 10-pass job: pass #8 completed successfully, then timeout killed Java
# with exit 124 before an SES could be written, so the new GND-tail closure code
# never ran. Keep the quality bound (-mp 10) and give it deterministic headroom
# to exit normally; the enclosing Actions job still has its own 35 minute cap.
#
# Run 32339166203 proved fanout=true is not a valid fallback on this board: it
# generated 72 x 0.15 mm tracks below the 0.20 mm board minimum. Keep the proven
# fanout-disabled route and solve residual GND closure deterministically.
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
# pad-connected F.Cu GND island to L2, then refill for authoritative DRC/audit.
$KPY hardware/kicad/tools/add_surface_ground.py "$PCB"
$K pcb drc --severity-error --refill-zones --save-board \
  --output "hardware/kicad/route-attempt-${attempt}-pre-stitch-drc.rpt" "$PCB" || true
$KPY hardware/kicad/tools/stitch_ground_islands.py "$PCB" | tee -a hardware/kicad/freerouting.log

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
