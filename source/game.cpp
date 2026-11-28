#include "game.hpp"

#include "sha256.hpp"
#include "util.hpp"

#include <cstdio>

#include <json.hpp>

using nlohmann::json;

namespace launcher
{

namespace
{

/// The port's own repository: the release the PC launcher installs comes from here, with the game as
/// `horizon.nro` and its checksum beside it.
const char* const RELEASE_API =
    "https://api.github.com/repos/KoHaku743/horizon-nx/releases/latest";

} // namespace

bool gameInstalled()
{
    return exists(gamePath());
}

bool latestGameRelease(GameRelease& release, std::string& error)
{
    HttpResult answer = httpRequest("GET", RELEASE_API, {}, "",
        { "Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28" });
    if (!answer.error.empty())
    {
        error = answer.error;
        return false;
    }
    if (answer.status != 200)
    {
        // GitHub answers 403 with a sentence in the body when it is rate-limiting the address, which
        // is the one refusal a player can do nothing about but wait out: say so, not "403".
        json why = json::parse(answer.body, nullptr, false);
        const std::string message = why.is_object() ? why.value("message", "") : "";
        if (answer.status == 403 || answer.status == 429)
            error = "GitHub is refusing requests from this connection right now" +
                (message.empty() ? "." : " (" + message + ").") + " Try again later.";
        else
            error = "GitHub answered " + std::to_string(answer.status) + " for the latest release.";
        return false;
    }

    json value = json::parse(answer.body, nullptr, false);
    if (value.is_discarded() || !value.is_object())
    {
        error = "The latest release could not be read.";
        return false;
    }

    release = GameRelease {};
    release.tag = value.value("tag_name", "");

    for (const json& asset : value.value("assets", json::array()))
    {
        if (!asset.is_object())
            continue;

        const std::string name = asset.value("name", "");
        const std::string url  = asset.value("browser_download_url", "");
        if (name == "horizon.nro")
        {
            release.url   = url;
            release.bytes = asset.value("size", (uint64_t)0);
        }
        else if (name == "horizon.nro.sha256")
            release.shaUrl = url;
    }

    if (release.url.empty())
    {
        error = "The latest release (" + release.tag + ") has no horizon.nro in it.";
        return false;
    }

    const std::string size =
        release.bytes ? ", " + std::to_string(release.bytes / (1024 * 1024)) + " MB" : "";
    log("game: latest release is %s%s", release.tag.c_str(), size.c_str());
    return true;
}

bool installGame(const GameRelease& release, const std::function<void(double)>& progress,
    std::string& error)
{
    // The checksum first: it is a few bytes, and it decides whether the big file arrived intact.
    std::string expected;
    if (!release.shaUrl.empty())
    {
        HttpResult answer = httpRequest("GET", release.shaUrl, {}, "");
        if (!answer.error.empty())
        {
            error = answer.error;
            return false;
        }
        if (answer.status != 200)
        {
            error = "The release's checksum could not be fetched (GitHub answered " +
                std::to_string(answer.status) + ").";
            return false;
        }

        const size_t space = answer.body.find_first_of(" \t\r\n");
        expected = answer.body.substr(0, space == std::string::npos ? answer.body.size() : space);
    }
    else
        log("game: this release publishes no checksum, so the download cannot be verified");

    // Written beside the game and moved into place once it has arrived whole: a download that stops
    // half way never leaves a horizon.nro that the loader would try to start.
    const std::string part = gamePath() + ".part";
    if (!downloadFile(release.url, part, progress, error))
    {
        removeTree(part);
        return false;
    }

    if (!expected.empty())
    {
        const std::string got = sha256FileHex(part);
        if (got != expected)
        {
            removeTree(part);
            error = "The download did not match the release's checksum.";
            log("game: checksum mismatch: wanted %s, got %s", expected.c_str(), got.c_str());
            return false;
        }
        log("game: checksum matches %s", got.c_str());
    }

    // The SD card's filesystem will not rename onto a name that exists, so the old game goes first.
    ::remove(gamePath().c_str());
    if (::rename(part.c_str(), gamePath().c_str()) != 0)
    {
        error = "The game arrived but could not be moved into place.";
        removeTree(part);
        return false;
    }

    log("game: installed %s to %s", release.tag.c_str(), gamePath().c_str());
    return true;
}

} // namespace launcher
