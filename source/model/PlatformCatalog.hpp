#pragma once

#include <string>

namespace romm::model {

    // Canonical id for a platform as the server reports it, using the display
    // name as a second opinion when the slug alone doesn't land on a known
    // platform.
    //
    // RomM binds a library folder to a platform, and the two can diverge: a
    // folder can be "Auto-detected" (folder name == canonical slug) or a
    // "Folder alias" (any folder name at all, bound to a real platform). An old
    // instance carries "ps4--1" where a current one has "ps4", and an admin can
    // bind something arbitrary. Whatever the folder is called, RomM still
    // reports the bound platform's name — so falling back to the name recovers
    // the identity that the slug lost.
    std::string ResolvePlatformIdentity(const std::string& slug, const std::string& name);

}
