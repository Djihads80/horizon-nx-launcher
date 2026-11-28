#include "pages.hpp"

#include "config.hpp"
#include "game.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <memory>

namespace launcher
{

namespace
{

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

/// What the game download is doing. The transfer runs on the worker, so these are the only things it
/// touches: a percentage and whether it is still going. The page reads them while it draws.
std::atomic<bool> installing { false };
std::atomic<int> installPercent { 0 };
std::string installError;

/// Downloads the game and puts it in place. `done` runs on the UI thread with what happened.
void installTheGame(std::function<void(bool, std::string)> done)
{
    if (installing.exchange(true))
        return;

    installPercent = 0;
    installError.clear();

    Worker::get().post(
        [] {
            GameRelease release;
            std::string error;
            bool ok = latestGameRelease(release, error);
            if (ok)
            {
                const uint64_t expected = release.bytes;
                ok = installGame(release, [expected](double part) {
                    installPercent = (int)(part * 100.0);
                    (void)expected;
                }, error);
            }
            installError = error;
            installing   = false;
        },
        [done] {
            const bool ok = installError.empty();
            const std::string error = installError;
            if (done)
                done(ok, error);
        });
}

/// Pictures by path, each loaded once. A path that fails is remembered as failed too: retrying a
/// missing or broken file in draw() on every frame is a freeze, which is exactly what a bad icon used
/// to look like — the app stopped instead of drawing nothing there.
std::map<std::string, int>& pictures()
{
    static std::map<std::string, int> cache;
    return cache;
}

int picture(NVGcontext* vg, const std::string& file)
{
    if (file.empty())
        return 0;

    auto& cache = pictures();
    auto found  = cache.find(file);
    if (found != cache.end())
        return found->second;

    const int image = nvgCreateImage(vg, file.c_str(), NVG_IMAGE_NEAREST);
    cache[file]     = image;
    // A console has no terminal: a picture that will not load says so here instead of appearing as
    // an empty tile with nothing to go on.
    if (image == 0)
        log("picture: %s did not load", file.c_str());
    return image;
}

/// The rows of a chooser ignore the button that opened the page for the first frames. The press is
/// still down when the page appears, and acting on it makes a chooser pick its first row and close in
/// the same frame it opened — which is exactly what the picture picker did.
class Gate
{
  public:
    /// True once the page has settled and nobody has chosen yet. A held button reaches a row more
    /// than once — two pops from one press took the chooser *and* the screen under it away, which is
    /// what left the app on a page with no focus.
    bool ready() const { return this->isOpen && !this->isUsed; }
    void use() { this->isUsed = true; }

  private:
    friend class ChooserPage;
    bool isOpen = false;
    bool isUsed = false;
};

/// A row with a picture: its name, a line under it, and what it is (in use, change). Drawn here
/// rather than as a list item because a list item sizes its picture from the row height (so it sits
/// off-centre and can spill past the focus ring) and draws its description outside that ring.
class Row : public brls::View
{
  public:
    Row(std::string file, std::string title, std::string detail, std::string value,
        std::function<void()> onClick, std::shared_ptr<Gate> gate = nullptr)
        : gate(gate)
        , iconFile(std::move(file))
        , title(std::move(title))
        , detail(std::move(detail))
        , value(std::move(value))
        , onClick(std::move(onClick))
    {
        this->setHeight(this->detail.empty() ? 56 : 96);
        this->registerAction("OK", brls::Key::A, [this] {
            if (this->gate)
            {
                if (!this->gate->ready())
                    return true; // still opening, or already chosen: one press, one choice
                this->gate->use();
            }
            if (this->onClick)
                this->onClick();
            return true;
        });
    }

    View* getDefaultFocus() override { return this; }

