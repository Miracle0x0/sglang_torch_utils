class CuptiMemoryProfiler:
    def __init__(self):
        self._cdll = None

    def initialize(self):
        os.environ[
            'CUPTI_MEMORY_PROFILER_OUTPUT_PATH'] = f"/host_home/temp_sglang_server2local/cupti_memory_profiler_{time.time()}.log"
        self._cdll = ctypes.CDLL("/host_home/primary_synced/tom_sglang_server/misc/cupti_memory_profiler.so")
        print("hi call cuptiMemoryProfilerInit")
        self._cdll.cuptiMemoryProfilerInit()

    def shutdown(self):
        if self._cdll is not None:
            print("hi call cuptiMemoryProfilerShutdown")
            self._cdll.cuptiMemoryProfilerShutdown()


cupti_memory_profiler_instance = CuptiMemoryProfiler()
