#include "pch.h"
#include "Update.h"

// StarCap v0.9.7 deliberately disables the inherited automatic updater.
// The previous updater downloaded binaries and metadata from the upstream
// ScreenCapture distribution endpoints. StarCap will re-enable updates only
// after its own signed/reviewed release channel and version metadata are ready.
void Update::checkLater()
{
    // Intentionally disabled during the StarCap project-independence migration.
}
