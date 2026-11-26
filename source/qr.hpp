//! A QR code drawn straight into the app: the sign-in screen shows one so a phone can open the
//! page without the player typing the address.
#pragma once

#include <string>

#include <borealis.hpp>

namespace launcher
{

class QRView : public brls::View
{
  public:
    explicit QRView(const std::string& payload);
    ~QRView() override;

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style,
        brls::FrameContext* ctx) override;

    /// The address the code encodes.
    void setPayload(const std::string& payload);

  private:
    struct Code;
    Code* code = nullptr;
};

} // namespace launcher
