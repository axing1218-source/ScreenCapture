$ErrorActionPreference = 'Stop'

function Replace-Checked([ref]$textRef, [string]$old, [string]$new, [string]$label) {
    if (-not $textRef.Value.Contains($old)) { throw "Patch target not found: $label" }
    $textRef.Value = $textRef.Value.Replace($old, $new)
}

# -----------------------------------------------------------------------------
# WeShot v0.9.0 foundation
# 1) Clipboard history gets a real toggle action: one hotkey opens it; pressing
#    the same hotkey while it is visible hides it. The window is kept alive so
#    toggling back is instant and the in-memory history remains intact.
# -----------------------------------------------------------------------------
$path = 'Src\ClipboardHistory.h'
$src = Get-Content $path -Raw
Replace-Checked ([ref]$src) @'
    inline void dispose()
'@ @'
    inline void toggle()
    {
        if (historyWnd && IsWindow(historyWnd) && IsWindowVisible(historyWnd)) {
            ShowWindow(historyWnd, SW_HIDE);
            return;
        }
        show();
    }

    inline void dispose()
'@ 'clipboard toggle function'
Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 2) Generalize the existing single-hotkey implementation. v0.8.x hard-coded
#    every shortcut edit to message id 100, which means adding a second shortcut
#    would silently replace the capture hotkey. Give each action its own id and
#    use per-action defaults for old config.json files that do not have the new key.
# -----------------------------------------------------------------------------
$path = 'Src\Setting.cpp'
$src = Get-Content $path -Raw

Replace-Checked ([ref]$src) @'
#include "App.h"
'@ @'
#include "App.h"
#include "ClipboardHistory.h"
'@ 'Setting clipboard include'

Replace-Checked ([ref]$src) @'
    constexpr int capShortcutMsgId{ 100 };
    // 配置文件的默认内容。空文件、坏 JSON、缺键都拿它兜底，所以这里列出的每一项
    // 都是代码里会直接按名字取的（见 getLang / getAutoStart / initShortcutKeys）
    constexpr std::wstring_view defaultConfig{ LR"""({"common":{"autoStart":false,"language":"zh-CN"},"shortcutKey":{"cap":"Ctrl+Alt+A"}})""" };
'@ @'
    constexpr int capShortcutMsgId{ 100 };
    constexpr int clipboardShortcutMsgId{ 101 };

    int shortcutMsgId(const std::wstring& type)
    {
        if (type == L"cap") return capShortcutMsgId;
        if (type == L"clipboard") return clipboardShortcutMsgId;
        return 0;
    }

    std::wstring shortcutDefault(const std::wstring& type)
    {
        if (type == L"cap") return L"Ctrl+Alt+A";
        // Do not take Win+V from the Windows clipboard and do not take
        // Ctrl+Shift+V from applications that use it for plain-text paste.
        if (type == L"clipboard") return L"Ctrl+Alt+V";
        return L"";
    }

    // 配置文件的默认内容。旧版 config.json 没有 clipboard 键时，getShortcutKey
    // 仍会回落到 Ctrl+Alt+V，因此升级不会要求用户删配置重来。
    constexpr std::wstring_view defaultConfig{ LR"""({"common":{"autoStart":false,"language":"zh-CN"},"shortcutKey":{"cap":"Ctrl+Alt+A","clipboard":"Ctrl+Alt+V"}})""" };
'@ 'shortcut ids and defaults'

$setPattern = '(?s)void Setting::setShortcutKey\(const std::wstring& type, const std::vector<std::wstring>& keys\)\r?\n\{.*?\r?\n\}\r?\n\r?\nstd::wstring Setting::getShortcutKey'
$setReplacement = @'
void Setting::setShortcutKey(const std::wstring& type, const std::vector<std::wstring>& keys)
{
    std::wstring str;
    for (size_t i = 0; i < keys.size(); i++)
    {
        str += L"+" + keys[i];
    }
    if (!str.empty()) str.erase(0, 1);

    auto shortcutKey = configObj.GetNamedObject(L"shortcutKey", nullptr);
    if (!shortcutKey) {
        shortcutKey = JsonObject();
        configObj.SetNamedValue(L"shortcutKey", shortcutKey);
    }
    shortcutKey.SetNamedValue(type, JsonValue::CreateStringValue(str));

    const int msgId = shortcutMsgId(type);
    if (msgId != 0) {
        auto app = Ling::App::get();
        app->unRegHotKey(msgId);
        if (!str.empty()) app->regHotKey(str, msgId);
    }
    save();
}

