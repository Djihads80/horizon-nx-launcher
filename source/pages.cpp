#include "pages.hpp"

#include "config.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <algorithm>

namespace launcher
{

namespace
{

const char* const ICONS[] = { "grass", "stone", "diamond", "heart", "pickaxe", "star" };

bool endsWith(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() &&
        text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string ellipsis(const std::string& text, size_t limit)
{
    if (text.size() <= limit)
        return text;
    return text.substr(0, limit - 1) + "…";
}

/// A list row that opens the keyboard and reports what was typed as it changes: InputListItem
/// alone only updates its own value, and nothing learns about it.
class Field : public brls::InputListItem
{
  public:
    Field(const std::string& label, const std::string& value, const std::string& help,
        const std::string& description, int maxLength, std::function<void(std::string)> changed)
        : brls::InputListItem(label, value, help, description, maxLength)
        , changed(std::move(changed))
    {
        this->setValue(value, false, false);
    }

    bool onClick() override
    {
        brls::Swkbd::openForText(
            [this](std::string text) {
                this->setValue(text, false, false);
                if (this->changed)
                    this->changed(text);
            },
            this->helpText, "", this->maxInputLength, this->getValue());
        return true;
    }

  private:
    std::function<void(std::string)> changed;
};

/// A row for one instance: its picture, name and time played. Drawn here rather than as a list item
/// because a list item sizes its picture from the row height (so it sits off-centre and can spill
/// past the focus ring) and draws its description outside that ring.
class InstanceRow : public brls::View
{
  public:
    InstanceRow(const Instance& instance, std::string value, std::function<void()> onClick)
        : iconFile(ui::iconPath(instance))
        , name(instance.name)
        , playtime(ui::playtimeText(instance.played))
        , value(std::move(value))
        , onClick(std::move(onClick))
    {
        this->setHeight(96);
        this->registerAction("OK", brls::Key::A, [this] {
            if (this->onClick)
                this->onClick();
            return true;
        });
    }

    View* getDefaultFocus() override { return this; }

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style,
        brls::FrameContext* ctx) override
    {
        const int iconSize = 56;
        const int iconX    = x + 16;
        // Exactly centred in the row, whatever the row's height is.
        const int iconY = y + ((int)height - iconSize) / 2;

        const int image = this->texture(vg);
        nvgBeginPath(vg);
        nvgRect(vg, (float)iconX, (float)iconY, (float)iconSize, (float)iconSize);
        if (image != 0)
            nvgFillPaint(vg,
                nvgImagePattern(vg, (float)iconX, (float)iconY, (float)iconSize, (float)iconSize,
                    0.0f, image, 1.0f));
        else
            nvgFillColor(vg, nvgRGBA(0, 255, 204, 46));
        nvgFill(vg);

        const float textX = (float)(iconX + iconSize + 18);
        nvgFontFaceId(vg, ctx->fontStash->regular);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        nvgFontSize(vg, 26.0f);
        nvgFillColor(vg, ctx->theme->textColor);
        nvgText(vg, textX, (float)y + height * 0.38f, this->name.c_str(), nullptr);

        nvgFontSize(vg, 19.0f);
        nvgFillColor(vg, ctx->theme->descriptionColor);
        nvgText(vg, textX, (float)y + height * 0.68f, this->playtime.c_str(), nullptr);

        if (!this->value.empty())
        {
            nvgFontSize(vg, 20.0f);
            nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, ctx->theme->listItemValueColor);
            nvgText(vg, (float)(x + width) - 24.0f, (float)y + height / 2.0f, this->value.c_str(),
                nullptr);
        }
    }

  private:
    int texture(NVGcontext* vg)
    {
        if (this->iconFile.empty())
            return 0;
        if (this->image != 0 && this->forFile == this->iconFile)
            return this->image;

        if (this->image != 0)
        {
            nvgDeleteImage(vg, this->image);
            this->image = 0;
        }

        this->image   = nvgCreateImage(vg, this->iconFile.c_str(), NVG_IMAGE_NEAREST);
        this->forFile = this->iconFile;
        return this->image;
    }

