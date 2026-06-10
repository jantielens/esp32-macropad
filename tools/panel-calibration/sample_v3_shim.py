"""Import shim: expose sample-v3.py (hyphenated filename) as a module.

We strip the __main__ guard side-effects by importing without running main().
"""
import importlib.util
from pathlib import Path

_here = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("sample_v3", _here / "sample-v3.py")
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)

# Re-export everything public into this module's namespace.
globals().update({k: v for k, v in vars(_mod).items() if not k.startswith("__")})
