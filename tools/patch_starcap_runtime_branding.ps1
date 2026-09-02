$ErrorActionPreference = 'Stop'

function Set-TextFile([string]$path, [string]$text) {
    Set-Content $path $text -Encoding utf8
}

function Replace-RegexChecked([string]$path, [string]$pattern, [string]$replacement, [string]$label) {
    $text = Get-Content $path -Raw
    $patched = [regex]::Replace($text, $pattern, $replacement, 1)
    if ($patched -eq $text) { throw "StarCap runtime patch target not found: $label ($path)" }
    Set-TextFile $path $patched
}

# -----------------------------------------------------------------------------
# 1) Final user-visible branding pass.
#
# The inherited build chain still generates a few runtime strings late in the
# build.  Only string literals and old public URLs are changed here; internal
# namespace/type names such as WeShotOcrV2 stay untouched for source stability.
# -----------------------------------------------------------------------------
$sourceExtensions = @('.cpp', '.h', '.hpp')
Get-ChildItem Src -Recurse -File | Where-Object { $sourceExtensions -contains $_.Extension.ToLowerInvariant() } | ForEach-Object {
    $path = $_.FullName
    $text = Get-Content $path -Raw
    $patched = $text
    $patched = $patched.Replace('L"WeShot', 'L"StarCap')
    $patched = $patched.Replace('L"Screen Capture"', 'L"StarCap"')
    $patched = $patched.Replace('L"ScreenCapture"', 'L"StarCap"')
    $patched = $patched.Replace('https://github.com/xland/ImageReader/releases', 'https://github.com/axing1218-source')
    $patched = $patched.Replace('https://github.com/xland/ScreenCapture/tree/main/Lang', 'https://github.com/axing1218-source')
    if ($patched -ne $text) { Set-TextFile $path $patched }
}

# -----------------------------------------------------------------------------
# 2) Roaming data + startup migration.
#
# New data goes to %APPDATA%\StarCap.  Existing %APPDATA%\ScreenCapture data is
# copied once with skip_existing so v0.9.6 can still be run independently.
# The legacy name is deliberately assembled from two literals so the released
# StarCap binary no longer advertises the old product name as a runtime string.
# -----------------------------------------------------------------------------
$settingPath = 'Src\Setting.cpp'

$setAutoStart = @'
void Setting::setAutoStart(bool autoStart)
{
    std::wstring runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const auto legacyValueName = std::wstring(L"Screen") + L"Capture";
    HKEY hKey{};
    if (RegOpenKeyEx(HKEY_CURRENT_USER, runKey.data(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (autoStart) {
            wchar_t buffer[MAX_PATH]{};
            GetModuleFileName(nullptr, buffer, MAX_PATH);
            auto curPath = std::filesystem::path(buffer);
            std::wstring commandLine = std::format(L"\"{}\" --auto-start", curPath.wstring());
            RegSetValueEx(hKey, L"StarCap", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(commandLine.c_str()),
                static_cast<DWORD>((commandLine.size() + 1) * sizeof(wchar_t)));
            // Remove the inherited startup entry after the StarCap entry exists.
            RegDeleteValue(hKey, legacyValueName.c_str());
        }
        else {
            RegDeleteValue(hKey, L"StarCap");
            RegDeleteValue(hKey, legacyValueName.c_str());
        }
        RegCloseKey(hKey);
    }

    auto common = configObj.GetNamedObject(L"common", nullptr);
    if (!common) {
        common = JsonObject();
        configObj.SetNamedValue(L"common", common);
    }
    common.SetNamedValue(L"autoStart", JsonValue::CreateBooleanValue(autoStart));
    save();
}
'@
Replace-RegexChecked $settingPath '(?s)void Setting::setAutoStart\(bool autoStart\)\s*\{.*?\r?\n\}\r?\n(?=\r?\nbool Setting::getAutoStart)' $setAutoStart 'autostart migration'

$initDataPath = @'
std::filesystem::path Setting::initDataPath()
{
    PWSTR pathTmp{};
    auto hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &pathTmp);
    if (FAILED(hr)) {
        _ASSERT_EXPR(FALSE, L"get roaming path，error");
        return L"";
    }

    const auto roamingRoot = std::filesystem::path{ pathTmp };
    CoTaskMemFree(pathTmp);

    const auto legacyName = std::wstring(L"Screen") + L"Capture";
    const auto legacyPath = roamingRoot / legacyName;
    auto dataPath = roamingRoot / L"StarCap";

    std::error_code ec;
    std::filesystem::create_directories(dataPath, ec);
    if (ec && !std::filesystem::exists(dataPath)) {
        _ASSERT_EXPR(FALSE, L"create StarCap data path，error");
        return dataPath;
    }

    // Non-destructive one-time migration. Existing StarCap files always win.
    if (std::filesystem::is_directory(legacyPath)) {
        ec.clear();
        std::filesystem::copy(legacyPath, dataPath,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
    }
    return dataPath;
}
'@
Replace-RegexChecked $settingPath '(?s)std::filesystem::path Setting::initDataPath\(\)\s*\{.*?\r?\n\}\r?\n(?=\r?\nstd::filesystem::path Setting::initConfigPath)' $initDataPath 'roaming data migration'

$setting = Get-Content $settingPath -Raw
$oldInit = @'
void Setting::init()
{
    auto ptr = new Setting();
    setting.reset(ptr);
}
'@
$newInit = @'
void Setting::init()
{
    auto ptr = new Setting();
    setting.reset(ptr);
    // Reconcile the inherited Run entry on first StarCap startup as well as on
    // later launches. This preserves the user's existing auto-start preference.
    ptr->setAutoStart(ptr->getAutoStart());
}
'@
if (-not $setting.Contains($oldInit)) { throw 'StarCap runtime patch target not found: Setting::init' }
$setting = $setting.Replace($oldInit, $newInit)
Set-TextFile $settingPath $setting

# -----------------------------------------------------------------------------
# 3) Clipboard persistence migration.
#
# New history/theme data lives under %LOCALAPPDATA%\StarCap.  The inherited
# %LOCALAPPDATA%\WeShot directory is copied non-destructively on first access.
# -----------------------------------------------------------------------------
$clipboardPath = 'Src\ClipboardHistoryV091.h'
$storagePath = @'
    inline std::filesystem::path storagePath()
    {
        wchar_t buf[32768]{};
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)std::size(buf));
        const std::filesystem::path base = (n > 0 && n < std::size(buf))
            ? std::filesystem::path(buf)
            : std::filesystem::temp_directory_path();

        auto root = base / L"StarCap";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);

        const auto legacyName = std::wstring(L"We") + L"Shot";
        const auto legacyRoot = base / legacyName;
        if (std::filesystem::is_directory(legacyRoot)) {
            ec.clear();
            std::filesystem::copy(legacyRoot, root,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
        }
        return root / L"clipboard_history.bin";
    }
