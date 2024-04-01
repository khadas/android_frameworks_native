/*
 * Copyright 2022 The Android Open Source Project
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

// #define LOG_NDEBUG 0
#undef LOG_TAG
#define LOG_TAG "LayerFE"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <gui/GLConsumer.h>
#include <gui/TraceUtils.h>
#include <math/vec3.h>
#include <system/window.h>
#include <utils/Log.h>
#include <renderengine/impl/ExternalTexture.h>
#include <ui/GraphicBuffer.h>
#include <gralloctypes/Gralloc4.h>
#if RK_NV12_10_TO_NV12_BY_RGA
// Use im2d api
#include <im2d.hpp>
#endif

#include "LayerFE.h"
#include "SurfaceFlinger.h"

namespace android {

namespace {
constexpr float defaultMaxLuminance = 1000.0;

constexpr mat4 inverseOrientation(uint32_t transform) {
    const mat4 flipH(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1);
    const mat4 flipV(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1);
    const mat4 rot90(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1);
    mat4 tr;

    if (transform & NATIVE_WINDOW_TRANSFORM_ROT_90) {
        tr = tr * rot90;
    }
    if (transform & NATIVE_WINDOW_TRANSFORM_FLIP_H) {
        tr = tr * flipH;
    }
    if (transform & NATIVE_WINDOW_TRANSFORM_FLIP_V) {
        tr = tr * flipV;
    }
    return inverse(tr);
}

FloatRect reduce(const FloatRect& win, const Region& exclude) {
    if (CC_LIKELY(exclude.isEmpty())) {
        return win;
    }
    // Convert through Rect (by rounding) for lack of FloatRegion
    return Region(Rect{win}).subtract(exclude).getBounds().toFloatRect();
}

// Computes the transform matrix using the setFilteringEnabled to determine whether the
// transform matrix should be computed for use with bilinear filtering.
void getDrawingTransformMatrix(const std::shared_ptr<renderengine::ExternalTexture>& buffer,
                               Rect bufferCrop, uint32_t bufferTransform, bool filteringEnabled,
                               float outMatrix[16]) {
    if (!buffer) {
        ALOGE("Buffer should not be null!");
        return;
    }
    GLConsumer::computeTransformMatrix(outMatrix, static_cast<float>(buffer->getWidth()),
                                       static_cast<float>(buffer->getHeight()),
                                       buffer->getPixelFormat(), bufferCrop, bufferTransform,
                                       filteringEnabled);
}

} // namespace

LayerFE::LayerFE(const std::string& name) : mName(name) {}

const compositionengine::LayerFECompositionState* LayerFE::getCompositionState() const {
    return mSnapshot.get();
}

bool LayerFE::onPreComposition(nsecs_t refreshStartTime, bool) {
    mCompositionResult.refreshStartTime = refreshStartTime;
    return mSnapshot->hasReadyFrame;
}


#if RK_NV12_10_TO_NV12_BY_RGA
#define HAL_PIXEL_FORMAT_YCrCb_NV12 0x15
#define HAL_PIXEL_FORMAT_YCrCb_NV12_10 0x17

#define MAX_DST_BUFFER_NUM  2
sp<GraphicBuffer> dstBufferRGA[MAX_DST_BUFFER_NUM];
#define yuvTexUsage GraphicBuffer::USAGE_HW_TEXTURE /*| HDRUSAGE*/
#define yuvTexFormat HAL_PIXEL_FORMAT_YCrCb_NV12

