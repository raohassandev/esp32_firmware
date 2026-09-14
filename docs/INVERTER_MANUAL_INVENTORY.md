# Inverter manual inventory and authority boundary

Date audited: 2026-09-14

This inventory closes the **file-discovery** portion of the multi-brand inverter manual task. It does **not** qualify any production write profile. Production approval remains governed by Issue #82 and the physical evidence contract implemented by PR #158 and hardened by PR #193.

## Authority rules

1. A filename or plausible register map is not production authority.
2. A real profile requires an exact manufacturer, exact model, inverter firmware, connection path, official applicable manual/revision, controller firmware/artifact identity, endpoint/Unit ID, physical identity proof, read-only proof, controlled write/readback/failure/rollback evidence, and signed production approval.
3. A manual stored in `raohassandev/SolTrix` is an engineering source input. Its manufacturer origin, revision and exact model/firmware applicability must still be verified before a profile moves beyond `DOCUMENTED`.
4. Simulator maps are synthetic and may never be reused as manufacturer register evidence.
5. Unknown/ambiguous documents remain inventory-only. Do not infer manufacturer/model from a filename when that identity is not explicit.

## Repository source identity

Manual source repository: `raohassandev/SolTrix`

Audited inverter-manual tree:

- path: `Manuals/Inverter`
- tree SHA: `afa97927eca7bb7f8a602955b4c8e545d0cf00c2`

The inventory below records source blob SHAs so later extraction can be tied to immutable input documents.

## High-priority current families

| Candidate family | Repository evidence | Source blob SHA | What is established | What remains before a real profile |
| --- | --- | --- | --- | --- |
| Huawei SUN2000 | `Huawei/Huawei Inverter Modbus Interface Definitions (V3.0).pdf` | `6248c9ef10509a6e0a24386f950b8f72b1d02589` | A Huawei inverter Modbus-definition document is present. | Verify exact model list/revision applicability to the installed model/firmware, extract identity/status/telemetry/command/readback definitions, then physical qualification. |
| Huawei SmartLogger | `Huawei/SmartLogger ModBus Interface Definitions.pdf` | `8ba105aec01440ddc8a76556fa7768acb2cf83f0` | A SmartLogger Modbus-definition document is present. | Keep logger control path distinct from direct-inverter control; verify exact logger model/firmware and downstream inverter applicability. |
| Huawei SmartLogger 3000A | `Huawei/SmartLogger_3000A_manual_240727_191232.pdf` | `31cb9e6299500b841cdf94544e4e4de3c8488524` | SmartLogger 3000A operating-manual source is present. | Extract only documented commissioning/control semantics; it is not by itself a direct inverter register map. |
| Huawei 60KTL | `Huawei/Huawei 60ktl.pdf` | `9b692766b408a7a73b4c0506f7ab0ff7127eafc4` | A 60KTL-labelled source is present. | Identify exact model/revision from the document body before use. |
| Growatt | `GROWATT.pdf` | `c5b516fe5d951a265297a3a4fe02a020f12ebcbe` | A Growatt-labelled source is present. | Exact model family, revision, protocol applicability and register authority are unresolved from the repository filename alone. |
| Solis | `Solis.pdf` | `b6a26f39cd3e58841f81a43bb079411da49358e8` | A Solis-labelled source is present. | Exact model/revision and production command-map authority must be established from the document body and official source provenance. |
| Knox / ASW LT-G2 | `Knox/ASW 30K_33K_36K_40K_45K_50K-LT-G2 Series_240820_192850.pdf` | `5f27d559b8ecc59cb968bdd3d80eb83083c3d379` | ASW 30-50K LT-G2 family manual source is present in the Knox folder. | Do not equate folder name `Knox` with inverter manufacturer identity. Verify physical nameplate/model/firmware and document provenance. |
| ASW GEN Modbus | `Knox/MB001_ASW GEN-Modbus-en_V2.1.5(2).pdf` | `4508b2455806d517bdd9ace20e6803098bbea345` | ASW GEN Modbus V2.1.5 source is present. | Establish exact ASW model applicability, manufacturer identity and installed firmware before extraction/qualification. |
| ASW GEN Modbus alternate copy | `Knox/MB001_ASW GEN-Modbus-en_V215(2)_240820_192756.pdf` | `59f5e961d63be9b1c3c75d62b6ff387e8789a067` | A second V2.1.5-labelled source exists. | Compare document revision/content before selecting an authority; do not mix register evidence across copies. |
| ASW GEN Modbus older root copy | `MB001_ASW GEN-Modbus-en_V211_240529_233746.pdf` | `61063a053381e15f048698d9976951912bd7675f` | An older V2.1.1-labelled root source exists. | Supersession/applicability must be established before use. |
| ASW 30-50K LT-G2 user manual | `UM0011_ASW-30-50K-LT-G2_EN_V03_11212_240529_234923.pdf` | `63760d5f5d35431c6ecb0aad3a5d4f4cee7391c7` | Family user-manual source is present. | Pair with an applicable protocol manual and physical identity; user manual alone does not authorize writes. |

## Other inventoried protocol/manual sources

These documents are useful for future exact-profile work but are not current production-approved profiles.

