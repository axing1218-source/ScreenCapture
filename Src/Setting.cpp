#include "pch.h"
#include <include/Ling.h>
#include <dpapi.h>
#include <wincrypt.h>
#include <algorithm>
#include <cwctype>
#include "Setting.h"
#include "Util.h"
#include "Lang.h"
#include "Win/WinCap.h"
#include "App.h"
#include "ClipboardHistory.h"

namespace {
    std::unique_ptr<Setting> setting;
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

    std::wstring normalizeAiProviderId(std::wstring provider)
    {
        std::transform(provider.begin(), provider.end(), provider.begin(),
            [](wchar_t ch) { return (wchar_t)std::towlower(ch); });
        if (provider == L"openai" || provider == L"anthropic" || provider == L"deepseek") return provider;
        return L"gemini";
    }

    std::wstring defaultAiModel(const std::wstring& provider)
    {
        const auto id = normalizeAiProviderId(provider);
        if (id == L"openai") return L"gpt-5.6-luna";
        if (id == L"anthropic") return L"claude-sonnet-5";
        if (id == L"deepseek") return L"deepseek-v4-flash-vision-exp";
        return L"gemini-3.5-flash-lite";
    }

    // 配置文件的默认内容。旧版 config.json 没有 clipboard / ai 键时，各 getter
    // 会使用兼容默认值，因此升级不要求用户删除配置重来。
    constexpr std::wstring_view defaultConfig{ LR"""({"common":{"autoStart":false,"language":"zh-CN"},"shortcutKey":{"cap":"Ctrl+Alt+A","clipboard":"Ctrl+Alt+V"}})""" };

    std::wstring protectText(const std::wstring& value)
    {
        if (value.empty()) return L"";
        DATA_BLOB in{};
        in.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(value.data()));
        in.cbData = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        DATA_BLOB out{};
        if (!CryptProtectData(&in, L"StarCap AI API Key", nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            return L"";
        }

        DWORD chars = 0;
        CryptBinaryToStringW(out.pbData, out.cbData,
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars);
        std::wstring encoded(chars, L'\0');
        if (chars > 0 && CryptBinaryToStringW(out.pbData, out.cbData,
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &chars)) {
            while (!encoded.empty() && encoded.back() == L'\0') encoded.pop_back();
        }
        else {
            encoded.clear();
        }
        LocalFree(out.pbData);
        return encoded;
    }

    std::wstring unprotectText(const std::wstring& encoded)
    {
        if (encoded.empty()) return L"";
        DWORD bytes = 0;
        if (!CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64,
            nullptr, &bytes, nullptr, nullptr) || bytes == 0) return L"";
        std::vector<BYTE> encrypted(bytes);
        if (!CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64,
            encrypted.data(), &bytes, nullptr, nullptr)) return L"";

        DATA_BLOB in{};
        in.pbData = encrypted.data();
        in.cbData = bytes;
        DATA_BLOB out{};
        if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            return L"";
        }
        std::wstring value;
        if (out.cbData >= sizeof(wchar_t)) {
            value.assign(reinterpret_cast<const wchar_t*>(out.pbData), out.cbData / sizeof(wchar_t));
        }
        LocalFree(out.pbData);
        return value;
    }
}

Setting::Setting() :dataPath{ initDataPath() }, configPath{ initConfigPath() }
{
    if (std::filesystem::exists(configPath)) {
        auto content = Ling::Util::readFileText(configPath);
        if (content.empty() || content.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
            configObj = JsonObject::Parse(defaultConfig);
            save();
            return;
        }
        JsonObject obj{ nullptr };
        if (JsonObject::TryParse(content, obj)) {
            configObj = obj;
            return;
        }
        MessageBox(nullptr, L"config.json parse error，use default config", L"StarCap", MB_OK | MB_ICONWARNING);
    }
    configObj = JsonObject::Parse(defaultConfig);
}

Setting::~Setting()
{
}

void Setting::init()
{
    auto ptr = new Setting();
    setting.reset(ptr);
    // Reconcile the inherited Run entry on first StarCap startup as well as on
    // later launches. This preserves the user's existing auto-start preference.
    ptr->setAutoStart(ptr->getAutoStart());
}

void Setting::dispose()
{
    setting.reset();
}

Setting* Setting::get()
{
    return setting.get();
}

std::filesystem::path Setting::getDataPath()
{
    return dataPath; //复制一份路径对象，不允许就地修改
}

const JsonObject Setting::getConfigObj()
{
    return configObj;
}

