//! Small things every screen uses: dialogs, list rows, the keyboard, the theme, and starting the
//! game.
#pragma once

#include <functional>
#include <string>

#include <borealis.hpp>
#include <nanovg/nanovg.h>

#include "config.hpp"

namespace ui
{

/// Applies the Appearance setting (`system`, `dark` or `light`).
void applyTheme(const std::string& name);

/// A message with one button.
void alert(const std::string& text, const std::string& button = "OK",
    std::function<void()> after = nullptr);

/// A question with a Yes and a No; `onYes` runs on Yes.
void confirm(const std::string& text, std::function<void()> onYes, const std::string& yes = "Yes",
    const std::string& no = "No");

/// A list row that does something when it is picked.
brls::ListItem* item(const std::string& label, const std::string& description,
    std::function<void()> onClick, const std::string& value = "");

/// Text a player only reads.
brls::Label* text(const std::string& value, bool multiline = true, unsigned fontSize = 0,
    NVGalign align = NVG_ALIGN_LEFT);

/// The console's keyboard; `done` runs with what was typed.
void ask(const std::string& header, const std::string& help, const std::string& initial,
    int maxLength, std::function<void(std::string)> done);

/// The picture of an instance, or "" when it has none.
std::string iconPath(const launcher::Instance& instance);

/// The name of the active account, or "" when signed out.
std::string accountName();

/// "Microsoft Account" under the name in the header, or how to get one.
std::string accountSubtitle();

/// Numbers the library keeps to itself that the launcher wants different (the size of a list row's
/// picture, for one). Call once, after the UI library starts.
void initStyle();

/// "34min played", the way the game's own screens say it.
std::string playtimeText(uint64_t seconds);

/// Fetches the active account's skin once, in the background, and keeps it on the SD card: the
/// header draws the player's head from it.
void fetchSkin();

/// The skin's image for nanovg, or 0 when there is none cached yet.
int headTexture(NVGcontext* vg);

/// Starts the game with this instance's folder and arguments. Returns false, with `error` set,
/// when the console's homebrew loader refused (the launcher has to run as an application).
bool launchGame(const launcher::Instance& instance, std::string& error);

} // namespace ui
