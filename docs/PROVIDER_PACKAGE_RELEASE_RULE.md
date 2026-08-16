# Provider Package Release Rule

Only a single controlled Rev-A package may be sent to the PCB/PCBA/enclosure service provider.

Release prerequisites:
- H1 schematic PASS.
- H2 placement/routing PASS.
- ERC clean.
- DRC + schematic parity clean.
- STEP export PASS.
- Gerber/drill/BOM/CPL regenerated from the same commit.
- Casing geometry generated from the same final PCB STEP.

Anything before these gates is engineering-preview material only.
