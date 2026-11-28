//! The game itself: where it comes from, and putting it on the console.
//!
//! The launcher ships without the game — one .nro, nothing else to copy over — and fetches the same
//! release the PC launcher installs: `horizon.nro` and the `horizon.nro.sha256` published beside it.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace launcher
{

struct GameRelease
{
    std::string tag;    // the release's version, for saying what was installed
    std::string url;    // horizon.nro
    std::string shaUrl; // horizon.nro.sha256, when the release publishes one
    uint64_t bytes = 0;
};

/// Is there a game to start?
bool gameInstalled();

/// The latest release, as GitHub's API lists it.
bool latestGameRelease(GameRelease& release, std::string& error);

/// Downloads the release, checks it against the published checksum and puts it where launch expects
/// it. `progress` is called with 0..1 as the bytes arrive; both run on the calling thread, so call
/// this from the worker.
bool installGame(const GameRelease& release, const std::function<void(double)>& progress,
    std::string& error);

} // namespace launcher
