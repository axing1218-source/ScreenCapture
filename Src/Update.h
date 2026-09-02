#pragma once

// StarCap update hook.
//
// Update checks are driven exclusively by the latest published GitHub Release
// in axing1218-source/StarCap. Drafts, prereleases and unpublished main-branch
// builds do not trigger user update prompts.
class Update
{
public:
    static void checkLater();
};
