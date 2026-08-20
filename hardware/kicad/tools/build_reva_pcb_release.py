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
    # SPI series/pull conditioning belongs on the controller side of the long
    # MCU-to-socket run. Keeping it with U1 also frees the small edge socket
    # region for the connector and local decoupling only.
    if old in SD_SIGNAL_PARTS:
        return 'U1'
    return BASE_CLUSTER(old)


def release_candidates(anchor_old, base):
    ax=pcbnew.ToMM(base.x); ay=pcbnew.ToMM(base.y)
    pts=list(BASE_CANDIDATES(anchor_old, base))

    # The 5 V field-power block needs a third compact reserve below the USB
    # horizontal corridor. Earlier H2 routing used this same low-voltage area;
    # it remains above the relay-contact zone and outside the protected USB path.
    if anchor_old == 'U5':
        pts.extend(grid(31.0, 40.0, 50.0, 51.0, 1.0))

    if anchor_old in ('U_DI1','U_DI2','U_DI3','U_DI4'):
        pts.extend(grid(94.0, 70.0, 136.0, 91.0, 1.0))

    if anchor_old in ('U3','U4'):
        pts.extend(grid(27.0, 69.0, 70.0, 88.0, 1.0))

    if anchor_old in ('J_HMI','U7'):
        pts.extend(grid(69.0, 68.0, 103.0, 88.0, 1.0))

    # RTC + SD socket/decoupling share a compact optional-peripheral block.
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