    std::string iconFile, name, playtime, value;
    std::function<void()> onClick;
    int image = 0;
    std::string forFile;
};


} // namespace

// The tabs are built once and kept alive, so a change made on one is invisible on the other until
// they are told: these are the two that show instance data.
namespace
{
PlayPage* livePlay           = nullptr;
InstancesPage* liveInstances = nullptr;
} // namespace

void refreshPages()
{
    if (livePlay)
        livePlay->rebuild();
    if (liveInstances)
        liveInstances->rebuild();
}

// ----- Play ----------------------------------------------------------------------------------

PlayPage::PlayPage()
{
    this->setMargins(24, 24, 24, 24);
    livePlay = this;
    this->rebuild();
}

PlayPage::~PlayPage()
{
    if (livePlay == this)
        livePlay = nullptr;
    this->alive->flag = false;
}

void PlayPage::rebuild()
{
    log("page: play");
    this->clear();

    auto& config = Config::get();
    Instance* instance = config.current();

    this->addView(new brls::Header("Play"));
    if (!instance)
    {
        this->addView(ui::text("There is no instance yet. Make one on the Instances tab."));
        return;
    }

    const std::string id = instance->id;

    // The instance this launcher would start: its picture, its name, how long it has been played.
    // Picking the row picks another; changing them is the Instances tab's job.
    std::vector<std::string> names;
    unsigned selected = 0;
    for (size_t i = 0; i < config.instances.size(); i++)
    {
        names.push_back(config.instances[i].name);
        if (config.instances[i].id == id)
            selected = (unsigned)i;
    }

    brls::View* picker = new InstanceRow(*instance,
        config.instances.size() > 1 ? "Change" : "",
        [this, names, selected] {
            brls::Dropdown::open(
                "Instance", names,
                [this](int index) {
                    auto& config = Config::get();
                    if (index < 0 || index >= (int)config.instances.size())
                        return;
                    config.selected = config.instances[index].id;
                    config.save();
                    this->rebuild();
                },
                selected);
        });
    this->addView(picker);

    // Play.
    brls::Button* play = new brls::Button(brls::ButtonStyle::PRIMARY);
    play->setLabel("Play");
    // A button in a list keeps no height of its own: without this it is a line.
    play->setHeight(96);
    play->getClickEvent()->subscribe([this, id](brls::View*) { this->play(id); });
    this->addView(play);

    // The arguments every instance gets on top of its own.
    const std::string arguments = trim(config.globalArgs);
    this->addView(ui::item("Java Arguments",
        arguments.empty() ? "None. What is typed here is added to the game's own command line."
                          : arguments,
        [this] { this->askArguments(); }));
}

void PlayPage::play(const std::string& instanceId)
{
    auto& config = Config::get();
    Instance* instance = config.find(instanceId);
    if (!instance)
        return;

    // Written down before the hand-over: the launcher isn't running while the game is, so this is
    // the only way its playtime gets counted.
    config.beginSession(instanceId);

    std::string error;
    if (!ui::launchGame(*instance, error))
    {
        config.endSession();
        ui::alert(error);
    }
}

void PlayPage::askArguments()
{
    auto& config = Config::get();
    ui::ask("Java Arguments", "Added to the game's command line", config.globalArgs, 256,
        [this](std::string text) {
            auto& config = Config::get();
            config.globalArgs = trim(text);
            config.save();
            this->rebuild();
        });
}

// ----- Instances -----------------------------------------------------------------------------

InstancesPage::InstancesPage()
{
    this->setMargins(24, 24, 24, 24);
    liveInstances = this;
    this->rebuild();
}

InstancesPage::~InstancesPage()
{
    if (liveInstances == this)
        liveInstances = nullptr;
    this->alive->flag = false;
}

void InstancesPage::rebuild()
{
    log("page: instances");
    this->clear();

    auto& config = Config::get();

    this->addView(new brls::Header("Instances"));
    this->addView(ui::text(
        "Each instance keeps its own worlds, resource packs, shader packs, mods and arguments. The "
        "one marked Selected is what Play starts.",
        true, 18));

    for (const Instance& instance : config.instances)
    {
        const std::string id = instance.id;
        brls::View* row = new InstanceRow(instance,
            instance.id == config.selected ? "Selected" : "", [this, id] { this->edit(id); });
        this->addView(row);
    }

    this->addView(ui::item("New instance…", "Another way to play, with its own folder.",
        [this] { this->create(); }));
}