    /// What a row shows can change under it (a picture picked, a name edited): update it in place.
    void set(std::string file, std::string title, std::string detail, std::string value)
    {
        this->iconFile = std::move(file);
        this->title    = std::move(title);
        this->detail   = std::move(detail);
        this->value    = std::move(value);
        this->setHeight(this->detail.empty() ? 56 : 96);
        this->invalidate();
    }

    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style,
        brls::FrameContext* ctx) override
    {
        // A row with one line of text needs less room: 56 with the picture inside it.
        const int iconSize = this->detail.empty() ? 40 : 56;
        const int iconX    = x + 16;
        // Exactly centred in the row, whatever the row's height is.
        const int iconY = y + ((int)height - iconSize) / 2;

        const int image = picture(vg, this->iconFile);
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
        nvgText(vg, textX, this->detail.empty() ? (float)y + height / 2.0f : (float)y + height * 0.38f,
            this->title.c_str(), nullptr);

        if (!this->detail.empty())
        {
            nvgFontSize(vg, 19.0f);
            nvgFillColor(vg, ctx->theme->descriptionColor);
            nvgText(vg, textX, (float)y + height * 0.68f, this->detail.c_str(), nullptr);
        }

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
    // Held, not pointed at: the rows outlive the function that made the page, and a gate on its
    // stack was gone by the time the first frame asked whether it was open.
    std::shared_ptr<Gate> gate;
    std::string iconFile, title, detail, value;
    std::function<void()> onClick;
};

/// A picture an instance can wear: none, one of the launcher's, or one from the SD card.
struct Choice
{
    std::string label;   // what the picker calls it
    std::string value;   // what the instance keeps, and what "in use" is compared with
    std::string picture; // the file to draw, empty for none
};

std::string capitalised(const std::string& text)
{
    std::string out = text;
    if (!out.empty())
        out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

/// A picture's name without its extension: "Ancient Debris.png" reads "Ancient Debris", and a
/// lowercase one is capitalised so the list does not start every row in lowercase.
std::string pictureName(const std::string& file)
{
    std::string name = file;
    if (endsWith(name, ".png"))
        name = name.substr(0, name.size() - 4);
    return capitalised(name);
}

/// The PNGs in a folder, in name order. Nothing is listed in the code: a file dropped into either
/// folder is offered the next time the picker opens.
std::vector<std::string> picturesIn(const std::string& folder)
{
    std::vector<std::string> names;
    for (const std::string& file : listDir(folder))
        if (endsWith(file, ".png"))
            names.push_back(file);

    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return lower(a) < lower(b);
    });
    return names;
}

/// The pictures on offer: the ones packed into the app first (`romfs/img/icons`), then the SD card's
/// `switch/horizon/icons`. PNG only: a picture dropped in as a phone photo is tens of megabytes
/// decoded, which the console takes seconds over — it reads as a freeze, so those are not offered.
std::vector<Choice> iconChoices()
{
    std::vector<Choice> choices;
    choices.push_back({ "None", "", "" });

    for (const std::string& file : picturesIn(asset("img/icons")))
        choices.push_back({ pictureName(file), file.substr(0, file.size() - 4),
            asset("img/icons/" + file) });

    for (const std::string& file : picturesIn(path("icons")))
        choices.push_back(
            { pictureName(file), path("icons") + "/" + file, path("icons") + "/" + file });

    return choices;
}

/// What the instance is wearing now, in words: "None", "Grass", "Ancient Debris".
std::string pictureLabel(const Instance& instance)
{
    if (instance.icon.empty())
        return "None";

    const bool fromCard = instance.icon.find('/') != std::string::npos
        || instance.icon.find(':') != std::string::npos;
    if (!fromCard)
        return capitalised(instance.icon);

    const size_t slash = instance.icon.find_last_of('/');
    return pictureName(slash == std::string::npos ? instance.icon : instance.icon.substr(slash + 1));
}

/// A list of rows to choose from. It takes the first choice and nothing else, and leaves itself a
/// frame later: one press reached more than one row (a held button is dispatched again), and two pops
/// from a single press took the chooser *and* the screen under it away, leaving the app with no focus.
class ChooserPage : public brls::List
{
  public:
    explicit ChooserPage(std::function<void(std::string)> done)
        : done(std::move(done))
    {
    }

    /// The rows ask this. Only the first call counts.
    void choose(const std::string& value)
    {
        if (this->chosen)
            return;
        this->chosen = true;
        this->value  = value;
    }

    /// Rows stay inert until the button that opened the page is released.
    std::shared_ptr<Gate> gate() const { return this->gateState; }