const sp<GraphicBuffer> & rgaCopyBit(sp<GraphicBuffer> src_buf, const Rect& rect, int dst_width, int dst_height)
{
    int ret = 0;
    rga_buffer_t src;
    rga_buffer_t dst;
    rga_buffer_t pat;
    im_rect src_rect;
    im_rect dst_rect;
    im_rect pat_rect;
    rga_buffer_handle_t src_handle, dst_handle;

    memset(&src, 0, sizeof(rga_buffer_t));
    memset(&dst, 0, sizeof(rga_buffer_t));
    memset(&pat, 0, sizeof(rga_buffer_t));
    memset(&src_rect, 0, sizeof(im_rect));
    memset(&dst_rect, 0, sizeof(im_rect));
    memset(&pat_rect, 0, sizeof(im_rect));

    dst_rect.x = src_rect.x = rect.left;
	dst_rect.y = src_rect.y = rect.top;
	dst_rect.width  = src_rect.width  = rect.right - rect.left;
	dst_rect.height = src_rect.height = rect.bottom -  rect.top;

    static int yuvcnt;
    int yuvIndex = 0;
    yuvcnt ++;
    yuvIndex = yuvcnt % MAX_DST_BUFFER_NUM;
    if((dstBufferRGA[yuvIndex] != NULL) &&
    (dstBufferRGA[yuvIndex]->getWidth() != (uint32_t)dst_width ||
     dstBufferRGA[yuvIndex]->getHeight() != (uint32_t)dst_height))
    {
        dstBufferRGA[yuvIndex] = NULL;
    }
    if(dstBufferRGA[yuvIndex] == NULL)
    {
        ALOGV("nv12_10: sf new GraphicBuffer w:%d h:%d f:0x%x u:0x%x\n",dst_width, dst_height, yuvTexFormat, yuvTexUsage);
        dstBufferRGA[yuvIndex] = new GraphicBuffer((uint32_t)dst_width, (uint32_t)dst_height, yuvTexFormat, yuvTexUsage);
    }

    /*
     * Import the allocated GraphicBuffer into RGA by calling
     * importbuffer_GraphicBuffer, and use the returned buffer_handle
     * to call RGA to process the image.
     */
    src_handle = importbuffer_GraphicBuffer(src_buf);
    dst_handle = importbuffer_GraphicBuffer(dstBufferRGA[yuvIndex]);
    if (src_handle == 0 || dst_handle == 0) {
        ALOGE("nv12_10: RGA import GraphicBuffer error!\n");
    } else {
        src = wrapbuffer_handle(src_handle, (int)src_buf->getWidth(),
                                            (int)src_buf->getHeight(),
                                            (int)src_buf->getPixelFormat());
        dst = wrapbuffer_handle(dst_handle, (int)dstBufferRGA[yuvIndex]->getWidth(),
                                            (int)dstBufferRGA[yuvIndex]->getHeight(),
                                            (int)dstBufferRGA[yuvIndex]->getPixelFormat());

        ret = improcess(src, dst, pat, src_rect, dst_rect, pat_rect, 0);
        if (ret != IM_STATUS_SUCCESS) {
            ALOGE("nv12_10: RGA run fail! src[x=%d,y=%d,w=%d,h=%d,ws=%d,format=0x%x], "
                                    "dst[x=%d,y=%d,w=%d,h=%d,ws=%d,format=0x%x]\n",
                                    src_rect.x, src_rect.y, src_rect.width, src_rect.height,
                                    src_buf->getStride(), src_buf->getPixelFormat(),
                                    dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height,
                                    dstBufferRGA[yuvIndex]->getStride(), dstBufferRGA[yuvIndex]->getPixelFormat());
            ALOGE("nv12_10: RGA running failed, %s\n", imStrError((IM_STATUS)ret));
        }
    }

    if (src_handle > 0)
        releasebuffer_handle(src_handle);
    if (dst_handle > 0)
        releasebuffer_handle(dst_handle);

    return dstBufferRGA[yuvIndex];
}

#endif

#if (RK_NV12_10_TO_P010_BY_NEON | RK_NV12_10_TO_NV12_BY_NEON)

#define HAL_PIXEL_FORMAT_YCrCb_NV12 0x15
#define HAL_PIXEL_FORMAT_YCrCb_NV12_10 0x17
//#define HAL_PIXEL_FORMAT_YCBCR_P010 0x36

#define ALIGN(val, align) (((val) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(value, base)        (value & (~(base-1)))

#define MAX_DST_BUFFER_NUM  2
sp<GraphicBuffer> dstBufferCache[MAX_DST_BUFFER_NUM];

#include <dlfcn.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;

typedef void (*__rockchipxxx)(u8 *src, u8 *dst, int w, int h, int srcStride, int dstStride, int area);

#if RK_NV12_10_TO_P010_BY_NEON
#define RK_XXX_PATH         "/system/lib64/librockchipxxx.so"
#define dstBufferFormat  HAL_PIXEL_FORMAT_YCBCR_P010  //HAL_PIXEL_FORMAT_YCrCb_NV12_10
#define dstBufferUsage  GraphicBuffer::USAGE_HW_TEXTURE