std::wstring Setting::getShortcutKey
'@
$patched = [regex]::Replace($src, $setPattern, $setReplacement, 1)
if ($patched -eq $src) { throw 'Patch target not found: generalized setShortcutKey' }
$src = $patched

$getPattern = '(?s)std::wstring Setting::getShortcutKey\(const std::wstring& type\)\r?\n\{.*?\r?\n\}\r?\n\r?\nvoid Setting::setAutoStart'
$getReplacement = @'
std::wstring Setting::getShortcutKey(const std::wstring& type)
{
    auto obj = configObj.GetNamedObject(L"shortcutKey", nullptr);
    if (!obj) return shortcutDefault(type);
    auto value = std::wstring{ obj.GetNamedString(type, L"") };
    return value.empty() ? shortcutDefault(type) : value;
}

void Setting::setAutoStart
'@
$patched = [regex]::Replace($src, $getPattern, $getReplacement, 1)
if ($patched -eq $src) { throw 'Patch target not found: shortcut default migration' }
$src = $patched

$initPattern = '(?s)void Setting::initShortcutKeys\(\)\r?\n\{.*?\r?\n\}\s*$'
$initReplacement = @'
void Setting::initShortcutKeys()
{
    auto lingApp = Ling::App::get();

    const auto capStr = getShortcutKey(L"cap");
    if (!capStr.empty()) lingApp->regHotKey(capStr, capShortcutMsgId);

    const auto clipboardStr = getShortcutKey(L"clipboard");
    if (!clipboardStr.empty()) lingApp->regHotKey(clipboardStr, clipboardShortcutMsgId);

    lingApp->onHotKey.add([this](UINT msg) {
        if (msg == capShortcutMsgId) {
            WinCap::init();
        }
        else if (msg == clipboardShortcutMsgId) {
            ClipboardHistory::toggle();
        }
    });
    lingApp->onSecondInstance.add([this]() {
        WinCap::init();
    });
}
'@
$patched = [regex]::Replace($src, $initPattern, $initReplacement, 1)
if ($patched -eq $src) { throw 'Patch target not found: init both shortcut keys' }
$src = $patched
Set-Content $path $src -Encoding utf8

# -----------------------------------------------------------------------------
# 3) Show the second configurable hotkey in Settings -> Shortcuts.
# -----------------------------------------------------------------------------
$path = 'Src\Win\WinSettingShortcut.cpp'
$src = Get-Content $path -Raw
Replace-Checked ([ref]$src) @'
    std::vector<std::wstring> keys = { L"cap" };
'@ @'
    std::vector<std::wstring> keys = { L"cap", L"clipboard" };
'@ 'clipboard shortcut settings row'
Set-Content $path $src -Encoding utf8

# Verification: fail CI immediately if a previous patch changes one of these assumptions.
$checks = @(
    @{ Path='Src\ClipboardHistory.h'; Needle='inline void toggle()' },
    @{ Path='Src\ClipboardHistory.h'; Needle='IsWindowVisible(historyWnd)' },
    @{ Path='Src\Setting.cpp'; Needle='constexpr int clipboardShortcutMsgId{ 101 };' },
    @{ Path='Src\Setting.cpp'; Needle='L"Ctrl+Alt+V"' },
    @{ Path='Src\Setting.cpp'; Needle='ClipboardHistory::toggle();' },
    @{ Path='Src\Win\WinSettingShortcut.cpp'; Needle='L"cap", L"clipboard"' }
)
foreach ($check in $checks) {
    $value = Get-Content $check.Path -Raw
    if (-not $value.Contains($check.Needle)) { throw "v0.9.0 verification failed: $($check.Needle)" }
}

Write-Host 'v0.9.0 foundation applied: versioned shortcut architecture + clipboard window toggle.'
