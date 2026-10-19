#pragma once
#ifndef WINPREVIEW_CORE_H
#define WINPREVIEW_CORE_H

#include <stdint.h>
#include <stdbool.nderSurfaceDescriptor* desc);
void DestroyRenderer(void);nderSurfaceDescriptor* desc);
void DestroyRenderer(void);nderSurfaceDescriptor* desc);
#ifdef plusplusoo
extern "C" {
#end

typedef struct {
    float x;
    float y;
    float width;
    float height;
} WinPreviewRectF;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frame_rate;
    bool is_hardware_accelerated;
} RenderSurfaceDescriptor;

bool InitializeRenderer(const RenderSurfaceDescriptor* desc);
void DestroyRenderer(void);
void RenderFrame(const WinPreviewRectF* viewport);

#ifdef __cplusplus
}
#endif

#endif // WINPREVIEW_CORE_H
