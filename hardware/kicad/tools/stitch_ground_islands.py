#!/usr/bin/env python3
"""Tie every safe F.Cu GND fill island to the solid In1.Cu GND plane.

Dense routing can leave F.Cu GND fragments too narrow for an in-island through
via. Prefer a direct stitch via; otherwise start from an existing GND pad/track
or verified filled copper and route a short DRC-aware GND tail to a safe through
via. The final KiCad DRC/connectivity audit remains the release authority.
"""
from pathlib import Path
import math
import sys
import pcbnew

VIA_D = 0.60
DRILL = 0.30
CLEAR = 0.24
TRACK_W = 0.20
EDGE = 1.0
LOGIC_Y0 = 30.0
MAG_X0 = 127.0
MAG_Y0 = 55.5
MAG_Y1 = 74.5
TAIL_RADIUS = 8.0
ORIGIN_LIMIT = 64
TAIL_ANGLES = 24
TAIL_RADII = (0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0, 7.0, 8.0)


def mm(v):
    return pcbnew.FromMM(float(v))


def pt(x, y):
    return pcbnew.VECTOR2I_MM(float(x), float(y))


def xy(p):
    return pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)


def point_seg_dist(px, py, ax, ay, bx, by):
    vx = bx - ax
    vy = by - ay
    wx = px - ax
    wy = py - ay
    vv = vx * vx + vy * vy
    if vv <= 1e-12:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, (wx * vx + wy * vy) / vv))
    qx = ax + t * vx
    qy = ay + t * vy
    return math.hypot(px - qx, py - qy)


def orient(ax, ay, bx, by, cx, cy):
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)


def on_segment(ax, ay, bx, by, cx, cy, eps=1e-9):
    return min(ax, bx) - eps <= cx <= max(ax, bx) + eps and min(ay, by) - eps <= cy <= max(ay, by) + eps


def segments_intersect(a, b, c, d):
    ax, ay = a
    bx, by = b
    cx, cy = c
    dx, dy = d
    o1 = orient(ax, ay, bx, by, cx, cy)
    o2 = orient(ax, ay, bx, by, dx, dy)
    o3 = orient(cx, cy, dx, dy, ax, ay)
    o4 = orient(cx, cy, dx, dy, bx, by)
    eps = 1e-9
    if ((o1 > eps and o2 < -eps) or (o1 < -eps and o2 > eps)) and ((o3 > eps and o4 < -eps) or (o3 < -eps and o4 > eps)):
        return True
    if abs(o1) <= eps and on_segment(ax, ay, bx, by, cx, cy):
        return True
    if abs(o2) <= eps and on_segment(ax, ay, bx, by, dx, dy):
        return True
    if abs(o3) <= eps and on_segment(cx, cy, dx, dy, ax, ay):
        return True
    if abs(o4) <= eps and on_segment(cx, cy, dx, dy, bx, by):
        return True
    return False


def seg_seg_dist(a, b, c, d):
    if segments_intersect(a, b, c, d):
        return 0.0
    return min(
        point_seg_dist(a[0], a[1], c[0], c[1], d[0], d[1]),
        point_seg_dist(b[0], b[1], c[0], c[1], d[0], d[1]),
        point_seg_dist(c[0], c[1], a[0], a[1], b[0], b[1]),
        point_seg_dist(d[0], d[1], a[0], a[1], b[0], b[1]),
    )


def bbox_mm(bb):
    return pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetTop()), pcbnew.ToMM(bb.GetBottom())


def inflate_box(bb, amount):
    x0, x1, y0, y1 = bb
    return x0 - amount, x1 + amount, y0 - amount, y1 + amount


def point_in_box(p, bb):
    x, y = p
    x0, x1, y0, y1 = bb
    return x0 <= x <= x1 and y0 <= y <= y1


def segment_hits_box(a, b, bb):
    x0, x1, y0, y1 = bb
    if point_in_box(a, bb) or point_in_box(b, bb):
        return True
    edges = [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]
    return any(segments_intersect(a, b, c, d) for c, d in edges)


def rule_keepout_boxes(board, kind, inflate):
    out = []
    try:
        zones = list(board.Zones())
    except Exception:
        zones = [board.GetArea(i) for i in range(board.GetAreaCount())]
    for fp in board.Footprints():
        try:
            zones.extend(list(fp.Zones()))
        except Exception:
            pass
    for z in zones:
        try:
            if not z.GetIsRuleArea():
                continue
            blocked = z.GetDoNotAllowVias() if kind == "via" else z.GetDoNotAllowTracks()
            if blocked:
                out.append(inflate_box(bbox_mm(z.GetBoundingBox()), inflate))
        except Exception:
            continue
    return out


