#include "qr.hpp"

#include <algorithm>

#include <qrcodegen.hpp>

namespace launcher
{

struct QRView::Code
{
    qrcodegen::QrCode qr;
    explicit Code(const std::string& payload)
        : qr(qrcodegen::QrCode::encodeText(payload.c_str(), qrcodegen::QrCode::Ecc::MEDIUM))
    {
    }
};

QRView::QRView(const std::string& payload)
{
    this->code = new Code(payload);
    this->setHeight(320);
}

QRView::~QRView() { delete this->code; }

void QRView::setPayload(const std::string& payload)
{
    delete this->code;
    this->code = new Code(payload);
    this->invalidate(true);
}

void QRView::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style,
    brls::FrameContext* ctx)
{
    if (!this->code)
        return;

    const int size = (int)std::min<unsigned>(width, height);
    if (size <= 0)
        return;

    const int left = x + ((int)width - size) / 2;
    const int top  = y + ((int)height - size) / 2;

    // The quiet zone the standard asks for, then the modules: black on white, whichever the
    // console's theme is, so a phone can read it.
    const float margin = size * 0.06f;
    nvgBeginPath(vg);
    nvgRect(vg, (float)left, (float)top, (float)size, (float)size);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgFill(vg);

    const int modules = this->code->qr.getSize();
    const float module = (float)(size - 2 * margin) / modules;

    nvgBeginPath(vg);
    for (int my = 0; my < modules; my++)
        for (int mx = 0; mx < modules; mx++)
            if (this->code->qr.getModule(mx, my))
                nvgRect(vg, (float)left + margin + mx * module, (float)top + margin + my * module,
                    module + 0.5f, module + 0.5f);
    nvgFillColor(vg, nvgRGB(0, 0, 0));
    nvgFill(vg);
}

} // namespace launcher
