# Provider Release Acceptance — Rev-A

The PCB/PCBA/enclosure service provider package may be issued only when all of these are true:

- H1 schematic freeze is PASS.
- H2 PCB placement and routing are PASS.
- KiCad ERC is clean.
- KiCad DRC and schematic parity are clean.
- STEP export succeeds.
- Gerber, drill, BOM and CPL outputs are regenerated from the same release commit.
- Optional/DNP population variants are clearly identified.
- Relay/contact safety zoning and connector pinouts are documented.
- Enclosure inputs use the same final PCB STEP and connector geometry.
- Package name includes revision and short commit SHA.

Anything before these gates pass is an engineering preview, not a manufacturing-final release.
