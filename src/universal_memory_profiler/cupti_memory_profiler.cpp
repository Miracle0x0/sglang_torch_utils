#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cupti.h>
#include <mutex>
#include <functional>
#include <chrono>
#include <thread>
#include <Python.h>
#include <execinfo.h>
#include <cpptrace/cpptrace.hpp>

// ----------------------------- utils -------------------------------

#define CHECK_CUPTI(err) \
    if (err != CUPTI_SUCCESS) { \
        const char *errstr; \
        cuptiGetResultString(err, &errstr); \
        std::cerr << "CUPTI error in " << __FILE__ << ":" << __LINE__      \
                  << ": " << errstr << std::endl;        \
        exit(EXIT_FAILURE); \
    }

class LazyLogFile {
public:
    void withLogFile(const std::function<void(std::ofstream&)>& callback) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!ofs.is_open()) {
            openFile();
        }
        callback(ofs);
    }

    void close() {
        std::lock_guard<std::mutex> lock(mtx);
        ofs.close();
    }

private:
    void openFile() {
        const char* outputPath = std::getenv("CUPTI_MEMORY_PROFILER_OUTPUT_PATH");
        if (!outputPath) {
            std::cerr << "Environment variable MEMORY_PROFILER_OUTPUT_PATH not set." << std::endl;
            std::exit(EXIT_FAILURE);
        }
        std::cout << "[cupti_memory_profiler.cpp] write to outputPath=" << outputPath << std::endl;
        ofs.open(outputPath, std::ios::out);
        if (!ofs.is_open()) {
            std::cerr << "Failed to open file: " << outputPath << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }
private:
    std::ofstream ofs;
    std::mutex mtx;
};
LazyLogFile logFile;