void InstancesPage::create()
{
    ui::ask("New instance", "Its name", "", 48, [this](std::string name) {
        if (trim(name).empty())
            return;

        auto& config = Config::get();
        const std::string id = config.add(trim(name));
        config.selected = id;
        config.save();
        this->rebuild();
        brls::Application::notify("Instance created");
        refreshPages();
    });
}

void InstancesPage::select(const std::string& instanceId)
{
    auto& config = Config::get();
    config.selected = instanceId;
    config.save();
    refreshPages();
    brls::Application::notify("Play starts " + config.find(instanceId)->name);
}

void InstancesPage::edit(const std::string& instanceId)
{
    auto& config = Config::get();
    Instance* instance = config.find(instanceId);
    if (!instance)
        return;

    // The edit screen: its own list, pushed over the tabs.
    class Edit : public brls::List
    {
      public:
        explicit Edit(const std::string& id)
            : id(id)
        {
            auto& config = Config::get();
            Instance* instance = config.find(id);
            if (!instance)
                return;

            this->setMargins(24, 24, 24, 24);
            this->addView(new brls::Header(instance->name));

            name = new Field("Name", instance->name, "What this instance is called",
                "Shown here and on the Play tab.", 48, {});
            this->addView(name);

            // The picture: the launcher's own, one from the SD card's icons folder, or none.
            std::vector<std::string> labels = { "None" };
            std::vector<std::string> values = { "" };
            for (const char* icon : ICONS)
            {
                std::string label = icon;
                if (!label.empty())
                    label[0] = (char)std::toupper((unsigned char)label[0]);
                labels.push_back(label);
                values.push_back(icon);
            }
            for (const std::string& file : listDir(path("icons")))
                if (endsWith(file, ".png") || endsWith(file, ".jpg"))
                {
                    labels.push_back(ellipsis(file, 24));
                    values.push_back(path("icons") + "/" + file);
                }

            unsigned selected = 0;
            for (size_t i = 0; i < values.size(); i++)
                if (values[i] == instance->icon)
                    selected = (unsigned)i;

            iconValues = values;
            iconChoice = selected;

            auto* picture = new brls::SelectListItem("Picture", labels, selected,
                "A picture on the SD card goes in sdmc:/switch/horizon/icons/.");
            picture->getValueSelectedEvent()->subscribe([this](int index) {
                if (index >= 0)
                    this->iconChoice = (size_t)index;
            });
            this->addView(picture);

            arguments = new Field("These arguments", instance->args, "This instance's arguments",
                "Added after the Java Arguments on the Play tab.", 256, {});
            this->addView(arguments);

            this->addView(ui::text(ui::playtimeText(instance->played) + " · " +
                    gameDirOf(instance->id),
                true, 16));

            if (instance->id != config.selected)
                this->addView(ui::item("Use this instance", "Play starts it from now on.",
                    [this, id] {
                        this->save(id);
                        Config::get().selected = id;
                        Config::get().save();
                        brls::Application::notify("That is the instance Play starts");
                        brls::Application::popView();
                        refreshPages();
                    }));

            this->addView(ui::item("Done", "Keep these settings.", [this, id] { this->save(id); }));

            if (config.instances.size() > 1)
                this->addView(ui::item("Delete this instance",
                    "Removes its worlds, packs and mods too.", [this, id] {
                        brls::Application::popView();
                        if (InstancesPage* page = live())
                            page->remove(id);
                    }));
        }

        void save(const std::string& instanceId)
        {
            auto& config = Config::get();
            Instance* instance = config.find(instanceId);
            if (!instance)
                return;

            if (name && !trim(name->getValue()).empty())
                instance->name = trim(name->getValue());
            if (arguments)
                instance->args = trim(arguments->getValue());
            if (iconChoice < iconValues.size())
                instance->icon = iconValues[iconChoice];

            config.save();
            brls::Application::notify("Saved");
            brls::Application::popView();
            refreshPages();
        }

        static InstancesPage*& live()
        {
            static InstancesPage* page = nullptr;
            return page;
        }

        std::string id;
        std::vector<std::string> iconValues;
        size_t iconChoice = 0;
        Field* name = nullptr;
        Field* arguments = nullptr;
    };

    Edit::live() = this;
    Edit* edit = new Edit(instanceId);
    brls::Application::pushView(edit);
    bindSignIn(edit);
    bindBack(edit);
}

