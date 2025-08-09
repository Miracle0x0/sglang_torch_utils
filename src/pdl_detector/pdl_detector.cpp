#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cupti.h>
#include <mutex>
#include <functional>
#include <chrono>

// ----------------------------- utils -------------------------------

#define CHECK_CUPTI(err) \
    if (err != CUPTI_SUCCESS) { \
        const char *errstr; \
        cuptiGetResultString(err, &errstr); \
        std::cerr << "CUPTI error in " << __FILE__ << ":" << __LINE__      \
                  << ": " << errstr << std::endl;        \
        exit(EXIT_FAILURE); \
    }

// ----------------------------- cupti callbacks -------------------------------

void CUPTIAPI cuptiCallbackHandler(
    void* userdata,
    CUpti_CallbackDomain domain,
    CUpti_CallbackId cbid,
    const CUpti_CallbackData* cbInfo
) {
    if (cbInfo->callbackSite != CUPTI_API_ENTER) {
        return;
    }

    bool is_interesting = false;
    bool enable_pdl = false;
    switch (domain) {
        // https://gitlab.com/nvidia/headers/cuda-individual/cupti/-/blob/main/generated_cuda_meta.h
        case CUPTI_CB_DOMAIN_DRIVER_API:
            switch (cbid) {
                case CUPTI_DRIVER_TRACE_CBID_cuLaunchKernel: {
                    is_interesting = true;
                    break;
                }
                case CUPTI_DRIVER_TRACE_CBID_cuLaunchKernelEx: {
                    is_interesting = true;

                    const cuLaunchKernelEx_params_st* params = (cuLaunchKernelEx_params_st*) cbInfo->functionParams;
                    const CUlaunchConfig* config = params->config;
                    for (int i = 0; i < config->numAttrs; ++i) {
                        const CUlaunchAttribute* attr = &config->attrs[i];
                        if (
                            // https://docs.rs/cudarc/latest/x86_64-pc-windows-msvc/cudarc/driver/sys/enum.CUlaunchAttributeID_enum.html
                            (attr->id == CU_LAUNCH_ATTRIBUTE_PROGRAMMATIC_STREAM_SERIALIZATION)
                            // https://docs.rs/cudarc/latest/x86_64-pc-windows-msvc/cudarc/driver/sys/union.CUlaunchAttributeValue_union.html
                            && (attr->value.programmaticStreamSerializationAllowed != 0)
//                            (attr->id == cudaLaunchAttributeProgrammaticStreamSerialization)
//                            && (attr->val.programmaticStreamSerializationAllowed != 0)
                        ) {
                            enable_pdl = true;
                        }
                    }
                    break;
                }
                default: break;
            }
            break;

        default: break;
    }

    if (!is_interesting) {
        return;
    }

    std::cout << "[pdl_detector.cpp]"
        << " functionName=" << cbInfo->functionName
        << " symbolName=" << cbInfo->symbolName
        << " enable_pdl=" << enable_pdl
        << std::endl;
}

// ----------------------------- api -------------------------------

extern "C" {
void pdlDetectorInit() {
    std::cout << "[pdl_detector.cpp] init START" << std::endl;

    CUpti_SubscriberHandle subscriber;
    CHECK_CUPTI(cuptiSubscribe(&subscriber, (CUpti_CallbackFunc)cuptiCallbackHandler, nullptr));
    CHECK_CUPTI(cuptiEnableDomain(1, subscriber, CUPTI_CB_DOMAIN_DRIVER_API));

    std::cout << "[pdl_detector.cpp] init END" << std::endl;
}

void pdlDetectorShutdown() {
    std::cout << "[pdl_detector.cpp] shutdown START" << std::endl;
    std::cout << "[pdl_detector.cpp] shutdown END" << std::endl;
}
}
