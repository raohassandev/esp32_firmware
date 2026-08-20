#!/usr/bin/env python3
"""Production placement entrypoint layered over build_reva_pcb_final.

The finalizer owns safety/mechanical rules and semantic clustering. This release
wrapper widens placement only inside controlled low-voltage functional blocks.
It never re-enables the generic wide fallback and never permits logic parts into
the relay-contact side (Y<30) or through critical USB/Ethernet corridors.
"""
import pcbnew
import build_reva_pcb as b
import build_reva_pcb_final as final

BASE_CANDIDATES = final._dense_zone_candidates
BASE_CLUSTER = b.cluster_for
SD_SIGNAL_PARTS = {'R_SDCS','R_SDMISO','R_SDMOSI','R_SDSCLK'}
ETH_DAMPERS = {
    'R_ETH_TXP_DAMP','R_ETH_TXN_DAMP','R_ETH_RXP_DAMP','R_ETH_RXN_DAMP',
}

# Exact Phoenix MKDS-3 terminal is slightly deeper than the earlier generic
# prototype body. J_RLY1 must remain clear of the H1 mounting keep-out, so keep
# the terminal row fixed and shift only K1 laterally into its channel. This
# restores coil-driver service space while increasing terminal/relay separation.
b.FIXED['K1'] = (28.0, 24.0, 0)

# Rotate W5500 only. This brings XI/XO toward the nearby 25 MHz crystal and puts
# the four MDI pins toward the MagJack.
b.FIXED['U2'] = (116.0, 64.0, 180)

# Controlled W5500 MDI damping placement. All four 0R parts are horizontal:
# pad1 faces U2, pad2 faces J3. Run #31 proved 1.8 mm row spacing was smaller
# than the real KiCad courtyard+placement-clearance envelope. Runs #33/#34 then
# proved the RXN chip escape also needs more vertical separation from the TXP
# damper pad. Keep the RX pair fixed and shift the TX pair down while retaining
# >=2.5 mm damper-row spacing. This stays inside the reserved Ethernet corridor.
final.ETH_ALLOWED.update(ETH_DAMPERS)
b.FIXED.update({
    'R_ETH_RXP_DAMP': (123.6, 60.3, 0),
    'R_ETH_RXN_DAMP': (123.6, 62.8, 0),
    'R_ETH_TXP_DAMP': (123.6, 66.0, 0),
    'R_ETH_TXN_DAMP': (123.6, 68.5, 0),
})


def grid(x0, y0, x1, y1, step=1.0):
    pts=[]
    x=x0
    while x <= x1 + 1e-9:
        y=y0
        while y <= y1 + 1e-9:
            pts.append((round(x,3), round(y,3)))
            y += step
        x += step
    return pts


def release_cluster(old):
    if old in SD_SIGNAL_PARTS:
        return 'U1'
    return BASE_CLUSTER(old)


def release_candidates(anchor_old, base):
    ax=pcbnew.ToMM(base.x); ay=pcbnew.ToMM(base.y)
    pts=list(BASE_CANDIDATES(anchor_old, base))

    if anchor_old == 'U5':
        pts.extend(grid(31.0, 40.0, 50.0, 51.0, 1.0))

    if anchor_old in ('U_DI1','U_DI2','U_DI3','U_DI4'):
        pts.extend(grid(94.0, 70.0, 136.0, 91.0, 1.0))

    if anchor_old in ('U3','U4'):
        pts.extend(grid(27.0, 69.0, 70.0, 88.0, 1.0))

    if anchor_old in ('J_HMI','U7'):
        pts.extend(grid(69.0, 68.0, 103.0, 88.0, 1.0))

    if anchor_old in ('U_RTC','J_SD'):
        pts.extend(grid(72.0, 38.0, 132.0, 51.0, 1.0))

    if anchor_old == 'J_USB':
        pts.extend(grid(108.0, 31.0, 142.0, 39.0, 1.0))
        pts.extend(grid(108.0, 40.0, 125.0, 51.0, 1.0))

    pts=list(dict.fromkeys(pts))
    pts.sort(key=lambda p:(abs(p[0]-ax)+abs(p[1]-ay), abs(p[1]-ay), abs(p[0]-ax), p[1], p[0]))
    return pts


b.cluster_for = release_cluster
b.zone_candidates = release_candidates
b.SLOTS = []

if __name__ == '__main__':
    b.main()
