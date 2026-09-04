#!/usr/bin/env python3
"""Locked MCU<->W5500 SPI backbone for Rev-A.

Freerouting plateaus on this board (run 32515524912 stopped improving at pass
#4 and repeated the same score through pass #10), so every run finishes with a
small unrouted residue. The residue is never in the same place: successive runs
left ETH_EXRES, USB_CC2, USB_5V and ETH_MISO open. What those nets have in
common is that they are the longest low-speed connections on the board - the six
SPI/control lines from U1 at x=18.5 to U2 at x=111.8 cross the entire logic
area - so they are the first thing the router gives up on when it stalls.

These are Freerouting's own vertices from run 32515524912, where the whole set
routed DRC-clean. Locking them before Specctra export removes the six longest
nets from its search instead of raising its budget or relaxing a gate, and makes
the routed result repeatable. None of them carries a timing requirement: the
W5500 SPI runs well below any length-matching threshold, and RST/INT are static.

Regenerate from a routed board with tools/dump_spi_backbone if the placement of
U1 or U2 ever changes; the endpoint assertions below fail loudly if it has.
"""
from pathlib import Path
import sys
import pcbnew
import pre_route_critical_nets as base

WIDTH_SPI_MM = 0.20

ETH_SPI_BACKBONE = {
    'ETH_CS': {
        'tracks': (
            ('F.Cu', (18.5000, 59.1750), (19.5517, 59.1750)),
            ('F.Cu', (29.9835, 59.5766), (31.5233, 61.1164)),
            ('F.Cu', (32.3597, 61.1164), (33.0573, 61.8140)),
            ('F.Cu', (113.0996, 64.7500), (111.8375, 64.7500)),
            ('F.Cu', (19.9533, 59.5766), (29.9835, 59.5766)),
            ('F.Cu', (19.5517, 59.1750), (19.9533, 59.5766)),
            ('F.Cu', (31.5233, 61.1164), (32.3597, 61.1164)),
            ('B.Cu', (96.4057, 61.8140), (100.1690, 61.8140)),
            ('B.Cu', (96.0126, 62.2072), (96.4057, 61.8141)),
            ('B.Cu', (96.4057, 61.8141), (96.4057, 61.8140)),
            ('B.Cu', (33.4505, 62.2072), (96.0126, 62.2072)),
            ('B.Cu', (33.0573, 61.8140), (33.4505, 62.2072)),
            ('In2.Cu', (106.2428, 60.5607), (110.4321, 64.7500)),
            ('In2.Cu', (100.1690, 61.8140), (101.4223, 60.5607)),
            ('In2.Cu', (110.4321, 64.7500), (113.0996, 64.7500)),
            ('In2.Cu', (101.4223, 60.5607), (106.2428, 60.5607)),
        ),
        'vias': ((113.0996, 64.7500), (100.1690, 61.8140), (33.0573, 61.8140), ),
    },
    'ETH_INT': {
        'tracks': (
            ('F.Cu', (23.8191, 60.4450), (24.2241, 60.8500)),
            ('F.Cu', (111.8375, 66.7500), (110.8960, 66.7500)),
            ('F.Cu', (110.8960, 66.7500), (109.5899, 68.0561)),
            ('F.Cu', (18.5000, 60.4450), (23.8191, 60.4450)),
            ('B.Cu', (109.5899, 68.0561), (108.8802, 68.0561)),
            ('B.Cu', (39.6403, 66.8794), (37.7380, 64.9771)),
            ('B.Cu', (108.8802, 68.0561), (107.7035, 66.8794)),
            ('B.Cu', (107.7035, 66.8794), (39.6403, 66.8794)),
            ('B.Cu', (37.7380, 64.9771), (28.3512, 64.9771)),
            ('B.Cu', (28.3512, 64.9771), (24.2241, 60.8500)),
        ),
        'vias': ((109.5899, 68.0561), (24.2241, 60.8500), ),
    },
    'ETH_MISO': {
        'tracks': (
            ('F.Cu', (18.5000, 55.3650), (23.1197, 55.3650)),
            ('F.Cu', (108.3344, 65.6158), (108.4686, 65.7500)),
            ('F.Cu', (108.4686, 65.7500), (111.8375, 65.7500)),
            ('F.Cu', (23.1197, 55.3650), (23.6226, 55.8679)),
            ('B.Cu', (23.6226, 55.8679), (24.0871, 55.4034)),
            ('B.Cu', (103.2758, 64.6017), (107.3203, 64.6017)),
            ('B.Cu', (103.1566, 64.4825), (103.2758, 64.6017)),
            ('B.Cu', (69.7451, 64.4825), (103.1566, 64.4825)),
            ('B.Cu', (31.0424, 55.4034), (31.3697, 55.7307)),
            ('B.Cu', (24.0871, 55.4034), (31.0424, 55.4034)),
            ('B.Cu', (60.9934, 55.7307), (61.1059, 55.8432)),
            ('B.Cu', (31.3697, 55.7307), (60.9934, 55.7307)),
            ('B.Cu', (107.3203, 64.6017), (108.3344, 65.6158)),
            ('In2.Cu', (69.7451, 64.4825), (61.1059, 55.8433)),
            ('In2.Cu', (61.1059, 55.8433), (61.1059, 55.8432)),
        ),
        'vias': ((108.3344, 65.6158), (69.7451, 64.4825), (23.6226, 55.8679), (61.1059, 55.8432), ),
    },
    'ETH_MOSI': {
        'tracks': (
            ('F.Cu', (111.8375, 66.2500), (109.5466, 66.2500)),
            ('F.Cu', (109.5466, 66.2500), (109.4421, 66.3545)),
            ('F.Cu', (18.5000, 57.9050), (26.7179, 57.9050)),
            ('F.Cu', (26.7179, 57.9050), (26.9984, 58.1855)),
            ('B.Cu', (31.8129, 63.0000), (103.6656, 63.0000)),
            ('B.Cu', (26.9984, 58.1855), (31.8129, 63.0000)),
            ('In2.Cu', (103.6656, 63.0000), (107.0201, 66.3545)),
            ('In2.Cu', (107.0201, 66.3545), (109.4421, 66.3545)),
        ),
        'vias': ((26.9984, 58.1855), (103.6656, 63.0000), (109.4421, 66.3545), ),
    },
    'ETH_RST': {
        'tracks': (
            ('F.Cu', (112.5710, 68.8415), (113.2500, 68.1625)),
            ('F.Cu', (14.7100, 63.6983), (15.7921, 63.6983)),
            ('F.Cu', (15.7921, 63.6983), (16.5317, 62.9587)),
            ('F.Cu', (111.6174, 68.8415), (112.5710, 68.8415)),
            ('F.Cu', (14.7100, 64.7500), (14.7100, 63.6983)),
            ('B.Cu', (38.6896, 66.4967), (28.0969, 66.4967)),
            ('B.Cu', (39.1912, 67.0691), (39.1912, 66.9983)),
            ('B.Cu', (41.7293, 69.6072), (39.1912, 67.0691)),
            ('B.Cu', (111.6174, 68.8415), (109.7357, 68.8415)),
            ('B.Cu', (28.0969, 66.4967), (26.6002, 65.0000)),
            ('B.Cu', (18.5730, 65.0000), (16.5317, 62.9587)),
            ('B.Cu', (108.9700, 69.6072), (41.7293, 69.6072)),
            ('B.Cu', (39.1912, 66.9983), (38.6896, 66.4967)),
            ('B.Cu', (26.6002, 65.0000), (18.5730, 65.0000)),
            ('B.Cu', (109.7357, 68.8415), (108.9700, 69.6072)),
        ),
        'vias': ((111.6174, 68.8415), (16.5317, 62.9587), ),
    },
    'ETH_SCLK': {
        'tracks': (
            ('F.Cu', (19.5517, 56.6350), (20.1181, 57.2014)),
            ('F.Cu', (109.3677, 65.2500), (109.0532, 64.9355)),
            ('F.Cu', (20.1181, 57.2014), (25.7949, 57.2014)),
            ('F.Cu', (111.8375, 65.2500), (109.3677, 65.2500)),
            ('F.Cu', (18.5000, 56.6350), (19.5517, 56.6350)),
            ('B.Cu', (101.4839, 57.3662), (25.9597, 57.3662)),
            ('B.Cu', (25.9597, 57.3662), (25.7949, 57.2014)),
            ('B.Cu', (109.0532, 64.9355), (101.4839, 57.3662)),
        ),
        'vias': ((109.0532, 64.9355), (25.7949, 57.2014), ),
    },
}


