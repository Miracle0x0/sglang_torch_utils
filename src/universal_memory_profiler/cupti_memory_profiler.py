import os
import ctypes


class CuptiMemoryProfiler:
    def __init__(self):
        self._cdll = None

    def initialize(self, path: str):
        os.environ['CUPTI_MEMORY_PROFILER_OUTPUT_PATH'] = path
        library_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "_cupti_memory_profiler.so",
        )
        print(f"Loading CUPTI memory profiler from {library_path}")
        self._cdll = ctypes.CDLL(library_path)
        print("hi call cuptiMemoryProfilerInit")
        self._cdll.cuptiMemoryProfilerInit()

    def shutdown(self):
        if self._cdll is not None:
            print("hi call cuptiMemoryProfilerShutdown")
            self._cdll.cuptiMemoryProfilerShutdown()


cupti_memory_profiler_instance = CuptiMemoryProfiler()
