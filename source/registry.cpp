#include "registry.hpp"

#include "sha512.hpp"
#include "util.hpp"

#include <json.hpp>

using nlohmann::json;

namespace launcher
{

namespace
{

const char* API = "https://api.modrinth.com/v2";

std::string facet(Kind kind)
{
    switch (kind)
    {
        case Kind::Mod:
            return "mod";
        case Kind::Modpack:
            return "modpack";
        case Kind::ResourcePack:
            return "resourcepack";
        case Kind::ShaderPack:
            return "shader";
        case Kind::DataPack:
            return "datapack";
    }
    return "mod";
}

/// What each kind does on a console: the words the game's own screens use.
void explain(Kind kind, Hit& hit)
{
    switch (kind)
    {
        case Kind::ResourcePack:
            hit.verdict     = "Works";
            hit.note        = "A resource pack loads as it does in the game's Resource Packs screen.";
            hit.installable = true;
            break;
        case Kind::DataPack:
            hit.verdict     = "Partly";
            hit.note        = "Functions, function tags, predicates and item modifiers run; recipes, "
                              "loot tables and worldgen don't yet.";
            hit.installable = true;
            break;
        case Kind::Mod:
            hit.verdict     = "Doesn't run";
            hit.note        = "This is a Java mod: Horizon has no Java runtime. It only runs after "
                              "Horizon ports it on a computer.";
            break;
        case Kind::Modpack:
            hit.verdict     = "Doesn't run";
            hit.note        = "A modpack has to be imported and ported on a computer first.";
            break;
        case Kind::ShaderPack:
            hit.verdict     = "Doesn't run";
            hit.note        = "Horizon runs its own .hshader packs; this is Iris/OptiFine format.";
            break;
    }
}

std::string joinVersions(const json& value, Kind kind)
{
    if (!value.is_array())
        return "";

    // The newest few, as the row shows them.
    std::vector<std::string> seen;
    for (auto it = value.rbegin(); it != value.rend() && seen.size() < 3; ++it)
        if (it->is_string())
            seen.push_back(it->get<std::string>());

    std::string out;
    for (size_t i = seen.size(); i-- > 0;)
    {
        if (!out.empty())
            out += ", ";
        out += seen[i];
    }

    if (kind == Kind::Mod && !out.empty())
        out += " · Java";
    return out;
}

/// The newest version's main file, with the checksum Modrinth lists for it.
struct File
{
    std::string name, url, sha512;
    uint64_t size = 0;
};

bool newestFile(const std::string& id, File& file, std::string& error)
{
    HttpResult result = httpRequest("GET", std::string(API) + "/project/" + id + "/version", {}, "",
        { "Accept: application/json" });
    if (!result.error.empty())
    {
        error = result.error;
        return false;
    }
    if (!result.ok())
    {
        error = "Modrinth did not answer for this project.";
        return false;
    }

    json versions = json::parse(result.body, nullptr, false);
    if (versions.is_discarded() || !versions.is_array() || versions.empty())
    {
        error = "This project has no files.";
        return false;
    }

    // The newest by date; the API lists them newest first but doesn't promise it.
    const json* newest = nullptr;
    for (const json& version : versions)
    {
        if (newest == nullptr || version.value("date_published", "") > newest->value("date_published", ""))
            newest = &version;
    }

    const json& files = (*newest)["files"];
    if (!files.is_array() || files.empty())
    {
        error = "This project has no files.";
        return false;
    }

    const json* chosen = &files[0];
    for (const json& candidate : files)
        if (candidate.value("primary", false))
            chosen = &candidate;

    file.name = chosen->value("filename", "");
    file.url  = chosen->value("url", "");
    file.size = chosen->value("size", 0ULL);
    if (chosen->contains("hashes") && (*chosen)["hashes"].contains("sha512"))
        file.sha512 = (*chosen)["hashes"]["sha512"].get<std::string>();

    if (file.url.empty() || file.name.empty())
    {
        error = "This project has no file to download.";
        return false;
    }
    if (file.sha512.empty())
    {
        error = "Modrinth lists no checksum for this file, so it is not installed.";
        return false;
    }
    return true;
}

} // namespace

const std::vector<Kind>& allKinds()
{
    static const std::vector<Kind> kinds = { Kind::Mod, Kind::Modpack, Kind::ResourcePack,
        Kind::ShaderPack, Kind::DataPack };
    return kinds;
}

const char* kindLabel(Kind kind)
{
    switch (kind)
    {
        case Kind::Mod:
            return "Mods";
        case Kind::Modpack:
            return "Modpacks";
        case Kind::ResourcePack:
            return "Resource packs";
        case Kind::ShaderPack:
            return "Shader packs";
        case Kind::DataPack:
            return "Data packs";
    }
    return "";
}

const char* kindTarget(Kind kind)
{
    switch (kind)
    {
        case Kind::ResourcePack:
            return "resourcepacks";
        case Kind::DataPack:
            return "datapacks";
        default:
            return "";
    }
}

std::vector<Hit> search(Kind kind, const std::string& query, std::string& error)
{
    std::vector<Hit> hits;

    std::string url = std::string(API) + "/search?limit=20&index=downloads&query=" +
        urlEncode(trim(query)) + "&facets=" + urlEncode("[[\"project_type:" + facet(kind) + "\"]]");

    HttpResult result = httpRequest("GET", url, {}, "", { "Accept: application/json" });
    if (!result.error.empty())
    {
        error = result.error;
        return hits;
    }
    if (!result.ok())
    {
        error = "Modrinth did not answer (" + std::to_string(result.status) + ").";
        return hits;
    }

    json value = json::parse(result.body, nullptr, false);
    if (value.is_discarded() || !value.contains("hits") || !value["hits"].is_array())
    {
        error = "Modrinth's answer could not be read.";
        return hits;
    }

    for (const json& item : value["hits"])
    {
        Hit hit;
        hit.id          = item.value("project_id", "");
        hit.slug        = item.value("slug", "");
        hit.title       = item.value("title", "");
        hit.author      = item.value("author", "");
        hit.description = item.value("description", "");
        hit.downloads   = item.value("downloads", 0ULL);
        if (item.contains("versions"))
            hit.versions = joinVersions(item["versions"], kind);
        if (hit.id.empty() || hit.title.empty())
            continue;

        explain(kind, hit);
        hits.push_back(hit);
    }

    if (hits.empty() && error.empty())
        error = "Nothing found.";
    return hits;
}

bool install(const Hit& hit, const std::string& folder, std::string& error)
{
    if (!hit.installable)
    {
        error = "This kind can't be installed on the console.";
        return false;
    }

    File file;
    if (!newestFile(hit.id, file, error))
        return false;

    if (!makeDirs(folder))
    {
        error = "Could not create " + folder + ".";
        return false;
    }

    std::string part = folder + "/" + file.name + ".part";
    if (!httpDownload(file.url, part, { "Accept: application/octet-stream" }, error))
        return false;

    uint64_t size = 0;
    if (!fileSize(part, size) || (file.size != 0 && size != file.size))
    {
        removeTree(part);
        error = "The download was incomplete.";
        return false;
    }

    std::string digest = sha512FileHex(part);
    if (lower(digest) != lower(file.sha512))
    {
        removeTree(part);
        log("mods: %s hashed %s, Modrinth lists %s", file.name.c_str(), digest.c_str(),
            file.sha512.c_str());
        error = "The file did not match its checksum.";
        return false;
    }

    std::string target = folder + "/" + file.name;
    removeTree(target);
    if (::rename(part.c_str(), target.c_str()) != 0)
    {
        removeTree(part);
        error = "Could not save the file.";
        return false;
    }

    log("mods: installed %s", target.c_str());
    return true;
}

std::vector<std::string> worlds(const std::string& gameDir)
{
    std::vector<std::string> names;
    for (const std::string& name : listDir(gameDir + "/saves"))
        if (exists(gameDir + "/saves/" + name + "/level.dat"))
            names.push_back(name);
    return names;
}

} // namespace launcher