    void frame(brls::FrameContext* ctx) override
    {
        if (++this->frames > 2)
            this->gateState->isOpen = true;

        if (this->chosen && !this->leaving)
        {
            this->leaving = true;
            // The callback outlives this page: popView deletes it.
            std::function<void(std::string)> callback = this->done;
            const std::string chosen                  = this->value;
            brls::Application::popView(brls::ViewAnimation::FADE,
                [callback, chosen] {
                    if (callback)
                        callback(chosen);
                });
        }

        brls::List::frame(ctx);
    }

  private:
    std::shared_ptr<Gate> gateState = std::make_shared<Gate>();
    std::function<void(std::string)> done;
    std::string value;
    bool chosen   = false;
    bool leaving  = false;
    uint64_t frames = 0;
};

/// A page of rows to pick from, pushed over the screen that asked for it. `fill` adds the rows; they
/// ask `page->choose(value)`, and `done` is called with the first choice once the page has gone.
void pushChoices(const std::string& heading, const std::string& note,
    std::function<void(std::string)> done, std::function<void(ChooserPage*)> fill)
{
    ChooserPage* page = new ChooserPage(std::move(done));
    page->setMargins(24, 24, 24, 24);
    if (!heading.empty())
        page->addView(new brls::Header(heading));
    if (!note.empty())
        page->addView(ui::text(note, true, 16));

    fill(page);

    brls::Application::pushView(page);
    bindBack(page);
}

/// The page of pictures an instance can wear, `current` marked "in use"; `pick` gets what was chosen.
void pushPictureChooser(const std::string& current, std::function<void(std::string)> pick)
{
    log("pictures: %zu on offer", iconChoices().size());

    pushChoices("Picture", "PNG files in sdmc:/switch/horizon/icons/ are listed too.", std::move(pick),
        [current](ChooserPage* page) {
            for (const Choice& choice : iconChoices())
            {
                const std::string value = choice.value;
                page->addView(new Row(choice.picture, choice.label, "",
                    value == current ? "In use" : "",
                    [page, value] { page->choose(value); }, page->gate()));
            }
        });
}

/// The page of instances to play, `current` marked; `pick` gets the instance's id.
void pushInstanceChooser(const std::string& current, std::function<void(std::string)> pick)
{
    pushChoices("Instance", "Which one Play starts.", std::move(pick),
        [current](ChooserPage* page) {
            for (const Instance& candidate : Config::get().instances)
            {
                const std::string id = candidate.id;
                page->addView(new Row(ui::iconPath(candidate), candidate.name,
                    ui::playtimeText(candidate.played), candidate.id == current ? "Playing" : "",
                    [page, id] { page->choose(id); }, page->gate()));
            }
        });
}

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
        livePlay->refresh();
    if (liveInstances)
        liveInstances->refresh();
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

void PlayPage::refresh()
{
    auto& config       = Config::get();
    Instance* instance = config.current();
    if (!instance || !this->pickerRow)
    {
        this->rebuild();
        return;
    }

    static_cast<Row*>(this->pickerRow)
        ->set(ui::iconPath(*instance), instance->name, ui::playtimeText(instance->played),
            config.instances.size() > 1 ? "Change" : "");
}

void PlayPage::rebuild()
{
    log("page: play");
    this->pickerRow        = nullptr;
    this->playButton       = nullptr;
    this->buttonIsProgress = false;
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
    // Picking the row picks another, with the same pictures the Instances tab shows; changing them
    // is that tab's job.
    brls::View* picker = new Row(ui::iconPath(*instance), instance->name,
        ui::playtimeText(instance->played), config.instances.size() > 1 ? "Change" : "",
        [this, id] {
            pushInstanceChooser(id, [this](std::string chosen) {
                auto& config    = Config::get();
                config.selected = chosen;
                config.save();
                this->refresh();
            });
        });
    this->addView(picker);
    this->pickerRow = picker;

    // Play.
    brls::Button* play = new brls::Button(brls::ButtonStyle::PRIMARY);
    play->setLabel("Play");
    // A button in a list keeps no height of its own: without this it is a line.
    play->setHeight(96);
    play->getClickEvent()->subscribe([this, id](brls::View*) { this->play(id); });
    this->addView(play);
    this->playButton = play;

    // The arguments every instance gets on top of its own.
    const std::string arguments = trim(config.globalArgs);
    this->addView(ui::item("Global arguments",
        arguments.empty() ? "None. Whatever goes here is added to every instance's command line."
                          : arguments,
        [this] { this->askArguments(); }));
}

void PlayPage::install(const std::string& instanceId)
{
    brls::Application::notify("Fetching the latest release…");

    installTheGame([this, instanceId](bool ok, std::string error) {
        if (!ok)
        {
            ui::alert("The game could not be installed: " + error);
            return;
        }

        brls::Application::notify("The game is installed");
        this->play(instanceId);
    });
}

void PlayPage::frame(brls::FrameContext* ctx)
{
    // While a download runs the Play button says how far it has got: the page is not rebuilt under it.
    if (this->playButton)
    {
        if (installing)
        {
            this->playButton->setLabel(
                "Downloading… " + std::to_string(installPercent.load()) + "%");
            this->buttonIsProgress = true;
        }
        else if (this->buttonIsProgress)
        {
            this->playButton->setLabel("Play");
            this->buttonIsProgress = false;
        }
    }

    brls::List::frame(ctx);
}

void PlayPage::play(const std::string& instanceId)
{
    auto& config = Config::get();
    Instance* instance = config.find(instanceId);
    if (!instance)
        return;

    // Nothing to start yet: the launcher is the installer too, so Play offers to fetch the game
    // rather than sending the player off to find an .nro.
    if (!gameInstalled())
    {
        ui::confirm(
            "The game isn't on this console yet.\n\nDownload the latest release (about 90 MB) and "
            "install it now?",
            [this, instanceId] { this->install(instanceId); }, "Download");
        return;
    }

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

void InstancesPage::refresh()
{
    auto& config = Config::get();
    if (this->rows.size() != config.instances.size())
    {
        this->rebuild();
        return;
    }

    for (size_t i = 0; i < config.instances.size(); i++)
    {
        const Instance& instance = config.instances[i];
        static_cast<Row*>(this->rows[i])
            ->set(ui::iconPath(instance), instance.name, ui::playtimeText(instance.played),
                instance.id == config.selected ? "Selected" : "");
    }
}

void InstancesPage::rebuild()
{
    log("page: instances");
    this->rows.clear();
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
        brls::View* row     = new Row(ui::iconPath(instance), instance.name,
                ui::playtimeText(instance.played), instance.id == config.selected ? "Selected" : "",
                [this, id] { this->edit(id); });
        this->addView(row);
        this->rows.push_back(row);
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
        // New rows: the old ones, and whatever had focus among them, are gone.
        this->focusFirst();
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

            // The picture: the launcher's own, one from the SD card's icons folder, or none. It opens
            // a page of the pictures themselves, so the choice is not a name read blind.
            picture = new Row(ui::iconPath(*instance), pictureLabel(*instance), "", "Choose",
                [this, id] { this->choosePicture(id); });
            this->addView(picture);

            arguments = new Field("Extra arguments", instance->args, "None",
                "Added after the global arguments on the Play tab.", 256, {});
            this->addView(arguments);

            this->addView(ui::text(ui::playtimeText(instance->played) + " · " +
                    gameDirOf(instance->id),
                true, 16));

            if (instance->id != config.selected)
                this->addView(ui::item("Use this instance", "Play starts it from now on.",
                    [this, id] {
                        this->apply(id);
                        Config::get().selected = id;
                        Config::get().save();
                        brls::Application::notify("That is the instance Play starts");
                        this->leave();
                    }));

            this->addView(ui::item("Done", "Keep these settings.", [this, id] {
                brls::Application::notify(this->apply(id) ? "Saved" : "The card would not take it");
                this->leave();
            }));

            if (config.instances.size() > 1)
                this->addView(ui::item("Delete this instance",
                    "Removes its worlds, packs and mods too.", [this, id] {
                        brls::Application::popView(brls::ViewAnimation::FADE, [id] {
                            if (InstancesPage* page = live())
                                page->remove(id);
                        });
                    }));
        }

        /// Writes what the fields hold, without leaving this screen. False when the card would not
        /// take it, so the screen can say so instead of claiming it is saved.
        bool apply(const std::string& instanceId)
        {
            auto& config = Config::get();
            Instance* instance = config.find(instanceId);
            if (!instance)
                return false;

            if (name && !trim(name->getValue()).empty())
                instance->name = trim(name->getValue());
            if (arguments)
                instance->args = trim(arguments->getValue());

            return config.save();
        }

        /// Leaves this screen and refreshes the pages behind it, once the pop has finished: rebuilding
        /// them while their focused row is being handed back leaves the console with nothing listening
        /// but Exit and the frame counter.
        void leave()
        {
            // A held button reaches this twice; the second pop would take the page underneath too.
            if (this->left)
                return;
            this->left = true;

            brls::Application::popView(brls::ViewAnimation::FADE, [] { refreshPages(); });
        }

        /// The pictures on offer, each drawn, so the choice is made by looking at it. Picking one
        /// saves it at once and the row behind shows it as soon as this page pops.
        void choosePicture(const std::string& instanceId)
        {
            const Instance* instance = Config::get().find(instanceId);
            if (!instance)
                return;
            const std::string current = instance->icon;

            pushPictureChooser(current, [this, instanceId](std::string value) {
                this->usePicture(instanceId, value);
            });
        }

        void usePicture(const std::string& instanceId, const std::string& value)
        {
            auto& config      = Config::get();
            Instance* instance = config.find(instanceId);
            if (!instance)
                return;

            instance->icon = value;
            if (!config.save())
                log("picture: %s could not be saved", instanceId.c_str());
            log("picture: %s now wears \"%s\"", instanceId.c_str(), value.c_str());
            log("picture: the file holds \"%s\"", config.storedPicture(instanceId).c_str());

            // The chooser has already popped itself: this only has to bring the row and the pages up
            // to date. Popping here took the editor away as well.
            if (this->picture)
                this->picture->set(ui::iconPath(*instance), pictureLabel(*instance), "", "Choose");

            refreshPages();
        }

        static InstancesPage*& live()
        {
            static InstancesPage* page = nullptr;
            return page;
        }

        std::string id;
        bool left         = false;
        Row* picture      = nullptr;
        Field* name       = nullptr;
        Field* arguments  = nullptr;
    };

