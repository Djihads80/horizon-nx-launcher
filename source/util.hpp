//! The launcher's plumbing: where its files live on the SD card, reading and writing them, HTTP
//! for the mod browser and Microsoft sign-in, and a worker so the network never blocks the UI.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace launcher
{

// ----- Where things live ---------------------------------------------------------------------
//
// Everything sits beside the game, in `sdmc:/switch/horizon/`, so a launcher update never moves
// a player's worlds:
//
//   horizon.nro            the game the launcher starts
//   resources/             the prepared game files (made on a computer, see the README)
//   account.json           the signed-in account, as the game keeps it
//   accounts/<uuid>.json   every account the launcher signed in, one file each
//   instances/<id>/home/   one instance's game folder (saves, options.txt, packs, mods)
//   launcher.json          this launcher's own settings

/// The folder holding the game, its data and the launcher's files. `HORIZON_LAUNCHER_ROOT`
/// overrides it, which is how the launcher's own logic is tested on a computer.
const std::string& root();
/// `root() + "/" + rel`.
std::string path(const std::string& rel);
std::string configPath();
std::string accountsDir();
std::string instancesDir();
std::string gamePath();
/// The game folder of one instance: `instances/<id>/home`. The default instance is the game's
/// own folder, so a player's existing worlds stay where they were.
std::string gameDirOf(const std::string& instanceId);
/// A file in the app's romfs, e.g. `asset("img/logo.png")`.
std::string asset(const std::string& rel);

/// Set once the loader has taken the next NRO: the exit below it is a hand-over, not a shutdown.
bool handedOver();
void markHandedOver();

// ----- Files and folders ---------------------------------------------------------------------

bool exists(const std::string& p);
bool isDir(const std::string& p);
bool makeDirs(const std::string& p);
bool readFile(const std::string& p, std::string& out);
bool writeFile(const std::string& p, const std::string& data);
/// Writes beside the target and renames, so a crash never leaves half a file.
bool writeFileAtomic(const std::string& p, const std::string& data);
bool removeTree(const std::string& p);
std::vector<std::string> listDir(const std::string& p);
bool fileSize(const std::string& p, uint64_t& size);
uint64_t freeBytes(const std::string& p);
bool copyFile(const std::string& from, const std::string& to);

std::string trim(const std::string& s);
/// A name that can be a folder on the SD card: no `/\:*?"<>|`, no leading dot, not empty.
std::string sanitizeFileName(const std::string& s);
std::string humanSize(uint64_t bytes);
std::string lower(std::string s);

// ----- HTTP ----------------------------------------------------------------------------------

struct HttpResult
{
    long status = 0;
    std::string body;
    std::string error; // set when the request never got an answer

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

using Fields = std::vector<std::pair<std::string, std::string>>;

/// One request. `form` is sent as `application/x-www-form-urlencoded`, `json` as JSON (leave one
/// empty). Blocking: call it from the worker.
/// Streams a URL straight into a file, reporting 0..1 as the bytes arrive. Runs on the calling
/// thread: call it from the worker. True when the whole body arrived and the file closed cleanly.
bool downloadFile(const std::string& url, const std::string& path,
    const std::function<void(double)>& progress, std::string& error);

HttpResult httpRequest(const std::string& method, const std::string& url,
    const Fields& form = {}, const std::string& json = "",
    const std::vector<std::string>& headers = {});

/// Downloads to `dest` (written whole only once it arrived). Blocking.
bool httpDownload(const std::string& url, const std::string& dest,
    const std::vector<std::string>& headers, std::string& error);

std::string urlEncode(const std::string& s);

/// Base64, for the profile's textures property (Mojang sends the skin's address inside one).
std::string base64Decode(const std::string& text);

// ----- Log -----------------------------------------------------------------------------------

/// Appends to `root()/launcher-log.txt` and stdout; the only way to know what happened on a
/// console that has no terminal.
void log(const char* format, ...);

// ----- Work off the UI thread ----------------------------------------------------------------

/// Tells a callback posted from the UI whether the screen that posted it still exists: switching
/// tabs frees a tab's views, and the worker may still be downloading.
struct Alive
{
    std::atomic<bool> flag { true };
    bool ok() const { return flag.load(); }
};

using AlivePtr = std::shared_ptr<Alive>;

/// Runs `work` on a worker thread, then `done` on the UI thread. Borealis has no async helper of
/// its own: one repeating task pumps the finished callbacks every frame.
class Worker
{
  public:
    static Worker& get();

    /// Starts the worker thread and the UI pump. Call once, after Application::init.
    void start();

    void post(std::function<void()> work, std::function<void()> done);

    /// Asks the worker to finish and waits for its thread. The homebrew loader reads the next NRO
    /// into this process's heap, and a live thread's stack keeps pages of it locked: the hand-over
    /// ends on the console's error screen without this.
    void stop();

    /// False when the worker thread could not be created: jobs then run inline, which is fine for
    /// a file copy and not for a network wait the UI would freeze on.
    bool hasThread() const;

  private:
    static void threadMain(void* arg);
    void loop();

    bool started = false;
    std::atomic<bool> stopping { false };
};

} // namespace launcher