void memcpy_to_p010(void * src_vaddr, void *dst_vaddr, int w, int h, int src_stride) {
    static void* dso = NULL;
    static __rockchipxxx rockchipxxx = NULL;

    if (dso == NULL) {
        dso = dlopen(RK_XXX_PATH, RTLD_NOW | RTLD_LOCAL);
    }
    if (dso == 0) {
        ALOGE("nv12_10: can't not find %s ! error=%s \n",RK_XXX_PATH,dlerror());
        return ;
    }

    if (rockchipxxx == NULL) {
        rockchipxxx = (__rockchipxxx)dlsym(dso, "_Z11rockchipxxxPhS_iiiii");
    }
    if (rockchipxxx == NULL) {
        ALOGE("nv12_10: can't not find target function in %s ! \n",RK_XXX_PATH);
        dlclose(dso);
        return ;
    }

    int src_w = 0;
    if (src_stride < (int)(ALIGN(w, 64) * 1.25)) {
        int w_tmp = static_cast<int>(floor(static_cast<float>(src_stride) / 1.25));
        src_w = ALIGN_DOWN(w_tmp, 64);
        ALOGW("nv12_10 Warning[%s,%d]: src_w=%d align_w=%d src_stride=%d!\n",
                                __FUNCTION__,__LINE__, w, src_w, src_stride);
    } else {
        src_w = ALIGN(w, 64);
    }
    rockchipxxx((u8*)src_vaddr, (u8*)dst_vaddr, src_w, h, src_stride, src_w * 2, 0);
}
#endif

#if RK_NV12_10_TO_NV12_BY_NEON
#define RK_XXX_PATH         "/system/lib/librockchipxxx.so"
#define dstBufferFormat  HAL_PIXEL_FORMAT_YCrCb_NV12  //HAL_PIXEL_FORMAT_YCrCb_NV12_10
#define dstBufferUsage  GraphicBuffer::USAGE_HW_TEXTURE

void memcpy_to_NV12(void * src_vaddr, void *dst_vaddr, int w, int h, int src_stride) {
    static void* dso = NULL;
    static __rockchipxxx rockchipxxx = NULL;

    if (dso == NULL)
        dso = dlopen(RK_XXX_PATH, RTLD_NOW | RTLD_LOCAL);
    if (dso == 0) {
        ALOGE("nv12_10: can't not find %s ! error=%s \n",RK_XXX_PATH,dlerror());
        return ;
    }

    if (rockchipxxx == NULL)
        rockchipxxx = (__rockchipxxx)dlsym(dso, "_Z15rockchipxxx3288PhS_iiiii");
    if (rockchipxxx == NULL) {
        ALOGE("nv12_10: can't not find target function in %s ! \n",RK_XXX_PATH);
        dlclose(dso);
        return ;
    }

    int src_w = 0;
    if (src_stride < (int)(ALIGN(w, 32) * 1.25)) {
        int w_tmp = static_cast<int>(floor(static_cast<float>(src_stride) / 1.25));
        src_w = ALIGN_DOWN(w_tmp, 32);
        ALOGW("nv12_10 Warning[%s,%d]: src_w=%d align_w=%d src_stride=%d!\n",
                                __FUNCTION__,__LINE__, w, src_w, src_stride);
    } else {
        src_w = ALIGN(w, 32);
    }
    rockchipxxx((u8*)src_vaddr, (u8*)dst_vaddr, src_w, h, src_stride, w, 0);
}
#endif

