//! Microsoft sign-in on the console: the OAuth device-code flow (a QR code and a code the player
//! enters at microsoft.com/link on a phone), then Xbox Live, XSTS, the Minecraft services, the
//! ownership check and the profile. It follows the game's own sign-in
//! (`crates/horizon-client-render/src/msa.rs`) step for step, and writes the same `account.json`,
//! so the game starts already signed in.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace launcher
{
namespace auth
{

enum class Phase
{
    Contacting, // asking Microsoft for a code
    Code,       // the player enters the code on their phone
    Finishing,  // signed in at Microsoft; Xbox Live and Minecraft run
    SignedIn,
    Failed,
};

/// Where a sign-in is. The worker thread writes it; the UI reads it every frame.
struct State
{
    Phase getPhase();
    std::string getUrl();
    std::string getCode();
    std::string getName();
    std::string getError();
    bool isCancelled() { return cancelled.load(); }

    void setPhase(Phase value);
    void setCode(const std::string& url, const std::string& code);
    void fail(const std::string& error);
    void succeed(const std::string& name);

    std::atomic<bool> cancelled { false };

  private:
    std::mutex lock;
    Phase phase = Phase::Contacting;
    std::string url, code, name, error;
};

/// The Azure app the sign-in uses: built in, or the one the game's env.txt names.
std::string clientId();

/// Starts the whole flow on the worker thread; watch the state it returns.
std::shared_ptr<State> start();

} // namespace auth
} // namespace launcher