void InstancesPage::remove(const std::string& instanceId)
{
    auto& config = Config::get();
    Instance* instance = config.find(instanceId);
    if (!instance)
        return;

    const std::string name   = instance->name;
    const std::string folder = gameDirOf(instanceId);

    ui::confirm("Delete \"" + name + "\"?\n\n" + folder + " and everything in it, worlds included.",
        [this, instanceId, name] {
            ui::confirm("Really delete \"" + name + "\"? This can't be undone.",
                [this, instanceId] {
                    if (Config::get().remove(instanceId))
                    {
                        this->rebuild();
                        refreshPages();
                        brls::Application::notify("Deleted");
                    }
                    else
                        ui::alert("The last instance can't be deleted.");
                },
                "Delete");
        },
        "Continue");
}

// ----- Mods ----------------------------------------------------------------------------------

ModsPage::ModsPage()
{
    this->setMargins(24, 24, 24, 24);
    this->rebuild();
}

ModsPage::~ModsPage() { this->alive->flag = false; }

void ModsPage::rebuild()
{
    log("page: mods");
    this->clear();
    auto& config = Config::get();
    Instance* instance = config.current();

    this->addView(new brls::Header("Mods"));
    this->addView(ui::text(
        "Horizon NX runs resource packs and data packs. Java mods, modpacks and Iris shaders need "
        "Horizon's own port or format, so the browser says before installing whether one runs.",
        true, 18));

    // The kind.
    std::vector<std::string> labels;
    const std::vector<Kind>& kinds = allKinds();
    unsigned selected = 0;
    for (size_t i = 0; i < kinds.size(); i++)
    {
        labels.push_back(kindLabel(kinds[i]));
        if (kinds[i] == kind)
            selected = (unsigned)i;
    }

    auto* picker = new brls::SelectListItem("Kind", labels, selected,
        "Modrinth, the same source the desktop launcher's browser uses.");
    picker->getValueSelectedEvent()->subscribe([this](int index) {
        if (index < 0)
            return;
        this->kind = allKinds()[(size_t)index];
        this->results.clear();
        this->status = "Pick a kind, then Search.";
        this->rebuild();
    });
    this->addView(picker);

    // Data packs go into one of the instance's worlds.
    if (instance && kind == Kind::DataPack)
    {
        const std::vector<std::string> names = worlds(gameDirOf(instance->id));
        if (names.empty())
            this->addView(ui::text("Play a world once, then come back: a data pack goes into one."));
        else
        {
            if (std::find(names.begin(), names.end(), world) == names.end())
                world = names.front();

            unsigned at = 0;
            for (size_t i = 0; i < names.size(); i++)
                if (names[i] == world)
                    at = (unsigned)i;

            auto* pick = new brls::SelectListItem("World", names, at,
                "The data pack is installed into this world.");
            pick->getValueSelectedEvent()->subscribe([this, names](int index) {
                if (index >= 0 && index < (int)names.size())
                    this->world = names[(size_t)index];
            });
            this->addView(pick);
        }
    }

    this->addView(ui::item("Search…", this->status, [this] { this->askSearch(); }));

    if (!results.empty())
    {
        this->addView(new brls::Header("Results"));
        for (size_t i = 0; i < results.size(); i++)
        {
            const Hit& hit = results[i];
            brls::ListItem* row = new brls::ListItem(hit.title, ellipsis(hit.description, 110),
                hit.versions);
            row->setValue(hit.verdict, hit.verdict != "Works");
            row->getClickEvent()->subscribe([this, i](brls::View*) { this->install(i); });
            this->addView(row);
        }
    }

    this->addView(ui::text("Downloads are checked against Modrinth's SHA-512 before they are "
                           "installed.",
        true, 16));
}

