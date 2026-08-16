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

success=0
for attempt in 1 2; do
  OUT="hardware/kicad/route-attempt-${attempt}.ses"
  rm -f "$OUT"; cp "$PLACED" "$PCB"
  case "$attempt" in
    1) LIMIT=300; OPTS='-mp 120 -mt 3 -oit 0.15 -is prioritized -us greedy' ;;
    2) LIMIT=420; OPTS='-mp 180 -mt 4 -oit 0.10 -is sequential -us hybrid' ;;
  esac
  echo "=== FREEROUTING 2.3 ATTEMPT $attempt limit=${LIMIT}s GND=PLANE FANOUT=OFF ===" | tee -a hardware/kicad/freerouting.log
  set +e
  timeout "${LIMIT}s" java -Xmx5g -jar /tmp/freerouting.jar --gui.enabled=false \
    --router.automatic_neckdown=false --router.fanout.enabled=false \
    -de "$DSN" -do "$OUT" -inc GND_PLANE $OPTS 2>&1 | tee -a hardware/kicad/freerouting.log
  ROUTER_RC=${PIPESTATUS[0]}
  set -e
  echo "FREEROUTING_ATTEMPT_${attempt}_EXIT=$ROUTER_RC" | tee -a hardware/kicad/freerouting.log
  [ -s "$OUT" ] || continue

  $KPY hardware/kicad/tools/route_reva_freerouting.py import "$PCB" "$OUT"

  # Replace the one repeatable local autorouter conflict with a controlled
  # 0.20mm route across F/B/In2 layers.
  python3 hardware/kicad/tools/strip_net_tracks_text.py "$PCB" SD_SCLK | tee -a hardware/kicad/freerouting.log
  $KPY hardware/kicad/tools/repair_sd_sclk.py "$PCB" | tee -a hardware/kicad/freerouting.log

  # Add surface copper, fill once so islands are measurable, stitch every safe
  # F.Cu GND island to L2, then refill for the authoritative DRC/audit.
  $KPY hardware/kicad/tools/add_surface_ground.py "$PCB"
  $K pcb drc --severity-error --refill-zones --save-board \
    --output "hardware/kicad/route-attempt-${attempt}-pre-stitch-drc.rpt" "$PCB" || true
  $KPY hardware/kicad/tools/stitch_ground_islands.py "$PCB" | tee -a hardware/kicad/freerouting.log
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