void Setting::setShortcutKey(const std::wstring& type, const std::vector<std::wstring>& keys)
{
    std::wstring str;
    for (size_t i = 0; i < keys.size(); i++) str += L"+" + keys[i];
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

std::wstring Setting::getShortcutKey(const std::wstring& type)
{
    auto obj = configObj.GetNamedObject(L"shortcutKey", nullptr);
    if (!obj) return shortcutDefault(type);
    auto value = std::wstring{ obj.GetNamedString(type, L"") };
    return value.empty() ? shortcutDefault(type) : value;
}

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

bool Setting::getAutoStart()
{
    auto common = configObj.GetNamedObject(L"common", nullptr);
    return common && common.GetNamedBoolean(L"autoStart", false);
}

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

std::filesystem::path Setting::initConfigPath()
{
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileName(nullptr, buffer, MAX_PATH);
    auto path = std::filesystem::path{ buffer }.parent_path().append(L"config.json");
    if (std::filesystem::exists(path)) return path;
    auto fallback = this->dataPath;
    return fallback.append(L"config.json");
}

void Setting::save()
{
    std::wstring str{ configObj.Stringify() };
    Ling::Util::saveFile(configPath.wstring(), str);
}

std::wstring Setting::getLang()
{
    auto common = configObj.GetNamedObject(L"common", nullptr);
    if (!common) return L"zh-CN";
    return std::wstring{ common.GetNamedString(L"language", L"zh-CN") };
}

void Setting::setLang(const std::wstring& langCode)
{
    auto common = setting->configObj.GetNamedObject(L"common", nullptr);
    if (!common) {
        common = JsonObject();
        setting->configObj.SetNamedValue(L"common", common);
    }
    common.SetNamedValue(L"language", JsonValue::CreateStringValue(langCode));
    setting->save();
    Lang::get()->initLang(langCode);
}

JsonObject Setting::getAiProviderObj(const std::wstring& provider)
{
    auto ai = configObj.GetNamedObject(L"ai", nullptr);
    if (!ai) {
        ai = JsonObject();
        configObj.SetNamedValue(L"ai", ai);
    }
    auto providers = ai.GetNamedObject(L"providers", nullptr);
    if (!providers) {
        providers = JsonObject();
        ai.SetNamedValue(L"providers", providers);
    }
    const auto id = normalizeAiProviderId(provider);
    auto obj = providers.GetNamedObject(id, nullptr);
    if (!obj) {
        obj = JsonObject();
        providers.SetNamedValue(id, obj);
    }
    return obj;
}

std::wstring Setting::getAiProvider()
{
    auto ai = configObj.GetNamedObject(L"ai", nullptr);
    if (!ai) return L"gemini";
    return normalizeAiProviderId(std::wstring{ ai.GetNamedString(L"provider", L"gemini") });
}

void Setting::setAiProvider(const std::wstring& provider)
{
    auto ai = configObj.GetNamedObject(L"ai", nullptr);
    if (!ai) {
        ai = JsonObject();
        configObj.SetNamedValue(L"ai", ai);
    }
    ai.SetNamedValue(L"provider", JsonValue::CreateStringValue(normalizeAiProviderId(provider)));
    save();
}

std::wstring Setting::getAiApiKey(const std::wstring& provider)
{
    const auto id = normalizeAiProviderId(provider);
    auto obj = getAiProviderObj(id);
    auto protectedValue = std::wstring{ obj.GetNamedString(L"apiKeyProtected", L"") };
    if (!protectedValue.empty()) return unprotectText(protectedValue);

    // v0.9.7 stored Gemini directly under the legacy "gemini" object. Read it
    // transparently so existing users do not need to enter their key again.
    if (id == L"gemini") {
        auto legacy = configObj.GetNamedObject(L"gemini", nullptr);
        if (legacy) {
            auto legacyValue = std::wstring{ legacy.GetNamedString(L"apiKeyProtected", L"") };
            if (!legacyValue.empty()) return unprotectText(legacyValue);
        }
    }
    return L"";
}

void Setting::setAiApiKey(const std::wstring& provider, const std::wstring& apiKey)
{
    const auto id = normalizeAiProviderId(provider);
    auto protectedValue = protectText(apiKey);
    auto obj = getAiProviderObj(id);
    obj.SetNamedValue(L"apiKeyProtected", JsonValue::CreateStringValue(protectedValue));

    // Mirror Gemini into the v0.9.7 location so a user can temporarily downgrade
    // without losing access to the previously configured Gemini key.
    if (id == L"gemini") {
        auto legacy = configObj.GetNamedObject(L"gemini", nullptr);
        if (!legacy) {
            legacy = JsonObject();
            configObj.SetNamedValue(L"gemini", legacy);
        }
        legacy.SetNamedValue(L"apiKeyProtected", JsonValue::CreateStringValue(protectedValue));
    }
    save();
}

std::wstring Setting::getAiModel(const std::wstring& provider)
{
    const auto id = normalizeAiProviderId(provider);
    auto obj = getAiProviderObj(id);
    auto model = std::wstring{ obj.GetNamedString(L"model", L"") };
    if (!model.empty()) {
        // Gemini 2.x access is now restricted for many new API users. Do not keep
        // an inherited v0.9.7 2.x selection as the active v0.9.8 default.
        if (id == L"gemini" && model.rfind(L"gemini-2.", 0) == 0) return defaultAiModel(id);
        return model;
    }

    if (id == L"gemini") {
        auto legacy = configObj.GetNamedObject(L"gemini", nullptr);
        if (legacy) {
            model = std::wstring{ legacy.GetNamedString(L"model", L"") };
            if (!model.empty() && model.rfind(L"gemini-2.", 0) != 0) return model;
        }
    }
    return defaultAiModel(id);
}

void Setting::setAiModel(const std::wstring& provider, const std::wstring& model)
{
    const auto id = normalizeAiProviderId(provider);
    const auto value = model.empty() ? defaultAiModel(id) : model;
    auto obj = getAiProviderObj(id);
    obj.SetNamedValue(L"model", JsonValue::CreateStringValue(value));

    if (id == L"gemini") {
        auto legacy = configObj.GetNamedObject(L"gemini", nullptr);
        if (!legacy) {
            legacy = JsonObject();
            configObj.SetNamedValue(L"gemini", legacy);
        }
        legacy.SetNamedValue(L"model", JsonValue::CreateStringValue(value));
    }
    save();
}

std::vector<std::wstring> Setting::getAiModels(const std::wstring& provider)
{
    std::vector<std::wstring> result;
    auto obj = getAiProviderObj(normalizeAiProviderId(provider));
    auto arr = obj.GetNamedArray(L"models", nullptr);
    if (!arr) return result;
    for (uint32_t i = 0; i < arr.Size(); ++i) {
        auto value = std::wstring{ arr.GetStringAt(i) };
        if (!value.empty() && std::find(result.begin(), result.end(), value) == result.end())
            result.push_back(std::move(value));
    }
    return result;
}

void Setting::setAiModels(const std::wstring& provider, const std::vector<std::wstring>& models)
{
    JsonArray arr;
    for (const auto& model : models) {
        if (!model.empty()) arr.Append(JsonValue::CreateStringValue(model));
    }
    auto obj = getAiProviderObj(normalizeAiProviderId(provider));
    obj.SetNamedValue(L"models", arr);
    save();
}

std::wstring Setting::getGeminiApiKey()
{
    return getAiApiKey(L"gemini");
}

void Setting::setGeminiApiKey(const std::wstring& apiKey)
{
    setAiApiKey(L"gemini", apiKey);
}

std::wstring Setting::getGeminiModel()
{
    return getAiModel(L"gemini");
}

void Setting::setGeminiModel(const std::wstring& model)
{
    setAiModel(L"gemini", model);
}

JsonObject Setting::getToolObj(const std::wstring& tool)
{
    auto root = configObj.GetNamedObject(L"toolPin", nullptr);
    if (!root) {
        root = JsonObject();
        configObj.SetNamedValue(L"toolPin", root);
    }
    auto obj = root.GetNamedObject(tool, nullptr);
    if (!obj) {
        obj = JsonObject();
        root.SetNamedValue(tool, obj);
    }
    return obj;
}

bool Setting::getToolFlag(const std::wstring& tool, const std::wstring& key, bool def)
{
    return getToolObj(tool).GetNamedBoolean(key, def);
}

void Setting::setToolFlag(const std::wstring& tool, const std::wstring& key, bool val)
{
    getToolObj(tool).SetNamedValue(key, JsonValue::CreateBooleanValue(val));
    save();
}

float Setting::getToolNum(const std::wstring& tool, const std::wstring& key, float def)
{
    return static_cast<float>(getToolObj(tool).GetNamedNumber(key, def));
}

void Setting::setToolNum(const std::wstring& tool, const std::wstring& key, float val)
{
    getToolObj(tool).SetNamedValue(key, JsonValue::CreateNumberValue(val));
    save();
}

long long Setting::getUpdateCheckDay()
{
    auto common = configObj.GetNamedObject(L"common", nullptr);
    if (!common) return 0;
    return static_cast<long long>(common.GetNamedNumber(L"updateCheckDay", 0));
}

void Setting::setUpdateCheckDay(long long day)
{
    auto common = configObj.GetNamedObject(L"common", nullptr);
    if (!common) return;
    common.SetNamedValue(L"updateCheckDay", JsonValue::CreateNumberValue(static_cast<double>(day)));
    save();
}

void Setting::initShortcutKeys()
{
    auto lingApp = Ling::App::get();

    const auto capStr = getShortcutKey(L"cap");
    if (!capStr.empty()) lingApp->regHotKey(capStr, capShortcutMsgId);

    const auto clipboardStr = getShortcutKey(L"clipboard");
    if (!clipboardStr.empty()) lingApp->regHotKey(clipboardStr, clipboardShortcutMsgId);

    lingApp->onHotKey.add([this](UINT msg) {
        if (msg == capShortcutMsgId) WinCap::init();
        else if (msg == clipboardShortcutMsgId) ClipboardHistory::toggle();
    });
    lingApp->onSecondInstance.add([this]() { WinCap::init(); });
}