void ModsPage::askSearch()
{
    ui::ask("Search " + std::string(kindLabel(this->kind)), "Words to look for", "", 64,
        [this](std::string words) {
            if (!trim(words).empty())
                this->runSearch(trim(words));
        });
}

void ModsPage::runSearch(const std::string& words)
{
    const Kind searchKind = this->kind;
    this->status      = "Searching Modrinth…";
    this->results.clear();
    this->rebuild();

    AlivePtr alive           = this->alive;
    auto found               = std::make_shared<std::vector<Hit>>();
    auto error               = std::make_shared<std::string>();

    Worker::get().post(
        [searchKind, words, found, error] { *found = search(searchKind, words, *error); },
        [this, alive, found, error] {
            if (!alive->ok())
                return;

            this->results = std::move(*found);
            this->status  = error->empty() ? std::to_string(this->results.size()) + " results"
                                           : *error;
            this->rebuild();
        });
}

void ModsPage::install(size_t index)
{
    if (index >= this->results.size())
        return;

    const Hit hit = this->results[index];
    if (!hit.installable)
    {
        ui::alert(hit.title + "\n\n" + hit.note);
        return;
    }

    auto& config = Config::get();
    Instance* instance = config.current();
    if (!instance)
        return;

    std::string folder;
    if (kind == Kind::ResourcePack)
        folder = gameDirOf(instance->id) + "/resourcepacks";
    else if (kind == Kind::DataPack)
    {
        if (this->world.empty())
        {
            ui::alert("Pick a world first: a data pack goes into one.");
            return;
        }
        folder = gameDirOf(instance->id) + "/saves/" + this->world + "/datapacks";
    }
    else
    {
        ui::alert("This kind can't be installed on the console.");
        return;
    }

    ui::confirm("Install \"" + hit.title + "\" into\n" + folder + "?", [this, hit, folder] {
        AlivePtr alive = this->alive;
        this->status = "Installing " + hit.title + "…";
        this->rebuild();

        Worker::get().post(
            [hit, folder] {
                std::string error;
                if (!launcher::install(hit, folder, error))
                    log("mods: %s: %s", hit.title.c_str(), error.c_str());
            },
            [this, alive, hit] {
                if (!alive->ok())
                    return;
                this->status = "Installed " + hit.title + ".";
                this->rebuild();
                brls::Application::notify("Installed " + hit.title);
            });
    },
        "Install");
}

// ----- Settings ------------------------------------------------------------------------------

SettingsPage::SettingsPage()
{
    this->setMargins(24, 24, 24, 24);
    this->rebuild();
}

SettingsPage::~SettingsPage() { this->alive->flag = false; }

