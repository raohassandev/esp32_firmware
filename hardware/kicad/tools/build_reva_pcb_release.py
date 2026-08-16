#!/usr/bin/env python3
"""Production placement entrypoint layered over build_reva_pcb_final.

The finalizer owns safety/mechanical rules and semantic clustering. This release
wrapper only widens *within* controlled low-voltage optional blocks where four
identical channels share one physical region. It never re-enables the generic
wide fallback and never permits logic parts into the relay-contact side (Y<30).
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

    # Four isolated DI channels form one optional functional block. Individual
    # tiny channel rectangles caused artificial packing failures for the TVS/
    # optocoupler/resistor support parts. Sharing this block is electrically and
    # mechanically appropriate while remaining far above the relay contacts.
    if anchor_old in ('U_DI1','U_DI2','U_DI3','U_DI4'):
        pts.extend(grid(94.0, 70.0, 136.0, 91.0, 1.0))

    # RTC and microSD are also optional low-voltage functions; give each a small
    # controlled spill area rather than ever falling back across the board.
    if anchor_old == 'U_RTC':
        pts.extend(grid(72.0, 41.0, 105.0, 54.0, 1.0))
    if anchor_old == 'J_SD':
        pts.extend(grid(106.0, 38.0, 132.0, 54.0, 1.0))

    pts=list(dict.fromkeys(pts))
    pts.sort(key=lambda p:(abs(p[0]-ax)+abs(p[1]-ay), abs(p[1]-ay), abs(p[0]-ax), p[1], p[0]))
    return pts


b.zone_candidates = release_candidates
b.SLOTS = []

if __name__ == '__main__':
    b.main()
