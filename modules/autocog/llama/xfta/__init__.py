
try:
    from .xfta_cxx import *
except ImportError as e:
    raise ImportError(f"Failed to import autocog.llama.xfta C++ extension: {e}")

