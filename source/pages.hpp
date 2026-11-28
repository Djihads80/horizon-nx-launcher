//! The four tabs, the frame around them (app icon, title, the account), and the screens they open.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "auth.hpp"
#include "qr.hpp"
#include "registry.hpp"
#include "util.hpp"

namespace launcher
{

/// The frame every tab lives in: the app's icon and name on the left, the signed-in account on the
/// right. Borealis draws the left half itself (the title and its separator); the account is painted
/// here, in the same header, because the library has no slot for it.
class LauncherFrame : public brls::TabFrame
{
  public:
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style,
        brls::FrameContext* ctx) override;
};

/// Play: the instance the launcher would start, the Play button, and the arguments every instance
/// gets. Instances themselves live on their own tab.
class PlayPage : public brls::List
{
  public:
    PlayPage();
    ~PlayPage() override;
    void rebuild();
    /// Brings the rows up to date without replacing them: a rebuild deletes the row that has focus,
    /// and a page whose focused row has been deleted answers nothing.
    void refresh();

    /// The download runs on the worker; this is called every frame, so the Play button can say how
    /// far it has got without the page being rebuilt under the progress.
    void frame(brls::FrameContext* ctx) override;

  private:
    void play(const std::string& instanceId);
    /// Fetches the game, then plays it: what Play does when there is no horizon.nro yet.
    void install(const std::string& instanceId);
    brls::View* pickerRow    = nullptr;
    brls::Button* playButton = nullptr;
    /// Whether the Play button is currently showing the download's progress instead of its name.
    bool buttonIsProgress = false;
    void askArguments();

    AlivePtr alive = std::make_shared<Alive>();
};

/// Instances: make one, change it (name, picture, own arguments), pick it, delete it.
class InstancesPage : public brls::List
{
  public:
    InstancesPage();
    ~InstancesPage() override;
    void rebuild();
    void refresh();

  private:
    void create();
    void edit(const std::string& instanceId);
    void focusFirst();
    void remove(const std::string& instanceId);
    void select(const std::string& instanceId);

    /// One row per instance, kept between refreshes so a refresh never deletes what has focus.
    std::vector<brls::View*> rows;

    AlivePtr alive = std::make_shared<Alive>();
};

/// Mods: the browser, five kinds, installs what a console can run.
class ModsPage : public brls::List
{
  public:
    ModsPage();
    ~ModsPage() override;
    void rebuild();

  private:
    void runSearch(const std::string& words);
    void askSearch();
    void install(size_t index);
    void pickWorld();
    /// Focus the first row: a rebuild replaces them, and a screen whose focused row was deleted
    /// answers nothing.
    void focusFirst();

    Kind kind = Kind::ResourcePack;
    std::string status = "Pick a kind, then Search.";
    std::string world; // where data packs go
    std::vector<Hit> results;
    AlivePtr alive = std::make_shared<Alive>();
};

/// Settings: accounts, appearance, and what the launcher does.
class SettingsPage : public brls::List
{
  public:
    SettingsPage();
    ~SettingsPage() override;
    void rebuild();

  private:
    void switchAccount(int index);
    void signOut();
    void focusFirst();

    AlivePtr alive = std::make_shared<Alive>();
};

/// The Microsoft sign-in screen: a QR code, the code to type, and what happens next. A box, not a
/// list: nothing may scroll the QR out of reach, and the whole thing fits one screen.
class SignInPage : public brls::BoxLayout
{
  public:
    SignInPage();
    ~SignInPage() override;

    /// The screen on the stack, or nullptr: one poller serves it, started once.
    static SignInPage* live();
    void refresh();

  private:
    std::shared_ptr<auth::State> state;
    brls::Label* status = nullptr;
    brls::Label* code = nullptr;
    QRView* qr = nullptr;

    std::string shown; // what the QR encodes
    bool reported = false;
};

/// Opens the sign-in screen (the Minus button, or Settings).
void pushSignIn();

/// Redraws the Play and Instances tabs after something they show changed (a picture, a name, the
/// instance Play starts). They are built once and kept, so they don't notice on their own.
void refreshPages();

/// Binds Minus to the sign-in screen on a view that was just pushed. It has to happen *after*
/// pushView: the library binds Minus itself there, and a view keeps one action per key, so a binding
/// made before the push is replaced by its frame-rate toggle.
void bindSignIn(brls::View* view);

/// Gives a pushed screen a way back: B pops it. The library's own Back comes from AppletFrame, which
/// the tabs are and a pushed list is not — without this, a pushed screen can only sign in or exit.
void bindBack(brls::View* view);

#ifdef HORIZON_DESKTOP
/// Opens a page by name (instances|picture): a development aid for the desktop build, where no
/// controller drives the UI.
void openScreenForTest(const std::string& name);

/// Runs the game download Play runs, for the same reason: HORIZON_INSTALL=1.
void installGameForTest(std::function<void(bool, std::string)> done);
#endif

} // namespace launcher
