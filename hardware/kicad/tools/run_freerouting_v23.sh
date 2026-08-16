#!/usr/bin/env bash
set -euo pipefail
PCB='hardware/kicad/Automatrix_PVDG_RevA.kicad_pcb'
PLACED='hardware/kicad/Automatrix_PVDG_RevA-placed.kicad_pcb'
DSN='hardware/kicad/Automatrix_PVDG_RevA.dsn'
SES='hardware/kicad/Automatrix_PVDG_RevA.ses'
KPY="docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/work -w /work kicad/kicad:10.0.5 python3"
K="docker run --rm -u $(id -u):$(id -g) -e HOME=/tmp -v $PWD:/work -w /work kicad/kicad:10.0.5 kicad-cli"

: > hardware/kicad/freerouting.log

# Lock the controlled high-speed and stable known routes at the board minimum
# before handing the remainder to the generic autorouter.
$KPY hardware/kicad/tools/pre_route_critical_nets.py "$PCB" | tee -a hardware/kicad/placement.log
$KPY hardware/kicad/tools/route_reva_stragglers.py "$PCB" | tee -a hardware/kicad/placement.log

# Establish the dedicated L2 reference plane before Specctra export.
$KPY hardware/kicad/tools/post_route_finalize.py "$PCB"
$K pcb drc --severity-error --refill-zones --save-board \
  --output hardware/kicad/Automatrix_PVDG_RevA-pre-route-drc.rpt "$PCB" || true
cp "$PCB" "$PLACED"
$KPY hardware/kicad/tools/route_reva_freerouting.py export "$PLACED" "$DSN"
python3 hardware/kicad/tools/prepare_dsn_plane_classes.py "$DSN"

curl -fL --retry 3 --retry-delay 2 -o /tmp/freerouting.jar \
  https://github.com/freerouting/freerouting/releases/download/v2.3.0/freerouting-2.3.0.jar

success=0
for attempt in 1 2; do
  OUT="hardware/kicad/route-attempt-${attempt}.ses"
  rm -f "$OUT"; cp "$PLACED" "$PCB"
  case "$attempt" in
    1) LIMIT=300; OPTS='-mp 120 -mt 3 -oit 0.15 -is prioritized -us greedy' ;;
    2) LIMIT=420; OPTS='-mp 180 -mt 4 -oit 0.10 -is sequential -us hybrid' ;;
  esac
  echo "=== FREEROUTING 2.3 ATTEMPT $attempt limit=${LIMIT}s ===" | tee -a hardware/kicad/freerouting.log
  set +e
  timeout "${LIMIT}s" java -Xmx5g -jar /tmp/freerouting.jar --gui.enabled=false \
    --router.automatic_neckdown=false --router.fanout.enabled=false \
    -de "$DSN" -do "$OUT" -inc GND_PLANE $OPTS 2>&1 | tee -a hardware/kicad/freerouting.log
  ROUTER_RC=${PIPESTATUS[0]}
  set -e
  echo "FREEROUTING_ATTEMPT_${attempt}_EXIT=$ROUTER_RC" | tee -a hardware/kicad/freerouting.log
  [ -s "$OUT" ] || continue

  $KPY hardware/kicad/tools/route_reva_freerouting.py import "$PCB" "$OUT"
  # Freerouting can re-express fixed Specctra traces. Replace those nets with
  # the deterministic KiCad-native topology before final DRC/SI validation.
  python3 hardware/kicad/tools/strip_critical_tracks_text.py "$PCB" | tee -a hardware/kicad/freerouting.log
  $KPY hardware/kicad/tools/restore_controlled_routes.py "$PCB" | tee -a hardware/kicad/freerouting.log
  $KPY hardware/kicad/tools/add_surface_ground.py "$PCB"
  $K pcb drc --severity-error --refill-zones --save-board \
    --output "hardware/kicad/route-attempt-${attempt}-drc.rpt" "$PCB" || true

  set +e
  $KPY hardware/kicad/tools/route_reva_freerouting.py audit "$PCB" 2>&1 | tee -a hardware/kicad/freerouting.log
  RC=${PIPESTATUS[0]}
  set -e
  if [ "$RC" = 0 ]; then
    cp "$OUT" "$SES"; success=1
    echo "ROUTING_COMPLETE_ATTEMPT=$attempt" | tee -a hardware/kicad/freerouting.log
    break
  fi
done

test "$success" = 1
$KPY hardware/kicad/tools/route_reva_freerouting.py audit "$PCB"
