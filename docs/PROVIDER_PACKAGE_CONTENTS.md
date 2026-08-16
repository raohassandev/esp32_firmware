# Rev-A Provider Package Contents

This manifest defines the files that will be sent to the PCB/PCBA/enclosure service provider after H2 passes.

## Engineering source
- hardware/kicad/Automatrix_PVDG_RevA.kicad_pro
- hardware/kicad/Automatrix_PVDG_RevA.kicad_sch
- hardware/kicad/Automatrix_PVDG_RevA.kicad_pcb
- project-local symbol/footprint tables and controlled helper files needed to reopen the design

## Manufacturing outputs
- schematic PDF
- BOM with MPN, package, quantity and DNP status
- CPL / pick-and-place
- Gerbers
- NC drill files
- assembly drawings
- fabrication drawing / stack-up notes
- STEP model
- DRC/ERC reports

## Provider documents
- docs/HARDWARE_PCB_REVA_MASTER_PLAN.md
- docs/PCB_AND_ENCLOSURE_SERVICE_PROVIDER_RFQ.md
- docs/PCB_PROVIDER_HANDOFF_READINESS.md
- docs/PROVIDER_PACKAGE_CONTENTS.md

## Mechanical / enclosure inputs
- final PCB STEP
- board outline and mounting-hole coordinates
- connector locations and opening envelopes
- maximum component heights
- relay/contact safety-zone note
- DIN-rail casing target and prototype enclosure requirements

## Release rule
The package must be generated from one release commit only. File name format:

`Automatrix_PVDG_RevA_PROVIDER_RFQ_<short-commit>.zip`

Do not send mixed outputs from different commits. Any electrical, footprint, connector or enclosure geometry change requires a regenerated full package.