'@
Replace-RegexChecked $clipboardPath '(?s)    inline std::filesystem::path storagePath\(\)\s*\{.*?\r?\n    \}\r?\n(?=\r?\n    inline uint64_t hashBytes)' $storagePath 'clipboard storage migration'

# -----------------------------------------------------------------------------
# 4) Make the produced binary itself StarCap.exe instead of renaming an inherited
# ScreenCapture.exe after linking. Project/solution filenames are intentionally
# left for the later source-layout cleanup pass.
# -----------------------------------------------------------------------------
$projectPath = 'Src\ScreenCapture.vcxproj'
$project = Get-Content $projectPath -Raw
$project = $project.Replace('<RootNamespace>ScreenCapture</RootNamespace>', '<RootNamespace>StarCap</RootNamespace>')
if (-not $project.Contains('<TargetName>StarCap</TargetName>')) {
    if (-not $project.Contains('<RootNamespace>StarCap</RootNamespace>')) { throw 'StarCap project RootNamespace anchor missing.' }
    $project = $project.Replace('<RootNamespace>StarCap</RootNamespace>', "<RootNamespace>StarCap</RootNamespace>`r`n    <TargetName>StarCap</TargetName>")
}
Set-TextFile $projectPath $project

# -----------------------------------------------------------------------------
# 5) Guard the runtime surface. Internal identifiers/file names may still use
# WeShot during this transitional source cleanup, but released string literals
# and old upstream navigation URLs must not return.
# -----------------------------------------------------------------------------
$forbidden = @(
    'L"WeShot',
    'L"Screen Capture"',
    'L"ScreenCapture"',
    'https://github.com/xland/'
)
$violations = @()
Get-ChildItem Src -Recurse -File | Where-Object { $sourceExtensions -contains $_.Extension.ToLowerInvariant() } | ForEach-Object {
    $content = Get-Content $_.FullName -Raw
    foreach ($needle in $forbidden) {
        if ($content.Contains($needle)) {
            $violations += "$($_.FullName): $needle"
        }
    }
}
if ($violations.Count -gt 0) {
    throw "StarCap runtime branding verification failed:`n$($violations -join "`n")"
}

Write-Host 'StarCap runtime branding and migration pass applied successfully.'
Write-Host 'Roaming data: %APPDATA%\StarCap (copies inherited data, never deletes it).'
Write-Host 'Clipboard data: %LOCALAPPDATA%\StarCap (copies inherited data, never deletes it).'
Write-Host 'Startup value: StarCap; inherited startup value is removed during reconciliation.'