void SettingsPage::rebuild()
{
    log("page: settings");
    this->clear();
    auto& config = Config::get();
    config.refreshAccounts();

    // Account.
    this->addView(new brls::Header("Account"));

    std::vector<std::string> names;
    unsigned selected = 0;
    for (size_t i = 0; i < config.accounts.size(); i++)
    {
        names.push_back(config.accounts[i].name);
        if (config.accounts[i].uuid == config.account)
            selected = (unsigned)i;
    }

    if (names.empty())
    {
        this->addView(ui::item("Sign in", "Use a QR code or a code typed at microsoft.com/link.",
            [] { pushSignIn(); }, "Not signed in"));
    }
    else
    {
        auto* picker = new brls::SelectListItem("Account", names, selected,
            "The account the game plays as, online and on the skin.");
        picker->getValueSelectedEvent()->subscribe([this](int index) { this->switchAccount(index); });
        this->addView(picker);

        this->addView(ui::item("Sign in another account",
            "Press Minus at any time to come straight here.",
            [] { pushSignIn(); }, "Minus"));

        this->addView(ui::item("Sign out of \"" + names[selected] + "\"",
            "Deletes its saved sign-in from this console.", [this] { this->signOut(); }));
    }

    // Appearance.
    this->addView(new brls::Header("Appearance"));
    std::vector<std::string> themes = { "System", "Dark", "Light" };
    unsigned theme = config.theme == "dark" ? 1 : (config.theme == "light" ? 2 : 0);
    auto* themeItem = new brls::SelectListItem("Theme", themes, theme,
        "System follows the console's own light or dark setting.");
    themeItem->getValueSelectedEvent()->subscribe([](int index) {
        auto& config = Config::get();
        config.theme = index == 1 ? "dark" : (index == 2 ? "light" : "system");
        config.save();
        ui::applyTheme(config.theme);
    });
    this->addView(themeItem);

    // What the launcher does.
    this->addView(new brls::Header("Launch"));
    brls::ToggleListItem* fps =
        new brls::ToggleListItem("Show frame rate", config.showFps, "A counter in the corner.");
    fps->getClickEvent()->subscribe([](brls::View*) {
        auto& config = Config::get();
        config.showFps = !config.showFps;
        config.save();
        brls::Application::setDisplayFramerate(config.showFps);
    });
    this->addView(fps);

    // The sign-in needs an Azure app; this is where a console that has none of its own gets one.
    this->addView(new brls::Header("Sign-in"));
    this->addView(new Field("Microsoft client ID", config.clientId,
        "The Azure app's client ID", "Same value as HORIZON_MSA_CLIENT_ID in the game's env.txt.",
        64,
        [](std::string text) {
            auto& config = Config::get();
            config.clientId = trim(text);
            config.save();
            brls::Application::notify("Client ID saved: open the sign-in again");
        }));

    // About.
    this->addView(new brls::Header("About"));
    this->addView(ui::text("Horizon Launcher 1.0.0\n"
                           "Starts the game with the instance's folder, opens Microsoft sign-in and "
                           "browses resource packs and data packs.\n\n"
                           "Files live in " +
            root() + ".",
        true, 18));
}

void SettingsPage::switchAccount(int index)
{
    auto& config = Config::get();
    if (index < 0 || index >= (int)config.accounts.size())
        return;

    const std::string uuid = config.accounts[(size_t)index].uuid;
    if (config.activateAccount(uuid))
    {
        brls::Application::notify("Now playing as " + config.accounts[(size_t)index].name);
        this->rebuild();
    }
    else
        ui::alert("That account's saved sign-in could not be read.");
}

void SettingsPage::signOut()
{
    auto& config = Config::get();
    std::string uuid = config.account;
    if (uuid.empty())
        return;

    ui::confirm("Sign out?\n\nThe saved sign-in is deleted from this console. The Microsoft "
                "account itself is untouched.",
        [this, uuid] {
            Config::get().forgetAccount(uuid);
            this->rebuild();
            brls::Application::notify("Signed out");
        },
        "Sign out");
}

// ----- Sign-in -------------------------------------------------------------------------------

namespace
{

/// Polls the sign-in on the UI thread. It belongs to the app, not to the screen, so closing the
/// screen mid-sign-in can't leave a task pointing at freed memory.
class SignInPoller : public brls::RepeatingTask
{
  public:
    SignInPoller()
        : brls::RepeatingTask(200)
    {
    }

    void run(retro_time_t currentTime) override
    {
        if (SignInPage* page = SignInPage::live())
            page->refresh();
        brls::RepeatingTask::run(currentTime);
    }
};

SignInPage*& current()
{
    static SignInPage* page = nullptr;
    return page;
}

} // namespace

SignInPage* SignInPage::live() { return current(); }

