//! Horizon launcher for the Nintendo Switch: a homebrew app that picks an instance, signs in to
//! Microsoft, browses mods, and starts `horizon.nro` with the instance's own game folder.
//!
//! The launcher never touches the game's code: it is a separate app, built on Borealis, that hands
//! over to the game through libnx's next-load (what the homebrew menu does), so the game runs with
//! the arguments the launcher chose.
//!
//! Every step below writes a line to `switch/horizon/launcher-log.txt` before it runs: a console has
//! no terminal, so that file is the only way to know where a start stopped.
#include <cstdio>
#include <cstdlib>
#include <exception>

#include <borealis.hpp>
#include <curl/curl.h>
#include <switch.h>
#include "config.hpp"
#include "pages.hpp"
#include "ui.hpp"
#include "util.hpp"

using namespace launcher;

/// An uncaught C++ exception ends the app with this: write it down first, so the log says what it
/// was instead of leaving a crash screen with only addresses.
static void onTerminate()
{
    log("launcher: std::terminate called");
    if (std::exception_ptr error = std::current_exception())
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception& e)
        {
            log("launcher: uncaught exception: %s", e.what());
        }
        catch (...)
        {
            log("launcher: uncaught exception of an unknown type");
        }
    }
    ::abort();
}

int main(int argc, char** argv)
{
    std::set_terminate(onTerminate);

    ::printf("Horizon Launcher: starting\n");
    log("launcher: starting (%d arguments)", argc);

    // romfs holds the fonts and the launcher's pictures; the network is for the browser and
    // sign-in, so both start before the UI reads anything.
    Result romfs = romfsInit();
    log("launcher: romfsInit 0x%08X", (unsigned)romfs);
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Applet mode (started from the Album) has a fraction of an application's memory: the game
    // can't be started from it, and the UI library may not get its framebuffers. Say so in the log
    // and stop, rather than fault part-way through drawing the first frame.
    // An emulator reports the launcher as a library applet even though it has an application's
    // memory, so the refusal can be lifted for it: HORIZON_ALLOW_APPLET=1.
    const bool forcedApplet = std::getenv("HORIZON_ALLOW_APPLET") != nullptr;
    const AppletType type    = appletGetAppletType();
    if (type != AppletType_Application && type != AppletType_SystemApplication && !forcedApplet)
    {
        log("launcher: started as an applet (%d). Hold R while starting any game to open the "
            "homebrew menu as an application, then choose Horizon Launcher: applet mode has too "
            "little memory for this and cannot start the game.",
            (int)type);
        return 3;
    }
    log("launcher: running as an application");

    log("launcher: reading %s", configPath().c_str());
    Config& config = Config::get();
    config.load();
    log("launcher: %zu instance(s), %zu account(s)", config.instances.size(),
        config.accounts.size());

    log("launcher: starting the UI library");
    if (!brls::Application::init("Horizon Launcher"))
    {
        log("launcher: the UI library could not start");
        return 1;
    }

    // HORIZON_DEBUG makes the UI library say where focus goes: a screen that stops answering is a
    // screen whose focus went somewhere that no longer exists, and that line names it.
    brls::Logger::setLogLevel(std::getenv("HORIZON_DEBUG") ? brls::LogLevel::DEBUG
                                                           : brls::LogLevel::INFO);
    brls::i18n::loadTranslations();
    ui::applyTheme(config.theme);
    ui::initStyle();
    brls::Application::setDisplayFramerate(config.showFps);
    log("launcher: UI up");

    // The worker drains its finished jobs on the UI thread every frame.
    Worker::get().start();
    log("launcher: worker started");

    brls::TabFrame* tabs = new LauncherFrame();
    tabs->setTitle("Horizon");
    tabs->setIcon(asset("img/launcher.png"));

    log("launcher: play page");
    tabs->addTab("Play", new PlayPage());
    log("launcher: instances page");
    tabs->addTab("Instances", new InstancesPage());
    log("launcher: mods page");
    tabs->addTab("Mods", new ModsPage());
    log("launcher: settings page");
    tabs->addTab("Settings", new SettingsPage());

    // The player's own head for the header: fetched once, in the background.
    ui::fetchSkin();

    // After the push: pushView binds Minus to its frame-rate counter on the view it pushes, and a
    // view keeps one action per key, so a binding made earlier is silently replaced.
    log("launcher: first frame");
    brls::Application::pushView(tabs);
    bindSignIn(tabs);

#ifdef HORIZON_DESKTOP
    // The desktop build has no controller wired to it: a screen three presses deep is opened at once
    // so it can be looked at. Nothing on a console reads this.
    if (const char* screen = std::getenv("HORIZON_SCREEN"))
        openScreenForTest(screen);

    if (std::getenv("HORIZON_INSTALL"))
        installGameForTest([](bool ok, std::string error) {
            log("test: install %s", ok ? "ok" : error.c_str());
        });
#endif

    log("launcher: running");

    while (brls::Application::mainLoop())
        ;

    log("launcher: closing");
    // Everything this app opened, closed: a service left open at exit is what turns "closed" into an
    // error dialog on the way out.
    curl_global_cleanup();
    romfsExit();
    socketExit();
    return 0;
}
