#!/usr/bin/env python3
"""Build the controlled Rev-A PCB/PCBA/enclosure provider package.

Fail-closed: a FINAL package is refused until H2 routing is explicitly complete
and every manufacturing output exists from the same checkout/commit.
"""
from pathlib import Path
import hashlib, subprocess, zipfile

ROOT = Path(__file__).resolve().parents[3]
K = ROOT / 'hardware' / 'kicad'
M = ROOT / 'hardware' / 'mechanical'
DOC = ROOT / 'docs'
OUT = ROOT / 'provider_release'
MF = K / 'manufacturing'

required_exact = [
    K / 'Automatrix_PVDG_RevA.kicad_pro', K / 'Automatrix_PVDG_RevA.kicad_sch',
    K / 'Automatrix_PVDG_RevA.kicad_pcb', K / 'Automatrix_PVDG_RevA-schematic.pdf',
    K / 'Automatrix_PVDG_RevA-bom.csv', K / 'BOM_TARGET.csv',
    K / 'Automatrix_PVDG_RevA-drc.rpt', K / 'Automatrix_PVDG_RevA-erc.rpt',
    K / 'Automatrix_PVDG_RevA.step', K / 'H2_ROUTING_COMPLETE',
    DOC / 'HARDWARE_PCB_REVA_MASTER_PLAN.md',
    DOC / 'PCB_AND_ENCLOSURE_SERVICE_PROVIDER_RFQ.md',
    DOC / 'PCB_PROVIDER_HANDOFF_READINESS.md', DOC / 'PROVIDER_PACKAGE_CONTENTS.md',
    M / 'Automatrix_PVDG_RevA_enclosure.scad', M / 'ENCLOSURE_REVA_SPEC.md',
]
required_manufacturing = {
    'Gerbers': list((MF / 'gerber').glob('*')),
    'Drill': list((MF / 'drill').glob('*')),
    'CPL': list(MF.glob('*cpl*.csv')) + list(MF.glob('*pos*.csv')),
    'Manufacturing BOM': list(MF.glob('*bom*.csv')),
    'Assembly drawings': list((MF / 'drawings').glob('*.pdf')),
    'STEP': list(MF.glob('*.step')),
    'Final DRC/ERC': [MF / 'DRC_FINAL.rpt', MF / 'ERC_FINAL.rpt'],
    'IPC-D-356': list(MF.glob('*.ipc')),
    'Board stats': list(MF.glob('*stats*.json')),
}

missing=[str(p.relative_to(ROOT)) for p in required_exact if not p.exists() or p.stat().st_size==0]
matched=[]
for label,candidates in required_manufacturing.items():
    hits=sorted({p for p in candidates if p.is_file() and p.stat().st_size>0})
    if not hits: missing.append(f'{label}: required manufacturing output missing')
    matched.extend(hits)
for drawing in ('ASSEMBLY_TOP.pdf','ASSEMBLY_BOTTOM.pdf'):
    p=MF/'drawings'/drawing
    if not p.exists() or p.stat().st_size==0: missing.append(f'Assembly drawings: {drawing} missing')
if missing:
    print('PROVIDER PACKAGE: NOT READY')
    for item in missing: print(' -',item)
    raise SystemExit(2)

sha_full=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
sha=sha_full[:10]
OUT.mkdir(exist_ok=True)
zip_path=OUT/f'Automatrix_PVDG_RevA_PROVIDER_RFQ_{sha}.zip'
files=sorted(set(required_exact+matched))
checksums=[f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(ROOT).as_posix()}' for p in files]
with zipfile.ZipFile(zip_path,'w',compression=zipfile.ZIP_DEFLATED) as z:
    for p in files: z.write(p,p.relative_to(ROOT))
    z.writestr('RELEASE_COMMIT.txt',sha_full+'\n')
    z.writestr('SHA256SUMS.txt','\n'.join(checksums)+'\n')
print(f'PROVIDER PACKAGE: PASS -> {zip_path}')
print(f'PROVIDER PACKAGE FILES: {len(files)} controlled files; SHA256 manifest embedded')