| Family | Source | Blob SHA | Inventory note |
| --- | --- | --- | --- |
| CPS/Chint 100/125 kW | `CPS/CPS_100_125kW-UL-Modbus-Map-Spec-FW-V12.0.pdf` | `ac9df8e02fd99cd8b13af9f10ea6ac2efae72344` | Explicit Modbus map and firmware V12.0 in filename; exact deployed identity still required. |
| CPS 100/125 kW | `CPS/CPS-SCH100-125KTL-DO-US-600-480_Manual_-Aug-2021-1.pdf` | `ac04f61327fdb238889da1f863b646e000a39bed` | Product manual source. |
| Chint/CPS 100/125 kW | `Chint/CPS_100_125kW-UL-Modbus-Map-Spec-FW-V120_240817_221331.pdf` | `314844f02b285fd9a91a032b25049c90f2930f37` | Duplicate/variant Modbus source; compare with CPS copy before authority selection. |
| SMA | `SMA/SMA-Modbus-general-TI-en-10.pdf` | `32e982d3d83fbb6a7a4e7b75c620985b35872c60` | General SMA Modbus technical information. |
| SMA ennexOS / SunSpec | `SMA/ennexOS-SunSpec-Modbus-TI-en-10.pdf` | `ad0c11643bb5c75af3213bc2f7be0578510fcbcb` | SunSpec/ennexOS technical information. |
| SMA STP50 | `SMA/SMA STP50-4x en.pdf` | `c38282b85081efd2601d8d1136a5c3991b881cf1` | Product-family manual source. |
| SolarEdge TerraMax | `Solar edge/se-modbus-interface-for-solaredge-terramax-inverter-technical-note.pdf` | `5f8d8eed404d436b12bb4e966145f0876cf4f86a` | Explicit TerraMax Modbus interface technical note. |
| SolarEdge SunSpec | `Solar edge/sunspec-implementation-technical-note.pdf` | `e689ca73278d90d3344e6c23b36fa8e14158f532` | SunSpec implementation technical note. |
| SolaX Hybrid X1/X3 G4 | `Solax/Hybrid-X1X3-G4-ModbusTCPRTU-V321-English_0622-pub_240818_001120.pdf` | `8663f46241dc3649881b50643333bf174bd381ca` | Explicit Modbus TCP/RTU V3.21-labelled source. |
| Sungrow | `Sungrow .pdf` | `da5620f5a5aca37750b05fbe2b0703aeb6d00eb1` | Manufacturer-labelled source; exact model/revision unresolved from filename. |
| Unclassified | `MC 200.pdf` | `95c7d2acd50d6713ded8289b1046bff78ddd75c0` | Do not infer manufacturer/model from filename. |
| Unclassified external Modbus map | `首航外部Modbus通讯协议地址说明(1)(1).xlsx` | `e8e534ca0873ccea068b2a59cab9c2adc1377ad9` | Spreadsheet protocol source present; manufacturer/model/revision must be established before use. |

## Public manufacturer-source recheck — 2026-09-14

This recheck is supplementary. Public-source existence does not bypass the exact-identity and physical qualification gates.

### Huawei

Huawei currently publishes the `SUN2000-115KTL-M2` product/datasheet and a current `SUN2000MB V200R023C10` Modbus Interface Definitions document family (Issue 03, 2024-07-15 is publicly indexed). This is stronger documentary evidence than the previous generic catalogue text, but the repository still lacks an accepted, externally locked proof that the exact intended inverter firmware/model is covered by the selected Modbus document and register map. Therefore the real Huawei catalogue entry remains write-locked.

### GoodWe

GoodWe's official `HT Series (100-136 kW)` user manual V1.7 dated 2025-07-10 identifies `GW100K-HT` and states `Modbus-RTU (SunSpec compliant)` over RS485. GoodWe also currently publishes a `GW_Third-Party EMS Protocol-MODBUS-EN` resource for its EMS/logger ecosystem. Neither source is sufficient, by itself, to infer the exact direct-inverter production command/register map for an installed HT unit. A historical GoodWe Solar Academy document explicitly directs users to obtain the HT Modbus RTU protocol through the GoodWe sales channel. The GoodWe production profile therefore remains pending.

### Solis

Solis currently publishes the exact `S6-EH3P(80-125)K10-NV-YD-H` product family and an official user manual `Solis_Manual_S6-EH3P(75-125)K10-NV-YD-H_EUR_V1.1(20260716)`. The public product/manual page does not establish an accepted production Modbus control-register map. Do not convert the existing pending entry into a writable profile from the user manual alone.

### Growatt

Growatt's current official downloads expose multiple model-family manuals and some protocol-facing products, but the repository's `GROWATT.pdf` filename does not establish which installed model/firmware it represents. Exact installed identity and protocol applicability remain required.

## Catalogue disposition

No change to `components/inverter_manager/inverter_profiles.c` is justified by this inventory alone. The existing manufacturer entries must stay non-commandable until the exact model/manual/firmware and physical evidence requirements are satisfied.

Safe status after this audit:

- manual **file inventory**: complete for the current `SolTrix/Manuals/Inverter` tree;
- immutable source SHAs: recorded for the principal protocol/manual inputs;
- public manufacturer-source recheck: complete as of 2026-09-14 for Huawei/GoodWe/Solis/Growatt discovery;
- exact register extraction: still required per selected deployment identity;
- read-only physical qualification: still required;
- write/readback/rollback qualification: still required;
- signed production approval: still required;
- production write gate: remains closed.
