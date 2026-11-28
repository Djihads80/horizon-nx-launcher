#include "ui.hpp"

#include "util.hpp"

#include <json.hpp>
#include <switch.h>

namespace ui
{

using namespace launcher;

void applyTheme(const std::string& name)
{
    brls::ThemeVariant variant;
    if (name == "dark")
        variant = brls::ThemeVariant::DARK;
    else if (name == "light")
        variant = brls::ThemeVariant::LIGHT;
    else
        return; // the system's own choice stays

    brls::Application::setThemeVariant(variant);
}

void alert(const std::string& message, const std::string& button, std::function<void()> after)
{
    brls::Dialog* dialog = new brls::Dialog(message);
    dialog->addButton(button, [dialog, after](brls::View*) {
        dialog->close();
        if (after)
            after();
    });
    dialog->open();
}

void confirm(const std::string& message, std::function<void()> onYes, const std::string& yes,
    const std::string& no)
{
    brls::Dialog* dialog = new brls::Dialog(message);
    dialog->addButton(yes, [dialog, onYes](brls::View*) {
        dialog->close();
        onYes();
    });
    dialog->addButton(no, [dialog](brls::View*) { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}

brls::ListItem* item(const std::string& label, const std::string& description,
    std::function<void()> onClick, const std::string& value)
{
    brls::ListItem* listItem = new brls::ListItem(label, description);
    if (!value.empty())
        listItem->setValue(value);
    if (onClick)
        listItem->getClickEvent()->subscribe([onClick](brls::View*) { onClick(); });
    return listItem;
}

brls::Label* text(const std::string& value, bool multiline, unsigned fontSize, NVGalign align)
{
    brls::Label* label = new brls::Label(brls::LabelStyle::REGULAR, value, multiline);
    label->setHorizontalAlign(align);
    if (fontSize > 0)
        label->setFontSize(fontSize);
    return label;
}

void ask(const std::string& header, const std::string& help, const std::string& initial,
    int maxLength, std::function<void(std::string)> done)
{
    brls::Swkbd::openForText(
        [done](std::string value) {
            if (done)
                done(value);
        },
        header, help, maxLength, initial);
}

std::string iconPath(const launcher::Instance& instance)
{
    if (instance.icon.empty())
        return "";

    // A picture from the SD card, or one of the launcher's own.
    if (instance.icon.find('/') != std::string::npos || instance.icon.find(':') != std::string::npos)
        return exists(instance.icon) ? instance.icon : "";

    return launcher::asset("img/icons/" + instance.icon + ".png");
}

std::string accountName()
{
    const launcher::Config& config = launcher::Config::get();
    for (const launcher::Account& account : config.accounts)
        if (account.uuid == config.account)
            return account.name;
    return "";
}

std::string accountSubtitle()
{
    return launcher::Config::get().account.empty() ? "Not signed in (Minus)" : "Microsoft Account";
}

void initStyle()
{
    brls::Style* style = brls::Application::getStyle();

    // A row's picture is as tall as the row minus this padding on both sides. The library's default
    // (11) makes it tall enough to spill over the row's rounded highlight; 24 keeps it inside.
    style->List.Item.thumbnailPadding = 24;

    // The library leaves 61 pixels between list rows, which reads as a list with gaps in it. Rows of
    // their own (the pickers, the instances) are drawn edge to edge, so they sit closer.
    style->List.spacing = 12;
}

std::string playtimeText(uint64_t seconds)
{
    if (seconds < 60)
        return "not played yet";

    const uint64_t minutes = seconds / 60;
    if (minutes < 60)
        return std::to_string(minutes) + "min played";

    const uint64_t hours = minutes / 60;
    const uint64_t rest  = minutes % 60;
    if (hours < 1000)
        return std::to_string(hours) + "h " + (rest < 10 ? "0" : "") + std::to_string(rest) +
            "min played";

    return std::to_string(hours) + "h played";
}

namespace
{

std::string skinPath()
{
    const launcher::Config& config = launcher::Config::get();
    if (config.account.empty())
        return "";
    return launcher::accountsDir() + "/" + config.account + "-skin.png";
}

} // namespace

void fetchSkin()
{
    const std::string file = skinPath();
    if (file.empty() || launcher::exists(file))
        return;

    const std::string uuid = launcher::Config::get().account;
    launcher::Worker::get().post(
        [uuid, file] {
            // Mojang names the skin in the profile's textures property: base64 JSON.
            launcher::HttpResult profile = launcher::httpRequest("GET",
                "https://sessionserver.mojang.com/session/minecraft/profile/" + uuid, {}, "",
                { "Accept: application/json" });
            if (!profile.ok())
            {
                log("skin: profile %ld", profile.status);
                return;
            }

            nlohmann::json value = nlohmann::json::parse(profile.body, nullptr, false);
            if (value.is_discarded() || !value.contains("properties"))
                return;

            std::string textures;
            for (const auto& property : value["properties"])
                if (property.value("name", "") == "textures")
                    textures = launcher::base64Decode(property.value("value", ""));

            nlohmann::json decoded = nlohmann::json::parse(textures, nullptr, false);
            if (decoded.is_discarded())
                return;

            std::string url = decoded["textures"]["SKIN"]["url"].get<std::string>();
            if (url.empty())
                return;

            std::string error;
            if (!launcher::httpDownload(url, file, {}, error))
                log("skin: %s", error.c_str());
        },
        [] {});
}

int headTexture(NVGcontext* vg)
{
    static int image = 0;
    static std::string loadedFor;

    const std::string file = skinPath();
    if (file.empty())
        return 0;
    if (image != 0 && loadedFor == file)
        return image;

    if (image != 0)
    {
        nvgDeleteImage(vg, image);
        image = 0;
    }
    if (!launcher::exists(file))
        return 0;

    image     = nvgCreateImage(vg, file.c_str(), NVG_IMAGE_NEAREST);
    loadedFor = file;
    return image;
}

bool launchGame(const launcher::Instance& instance, std::string& error)
{
    auto& config = launcher::Config::get();

    if (!exists(launcher::gamePath()))
    {
        error = "There is no horizon.nro in " + launcher::root() +
            ". Put the game next to the launcher, or install it from a release.";
        return false;
    }

    if (!exists(launcher::path("resources/horizon-resources.json")))
        log("launch: no prepared game files in %s/resources", launcher::root().c_str());

    // The game reads its account from the folder it runs in.
    const std::string gameDir = launcher::gameDirOf(instance.id);
    makeDirs(gameDir);
    if (!config.account.empty())
    {
        std::string saved;
        if (readFile(config.accountFile(config.account), saved))
            writeFileAtomic(gameDir + "/account.json", saved);
    }

    const std::string line = config.commandLine(instance);
    log("launch: path=%s", launcher::gamePath().c_str());
    log("launch: argv=%s", line.c_str());

    if (!envHasNextLoad())
    {
        error = "This console can't start another app from here. Start the launcher from the "
                "homebrew menu as an application (hold R while starting a game), then try again.";
        return false;
    }

    Result rc = envSetNextLoad(launcher::gamePath().c_str(), line.c_str());
    if (R_FAILED(rc))
    {
        char buffer[160];
        ::snprintf(buffer, sizeof(buffer),
            "The homebrew loader refused to start the game (0x%08X). Start the launcher from the "
            "homebrew menu, holding R while starting a game.",
            (unsigned)rc);
        error = buffer;
        return false;
    }

    // The loader starts the game once this app has ended.
    brls::Application::quit();
    return true;
}

} // namespace ui
