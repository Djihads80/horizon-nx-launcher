#include "config.hpp"

#include "util.hpp"

#include <algorithm>

#include <json.hpp>

using nlohmann::json;

namespace launcher
{

namespace
{

json toJson(const Instance& instance)
{
    return json { { "id", instance.id }, { "name", instance.name }, { "icon", instance.icon },
        { "args", instance.args }, { "played", instance.played } };
}

/// A string field, or the default when the file holds something else there.
std::string stringField(const json& value, const char* key, const std::string& fallback)
{
    if (value.contains(key) && value[key].is_string())
        return value[key].get<std::string>();
    return fallback;
}

/// A number, or the default: a hand-edited file can hold "60" where 60 belongs.
uint64_t numberField(const json& value, const char* key, uint64_t fallback)
{
    if (value.contains(key) && value[key].is_number_unsigned())
        return value[key].get<uint64_t>();
    if (value.contains(key) && value[key].is_number_integer())
        return (uint64_t)std::max<int64_t>(0, value[key].get<int64_t>());
    return fallback;
}

Instance fromJson(const json& value)
{
    Instance instance;
    instance.id     = stringField(value, "id", "");
    instance.name   = stringField(value, "name", instance.id);
    instance.icon   = stringField(value, "icon", "");
    instance.args   = stringField(value, "args", "");
    instance.played = numberField(value, "played", 0);
    return instance;
}

} // namespace

Config& Config::get()
{
    static Config config;
    return config;
}

void Config::load()
{
    instances.clear();
    accounts.clear();

    std::string text;
    if (readFile(configPath(), text))
    {
        json value = json::parse(text, nullptr, false);
        if (!value.is_discarded() && value.is_object())
        {
            // Every field is read for the type it should be: value() throws on a wrong type, and a
            // settings file edited by hand is the one place a wrong type turns up. Skipping the line
            // costs a default; throwing ends the app on its first screen.
            theme      = stringField(value, "theme", theme);
            globalArgs = stringField(value, "globalArgs", globalArgs);
            showFps    = value.contains("showFps") && value["showFps"].is_boolean()
                   ? value["showFps"].get<bool>()
                   : showFps;
            clientId   = stringField(value, "clientId", clientId);
            selected   = stringField(value, "selected", selected);
            account    = stringField(value, "account", account);
            session    = stringField(value, "session", "");
            sessionStart = numberField(value, "sessionStart", 0);
            if (value.contains("instances") && value["instances"].is_array())
                for (const json& item : value["instances"])
                    if (item.is_object())
                        instances.push_back(fromJson(item));
        }
    }
    else
        log("config: no launcher.json yet, starting fresh");

    bool changed = false;
    if (instances.empty())
    {
        // The folder the game already used: an existing install keeps its worlds.
        instances.push_back(Instance { "default", "Minecraft 26.3", "", "", 0 });
        selected = "default";
        changed  = true;
    }

    // The first release called it "Default" and left it without a picture; the game it starts has a
    // better name, and its own block looks like something.
    for (Instance& instance : instances)
        if (instance.id == "default")
        {
            if (instance.name == "Default")
            {
                instance.name = "Minecraft 26.3";
                changed       = true;
            }
            if (instance.icon.empty())
            {
                instance.icon = "grass";
                changed       = true;
            }
        }

    // A run that ended while this app wasn't running still counts: its time was written down before
    // the hand-over.
    if (!session.empty())
        changed = true;

    makeDirs(instancesDir());
    makeDirs(accountsDir());

    for (Instance& instance : instances)
        makeDirs(gameDirOf(instance.id));

    refreshAccounts();
    endSession();

    // What was read back, in one line: an instance whose picture did not survive a restart shows up
    // here as a name with no picture behind it.
    size_t painted = 0;
    for (const Instance& instance : instances)
        if (!instance.icon.empty())
            ++painted;
    log("config: %zu instance(s), %zu with a picture", instances.size(), painted);
    for (const Instance& instance : instances)
        log("config: %s wears \"%s\"", instance.id.c_str(), instance.icon.c_str());

    if (changed && !save())
        log("config: launcher.json could not be written");
}

void Config::beginSession(const std::string& instanceId)
{
    session      = instanceId;
    sessionStart = (uint64_t)::time(nullptr);
    save();
}

void Config::endSession()
{
    if (session.empty() || sessionStart == 0)
        return;

    const uint64_t now      = (uint64_t)::time(nullptr);
    const uint64_t elapsed  = now > sessionStart ? now - sessionStart : 0;

    // The console can sit in the homebrew menu for days: only the first six hours of a run count,
    // and a run shorter than half a minute isn't worth a line.
    const uint64_t counted = elapsed > 6 * 3600 ? 6 * 3600 : elapsed;

    Instance* instance = find(session);
    if (instance && counted >= 30)
    {
        instance->played += counted;
        log("instance: %s was played for %llu s", instance->id.c_str(), (unsigned long long)counted);
    }

    session.clear();
    sessionStart = 0;
    save();
}

std::string Config::storedPicture(const std::string& instanceId) const
{
    std::string text;
    if (!readFile(configPath(), text))
        return "<no file>";

    json value = json::parse(text, nullptr, false);
    if (value.is_discarded() || !value.is_object() || !value.contains("instances"))
        return "<unreadable>";

    for (const json& item : value["instances"])
        if (item.is_object() && item.value("id", "") == instanceId)
            return item.contains("icon") && item["icon"].is_string() ? item["icon"].get<std::string>()
                                                                    : "<no icon field>";
    return "<no such instance>";
}

bool Config::save()
{
    json value = json::object();
    value["version"]    = 1;
    value["theme"]      = theme;
    value["globalArgs"] = globalArgs;
    value["showFps"]    = showFps;
    value["clientId"]   = clientId;
    value["selected"]   = selected;
    value["account"]    = account;
    value["session"]    = session;
    value["sessionStart"] = sessionStart;
    value["instances"]  = json::array();
    for (const Instance& instance : instances)
        value["instances"].push_back(toJson(instance));

    return writeFileAtomic(configPath(), value.dump(2));
}

Instance* Config::current()
{
    Instance* instance = find(selected);
    if (!instance && !instances.empty())
        instance = &instances.front();
    return instance;
}

Instance* Config::find(const std::string& id)
{
    for (Instance& instance : instances)
        if (instance.id == id)
            return &instance;
    return nullptr;
}

std::string Config::add(const std::string& name)
{
    std::string clean = sanitizeFileName(name);
    if (clean.empty())
        clean = "instance";

    // A folder name and a command line argument: lowercase, no spaces.
    std::string base;
    for (char c : clean)
        base.push_back(c == ' ' ? '-' : (char)std::tolower((unsigned char)c));

    std::string id = base;
    for (int n = 2; find(id); n++)
        id = base + "-" + std::to_string(n);

    makeDirs(gameDirOf(id));

    Instance instance;
    instance.id   = id;
    instance.name = name.empty() ? clean : name;
    instances.push_back(instance);
    log("instance: added \"%s\" (%s)", instance.name.c_str(), id.c_str());
    return id;
}

bool Config::remove(const std::string& id)
{
    if (instances.size() <= 1)
        return false;

    auto it = std::find_if(instances.begin(), instances.end(),
        [&](const Instance& instance) { return instance.id == id; });
    if (it == instances.end())
        return false;

    // The default instance is the game's own folder: removing it would take the whole install
    // with it, so its folder is left alone.
    if (id != "default")
        removeTree(instancesDir() + "/" + id);

    instances.erase(it);
    if (selected == id)
        selected = instances.front().id;

    log("instance: removed %s", id.c_str());
    save();
    return true;
}

std::string Config::accountFile(const std::string& uuid) const
{
    return accountsDir() + "/" + uuid + ".json";
}

std::string Config::importAccountFile(const std::string& text)
{
    json value = json::parse(text, nullptr, false);
    if (value.is_discarded() || !value.is_object())
        return "";

    std::string uuid = value.value("uuid", "");
    std::string name = value.value("name", "");
    if (uuid.empty() || name.empty())
        return "";

    std::string file    = accountFile(uuid);
    std::string current = "";
    readFile(file, current);

    // The game renews its token as it plays: the launcher's copy follows.
    if (trim(current) != trim(text))
        writeFileAtomic(file, text);

    return uuid;
}

void Config::loadAccountFiles()
{
    accounts.clear();
    for (const std::string& file : listDir(accountsDir()))
    {
        if (file.size() < 6 || file.substr(file.size() - 5) != ".json")
            continue;

        std::string text;
        if (!readFile(accountsDir() + "/" + file, text))
            continue;

        json value = json::parse(text, nullptr, false);
        if (value.is_discarded() || !value.is_object())
            continue;

        Account found;
        found.uuid = value.value("uuid", "");
        found.name = value.value("name", "");
        if (found.uuid.empty() || found.name.empty())
            continue;

        if (std::none_of(accounts.begin(), accounts.end(),
                [&](const Account& other) { return other.uuid == found.uuid; }))
            accounts.push_back(found);
    }

    std::sort(accounts.begin(), accounts.end(),
        [](const Account& a, const Account& b) { return lower(a.name) < lower(b.name); });
}

void Config::refreshAccounts()
{
    makeDirs(accountsDir());

    // The game renews account.json as it plays, and signs in from its own title screen: it is the
    // account the console is using.
    std::string text;
    if (readFile(path("account.json"), text))
    {
        std::string uuid = importAccountFile(text);
        if (!uuid.empty())
            account = uuid;
    }

    this->importDesktopAccounts();
    loadAccountFiles();
    if (account.empty() && !accounts.empty())
        account = accounts.front().uuid;
}

void Config::importDesktopAccounts()
{
    std::string text;
    if (!readFile(path("accounts.json"), text))
        return;

    json value = json::parse(text, nullptr, false);
    if (value.is_discarded() || !value.is_object())
        return;

    int imported = 0;
    for (const json& entry : value.value("accounts", json::array()))
    {
        if (!entry.is_object())
            continue;

        Account found { entry.value("uuid", ""), entry.value("name", "") };
        if (found.uuid.empty() || found.name.empty())
            continue;

        // Only the list entry: the desktop launcher keeps its tokens in the PC's keychain, so there
        // is no session here to copy, and writing a half one would make the game call it damaged.
        if (readFile(accountFile(found.uuid), text))
            continue;

        json saved = { { "name", found.name }, { "uuid", found.uuid } };
        if (writeFileAtomic(accountFile(found.uuid), saved.dump(2)))
            ++imported;
    }

    if (imported > 0)
        log("accounts: imported %d from accounts.json", imported);
}

bool Config::hasSession(const std::string& uuid) const
{
    std::string text;
    if (!readFile(accountFile(uuid), text))
        return false;

    json value = json::parse(text, nullptr, false);
    if (value.is_discarded() || !value.is_object())
        return false;

    return !value.value("minecraft_token", "").empty()
        && !value.value("refresh_token", "").empty() && value.contains("expires_at");
}

bool Config::activateAccount(const std::string& uuid)
{
    std::string text;
    if (!readFile(accountFile(uuid), text))
        return false;

    // The game cannot sign in from a list entry: it needs the session's three fields.
    if (!this->hasSession(uuid))
        return false;

    // The game reads its own file from the folder it runs in.
    if (!writeFileAtomic(path("account.json"), text))
        return false;

    account = uuid;
    save();
    log("account: %s is now active", uuid.c_str());
    return true;
}

void Config::forgetAccount(const std::string& uuid)
{
    removeTree(accountFile(uuid));
    if (account == uuid)
    {
        account.clear();
        removeTree(path("account.json"));
    }
    loadAccountFiles();
    if (account.empty() && !accounts.empty())
        account = accounts.front().uuid;
    save();
}

std::string Config::commandLine(const Instance& instance) const
{
    // argv[0] is the game itself: the game reads its folder from it.
    std::string line = "\"" + gamePath() + "\"";
    line += " --game-dir \"" + gameDirOf(instance.id) + "\"";

    if (!trim(globalArgs).empty())
        line += " " + trim(globalArgs);
    if (!trim(instance.args).empty())
        line += " " + trim(instance.args);

    return line;
}

} // namespace launcher
