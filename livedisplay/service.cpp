/*
 * Copyright (C) 2019 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <dlfcn.h>

#define LOG_TAG "lineage.livedisplay@2.0-service.oneplus_kona"

#define SDM_DISP_LIB "libsdm-disp-apis.qti.so"

#include <android-base/logging.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>

#include "DisplayModes.h"
#include "PictureAdjustment.h"
#include "SunlightEnhancement.h"

using android::OK;
using android::sp;
using android::status_t;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using ::vendor::lineage::livedisplay::V2_0::IDisplayModes;
using ::vendor::lineage::livedisplay::V2_0::IPictureAdjustment;
using ::vendor::lineage::livedisplay::V2_0::ISunlightEnhancement;
using ::vendor::lineage::livedisplay::V2_0::implementation::DisplayModes;
using ::vendor::lineage::livedisplay::V2_0::implementation::PictureAdjustment;
using ::vendor::lineage::livedisplay::V2_0::implementation::SunlightEnhancement;

int main() {
    // Vendor backend
    void* libHandle = nullptr;
    const char* libName = nullptr;
    int32_t (*disp_api_init)(uint64_t*, uint32_t) = nullptr;
    int32_t (*disp_api_deinit)(uint64_t, uint32_t) = nullptr;
    uint64_t cookie = 0;

    // HIDL frontend
    sp<DisplayModes> dm;
    sp<PictureAdjustment> pa;
    sp<SunlightEnhancement> se;

    status_t status = OK;

    android::ProcessState::initWithDriver("/dev/binder");

    LOG(INFO) << "LiveDisplay HAL service is starting.";

    libHandle = dlopen(SDM_DISP_LIB, RTLD_NOW);
    if (libHandle == nullptr) {
        LOG(ERROR) << "Failed to load SDM display lib, exiting.";
        goto shutdown;
    }

    disp_api_init =
            reinterpret_cast<int32_t (*)(uint64_t*, uint32_t)>(dlsym(libHandle, "disp_api_init"));
    if (disp_api_init == nullptr) {
        LOG(ERROR) << "Can not get disp_api_init from " << libName << " (" << dlerror() << ")";
        goto shutdown;
    }

    disp_api_deinit =
            reinterpret_cast<int32_t (*)(uint64_t, uint32_t)>(dlsym(libHandle, "disp_api_deinit"));
    if (disp_api_deinit == nullptr) {
        LOG(ERROR) << "Can not get disp_api_deinit from " << libName << " (" << dlerror() << ")";
        goto shutdown;
    }

    status = disp_api_init(&cookie, 0);
    if (status != OK) {
        LOG(ERROR) << "Can not initialize " << libName << " (" << status << ")";
        goto shutdown;
    }

    dm = new DisplayModes();
    if (dm == nullptr) {
        LOG(ERROR) << "Can not create an instance of LiveDisplay HAL DisplayModes Iface, exiting.";
        goto shutdown;
    }

    pa = new PictureAdjustment(libHandle, cookie);
    if (pa == nullptr) {
        LOG(ERROR) << "Can not create an instance of LiveDisplay HAL PictureAdjustment Iface, "
                      "exiting.";
        goto shutdown;
    }

    se = new SunlightEnhancement();
    if (se == nullptr) {
        LOG(ERROR) << "Can not create an instance of LiveDisplay HAL SunlightEnhancement Iface, "
                      "exiting.";
        goto shutdown;
    }

    if (!pa->isSupported()) {
        // Backend isn't ready yet, so restart and try again
        goto shutdown;
    }

    configureRpcThreadpool(1, true /*callerWillJoin*/);

    status = dm->registerAsService();
    if (status != OK) {
        LOG(ERROR) << "Could not register service for LiveDisplay HAL DisplayModes Iface ("
                   << status << ")";
        goto shutdown;
    }

    if (pa->isSupported()) {
        status = pa->registerAsService();
        if (status != OK) {
            LOG(ERROR) << "Could not register service for LiveDisplay HAL PictureAdjustment Iface ("
                       << status << ")";
            goto shutdown;
        }
    }

    status = se->registerAsService();
    if (status != OK) {
        LOG(ERROR) << "Could not register service for LiveDisplay HAL SunlightEnhancement Iface ("
                   << status << ")";
        goto shutdown;
    }

    LOG(INFO) << "LiveDisplay HAL service is ready.";
    joinRpcThreadpool();
    // Should not pass this line

shutdown:
    // Cleanup what we started
    if (disp_api_deinit != nullptr) {
        disp_api_deinit(cookie, 0);
    }

    if (libHandle != nullptr) {
        dlclose(libHandle);
    }

    // In normal operation, we don't expect the thread pool to shutdown
    LOG(ERROR) << "LiveDisplay HAL service is shutting down.";
    return 1;
}
