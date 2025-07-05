# How to use

If more people need this, I will polish and make it easy to use.

Dependency:

* https://github.com/jeremy-rifkin/cpptrace

Compile:

```shell
nvcc -Xcompiler -fPIC -shared -I/usr/include/python3.10 -o _cupti_memory_profiler.so cupti_memory_profiler.cpp -lcupti -I/tmp/cpptrace-test/resources/include -L/tmp/cpptrace-test/resources/lib -lcpptrace -ldwarf -lz -lzstd -ldl
```

Modify program (e.g. SGLang) to trigger the profiler (where you may copy-paste or import the `cupti_memory_profiler.py`):

```python
# init
cupti_memory_profiler_instance.initialize()

# shutdown
cupti_memory_profiler_instance.shutdown()
```

Run program (e.g. SGLang):

```shell
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/tmp/cpptrace-test/resources/lib python3 -m sglang.launch_server ...
```

Analyze results:

`cupti_memory_profiler_analyzer.py` is a Jupytext dump of my jupyter notebook, so you may run it directly as python file, or import to notebook.