class ClearanceModel:
    def __init__(self, board, gcode):
        self.gcode = gcode
        self.via_keepouts = rule_keepout_boxes(board, "via", VIA_D / 2)
        self.track_keepouts = rule_keepout_boxes(board, "track", TRACK_W / 2 + CLEAR)
        self.via_pad_boxes = []
        self.track_pad_boxes = []
        self.other_vias = []
        self.other_tracks_all = []
        self.other_tracks_front = []

        via_radius = VIA_D / 2 + CLEAR
        track_radius = TRACK_W / 2 + CLEAR
        for fp in board.Footprints():
            for pad in fp.Pads():
                if pad.GetNetCode() == gcode:
                    continue
                bb = bbox_mm(pad.GetBoundingBox())
                self.via_pad_boxes.append(inflate_box(bb, via_radius))
                try:
                    on_front = pad.IsOnLayer(pcbnew.F_Cu)
                except Exception:
                    on_front = True
                if on_front:
                    self.track_pad_boxes.append(inflate_box(bb, track_radius))

        for item in board.GetTracks():
            if item.GetNetCode() == gcode:
                continue
            if isinstance(item, pcbnew.PCB_VIA):
                ix, iy = xy(item.GetPosition())
                width = pcbnew.ToMM(item.GetWidth())
                self.other_vias.append((ix, iy, width))
                continue
            a = xy(item.GetStart())
            b = xy(item.GetEnd())
            width = pcbnew.ToMM(item.GetWidth())
            row = (a, b, width)
            self.other_tracks_all.append(row)
            try:
                if item.GetLayer() == pcbnew.F_Cu:
                    self.other_tracks_front.append(row)
            except Exception:
                pass

    def via_safe(self, x, y):
        if x < EDGE or x > 145.0 - EDGE or y < LOGIC_Y0 + 0.35 or y > 95.0 - EDGE:
            return False, "edge"
        if x > MAG_X0 - 0.6 and MAG_Y0 - 0.6 < y < MAG_Y1 + 0.6:
            return False, "magjack"
        p = (x, y)
        if any(point_in_box(p, bb) for bb in self.via_keepouts):
            return False, "via-keepout"
        if any(point_in_box(p, bb) for bb in self.via_pad_boxes):
            return False, "pad-clearance"
        radius = VIA_D / 2 + CLEAR
        for ix, iy, width in self.other_vias:
            if math.hypot(x - ix, y - iy) < radius + width / 2:
                return False, "via-clearance"
        for a, b, width in self.other_tracks_all:
            if point_seg_dist(x, y, a[0], a[1], b[0], b[1]) < radius + width / 2:
                return False, "track-clearance"
        return True, "ok"

    def track_safe(self, a, b):
        radius = TRACK_W / 2 + CLEAR
        for p in (a, b):
            if p[0] < EDGE or p[0] > 145.0 - EDGE or p[1] < LOGIC_Y0 or p[1] > 95.0 - EDGE:
                return False
        mag = (MAG_X0 - radius, 145.0, MAG_Y0 - radius, MAG_Y1 + radius)
        if segment_hits_box(a, b, mag):
            return False
        if any(segment_hits_box(a, b, bb) for bb in self.track_keepouts):
            return False
        if any(segment_hits_box(a, b, bb) for bb in self.track_pad_boxes):
            return False
        for ix, iy, width in self.other_vias:
            if point_seg_dist(ix, iy, a[0], a[1], b[0], b[1]) < radius + width / 2:
                return False
        for c, d, width in self.other_tracks_front:
            if seg_seg_dist(a, b, c, d) < radius + width / 2:
                return False
        return True


def bounds(outline):
    pts = [xy(outline.CPoint(i)) for i in range(outline.PointCount())]
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


def inside(outline, x, y):
    try:
        return outline.PointInside(pt(x, y))
    except Exception:
        return False


def inside_with_margin(outline, x, y, margin=0.11):
    if not inside(outline, x, y):
        return False
    for k in range(8):
        a = 2.0 * math.pi * k / 8.0
        if not inside(outline, x + margin * math.cos(a), y + margin * math.sin(a)):
            return False
    return True