const sp<GraphicBuffer> & compatible_rk_nv12_10_format(const sp<GraphicBuffer>& srcBuffer,uint32_t dstWidth,uint32_t dstHeight) {
    static int yuvcnt;
    int yuvIndex ;
    yuvcnt ++;
    yuvIndex = yuvcnt % MAX_DST_BUFFER_NUM;

    if ((dstBufferCache[yuvIndex] != NULL) &&
        (dstBufferCache[yuvIndex]->getWidth() != dstWidth ||
        dstBufferCache[yuvIndex]->getHeight() != dstHeight)) {
        dstBufferCache[yuvIndex] = NULL;
    }
    if (dstBufferCache[yuvIndex] == NULL) {
        ALOGV("nv12_10: sf new GraphicBuffer w:%d h:%d f:0x%x u:0x%x\n",dstWidth, dstHeight, dstBufferFormat, dstBufferUsage);
        dstBufferCache[yuvIndex] = new GraphicBuffer(dstWidth, dstHeight, dstBufferFormat, dstBufferUsage);
    }

    void *src_vaddr;
    void *dst_vaddr;
    srcBuffer->lock(GRALLOC_USAGE_SW_READ_OFTEN, &src_vaddr);
    dstBufferCache[yuvIndex]->lock(GRALLOC_USAGE_SW_WRITE_OFTEN|GRALLOC_USAGE_SW_READ_OFTEN, &dst_vaddr);

    int src_stride = 0;
    auto& mapper = GraphicBufferMapper::get();
    std::vector<ui::PlaneLayout> plane_layouts;
    mapper.getPlaneLayouts(srcBuffer->handle, &plane_layouts);
    for (const auto&plane_layout : plane_layouts) {
        for (const auto& plane_layout_component : plane_layout.components) {
            auto type = static_cast<aidl::android::hardware::graphics::common::PlaneLayoutComponentType>
                                                                    (plane_layout_component.type.value);
            if(type == aidl::android::hardware::graphics::common::PlaneLayoutComponentType::Y){
                src_stride = static_cast<int>(plane_layout.strideInBytes);
                break;
            }
        }
    }
#if RK_NV12_10_TO_P010_BY_NEON
    memcpy_to_p010(src_vaddr, dst_vaddr, (int)dstWidth, (int)dstHeight, src_stride);
#endif

#if RK_NV12_10_TO_NV12_BY_NEON
    memcpy_to_NV12(src_vaddr, dst_vaddr, (int)dstWidth, (int)dstHeight, src_stride);
#endif

    srcBuffer->unlock();
    dstBufferCache[yuvIndex]->unlock();

    return dstBufferCache[yuvIndex];
}

#endif

std::optional<compositionengine::LayerFE::LayerSettings> LayerFE::prepareClientComposition(
        compositionengine::LayerFE::ClientCompositionTargetSettings& targetSettings) const {
    std::optional<compositionengine::LayerFE::LayerSettings> layerSettings =
            prepareClientCompositionInternal(targetSettings);
    // Nothing to render.
    if (!layerSettings) {
        return {};
    }

    // HWC requests to clear this layer.
    if (targetSettings.clearContent) {
        prepareClearClientComposition(*layerSettings, false /* blackout */);
        return layerSettings;
    }

    // set the shadow for the layer if needed
    prepareShadowClientComposition(*layerSettings, targetSettings.viewport);

    return layerSettings;
}

std::optional<compositionengine::LayerFE::LayerSettings> LayerFE::prepareClientCompositionInternal(
        compositionengine::LayerFE::ClientCompositionTargetSettings& targetSettings) const {
    ATRACE_CALL();
    compositionengine::LayerFE::LayerSettings layerSettings;
    layerSettings.geometry.boundaries =
            reduce(mSnapshot->geomLayerBounds, mSnapshot->transparentRegionHint);
    layerSettings.geometry.positionTransform = mSnapshot->geomLayerTransform.asMatrix4();

    // skip drawing content if the targetSettings indicate the content will be occluded
    const bool drawContent = targetSettings.realContentIsVisible || targetSettings.clearContent;
    layerSettings.skipContentDraw = !drawContent;

    if (!mSnapshot->colorTransformIsIdentity) {
        layerSettings.colorTransform = mSnapshot->colorTransform;
    }

    const auto& roundedCornerState = mSnapshot->roundedCorner;
    layerSettings.geometry.roundedCornersRadius = roundedCornerState.radius;
    layerSettings.geometry.roundedCornersCrop = roundedCornerState.cropRect;

    layerSettings.alpha = mSnapshot->alpha;
    layerSettings.sourceDataspace = mSnapshot->dataspace;

    // Override the dataspace transfer from 170M to sRGB if the device configuration requests this.
    // We do this here instead of in buffer info so that dumpsys can still report layers that are
    // using the 170M transfer.
    if (targetSettings.treat170mAsSrgb &&
        (layerSettings.sourceDataspace & HAL_DATASPACE_TRANSFER_MASK) ==
                HAL_DATASPACE_TRANSFER_SMPTE_170M) {
        layerSettings.sourceDataspace = static_cast<ui::Dataspace>(
                (layerSettings.sourceDataspace & HAL_DATASPACE_STANDARD_MASK) |
                (layerSettings.sourceDataspace & HAL_DATASPACE_RANGE_MASK) |
                HAL_DATASPACE_TRANSFER_SRGB);
    }

    layerSettings.whitePointNits = targetSettings.whitePointNits;
    switch (targetSettings.blurSetting) {
        case LayerFE::ClientCompositionTargetSettings::BlurSetting::Enabled:
            layerSettings.backgroundBlurRadius = mSnapshot->backgroundBlurRadius;
            layerSettings.blurRegions = mSnapshot->blurRegions;
            layerSettings.blurRegionTransform = mSnapshot->localTransformInverse.asMatrix4();
            break;
        case LayerFE::ClientCompositionTargetSettings::BlurSetting::BackgroundBlurOnly:
            layerSettings.backgroundBlurRadius = mSnapshot->backgroundBlurRadius;
            break;
        case LayerFE::ClientCompositionTargetSettings::BlurSetting::BlurRegionsOnly:
            layerSettings.blurRegions = mSnapshot->blurRegions;
            layerSettings.blurRegionTransform = mSnapshot->localTransformInverse.asMatrix4();
            break;
        case LayerFE::ClientCompositionTargetSettings::BlurSetting::Disabled:
        default:
            break;
    }
    layerSettings.stretchEffect = mSnapshot->stretchEffect;
    // Record the name of the layer for debugging further down the stack.
    layerSettings.name = mSnapshot->name;

    if (hasEffect() && !hasBufferOrSidebandStream()) {
        prepareEffectsClientComposition(layerSettings, targetSettings);
        return layerSettings;
    }

    prepareBufferStateClientComposition(layerSettings, targetSettings);
    return layerSettings;
}

