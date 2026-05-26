"""pytest configuration for Python unit tests."""

import sys
import pathlib

# Add modules directory to path
repo = pathlib.Path(__file__).parent.parent.parent.parent
sys.path.insert(0, str(repo / "modules"))