    Edit::live() = this;
    Edit* edit = new Edit(instanceId);
    brls::Application::pushView(edit);
    bindSignIn(edit);
    bindBack(edit);
}

void InstancesPage::focusFirst()
{
    for (size_t i = 0; i < this->getViewsCount(); i++)
        if (brls::View* focus = this->getChild(i)->getDefaultFocus())
        {
            brls::Application::giveFocus(focus);
            return;
        }
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
                        this->focusFirst();
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
        "Horizon's own format, so each result says whether it runs before you install it.",
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

    auto* picker = new brls::SelectListItem("Browse", labels, selected,
        "Modrinth, the same source the desktop launcher uses.");
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
            "The account the game plays as.");
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

    // The game: installed here, or fetched from its release.
    this->addView(new brls::Header("Game"));
    this->addView(ui::item(gameInstalled() ? "The game is installed" : "The game is not installed",
        gameInstalled() ? "horizon.nro, in " + root() + "."
                        : "Download the latest release from inside the launcher.",
        [this] {
            installTheGame([this](bool ok, std::string error) {
                brls::Application::notify(ok ? "The game is installed"
                                             : "Not installed: " + error);
                this->rebuild();
            });
        },
        gameInstalled() ? "Reinstall" : "Download"));

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
    else if (!config.hasSession(uuid))
        ui::alert("That account has no sign-in on this console yet. Sign in with it (Minus) and it "
                  "will play as it is selected.");
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

#ifdef HORIZON_DESKTOP
void installGameForTest(std::function<void(bool, std::string)> done)
{
    installTheGame(std::move(done));
}

/// Opens a screen by name, for the desktop build: a PC has no controller wired to the harness, so a
/// page three presses deep is opened at once with HORIZON_SCREEN=instances|picture.
void openScreenForTest(const std::string& name)
{
    auto& config = Config::get();
    const Instance* instance = config.find(config.selected);
    const std::string icon   = instance ? instance->icon : "";

    if (name == "picture")
        pushPictureChooser(icon, [](std::string value) { log("test: picture %s", value.c_str()); });
    else if (name == "instances")
        pushInstanceChooser(config.selected,
            [](std::string id) { log("test: instance %s", id.c_str()); });
    else
        log("test: no screen called %s", name.c_str());
}
#endif

} // namespace launcher