void LayerFE::prepareClearClientComposition(LayerFE::LayerSettings& layerSettings,
                                            bool blackout) const {
    layerSettings.source.buffer.buffer = nullptr;
    layerSettings.source.solidColor = half3(0.0f, 0.0f, 0.0f);
    layerSettings.disableBlending = true;
    layerSettings.bufferId = 0;
    layerSettings.frameNumber = 0;

    // If layer is blacked out, force alpha to 1 so that we draw a black color layer.
    layerSettings.alpha = blackout ? 1.0f : 0.0f;
    layerSettings.name = mSnapshot->name;
}

void LayerFE::prepareEffectsClientComposition(
        compositionengine::LayerFE::LayerSettings& layerSettings,
        compositionengine::LayerFE::ClientCompositionTargetSettings& targetSettings) const {
    // If fill bounds are occluded or the fill color is invalid skip the fill settings.
    if (targetSettings.realContentIsVisible && fillsColor()) {
        // Set color for color fill settings.
        layerSettings.source.solidColor = mSnapshot->color.rgb;
    } else if (hasBlur() || drawShadows()) {
        layerSettings.skipContentDraw = true;
    }
}

void LayerFE::prepareBufferStateClientComposition(
        compositionengine::LayerFE::LayerSettings& layerSettings,
        compositionengine::LayerFE::ClientCompositionTargetSettings& targetSettings) const {
    ATRACE_CALL();
    if (CC_UNLIKELY(!mSnapshot->externalTexture)) {
        // If there is no buffer for the layer or we have sidebandstream where there is no
        // activeBuffer, then we need to return LayerSettings.
        return;
    }
    const bool blackOutLayer =
            (mSnapshot->hasProtectedContent && !targetSettings.supportsProtectedContent) ||
            ((mSnapshot->isSecure || mSnapshot->hasProtectedContent) && !targetSettings.isSecure);
    const bool bufferCanBeUsedAsHwTexture =
            mSnapshot->externalTexture->getUsage() & GraphicBuffer::USAGE_HW_TEXTURE;
    if (blackOutLayer || !bufferCanBeUsedAsHwTexture) {
        ALOGE_IF(!bufferCanBeUsedAsHwTexture, "%s is blacked out as buffer is not gpu readable",
                 mSnapshot->name.c_str());
        prepareClearClientComposition(layerSettings, true /* blackout */);
        return;
    }

#if (RK_NV12_10_TO_P010_BY_NEON | RK_NV12_10_TO_NV12_BY_NEON | RK_NV12_10_TO_NV12_BY_RGA)
    if (mSnapshot->externalTexture && mSnapshot->externalTexture->getPixelFormat() == HAL_PIXEL_FORMAT_YCrCb_NV12_10) {
#if RK_NV12_10_TO_NV12_BY_RGA
        const sp<GraphicBuffer> &dstGraphicBuffer = rgaCopyBit(mSnapshot->externalTexture->getBuffer(), mSnapshot->bufferSize,
                                                        (int)mSnapshot->externalTexture->getWidth(),
                                                        (int)mSnapshot->externalTexture->getHeight());
#else
        const sp<GraphicBuffer> &dstGraphicBuffer = compatible_rk_nv12_10_format(mSnapshot->externalTexture->getBuffer(),
                                                        mSnapshot->externalTexture->getWidth(),
                                                        mSnapshot->externalTexture->getHeight());
#endif
        std::shared_ptr<renderengine::ExternalTexture> externalTexture = std::make_shared<renderengine::impl::ExternalTexture>(
                                                                    dstGraphicBuffer, mSnapshot->mRenderEngineWapper->mRenderEngine,
                                                                    renderengine::impl::ExternalTexture::Usage::READABLE);
        layerSettings.source.buffer.buffer = externalTexture;
    }else{
        layerSettings.source.buffer.buffer = mSnapshot->externalTexture;
    }
#else
    layerSettings.source.buffer.buffer = mSnapshot->externalTexture;
#endif
    layerSettings.source.buffer.isOpaque = mSnapshot->contentOpaque;
    layerSettings.source.buffer.fence = mSnapshot->acquireFence;
    layerSettings.source.buffer.textureName = mSnapshot->textureName;
    layerSettings.source.buffer.usePremultipliedAlpha = mSnapshot->premultipliedAlpha;
    layerSettings.source.buffer.isY410BT2020 = mSnapshot->isHdrY410;
    bool hasSmpte2086 = mSnapshot->hdrMetadata.validTypes & HdrMetadata::SMPTE2086;
    bool hasCta861_3 = mSnapshot->hdrMetadata.validTypes & HdrMetadata::CTA861_3;
    float maxLuminance = 0.f;
    if (hasSmpte2086 && hasCta861_3) {
        maxLuminance = std::min(mSnapshot->hdrMetadata.smpte2086.maxLuminance,
                                mSnapshot->hdrMetadata.cta8613.maxContentLightLevel);
    } else if (hasSmpte2086) {
        maxLuminance = mSnapshot->hdrMetadata.smpte2086.maxLuminance;
    } else if (hasCta861_3) {
        maxLuminance = mSnapshot->hdrMetadata.cta8613.maxContentLightLevel;
    } else {
        switch (layerSettings.sourceDataspace & HAL_DATASPACE_TRANSFER_MASK) {
            case HAL_DATASPACE_TRANSFER_ST2084:
            case HAL_DATASPACE_TRANSFER_HLG:
                // Behavior-match previous releases for HDR content
                maxLuminance = defaultMaxLuminance;
                break;
        }
    }
    layerSettings.source.buffer.maxLuminanceNits = maxLuminance;
    layerSettings.frameNumber = mSnapshot->frameNumber;
    layerSettings.bufferId = mSnapshot->externalTexture->getId();

    // Query the texture matrix given our current filtering mode.
    float textureMatrix[16];
    getDrawingTransformMatrix(layerSettings.source.buffer.buffer, mSnapshot->geomContentCrop,
                              mSnapshot->geomBufferTransform, targetSettings.needsFiltering,
                              textureMatrix);

    if (mSnapshot->geomBufferUsesDisplayInverseTransform) {
        /*
         * the code below applies the primary display's inverse transform to
         * the texture transform
         */
        uint32_t transform = SurfaceFlinger::getActiveDisplayRotationFlags();
        mat4 tr = inverseOrientation(transform);

        /**
         * TODO(b/36727915): This is basically a hack.
         *
         * Ensure that regardless of the parent transformation,
         * this buffer is always transformed from native display
         * orientation to display orientation. For example, in the case
         * of a camera where the buffer remains in native orientation,
         * we want the pixels to always be upright.
         */
        const auto parentTransform = mSnapshot->parentTransform;
        tr = tr * inverseOrientation(parentTransform.getOrientation());

        // and finally apply it to the original texture matrix
        const mat4 texTransform(mat4(static_cast<const float*>(textureMatrix)) * tr);
        memcpy(textureMatrix, texTransform.asArray(), sizeof(textureMatrix));
    }

    const Rect win{layerSettings.geometry.boundaries};
    float bufferWidth = static_cast<float>(mSnapshot->bufferSize.getWidth());
    float bufferHeight = static_cast<float>(mSnapshot->bufferSize.getHeight());

    // Layers can have a "buffer size" of [0, 0, -1, -1] when no display frame has
    // been set and there is no parent layer bounds. In that case, the scale is meaningless so
    // ignore them.
    if (!mSnapshot->bufferSize.isValid()) {
        bufferWidth = float(win.right) - float(win.left);
        bufferHeight = float(win.bottom) - float(win.top);
    }

    const float scaleHeight = (float(win.bottom) - float(win.top)) / bufferHeight;
    const float scaleWidth = (float(win.right) - float(win.left)) / bufferWidth;
    const float translateY = float(win.top) / bufferHeight;
    const float translateX = float(win.left) / bufferWidth;

    // Flip y-coordinates because GLConsumer expects OpenGL convention.
    mat4 tr = mat4::translate(vec4(.5f, .5f, 0.f, 1.f)) * mat4::scale(vec4(1.f, -1.f, 1.f, 1.f)) *
            mat4::translate(vec4(-.5f, -.5f, 0.f, 1.f)) *
            mat4::translate(vec4(translateX, translateY, 0.f, 1.f)) *
            mat4::scale(vec4(scaleWidth, scaleHeight, 1.0f, 1.0f));

    layerSettings.source.buffer.useTextureFiltering = targetSettings.needsFiltering;
    layerSettings.source.buffer.textureTransform =
            mat4(static_cast<const float*>(textureMatrix)) * tr;

    return;
}

