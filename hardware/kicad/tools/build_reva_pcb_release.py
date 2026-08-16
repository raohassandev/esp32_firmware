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


def release_candidates(anchor_old, base):
    ax=pcbnew.ToMM(base.x); ay=pcbnew.ToMM(base.y)
    pts=list(BASE_CANDIDATES(anchor_old, base))

    # Four isolated DI channels are one optional functional block. Channel-local
    # tiny rectangles are too restrictive for optocoupler/TVS/resistor support.
    if anchor_old in ('U_DI1','U_DI2','U_DI3','U_DI4'):
        pts.extend(grid(94.0, 70.0, 136.0, 91.0, 1.0))

    # Both RS485 transceivers share one protected field-comms block. Bias,
    # termination and TVS parts may sit between channels while remaining close
    # to both transceivers and their top-edge terminal blocks.
    if anchor_old in ('U3','U4'):
        pts.extend(grid(27.0, 69.0, 70.0, 88.0, 1.0))

    # HMI TTL and optional RS232 are one service-interface block. The collision
    # and corridor rules in the finalizer still protect connectors and routing.
    if anchor_old in ('J_HMI','U7'):
        pts.extend(grid(69.0, 68.0, 103.0, 88.0, 1.0))

    # RTC and microSD are optional low-voltage functions; give each a controlled
    # spill area rather than ever falling back across the board.
    if anchor_old == 'U_RTC':
        pts.extend(grid(72.0, 41.0, 105.0, 54.0, 1.0))
    if anchor_old == 'J_SD':
        pts.extend(grid(106.0, 38.0, 132.0, 54.0, 1.0))

    # USB-C CC/ESD support can use the service strip immediately left/below the
    # connector. _try_position() remains authoritative and rejects any point
    # that overlaps the locked differential-pair corridor.
    if anchor_old == 'J_USB':
        pts.extend(grid(108.0, 31.0, 142.0, 39.0, 1.0))
        pts.extend(grid(108.0, 40.0, 125.0, 51.0, 1.0))

    pts=list(dict.fromkeys(pts))
    pts.sort(key=lambda p:(abs(p[0]-ax)+abs(p[1]-ay), abs(p[1]-ay), abs(p[0]-ax), p[1], p[0]))
    return pts


b.zone_candidates = release_candidates
b.SLOTS = []

if __name__ == '__main__':
    b.main()
