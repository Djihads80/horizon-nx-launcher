//! What the launcher remembers: its instances, the accounts signed in on this console, and the
//! settings. Kept in `launcher.json` beside the game.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace launcher
{

/// One way to play: its own game folder (worlds, options.txt, packs, mods) and its own arguments.
struct Instance
{
    std::string id;   // the folder's name under instances/, ascii
    std::string name; // what the player calls it
    std::string icon; // a picture on the SD card, or "" for the name's first letter
    std::string args; // extra arguments this instance gets, after the global ones
    uint64_t played = 0; // seconds played, as the launcher counts them
};

/// A Microsoft account signed in on this console: its Minecraft profile, and the game's own
/// `account.json` kept beside it in accounts/.
struct Account
{
    std::string uuid; // the profile UUID without dashes
    std::string name; // the Minecraft name
};

class Config
{
  public:
    static Config& get();

    /// Reads launcher.json, the accounts folder and the game's account.json. Missing files are
    /// normal: a first start has none.
    void load();
    bool save();

    // Settings
    std::string theme = "system"; // system | dark | light
    std::string globalArgs;       // extra arguments every instance gets
    bool showFps = false;
    std::string selected = "default"; // the instance the launcher shows

    /// The Azure app the Microsoft sign-in uses. Empty means the build's own, then the game's
    /// `env.txt`; setting it here lets a console take the same app the game signs in with.
    std::string clientId;

    std::vector<Instance> instances;
    std::vector<Account> accounts;
    std::string account; // the active account's uuid, "" when signed out

    /// The session in progress: which instance was started and when. Written before handing over to
    /// the game and counted the next time the launcher runs.
    std::string session;
    uint64_t sessionStart = 0;

    Instance* current();
    Instance* find(const std::string& id);
    /// A new instance and its folder. Returns its id: the name, lowercased, spaces turned into
    /// dashes and made unique, so it is a folder name and a command line argument.
    std::string add(const std::string& name);
    /// Deletes the instance's folder and forgets it. Never the last one.
    bool remove(const std::string& id);

    /// Reads accounts/*.json and the game's account.json (which the game renews), so the launcher
    /// lists every account, with fresh tokens.
    void refreshAccounts();
    bool activateAccount(const std::string& uuid);
    void forgetAccount(const std::string& uuid);

    std::string accountFile(const std::string& uuid) const;

    /// The game is about to start: remembers which instance and when, so the time it is played for
    /// can be counted when the launcher next runs (nothing else survives the hand-over).
    void beginSession(const std::string& instanceId);

    /// Adds the time since beginSession() to that instance, capped, and forgets the session.
    void endSession();

    /// The arguments the game is started with, argv[0] included: `--game-dir` for the instance's
    /// folder, then the global arguments, then this instance's.
    std::string commandLine(const Instance& instance) const;

  private:
    std::string importAccountFile(const std::string& text);
    void loadAccountFiles();
};

} // namespace launcher
