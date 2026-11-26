#include "auth.hpp"

#include "config.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <thread>

#include <json.hpp>

// Written by packaging/switch-launcher/build.sh: the Azure app this build signs in with, if it was
// given one.
#if defined(__has_include)
#if __has_include(<build_config.hpp>)
#include <build_config.hpp>
#endif
#endif

using nlohmann::json;

namespace launcher
{
namespace auth
{

namespace
{

const char* SCOPE       = "XboxLive.signin offline_access";
const char* MICROSOFT   = "https://login.microsoftonline.com/consumers/oauth2/v2.0";
const char* XBOX_USER   = "https://user.auth.xboxlive.com/user/authenticate";
const char* XSTS        = "https://xsts.auth.xboxlive.com/xsts/authorize";
const char* MINECRAFT   = "https://api.minecraftservices.com";

uint64_t now()
{
    return (uint64_t)::time(nullptr);
}

std::string field(const json& value, const char* key)
{
    auto it = value.find(key);
    if (it == value.end() || !it->is_string())
        return "";
    return it->get<std::string>();
}

/// The game's words for an OAuth failure.
std::string oauthError(const json& value)
{
    std::string code = field(value, "error");
    if (code == "authorization_declined")
        return "Sign-in was declined in the browser.";
    if (code == "expired_token")
        return "The sign-in code expired. Try again.";
    if (code == "bad_verification_code")
        return "The sign-in code was not recognised. Try again.";
    if (code == "invalid_grant")
        return "Your Microsoft sign-in expired. Sign in again.";
    if (code == "invalid_client" || code == "unauthorized_client")
        return "Microsoft refused this build's app registration (client ID).";
    if (!code.empty())
        return "Microsoft sign-in failed (" + code + ").";
    return "Microsoft sign-in failed.";
}

std::string xstsError(int64_t code)
{
    switch (code)
    {
        case 2148916233:
            return "This Microsoft account has no Xbox profile yet. Sign in once at xbox.com to "
                   "create one, then try again.";
        case 2148916235:
            return "Xbox Live is not available in your country or region.";
        case 2148916236:
        case 2148916237:
            return "This account needs adult verification on xbox.com before it can play.";
        case 2148916238:
            return "This is a child account: an adult has to add it to a Microsoft family first.";
        default:
            return "Xbox Live refused the sign-in (error " + std::to_string(code) + ").";
    }
}

/// A request that names its service, never its URL, when the network is down.
HttpResult send(const std::string& method, const std::string& url, const Fields& form,
    const std::string& body, const std::string& bearer, const char* service)
{
    std::vector<std::string> headers;
    if (!bearer.empty())
        headers.push_back("Authorization: Bearer " + bearer);

    HttpResult result = httpRequest(method, url, form, body, headers);
    if (!result.error.empty())
        result.error = std::string("Could not reach ") + service +
            ". Check the console's internet connection.";
    return result;
}

struct MsTokens
{
    std::string access, refresh;
};

struct Code
{
    std::string user_code, verification_uri, device_code;
    uint64_t interval = 5, expires_at = 0;
};

Code deviceCode(const std::string& clientId, std::string& error)
{
    HttpResult result = send("POST", std::string(MICROSOFT) + "/devicecode",
        { { "client_id", clientId }, { "scope", SCOPE } }, "", "", "Microsoft");
    if (!result.error.empty())
    {
        error = result.error;
        return {};
    }

    json value = json::parse(result.body, nullptr, false);
    Code code;
    code.user_code        = field(value, "user_code");
    code.device_code      = field(value, "device_code");
    code.verification_uri = field(value, "verification_uri");
    if (code.verification_uri.empty())
        code.verification_uri = "https://www.microsoft.com/link";
    if (value.contains("interval") && value["interval"].is_number())
        code.interval = std::max<uint64_t>(1, value["interval"].get<uint64_t>());
    code.expires_at = now() + (value.contains("expires_in") && value["expires_in"].is_number()
                                      ? value["expires_in"].get<uint64_t>()
                                      : 900);

    if (result.status != 200 || code.user_code.empty() || code.device_code.empty())
    {
        error = oauthError(value);
        return {};
    }
    return code;
}

bool msTokens(const HttpResult& result, MsTokens& tokens, std::string& error)
{
    json value = json::parse(result.body, nullptr, false);
    tokens.access  = field(value, "access_token");
    tokens.refresh = field(value, "refresh_token");
    if (result.status != 200 || tokens.access.empty() || tokens.refresh.empty())
    {
        error = oauthError(value);
        return false;
    }
    return true;
}

/// Polls until the player signed in, the code expired, or the screen closed.
bool waitForSignIn(const std::string& clientId, const Code& code, State& state,
    MsTokens& tokens, std::string& error)
{
    uint64_t interval = code.interval;
    while (!state.isCancelled())
    {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        if (state.isCancelled())
        {
            error = "Sign-in cancelled.";
            return false;
        }
        if (now() > code.expires_at)
        {
            error = "The sign-in code expired. Try again.";
            return false;
        }

        HttpResult result = send("POST", std::string(MICROSOFT) + "/token",
            { { "grant_type", "urn:ietf:params:oauth:grant-type:device_code" },
                { "client_id", clientId }, { "device_code", code.device_code } },
            "", "", "Microsoft");
        if (!result.error.empty())
        {
            error = result.error;
            return false;
        }

        json value = json::parse(result.body, nullptr, false);
        std::string pending = field(value, "error");
        log("auth: poll %ld %s", result.status, pending.empty() ? "answered" : pending.c_str());
        if (pending == "authorization_pending")
            continue;
        if (pending == "slow_down")
        {
            interval += 5;
            continue;
        }
        return msTokens(result, tokens, error);
    }

    error = "Sign-in cancelled.";
    return false;
}

struct Minecraft
{
    std::string token;
    std::string xuid;
    uint64_t expires_at = 0;
};

bool minecraftLogin(const std::string& msAccess, Minecraft& out, std::string& error)
{
    json body = { { "Properties",
                      { { "AuthMethod", "RPS" },
                          { "SiteName", "user.auth.xboxlive.com" },
                          { "RpsTicket", "d=" + msAccess } } },
        { "RelyingParty", "http://auth.xboxlive.com" }, { "TokenType", "JWT" } };

    HttpResult xbl = send("POST", XBOX_USER, {}, body.dump(), "", "Xbox Live");
    if (!xbl.error.empty())
    {
        error = xbl.error;
        return false;
    }
    json xblValue = json::parse(xbl.body, nullptr, false);
    std::string xblToken = field(xblValue, "Token");
    if (xbl.status != 200 || xblToken.empty())
    {
        error = "Xbox Live refused the sign-in.";
        return false;
    }

    json xstsBody = { { "Properties", { { "SandboxId", "RETAIL" }, { "UserTokens", { xblToken } } } },
        { "RelyingParty", "rp://api.minecraftservices.com/" }, { "TokenType", "JWT" } };
    HttpResult xsts = send("POST", XSTS, {}, xstsBody.dump(), "", "Xbox Live");
    if (!xsts.error.empty())
    {
        error = xsts.error;
        return false;
    }

    json xstsValue = json::parse(xsts.body, nullptr, false);
    if (xstsValue.contains("XErr") && xstsValue["XErr"].is_number())
    {
        error = xstsError(xstsValue["XErr"].get<int64_t>());
        return false;
    }

    std::string xstsToken = field(xstsValue, "Token");
    std::string uhs, xuid;
    if (xstsValue.contains("DisplayClaims") && xstsValue["DisplayClaims"].contains("xui") &&
        xstsValue["DisplayClaims"]["xui"].is_array() && !xstsValue["DisplayClaims"]["xui"].empty())
    {
        const json& claim = xstsValue["DisplayClaims"]["xui"][0];
        uhs               = field(claim, "uhs");
        xuid              = field(claim, "xid");
    }
    if (xsts.status != 200 || xstsToken.empty() || uhs.empty())
    {
        error = "Xbox Live refused the sign-in.";
        return false;
    }

    std::string xtoken = "XBL3.0 x=" + uhs + ";" + xstsToken;
    uint64_t lifetime  = 0;

    // `launcher/login` first, as the game does: a new account's login_with_xbox names no profile.
    HttpResult launcher = send("POST", std::string(MINECRAFT) + "/launcher/login", {},
        json { { "xtoken", xtoken }, { "platform", "PC_LAUNCHER" } }.dump(), "", "the Minecraft services");
    if (!launcher.error.empty())
    {
        error = launcher.error;
        return false;
    }
    json launcherValue = json::parse(launcher.body, nullptr, false);
    std::string token  = field(launcherValue, "access_token");
    if (launcher.status == 200 && !token.empty())
    {
        lifetime = launcherValue.contains("expires_in") && launcherValue["expires_in"].is_number()
            ? launcherValue["expires_in"].get<uint64_t>()
            : 86400;
        out = Minecraft { token, xuid, now() + lifetime };
        return true;
    }
    if (launcher.status != 200)
        log("auth: launcher/login said %ld: %.200s", launcher.status, launcher.body.c_str());

    HttpResult fallback = send("POST", std::string(MINECRAFT) + "/authentication/login_with_xbox",
        {}, json { { "identityToken", xtoken } }.dump(), "", "the Minecraft services");
    if (!fallback.error.empty())
    {
        error = fallback.error;
        return false;
    }
    json fallbackValue = json::parse(fallback.body, nullptr, false);
    token              = field(fallbackValue, "access_token");
    if (fallback.status == 200 && !token.empty())
    {
        lifetime = fallbackValue.contains("expires_in") && fallbackValue["expires_in"].is_number()
            ? fallbackValue["expires_in"].get<uint64_t>()
            : 86400;
        out = Minecraft { token, xuid, now() + lifetime };
        return true;
    }

    if (fallback.status == 403)
    {
        error = "The Minecraft services refused this build: its app is not approved by Mojang yet.";
        return false;
    }
    if (fallback.status == 429)
    {
        error = "Too many sign-ins in a short time. Wait a minute and try again.";
        return false;
    }
    if (fallback.status == 401 || launcher.status == 401)
    {
        // What a first-party Microsoft app gets: Minecraft only accepts applications it has approved,
        // and refuses a first-party one outright.
        error = "The Minecraft services don't accept this application (401). The sign-in's client ID "
                "isn't one Mojang approved for Minecraft: use the same client ID the game uses.";
        return false;
    }
    log("auth: login_with_xbox said %ld: %.200s", fallback.status, fallback.body.c_str());
    error = "The Minecraft services refused the sign-in.";
    return false;
}

/// Whether the account owns Minecraft: Java Edition (Game Pass included).
bool ownsGame(const std::string& token, bool& owned, std::string& error)
{
    HttpResult result = send("GET", std::string(MINECRAFT) + "/entitlements/mcstore", {}, "", token,
        "the Minecraft services");
    if (!result.error.empty())
    {
        error = result.error;
        return false;
    }
    if (result.status != 200)
    {
        error = "Could not check which games this account owns.";
        return false;
    }

    json value = json::parse(result.body, nullptr, false);
    owned      = false;
    if (value.contains("items") && value["items"].is_array())
        for (const json& item : value["items"])
        {
            std::string name = field(item, "name");
            if (name == "game_minecraft" || name == "product_minecraft")
                owned = true;
        }
    return true;
}

/// The profile's name and UUID (without dashes); empty when the account has no profile.
bool profile(const std::string& token, std::string& name, std::string& uuid, std::string& error)
{
    HttpResult result = send("GET", std::string(MINECRAFT) + "/minecraft/profile", {}, "", token,
        "the Minecraft services");
    if (!result.error.empty())
    {
        error = result.error;
        return false;
    }
    if (result.status == 404)
        return true;

    json value = json::parse(result.body, nullptr, false);
    name       = field(value, "name");
    uuid       = field(value, "id");
    if (uuid.find('-') != std::string::npos)
        uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
    for (char& c : uuid)
        c = (char)std::tolower((unsigned char)c);
    if (name.empty() || uuid.empty())
    {
        error = "Could not read the Minecraft profile.";
        return false;
    }
    return true;
}

} // namespace

// ----- State ---------------------------------------------------------------------------------

Phase State::getPhase()
{
    std::lock_guard<std::mutex> guard(lock);
    return phase;
}

std::string State::getUrl()
{
    std::lock_guard<std::mutex> guard(lock);
    return url;
}

std::string State::getCode()
{
    std::lock_guard<std::mutex> guard(lock);
    return code;
}

std::string State::getName()
{
    std::lock_guard<std::mutex> guard(lock);
    return name;
}

std::string State::getError()
{
    std::lock_guard<std::mutex> guard(lock);
    return error;
}

void State::setPhase(Phase value)
{
    std::lock_guard<std::mutex> guard(lock);
    phase = value;
}

void State::setCode(const std::string& newUrl, const std::string& newCode)
{
    std::lock_guard<std::mutex> guard(lock);
    url   = newUrl;
    code  = newCode;
    phase = Phase::Code;
}

void State::fail(const std::string& message)
{
    std::lock_guard<std::mutex> guard(lock);
    error = message;
    phase = Phase::Failed;
}

void State::succeed(const std::string& newName)
{
    std::lock_guard<std::mutex> guard(lock);
    name  = newName;
    phase = Phase::SignedIn;
}

// ----- The flow ------------------------------------------------------------------------------

std::string clientId()
{
    // In order: the launcher's own setting, the game's env.txt (so one file configures both), and
    // last the app the build was given — a build shared with someone else carries none.
    static std::string cached;
    if (!cached.empty())
        return cached;

    cached = trim(Config::get().clientId);

    if (cached.empty())
    {
        std::string text;
        if (readFile(path("env.txt"), text))
        {
            size_t at = text.find("HORIZON_MSA_CLIENT_ID=");
            if (at != std::string::npos)
            {
                size_t end = text.find_first_of("\r\n", at);
                cached = trim(text.substr(at + 21, end == std::string::npos ? std::string::npos : end - at - 21));
            }
        }
    }

#ifdef HORIZON_LAUNCHER_CLIENT_ID
    if (cached.empty() && *HORIZON_LAUNCHER_CLIENT_ID)
        cached = HORIZON_LAUNCHER_CLIENT_ID;
#endif

    return cached;
}

std::shared_ptr<State> start()
{
    auto state = std::make_shared<State>();

    Worker::get().post(
        [state] {
            std::string error;
            std::string client = clientId();
            log("auth: client id %s", client.empty() ? "is missing" : "found");
            if (client.empty())
            {
                state->fail("No Microsoft client ID: set one in Settings, or add "
                            "HORIZON_MSA_CLIENT_ID to switch/horizon/env.txt.");
                return;
            }

            Code code = deviceCode(client, error);
            if (code.user_code.empty())
            {
                log("auth: the device code request failed: %s", error.c_str());
                state->fail(error);
                return;
            }
            log("auth: code %s at %s, every %llu s", code.user_code.c_str(),
                code.verification_uri.c_str(), (unsigned long long)code.interval);

            // The page builds the QR from the address and the code.
            state->setCode(code.verification_uri, code.user_code);
            MsTokens tokens;
            if (!waitForSignIn(client, code, *state, tokens, error))
            {
                if (!state->isCancelled())
                    state->fail(error);
                return;
            }

            state->setPhase(Phase::Finishing);

            Minecraft minecraft;
            if (!minecraftLogin(tokens.access, minecraft, error))
            {
                log("auth: Xbox Live or Minecraft refused the sign-in: %s", error.c_str());
                state->fail(error);
                return;
            }

            bool owned = false;
            if (!ownsGame(minecraft.token, owned, error))
            {
                log("auth: the ownership check failed: %s", error.c_str());
                state->fail(error);
                return;
            }
            if (!owned)
            {
                log("auth: this account owns no Minecraft: Java Edition");
                state->fail("This account doesn't own Minecraft: Java Edition.");
                return;
            }

            std::string name, uuid;
            if (!profile(minecraft.token, name, uuid, error))
            {
                log("auth: the profile could not be read: %s", error.c_str());
                state->fail(error);
                return;
            }
            if (uuid.empty())
            {
                log("auth: the account has no Minecraft profile");
                state->fail("This account has no Minecraft profile yet. Create one at "
                            "minecraft.net, then sign in again.");
                return;
            }

            // The game's own account.json, so it starts signed in.
            json saved = { { "name", name }, { "uuid", uuid }, { "refresh_token", tokens.refresh },
                { "minecraft_token", minecraft.token }, { "expires_at", minecraft.expires_at } };
            if (!minecraft.xuid.empty())
                saved["xuid"] = minecraft.xuid;

            makeDirs(accountsDir());
            writeFileAtomic(Config::get().accountFile(uuid), saved.dump(2));
            writeFileAtomic(path("account.json"), saved.dump(2));

            log("auth: signed in as %s (%s)", name.c_str(), uuid.c_str());
            state->succeed(name);
        },
        [state] {
            // Back on the UI thread: the account list picks up the new file.
            Config::get().refreshAccounts();
            Config::get().save();
        });

    return state;
}

} // namespace auth
} // namespace launcher