void str_replace_all(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return;

    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

// ----------------------------- stack trace -------------------------------

std::string get_cpp_stacktrace() {
//    std::string out;
//
//    const int max_frames = 64;
//    void* frames[max_frames];
//    int frame_count = backtrace(frames, max_frames);
//    char **symbols = backtrace_symbols(frames, frame_count);
//    for (int i = 0; i < frame_count; ++i) {
//        out += symbols[i];
//        out += "[NL]";
//    }
//    free(symbols);
//
//    return out;

    std::string out = cpptrace::generate_trace().to_string();
    str_replace_all(out, "\n", "[NL]");
    return out;
}

std::string get_python_stacktrace() {
    if (!Py_IsInitialized()) return "py_not_init";
    if (!PyGILState_Check()) return "no_py_gil";

    std::string out;
    PyObject* traceback_module = PyImport_ImportModule("traceback");
    if (traceback_module) {
        PyObject* format_stack = PyObject_GetAttrString(traceback_module, "format_stack");
        if (format_stack && PyCallable_Check(format_stack)) {
            PyObject* py_stack = PyObject_CallObject(format_stack, nullptr);
            if (py_stack) {
                PyObject* empty = PyUnicode_FromString("");
                PyObject* joined = PyUnicode_Join(empty, py_stack);
                if (joined) {
                    out = PyUnicode_AsUTF8(joined);
                    Py_DECREF(joined);
                } else {
                    out = "no_joined";
                }
                Py_DECREF(empty);
                Py_DECREF(py_stack);
            } else {
                out = "no_py_stack";
            }
            Py_DECREF(format_stack);
        } else {
            out = "no_format_stack";
        }
        Py_DECREF(traceback_module);
    } else {
        out = "no_traceback_module";
    }

    str_replace_all(out, "\n", "[NL]");
    return out;
}

// ----------------------------- cupti callbacks -------------------------------

void CUPTIAPI cuptiActivityBufferRequested(uint8_t **buffer, size_t *size, size_t *maxNumRecords) {
//    std::cout << "hi cuptiActivityBufferRequested " << std::endl;
    *size = 16 * 1024;
    *buffer = (uint8_t *)malloc(*size);
    *maxNumRecords = 0;
}

void CUPTIAPI cuptiActivityBufferCompleted(CUcontext ctx, uint32_t streamId, uint8_t *buffer,
                               size_t size, size_t validSize) {
//    std::cout << "hi cuptiActivityBufferCompleted " << validSize << std::endl;

    CUptiResult status;
    CUpti_Activity *record = nullptr;

    while (true) {
        status = cuptiActivityGetNextRecord(buffer, validSize, &record);
        if (status == CUPTI_SUCCESS) {
            if (record->kind == CUPTI_ACTIVITY_KIND_MEMORY2) {
                CUpti_ActivityMemory3 *mem = (CUpti_ActivityMemory3 *)record;

                const char *opType = "should_not_see_this";
                switch (mem->memoryOperationType) {
                    case CUPTI_ACTIVITY_MEMORY_OPERATION_TYPE_ALLOCATION:
                        opType = "ALLOC"; break;
                    case CUPTI_ACTIVITY_MEMORY_OPERATION_TYPE_RELEASE:
                        opType = "FREE"; break;
                    default:
                        break;
                }

                const char *kind = "should_not_see_this";
                switch (mem->memoryKind) {
                    case CUPTI_ACTIVITY_MEMORY_KIND_PAGEABLE: kind = "PAGEABLE"; break;
                    case CUPTI_ACTIVITY_MEMORY_KIND_PINNED: kind = "PINNED"; break;
                    case CUPTI_ACTIVITY_MEMORY_KIND_DEVICE: kind = "DEVICE"; break;
                    case CUPTI_ACTIVITY_MEMORY_KIND_ARRAY: kind = "ARRAY"; break;
                    case CUPTI_ACTIVITY_MEMORY_KIND_MANAGED: kind = "MANAGED"; break;
                    case CUPTI_ACTIVITY_MEMORY_KIND_DEVICE_STATIC: kind = "DEVICE_STATIC"; break;
                    case CUPTI_ACTIVITY_MEMORY_KIND_MANAGED_STATIC: kind = "MANAGED_STATIC"; break;
                    default:
                        break;
                }

                logFile.withLogFile([&](std::ofstream& out) {
                    out << "category=ACTIVITY_MEMORY"
                            << "\tcorrelationId=" << mem->correlationId
                            << "\ttype=" << opType
                            << "\tkind=" << kind
                            << "\ttimestamp=" << mem->timestamp
                            << "\taddress=" << mem->address
                            << "\tsize=" << mem->bytes
                            << "\tdeviceId=" << mem->deviceId
                            << "\tcontextId=" << mem->contextId
                            << "\tstreamId=" << mem->streamId
                            << std::endl;
                });
            }
        } else if (status == CUPTI_ERROR_MAX_LIMIT_REACHED) {
            break;
        } else {
            const char *errstr;
            cuptiGetResultString(status, &errstr);
            std::cerr << "Error processing CUPTI activity records: " << errstr << std::endl;
            break;
        }
    }

    free(buffer);
}

void CUPTIAPI cuptiCallbackHandler(
    void* userdata,
    CUpti_CallbackDomain domain,
    CUpti_CallbackId cbid,
    const CUpti_CallbackData* cbInfo
) {
    const char* apiName = nullptr;
    switch (domain) {
        // https://gitlab.com/nvidia/headers/cuda-individual/cupti/-/blob/main/generated_cuda_runtime_api_meta.h
        case CUPTI_CB_DOMAIN_RUNTIME_API:
            switch (cbid) {
                case CUPTI_RUNTIME_TRACE_CBID_cudaMalloc_v3020: apiName = "cudaMalloc"; break;
                case CUPTI_RUNTIME_TRACE_CBID_cudaFree_v3020: apiName = "cudaFree"; break;
                case CUPTI_RUNTIME_TRACE_CBID_cudaMallocManaged_v6000: apiName = "cudaMallocManaged"; break;
                default: break;
            }
            break;

        // https://gitlab.com/nvidia/headers/cuda-individual/cupti/-/blob/main/generated_cuda_meta.h
        case CUPTI_CB_DOMAIN_DRIVER_API:
            switch (cbid) {
                case CUPTI_DRIVER_TRACE_CBID_cuMemCreate: apiName = "cuMemCreate"; break;
                case CUPTI_DRIVER_TRACE_CBID_cuMemRelease: apiName = "cuMemRelease"; break;
                case CUPTI_DRIVER_TRACE_CBID_cuMemSetAccess: apiName = "cuMemSetAccess"; break;
                default: break;
            }
            break;

        default: break;
    }

    const char *site = "UNKNOWN";
    switch (cbInfo->callbackSite) {
        case CUPTI_API_ENTER:
            site = "ENTER"; break;
        case CUPTI_API_EXIT:
            site = "EXIT"; break;
        default:
            break;
    }

    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    logFile.withLogFile([&](std::ofstream& out) {
        out << "category=CALLBACK"
                << "\tcorrelationId=" << cbInfo->correlationId
                << "\tdomain=" << domain
                << "\tcbid=" << cbid
                << "\tsite=" << site
                << "\ttimestamp=" << timestamp;
        if (apiName != nullptr) {
            out << "\tapiName=" << apiName
                << "\tcpp_stack=" << get_cpp_stacktrace()
                << "\tpython_stack=" << get_python_stacktrace();
        }
        out << std::endl;
    });
}

// ----------------------------- api -------------------------------

extern "C" {
void cuptiMemoryProfilerInit() {
    std::cout << "[cupti_memory_profiler.cpp] init START" << std::endl;

    CHECK_CUPTI(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMORY2));
    CHECK_CUPTI(cuptiActivityRegisterCallbacks(cuptiActivityBufferRequested, cuptiActivityBufferCompleted));

    CUpti_SubscriberHandle subscriber;
    CHECK_CUPTI(cuptiSubscribe(&subscriber, (CUpti_CallbackFunc)cuptiCallbackHandler, nullptr));
    CHECK_CUPTI(cuptiEnableDomain(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API));
    CHECK_CUPTI(cuptiEnableDomain(1, subscriber, CUPTI_CB_DOMAIN_DRIVER_API));

    std::cout << "[cupti_memory_profiler.cpp] init END" << std::endl;
}

void cuptiMemoryProfilerShutdown() {
    std::cout << "[cupti_memory_profiler.cpp] shutdown START" << std::endl;

    std::cout << "Sleeping..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    std::cout << "call cuptiActivityFlushAll" << std::endl;
    cuptiActivityFlushAll(0);

    std::cout << "Sleeping..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    std::cout << "call logFile.close" << std::endl;
    logFile.close();

    std::cout << "[cupti_memory_profiler.cpp] shutdown END" << std::endl;
}
}