void LayerFE::prepareShadowClientComposition(LayerFE::LayerSettings& caster,
                                             const Rect& layerStackRect) const {
    renderengine::ShadowSettings state = mSnapshot->shadowSettings;
    if (state.length <= 0.f || (state.ambientColor.a <= 0.f && state.spotColor.a <= 0.f)) {
        return;
    }

    // Shift the spot light x-position to the middle of the display and then
    // offset it by casting layer's screen pos.
    state.lightPos.x =
            (static_cast<float>(layerStackRect.width()) / 2.f) - mSnapshot->transformedBounds.left;
    state.lightPos.y -= mSnapshot->transformedBounds.top;
    caster.shadow = state;
}

void LayerFE::onLayerDisplayed(ftl::SharedFuture<FenceResult> futureFenceResult,
                               ui::LayerStack layerStack) {
    mCompositionResult.releaseFences.emplace_back(std::move(futureFenceResult), layerStack);
}

CompositionResult&& LayerFE::stealCompositionResult() {
    return std::move(mCompositionResult);
}

const char* LayerFE::getDebugName() const {
    return mName.c_str();
}

const LayerMetadata* LayerFE::getMetadata() const {
    return &mSnapshot->layerMetadata;
}

const LayerMetadata* LayerFE::getRelativeMetadata() const {
    return &mSnapshot->relativeLayerMetadata;
}

