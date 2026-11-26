//! The mod browser: Modrinth's public API, the five kinds the launcher's own browser has, and
//! what on a console can be installed and what can't.
//!
//! Horizon NX runs its own content mods (`.hmod`), resource packs and data packs; it has no Java
//! runtime, so a Java mod is a file it can't load, and its shader packs are Horizon's `.hshader`
//! format, not Iris's. Each result says which of these it is before anything is downloaded.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace launcher
{

enum class Kind
{
    Mod,
    Modpack,
    ResourcePack,
    ShaderPack,
    DataPack,
};

/// The five tabs, in order.
const std::vector<Kind>& allKinds();
const char* kindLabel(Kind kind);
/// The folder inside an instance's game folder the kind installs into, `""` when it can't.
const char* kindTarget(Kind kind);

struct Hit
{
    std::string id, slug, title, author, description;
    uint64_t downloads = 0;
    std::string versions; // the last few game versions it is made for
    std::string verdict;  // "Works", "Partly", "Doesn't run"
    std::string note;     // why, in the game's own words
    bool installable = false;
};

/// Searches Modrinth for one kind. Blocking: call it from the worker.
std::vector<Hit> search(Kind kind, const std::string& query, std::string& error);

/// Downloads a result's newest file, checks it against Modrinth's SHA-512 and puts it in
/// `folder` (created if missing). Blocking.
bool install(const Hit& hit, const std::string& folder, std::string& error);

/// The worlds of one game folder (names under saves/ that hold a level.dat).
std::vector<std::string> worlds(const std::string& gameDir);

} // namespace launcher
