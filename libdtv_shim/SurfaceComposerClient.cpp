/*
 * Copyright (C) 2026 The WitAqua Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <binder/IBinder.h>
#include <gui/LayerMetadata.h>
#include <gui/SurfaceControl.h>
#include <ui/PixelFormat.h>
#include <utils/String8.h>

#include <utility>

namespace android {

/*
 * libDtvULayer.so was built against Android 12, where SurfaceComposerClient::
 * createSurface() took the flags as a uint32_t and LayerMetadata still lived
 * directly in the android namespace. Both changed since, so the mangled name
 * no longer matches:
 *
 *   want: ...createSurfaceERKNS_7String8Ejji j RK...NS_    13LayerMetadataEPj
 *   have: ...createSurfaceERKNS_7String8Ejji i RK...NS_3gui13LayerMetadataEPj
 *
 * Declare the class here instead of including <gui/SurfaceComposerClient.h>,
 * so that the old overload can be named. A member function mangles the same
 * way whether the enclosing scope is a class or a namespace.
 */
struct LayerMetadata : gui::LayerMetadata {};

class SurfaceComposerClient {
  public:
    sp<SurfaceControl> createSurface(const String8& name, uint32_t w, uint32_t h,
                                     PixelFormat format, int32_t flags,
                                     const sp<IBinder>& parentHandle,
                                     gui::LayerMetadata metadata, uint32_t* outTransformHint);

    sp<SurfaceControl> createSurface(const String8& name, uint32_t w, uint32_t h,
                                     PixelFormat format, uint32_t flags,
                                     const sp<IBinder>& parentHandle, LayerMetadata metadata,
                                     uint32_t* outTransformHint);
};

sp<SurfaceControl> SurfaceComposerClient::createSurface(
        const String8& name, uint32_t w, uint32_t h, PixelFormat format, uint32_t flags,
        const sp<IBinder>& parentHandle, LayerMetadata metadata, uint32_t* outTransformHint) {
    // Slice to the base so that only the current overload is viable
    gui::LayerMetadata base = std::move(metadata);
    return createSurface(name, w, h, format, static_cast<int32_t>(flags), parentHandle,
                         std::move(base), outTransformHint);
}

}  // namespace android