int32_t LayerFE::getSequence() const {
    return mSnapshot->sequence;
}

bool LayerFE::hasRoundedCorners() const {
    return mSnapshot->roundedCorner.hasRoundedCorners();
}

void LayerFE::setWasClientComposed(const sp<Fence>& fence) {
    mCompositionResult.lastClientCompositionFence = fence;
}

bool LayerFE::hasBufferOrSidebandStream() const {
    return mSnapshot->externalTexture || mSnapshot->sidebandStream;
}

bool LayerFE::fillsColor() const {
    return mSnapshot->color.r >= 0.0_hf && mSnapshot->color.g >= 0.0_hf &&
            mSnapshot->color.b >= 0.0_hf;
}

bool LayerFE::hasBlur() const {
    return mSnapshot->backgroundBlurRadius > 0 || mSnapshot->blurRegions.size() > 0;
}

bool LayerFE::drawShadows() const {
    return mSnapshot->shadowSettings.length > 0.f &&
            (mSnapshot->shadowSettings.ambientColor.a > 0 ||
             mSnapshot->shadowSettings.spotColor.a > 0);
};

const sp<GraphicBuffer> LayerFE::getBuffer() const {
    return mSnapshot->externalTexture ? mSnapshot->externalTexture->getBuffer() : nullptr;
}

} // namespace android
