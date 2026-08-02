/*
 * Copyright (C) 2026 The WitAqua Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <src/piex.h>

namespace piex {

/*
 * libpiex only keeps the overload that reports the raw image type. The one
 * libDtvULayer.so was built against is gone.
 *
 * hardware/lineage/compat has the same shim, but it is vendor only and
 * libDtvULayer lives on /system, so carry a copy here.
 */
Error GetPreviewImageData(StreamInterface* data, PreviewImageData* preview_image_data) {
    return GetPreviewImageData(data, preview_image_data, nullptr);
}

}  // namespace piex