def interior_candidates(outline, step, inset, limit=None, require_margin=False):
    minx, maxx, miny, maxy = bounds(outline)
    cx = (minx + maxx) / 2
    cy = (miny + maxy) / 2
    cand = [(0.0, cx, cy)]
    x = minx + min(inset, max(0.01, (maxx - minx) / 4))
    x_end = maxx - min(inset, max(0.01, (maxx - minx) / 4))
    y_start = miny + min(inset, max(0.01, (maxy - miny) / 4))
    y_end = maxy - min(inset, max(0.01, (maxy - miny) / 4))
    while x <= x_end + 1e-9:
        y = y_start
        while y <= y_end + 1e-9:
            cand.append((math.hypot(x - cx, y - cy), x, y))
            y += step
        x += step
    cand.sort()
    emitted = 0
    seen = set()
    for _, x, y in cand:
        key = (round(x, 4), round(y, 4))
        if key in seen:
            continue
        seen.add(key)
        ok = inside_with_margin(outline, x, y) if require_margin else inside(outline, x, y)
        if not ok:
            continue
        yield x, y
        emitted += 1
        if limit is not None and emitted >= limit:
            return


def choose_candidate(outline, model):
    for step, inset in ((0.5, 0.35), (0.20, 0.20), (0.10, 0.08)):
        for x, y in interior_candidates(outline, step, inset):
            if model.via_safe(x, y)[0]:
                return x, y
    return None


def gnd_pads_in_outline(board, outline, gcode):
    out = []
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetCode() != gcode:
                continue
            p = xy(pad.GetPosition())
            if inside(outline, *p):
                out.append((pad, p))
    return out


def gnd_track_origins(board, outline, gcode):
    out = []
    for item in board.GetTracks():
        if item.GetNetCode() != gcode or isinstance(item, pcbnew.PCB_VIA):
            continue
        try:
            if item.GetLayer() != pcbnew.F_Cu:
                continue
        except Exception:
            continue
        a = xy(item.GetStart())
        b = xy(item.GetEnd())
        for p in (a, b, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)):
            if inside(outline, *p):
                out.append(p)
    return out


def copper_tail_origins(outline):
    minx, maxx, miny, maxy = bounds(outline)
    narrow = min(maxx - minx, maxy - miny)
    step = 0.08 if narrow < 0.6 else 0.12 if narrow < 1.2 else 0.20
    yielded = list(interior_candidates(outline, step, 0.03, ORIGIN_LIMIT, require_margin=True))
    if yielded:
        return yielded
    return list(interior_candidates(outline, step, 0.02, ORIGIN_LIMIT, require_margin=False))


def tail_points(a):
    ax, ay = a
    order = [0, 6, 12, 18, 3, 9, 15, 21]
    order.extend(k for k in range(TAIL_ANGLES) if k not in order)
    for radius in TAIL_RADII:
        if radius > TAIL_RADIUS + 1e-9:
            continue
        for k in order:
            ang = 2.0 * math.pi * k / TAIL_ANGLES
            yield ax + radius * math.cos(ang), ay + radius * math.sin(ang)


def dogleg_routes(a, b):
    ax, ay = a
    bx, by = b
    mx = (ax + bx) / 2
    my = (ay + by) / 2
    raw = [(ax, by), (bx, ay), (mx, ay), (mx, by), (ax, my), (bx, my)]
    for radius in (0.5, 1.0, 1.5, 2.0):
        for k in range(8):
            ang = 2.0 * math.pi * k / 8.0
            raw.append((ax + radius * math.cos(ang), ay + radius * math.sin(ang)))
    seen = set()
    for c in raw:
        key = (round(c[0], 4), round(c[1], 4))
        if key in seen:
            continue
        seen.add(key)
        if math.hypot(c[0] - ax, c[1] - ay) < 0.05 or math.hypot(c[0] - bx, c[1] - by) < 0.05:
            continue
        yield [a, c, b]


