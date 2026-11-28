//! Tests of the launcher's own logic, on a computer: the parts that don't draw anything.
//!
//!   g++ -std=c++17 -DLAUNCHER_HOST_TEST -Iswitch-launcher/source -Iswitch-launcher/third_party \
//!       -o /tmp/launcher-logic switch-launcher/source/util.cpp switch-launcher/source/config.cpp \
//!       switch-launcher/source/sha512.cpp switch-launcher/tests/logic_test.cpp -lcurl
//!   HORIZON_LAUNCHER_ROOT=/tmp/hz-launcher-test /tmp/launcher-logic
#include <cstdlib>
#include <cstdio>
#include <string>

#include "config.hpp"
#include "sha512.hpp"
#include "util.hpp"

using namespace launcher;

static int failures = 0;

#define CHECK(condition)                                                                                              \
    do                                                                                                                \
    {                                                                                                                 \
        if (!(condition))                                                                                             \
        {                                                                                                             \
            ::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                             \
            failures++;                                                                                               \
        }                                                                                                             \
    } while (0)

static std::string writeAccount(const std::string& uuid, const std::string& name)
{
    std::string text = "{\n  \"name\": \"" + name + "\",\n  \"uuid\": \"" + uuid +
        "\",\n  \"refresh_token\": \"r\",\n  \"minecraft_token\": \"m\",\n  \"expires_at\": 1\n}";
    writeFileAtomic(path("account.json"), text);
    return text;
}

int main()
{
    const std::string rootDir = root();
    ::printf("root: %s\n", rootDir.c_str());

    // A first start: one instance, the game's own folder, nothing else.
    Config& config = Config::get();
    config.load();
    CHECK(config.instances.size() == 1);
    CHECK(config.instances[0].id == "default");
    CHECK(config.selected == "default");
    CHECK(gameDirOf("default") == rootDir);
    CHECK(exists(configPath()));
    CHECK(isDir(instancesDir()));
    CHECK(isDir(accountsDir()));

    // Instances keep their names; ids are made unique, lowercase and free of spaces, so they are
    // folders and command line arguments.
    const std::string world = config.add("My World");
    CHECK(world == "my-world");
    const std::string twin = config.add("My World");
    CHECK(twin == "my-world-2");
    CHECK(config.find("my-world")->name == "My World");
    CHECK(isDir(gameDirOf("my-world-2")));
    CHECK(sanitizeFileName("a/b:c*d") == "a-b-c-d");
    CHECK(sanitizeFileName(".hidden") == "hidden");
    CHECK(sanitizeFileName("  ") == "");

    // The command line: argv[0] is the game, then the folder, then the arguments in order.
    config.globalArgs = "--render-distance 8";
    config.find("my-world")->args = "--world Test";
    config.selected   = "my-world";
    std::string line  = config.commandLine(*config.find("my-world"));
    // No quoting: the loader splits this on spaces and takes the first word as the program.
    CHECK(line.find(gamePath() + " --game-dir " + gameDirOf("my-world")) == 0);
    CHECK(line.find('"') == std::string::npos);
    CHECK(line.find("--render-distance 8 --world Test") != std::string::npos);

    // Settings survive being written and read again.
    config.theme = "dark";
    config.showFps = true;
    CHECK(config.save());
    config.instances.clear();
    config.theme.clear();
    config.load();
    CHECK(config.instances.size() == 3);
    CHECK(config.theme == "dark");
    CHECK(config.showFps);
    CHECK(config.selected == "my-world");

    // An account the game signed in is picked up, kept per account, and becomes active.
    writeAccount("069a79f444e94726a5befca90e38aaf5", "Steve");
    config.refreshAccounts();
    CHECK(config.accounts.size() == 1);
    CHECK(config.accounts[0].name == "Steve");
    CHECK(config.account == "069a79f444e94726a5befca90e38aaf5");
    CHECK(exists(config.accountFile("069a79f444e94726a5befca90e38aaf5")));

    // Switching to another account rewrites the game's own file.
    std::string other = writeAccount("11111111111111111111111111111111", "Alex");
    config.refreshAccounts();
    CHECK(config.accounts.size() == 2);
    CHECK(config.activateAccount("069a79f444e94726a5befca90e38aaf5"));
    std::string gameAccount;
    CHECK(readFile(path("account.json"), gameAccount));
    CHECK(gameAccount.find("Steve") != std::string::npos);

    // Signing out deletes its saved sign-in and the game's file.
    config.forgetAccount("069a79f444e94726a5befca90e38aaf5");
    CHECK(!exists(config.accountFile("069a79f444e94726a5befca90e38aaf5")));
    CHECK(!exists(path("account.json")));
    CHECK(config.accounts.size() == 1);

    // Playtime: a run is written down before the hand-over and counted when the launcher next
    // starts. Under half a minute doesn't count, and a run that never ended counts nothing.
    uint64_t before = config.find("my-world")->played;
    config.beginSession("my-world");
    CHECK(Config::get().session == "my-world");
    CHECK(Config::get().sessionStart != 0);
    config.sessionStart -= 5; // five seconds: too short
    config.endSession();
    CHECK(config.find("my-world")->played == before);
    CHECK(config.session.empty());

    config.beginSession("my-world");
    config.sessionStart -= 90;
    config.endSession();
    CHECK(config.find("my-world")->played >= before + 90);
    CHECK(config.find("my-world")->played <= before + 91);

    // An instance can go; the last one can't.
    CHECK(config.remove("my-world-2"));
    CHECK(config.find("my-world-2") == nullptr);
    CHECK(!isDir(gameDirOf("my-world-2")));
    CHECK(config.remove("my-world"));
    CHECK(!config.remove("default"));
    CHECK(config.instances.size() == 1);

    // SHA-512 still answers FIPS 180-4's own vectors.
    CHECK(sha512Hex("abc") ==
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    CHECK(sha512Hex("") ==
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");

    ::printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