def route(board):
    locked = 0
    for name, spec in ETH_SPI_BACKBONE.items():
        net = board.FindNet(name)
        if net is None:
            raise RuntimeError(f'SPI backbone net missing: {name}')
        pads = [(base.xy_mm(p.GetPosition()))
                for fp in board.Footprints() for p in fp.Pads()
                if p.GetNetname() == name]
        ends = set()
        for layer, a, b in spec['tracks']:
            ends.add(a); ends.add(b)
        for pad in pads:
            if not any(abs(pad[0] - x) < 1e-4 and abs(pad[1] - y) < 1e-4 for x, y in ends):
                raise RuntimeError(f'{name}: pad {pad} is not an endpoint of the locked backbone; '
                                   'placement changed, regenerate the table')
        for layer, a, b in spec['tracks']:
            base.add_track(board, a, b, board.GetLayerID(layer), net, WIDTH_SPI_MM)
            locked += 1
        for v in spec['vias']:
            base.add_via(board, v, net)
            locked += 1
    print(f'ETH_SPI_BACKBONE_LOCKED: PASS nets={len(ETH_SPI_BACKBONE)} items={locked}')


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f'cannot load board: {path}')
    route(board)
    pcbnew.SaveBoard(str(path), board)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: pre_route_eth_spi_backbone.py BOARD.kicad_pcb')
    main(sys.argv[1])