def choose_tail(board, outline, gcode, model):
    origins = []
    for pad, p in gnd_pads_in_outline(board, outline, gcode):
        origins.append((pad, p, "pad"))
    for p in gnd_track_origins(board, outline, gcode):
        origins.append((None, p, "gnd-track"))
    for p in copper_tail_origins(outline):
        origins.append((None, p, "filled-copper"))

    stats = {"origins": 0, "targets": 0, "safe_vias": 0, "straight_blocked": 0, "dogleg_blocked": 0}
    seen_origins = set()
    via_cache = {}
    for source, a, source_kind in origins:
        key = (round(a[0], 3), round(a[1], 3))
        if key in seen_origins:
            continue
        seen_origins.add(key)
        stats["origins"] += 1
        for b in tail_points(a):
            stats["targets"] += 1
            bkey = (round(b[0], 3), round(b[1], 3))
            if bkey not in via_cache:
                via_cache[bkey] = model.via_safe(*b)[0]
            if not via_cache[bkey]:
                continue
            stats["safe_vias"] += 1
            if model.track_safe(a, b):
                return (source, source_kind, [a, b]), stats
            stats["straight_blocked"] += 1
            for route in dogleg_routes(a, b):
                if model.track_safe(route[0], route[1]) and model.track_safe(route[1], route[2]):
                    return (source, source_kind, route), stats
            stats["dogleg_blocked"] += 1
    return None, stats


def add_via(board, x, y, net):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x, y))
    v.SetWidth(mm(VIA_D))
    v.SetDrill(mm(DRILL))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(net)
    v.SetLocked(True)
    board.Add(v)


def add_track(board, a, b, net):
    if math.hypot(a[0] - b[0], a[1] - b[1]) < 0.01:
        return
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pt(*a))
    t.SetEnd(pt(*b))
    t.SetWidth(mm(TRACK_W))
    t.SetLayer(pcbnew.F_Cu)
    t.SetNet(net)
    t.SetLocked(True)
    board.Add(t)


def pad_label(pad):
    try:
        return f"{pad.GetParent().GetReference()}:{pad.GetNumber()}"
    except Exception:
        return f"pad:{pad.GetNumber()}"


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f"cannot load board: {path}")

    gnet = None
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetname() == "GND":
                gnet = pad.GetNet()
                break
        if gnet:
            break
    if not gnet:
        raise SystemExit("GND net missing")

    gcode = gnet.GetNetCode()
    layer = pcbnew.F_Cu
    try:
        zones = list(board.Zones())
    except Exception:
        zones = [board.GetArea(i) for i in range(board.GetAreaCount())]
    gz = [z for z in zones if z.GetNetCode() == gcode and z.GetLayer() == layer and z.HasFilledPolysForLayer(layer)]
    if not gz:
        raise SystemExit("filled F.Cu GND zone missing; refill zones before stitching")

    model = ClearanceModel(board, gcode)
    added = 0
    tails = 0
    skipped = 0
    outlines = 0
    for z in gz:
        polys = z.GetFilledPolysList(layer)
        for idx in range(polys.OutlineCount()):
            outlines += 1
            outline = polys.COutline(idx)
            chosen = choose_candidate(outline, model)
            if chosen is not None:
                add_via(board, chosen[0], chosen[1], gnet)
                added += 1
                continue

            tail, stats = choose_tail(board, outline, gcode, model)
            if tail is not None:
                source, source_kind, route = tail
                for a, b in zip(route, route[1:]):
                    add_track(board, a, b, gnet)
                via = route[-1]
                add_via(board, via[0], via[1], gnet)
                added += 1
                tails += 1
                source_label = pad_label(source) if source is not None else source_kind
                route_s = "->".join(f"({p[0]:.3f},{p[1]:.3f})" for p in route)
                print(f"GND_ISLAND_TAIL idx={idx} source={source_label} route={route_s} stats={stats}")
                continue

            skipped += 1
            minx, maxx, miny, maxy = bounds(outline)
            print(
                f"GND_ISLAND_SKIP idx={idx} bounds=({minx:.3f},{miny:.3f})-({maxx:.3f},{maxy:.3f}) "
                f"reason=no-drc-safe-tail stats={stats}"
            )

    if added == 0:
        raise SystemExit(f"GND stitch failed: outlines={outlines} no safe candidates")
    pcbnew.SaveBoard(str(path), board)
    print(
        f"GND_ISLAND_STITCH: outlines={outlines} vias_added={added} tail_routes={tails} skipped={skipped} "
        f"via_keepouts={len(model.via_keepouts)} track_keepouts={len(model.track_keepouts)}"
    )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: stitch_ground_islands.py BOARD.kicad_pcb")
    main(sys.argv[1])
