import os
import ctypes


class PdlDetector:
    def __init__(self):
        self._cdll = None

    def initialize(self):
        library_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_pdl_detector.so")
        print(f"PdlDetector.initialize {library_path=}")
        self._cdll = ctypes.CDLL(library_path)
        self._cdll.pdlDetectorInit()

    def shutdown(self):
        print(f"PdlDetector.shutdown")
        if self._cdll is not None:
            self._cdll.pdlDetectorShutdown()


pdl_detector_instance = PdlDetector()
