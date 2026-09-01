#pragma once

// StarCap update hook.
//
// v0.9.7: automatic updating is intentionally disabled while the project moves
// away from the inherited ScreenCapture distribution channel. The public API is
// kept so existing call sites remain source-compatible. A StarCap-owned release
// channel can be wired back in later without touching those call sites.
class Update
{
public:
    static void checkLater();
};
