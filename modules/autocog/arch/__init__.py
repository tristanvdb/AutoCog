
from .architecture import CognitiveArchitecture as CogArch
from .orchestrator import Serial, Async

Serial.model_rebuild()
Async.model_rebuild()
CogArch.model_rebuild()
