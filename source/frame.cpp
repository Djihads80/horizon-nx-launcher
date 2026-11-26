#include "pages.hpp"

#include "config.hpp"
#include "ui.hpp"

namespace launcher
{

void LauncherFrame::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height,
    brls::Style* style, brls::FrameContext* ctx)
{
    // The library paints the app's own icon and title, and the line under them.
    brls::TabFrame::draw(vg, x, y, width, height, style, ctx);

    const float headerHeight = style->AppletFrame.headerHeightRegular;
    const float edge         = style->AppletFrame.separatorSpacing;
    const float right        = (float)x + (float)width - edge;

    const std::string name     = ui::accountName();
    const std::string subtitle = ui::accountSubtitle();

    const float headSize = 56.0f;
    const float headX    = right - headSize;
    const float headY    = (float)y + (headerHeight - headSize) / 2.0f;

    const int image = ui::headTexture(vg);
    if (image != 0)
    {
        int imageWidth = 0, imageHeight = 0;
        nvgImageSize(vg, image, &imageWidth, &imageHeight);
        if (imageWidth > 0 && imageHeight > 0)
        {
            // A skin is a 64x64 sheet whose face is the 8x8 square at (8,8), with the hat over it at
            // (40,8). One image pattern per layer, clipped to the head, draws both.
            const float scale = headSize / 8.0f;
            for (int layer = 0; layer < 2; layer++)
            {
                const float sourceX = layer == 0 ? 8.0f : 40.0f;
                nvgSave(vg);
                nvgScissor(vg, headX, headY, headSize, headSize);
                nvgBeginPath(vg);
                nvgRect(vg, headX, headY, headSize, headSize);
                nvgFillPaint(vg,
                    nvgImagePattern(vg, headX - sourceX * scale, headY - 8.0f * scale,
                        (float)imageWidth * scale, (float)imageHeight * scale, 0.0f, image, 1.0f));
                nvgFill(vg);
                nvgRestore(vg);
            }
        }
    }
    else
    {
        // No skin on the card yet: a square tile with the name's first letter, which the skin
        // replaces once it arrives.
        nvgBeginPath(vg);
        nvgRect(vg, headX, headY, headSize, headSize);
        nvgFillColor(vg, nvgRGBA(0, 255, 204, 46));
        nvgFill(vg);

        if (!name.empty())
        {
            nvgFontFaceId(vg, ctx->fontStash->regular);
            nvgFontSize(vg, 26.0f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, nvgRGBA(0, 255, 204, 200));
            nvgText(vg, headX + headSize / 2, headY + headSize / 2, name.substr(0, 1).c_str(),
                nullptr);
        }
    }

    // The name and, under it, what the sign-in is.
    nvgFontFaceId(vg, ctx->fontStash->regular);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

    nvgFontSize(vg, 26.0f);
    nvgFillColor(vg, ctx->theme->textColor);
    nvgText(vg, headX - 14.0f, (float)y + headerHeight * 0.36f, name.c_str(), nullptr);

    nvgFontSize(vg, 19.0f);
    nvgFillColor(vg, ctx->theme->descriptionColor);
    nvgText(vg, headX - 14.0f, (float)y + headerHeight * 0.66f, subtitle.c_str(), nullptr);
}

} // namespace launcher
