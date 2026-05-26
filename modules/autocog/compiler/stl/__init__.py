
try:
    from .stl_cxx import *
except ImportError as e:
    raise ImportError(f"Failed to import autocog.compiler.stl C++ extension: {e}")

