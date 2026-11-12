// Lucide-derived vector icon rendering.
// Geometry is normalized to a 24x24 box, stroked at a constant weight and cached
// once per icon id. ISC-licensed source geometry (https://lucide.dev).
#include "d2d_engine.h"
#include "icons_data.h"
#include <cmath>

namespace WinPreview {

ID2D1PathGeometry* D2DEngine::GetIconGeometry(int id) {
    if (!m_d2dFactory || id < 0 || id >= WPICON_COUNT) return nullptr;

    if (m_iconGeometries.size() != (size_t)WPICON_COUNT) {
        m_iconGeometries.assign((size_t)WPICON_COUNT, nullptr);
    }
    if (m_iconGeometries[(size_t)id]) return m_iconGeometries[(size_t)id];

    const WpIconAsset& asset = kIconAssets[id];
    if (!asset.points || asset.count < 4) return nullptr;

    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&geometry)) || !geometry) return nullptr;

    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink) {
        geometry->Release();
        return nullptr;
    }

    auto samePoint = [](D2D1_POINT_2F a, D2D1_POINT_2F b) {
        return std::abs(a.x - b.x) < 0.001f && std::abs(a.y - b.y) < 0.001f;
    };

    bool figureOpen = false;
    D2D1_POINT_2F figureStart = D2D1::Point2F(0, 0);
    D2D1_POINT_2F last = D2D1::Point2F(0, 0);

    for (int i = 0; i + 1 < asset.count; i += 2) {
        const float x = asset.points[i];
        const float y = asset.points[i + 1];

        if (x == -1.0f || x == -2.0f) {
            if (figureOpen) {
                sink->EndFigure(x == -2.0f ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
                figureOpen = false;
            }
            continue;
        }

        const D2D1_POINT_2F pt = D2D1::Point2F(x, y);
        if (!figureOpen) {
            sink->BeginFigure(pt, asset.filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
            figureStart = pt;
            last = pt;
            figureOpen = true;
        } else if (!samePoint(pt, last)) {
            sink->AddLine(pt);
            last = pt;
        }
    }

    if (figureOpen) {
        sink->EndFigure(samePoint(last, figureStart) ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    }

    sink->Close();
    sink->Release();
    m_iconGeometries[(size_t)id] = geometry;
    return geometry;
}

void D2DEngine::DrawIconAsset(int id, float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    ID2D1RenderTarget* target = rt ? rt : m_renderTarget;
    if (!target || !brush) return;

    ID2D1PathGeometry* geometry = GetIconGeometry(id);
    if (!geometry) return;

    const float scale = (size > 0.0f ? size : 16.0f) / 24.0f;

    D2D1_MATRIX_3X2_F previous = D2D1::Matrix3x2F::Identity();
    target->GetTransform(&previous);
    target->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(cx - 12.0f * scale, cy - 12.0f * scale));

    if (id >= 0 && id < WPICON_COUNT && kIconAssets[id].filled) {
        target->FillGeometry(geometry, brush);
    }
    target->DrawGeometry(geometry, brush, 1.9f, RoundStrokeStyle);
    target->SetTransform(previous);
}

// ---------------------------------------------------------------------------
// Semantic wrappers used by the UI. Every icon shares the same 24x24 grid,
// stroke weight and round caps, so mixed toolbars read as one family.
// ---------------------------------------------------------------------------

void D2DEngine::DrawIconSidebar(float cx, float cy, float size, bool isOpen, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(isOpen ? WPICON_PANEL_LEFT_CLOSE : WPICON_PANEL_LEFT_OPEN, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconChevronLeft(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_CHEVRON_LEFT, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconChevronRight(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_CHEVRON_RIGHT, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconZoomIn(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_ZOOM_IN, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconZoomOut(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_ZOOM_OUT, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconFit(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_EXPAND, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconRotateLeft(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_ROTATE_CCW, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconRotateRight(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_ROTATE_CW, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconPlay(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_PLAY, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconPause(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_PAUSE, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconVolume(float cx, float cy, float size, bool isMuted, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(isMuted ? WPICON_VOLUME_X : WPICON_VOLUME_2, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconFullscreen(float cx, float cy, float size, bool isFullscreen, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(isFullscreen ? WPICON_MINIMIZE : WPICON_MAXIMIZE, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconSkipBack(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_SKIP_BACK, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconSkipForward(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_SKIP_FORWARD, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconLoop(float cx, float cy, float size, bool /*active*/, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_REPEAT, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconMusic(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_MUSIC, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconFilePdf(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_FILE_TEXT, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconFileImage(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_IMAGE, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconFileVideo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_FILM, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconSearch(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_SEARCH, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconCrop(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_CROP, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconInfo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_INFO, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconOutline(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_LIST, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconCopy(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_COPY, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconWrap(float cx, float cy, float size, bool /*isWrapped*/, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_TEXT_WRAP, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconTable(float cx, float cy, float size, bool isTable, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(isTable ? WPICON_TABLE_2 : WPICON_CODE, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconSave(float cx, float cy, float size, bool /*isDirty*/, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_SAVE, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconUndo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_UNDO_2, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconRedo(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_REDO_2, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconCut(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_SCISSORS, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconPaste(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_CLIPBOARD, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconSettings(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_SETTINGS, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconEye(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_EYE, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconFileDocument(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_FILE_TEXT, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconExternalLink(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_EXTERNAL_LINK, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconHamburger(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_ELLIPSIS, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconFolder(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_FOLDER, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconSun(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_SUN, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconClose(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_X, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconSliders(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_SLIDERS_HORIZONTAL, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconKeyboard(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_KEYBOARD, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconMonitor(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_MONITOR, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconCheck(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_CHECK, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconChevronDown(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_CHEVRON_DOWN, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconChevronUp(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_CHEVRON_UP, cx, cy, size, brush, rt);
}
void D2DEngine::DrawIconHelp(float cx, float cy, float size, ID2D1Brush* brush, ID2D1RenderTarget* rt) {
    DrawIconAsset(WPICON_CIRCLE_HELP, cx, cy, size, brush, rt);
}

} // namespace WinPreview
