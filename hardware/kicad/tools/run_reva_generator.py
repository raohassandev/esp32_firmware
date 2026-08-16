#!/usr/bin/env python3
"""Stable CI entrypoint for Rev-A generator composition."""
import generate_reva_final as final

# Legacy manifest names the generic capacitor symbol `C`.  The optional RS232
# wrapper deliberately uses a semantic `CAP` alias; bind it to the exact same
# project-local symbol definition before generation.
final.g.DEFS["CAP"] = final.g.DEFS["C"]

if __name__ == "__main__":
    final.main()