SignInPage::SignInPage()
    : brls::BoxLayout(brls::BoxLayoutOrientation::VERTICAL)
{
    this->setMargins(24, 24, 24, 24);
    this->setSpacing(12);
    current() = this;

    static bool polling = false;
    if (!polling)
    {
        polling = true;
        SignInPoller* poller = new SignInPoller();
        poller->start();
    }

    // Two columns: the code to scan on the left, what to do with it on the right. Explicit sizes,
    // because a box does not measure its children: nothing here scrolls, so the QR code is always
    // where the player can point a phone at it.
    brls::BoxLayout* columns = new brls::BoxLayout(brls::BoxLayoutOrientation::HORIZONTAL);
    columns->setSpacing(20);
    columns->setHeight(340);

    this->qr = new QRView("https://www.microsoft.com/link");
    this->qr->setWidth(320);
    this->qr->setHeight(320);
    columns->addView(this->qr);

    brls::BoxLayout* details = new brls::BoxLayout(brls::BoxLayoutOrientation::VERTICAL);
    details->setSpacing(8);
    details->setWidth(560);
    details->setHeight(340);

    this->status = ui::text("Asking Microsoft for a code…", true, 26);
    this->status->setWidth(560);
    this->status->setHeight(60);
    details->addView(this->status);

    this->code = ui::text("· · · · · · · ·", false, 50, NVG_ALIGN_LEFT);
    this->code->setWidth(560);
    this->code->setHeight(80);
    details->addView(this->code);

    brls::Label* steps = ui::text(
        "1. Scan the code, or open www.microsoft.com/link on a phone or a computer.\n"
        "2. Type the code shown here.\n"
        "3. Sign in with the Microsoft account that owns Minecraft: Java Edition.\n\n"
        "This screen finishes on its own. The sign-in is kept on the SD card in "
        "switch/horizon/account.json, so anyone with the card can use it until you sign out.",
        true, 17);
    steps->setWidth(560);
    steps->setHeight(190);
    details->addView(steps);

    columns->addView(details);
    this->addView(columns);

    brls::ListItem* cancel = ui::item("Cancel", "Stop signing in.", [this] {
        if (this->state)
            this->state->cancelled = true;
        brls::Application::popView();
    });
    cancel->setHeight(64);
    this->addView(cancel);

    // A sign-in that can't be made says so at once instead of waiting for a code that never comes.
    if (auth::clientId().empty())
    {
        this->status->setText("No Microsoft client ID: add HORIZON_MSA_CLIENT_ID to "
                              "switch/horizon/env.txt, or set it in Settings (Sign-in).");
        return;
    }
    if (!Worker::get().hasThread())
    {
        this->status->setText("The background worker could not start, and signing in would freeze "
                              "this screen. See switch/horizon/launcher-log.txt.");
        return;
    }

    this->state = auth::start();
}

SignInPage::~SignInPage()
{
    if (current() == this)
        current() = nullptr;
    if (this->state)
        this->state->cancelled = true;
}

void SignInPage::refresh()
{
    if (!this->state || this->reported)
        return;

    switch (this->state->getPhase())
    {
        case auth::Phase::Contacting:
            this->status->setText("Asking Microsoft for a code…");
            break;

        case auth::Phase::Code:
        {
            const std::string code = this->state->getCode();
            const std::string url  = this->state->getUrl();

            // The plain address, not a `?otc=` deep link: Microsoft answers a prefilled first-party
            // app with "we're unable to complete your request" (invalid_request), and the code is
            // written out beside the code anyway.
            if (url != this->shown)
            {
                this->shown = url;
                this->qr->setPayload(url);
            }

            this->status->setText("Scan the code, or open " + url + ", and type:");
            this->code->setText(code);
            break;
        }

        case auth::Phase::Finishing:
            this->status->setText("Signed in. Checking Xbox Live and Minecraft…");
            break;

        case auth::Phase::SignedIn:
            this->reported = true;
            Config::get().refreshAccounts();
            Config::get().save();
            brls::Application::notify("Signed in as " + this->state->getName());
            brls::Application::popView();
            break;

        case auth::Phase::Failed:
            this->reported = true;
            this->status->setText(this->state->getError());
            break;
    }
}

void bindSignIn(brls::View* view)
{
    if (!view)
        return;
    view->registerAction("Sign in", brls::Key::MINUS, [] {
        pushSignIn();
        return true;
    });
}

void bindBack(brls::View* view)
{
    if (!view)
        return;
    view->registerAction("Back", brls::Key::B, [] {
        brls::Application::popView();
        return true;
    });
}

void pushSignIn()
{
    if (SignInPage::live())
        return;

    SignInPage* page = new SignInPage();
    brls::Application::pushView(page);
    // pushView binds Minus to its frame-rate counter on the view it just pushed; take it back.
    page->registerAction("Cancel", brls::Key::MINUS, [] {
        brls::Application::popView();
        return true;
    });
    bindBack(page);
}

} // namespace launcher
