from pathlib import Path


def replace_exact(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


ocr_path = Path("Src/StarCapOcrV2.h")
text = ocr_path.read_text(encoding="utf-8-sig")
text = replace_exact(text, '#include "GeminiClient.h"', '#include "AIClient.h"', 1, "OCR include")
text = text.replace("GeminiClient::", "AIClient::")
text = text.replace("geminiOcrBlocks", "aiOcrBlocks")
text = text.replace("setGeminiOcrResult", "setAiOcrResult")
text = text.replace("startGeminiTranslation", "startAiTranslation")
settings_old = '''            auto setting = Setting::get();
            auto apiKey = setting ? setting->getGeminiApiKey() : L"";
            auto model = setting ? setting->getGeminiModel() : L"gemini-3.7-flash";'''
settings_new = '''            auto setting = Setting::get();
            auto provider = setting ? setting->getAiProvider() : L"gemini";
            auto apiKey = setting ? setting->getAiApiKey(provider) : L"";
            auto model = setting ? setting->getAiModel(provider) : AIClient::defaultModel(provider);'''
text = replace_exact(text, settings_old, settings_new, 1, "OCR translation settings")
show_old = '''        auto setting = Setting::get();
        auto apiKey = setting ? setting->getGeminiApiKey() : L"";
        auto model = setting ? setting->getGeminiModel() : L"gemini-3.7-flash";'''
show_new = '''        auto setting = Setting::get();
        auto provider = setting ? setting->getAiProvider() : L"gemini";
        auto apiKey = setting ? setting->getAiApiKey(provider) : L"";
        auto model = setting ? setting->getAiModel(provider) : AIClient::defaultModel(provider);'''
text = replace_exact(text, show_old, show_new, 1, "OCR startup settings")
text = replace_exact(
    text,
    "width = imageW, height = imageH, apiKey = std::move(apiKey), model = std::move(model),",
    "width = imageW, height = imageH, provider = std::move(provider), apiKey = std::move(apiKey), model = std::move(model),",
    1,
    "OCR translation thread capture",
)
text = replace_exact(
    text,
    "AIClient::translateOcrBlocks(blocks, apiKey, model)",
    "AIClient::translateOcrBlocks(provider, blocks, apiKey, model)",
    1,
    "OCR text translation call",
)
text = replace_exact(
    text,
    "AIClient::translateImage(imagePixels, width, height, apiKey, model)",
    "AIClient::translateImage(provider, imagePixels, width, height, apiKey, model)",
    1,
    "OCR image translation call",
)
text = replace_exact(
    text,
    "std::thread([pixels = std::move(pixels), width, height, apiKey = std::move(apiKey),",
    "std::thread([pixels = std::move(pixels), width, height, provider = std::move(provider), apiKey = std::move(apiKey),",
    1,
    "OCR recognition thread capture",
)
text = replace_exact(
    text,
    "AIClient::recognizeImage(pixels, width, height, apiKey, model)",
    "AIClient::recognizeImage(provider, pixels, width, height, apiKey, model)",
    1,
    "OCR recognition call",
)
text = text.replace("请先在“设置 > 通用设置”填写 Gemini API Key", "请先在“设置 > 通用设置”填写当前 AI 服务商的 API Key")
text = text.replace("未设置 Gemini API Key，当前使用 Windows OCR", "当前 AI 服务商未设置 API Key，使用 Windows OCR")
text = text.replace("未设置 Gemini API Key，当前使用 Windows OCR；可在设置中填写 Key", "当前 AI 服务商未设置 API Key，使用 Windows OCR；可在设置中填写 Key")
text = text.replace("Gemini 文字识别失败；不会静默退回低准确率 OCR", "AI 文字识别失败；不会静默退回低准确率 OCR")
text = text.replace("Gemini 文字识别完成；可拖选文字复制，点击“翻译”继续", "AI 文字识别完成；可拖选文字复制，点击“翻译”继续")
text = text.replace("正在让 Gemini 识别图片并翻译...", "正在让当前 AI 服务识别图片并翻译...")
ocr_path.write_text(text, encoding="utf-8")

cap_path = Path("Src/StarCapCaptureTranslate.h")
text = cap_path.read_text(encoding="utf-8-sig")
text = replace_exact(text, '#include "GeminiClient.h"', '#include "AIClient.h"', 1, "Capture include")
text = text.replace("GeminiClient::", "AIClient::")
settings_old = '''        auto setting = Setting::get();
        auto apiKey = setting ? setting->getGeminiApiKey() : L"";
        auto model = setting ? setting->getGeminiModel() : L"gemini-3.7-flash";'''
settings_new = '''        auto setting = Setting::get();
        auto provider = setting ? setting->getAiProvider() : L"gemini";
        auto apiKey = setting ? setting->getAiApiKey(provider) : L"";
        auto model = setting ? setting->getAiModel(provider) : AIClient::defaultModel(provider);'''
text = replace_exact(text, settings_old, settings_new, 1, "Capture settings")
text = replace_exact(
    text,
    'MessageBoxW(win->hwnd, L"请先在 设置 > 通用设置 中填写并保存 Gemini API Key。",',
    'MessageBoxW(win->hwnd, L"请先在 设置 > 通用设置 中填写并保存当前 AI 服务商的 API Key。",',
    1,
    "Capture missing-key message",
)
text = replace_exact(
    text,
    "apiKey = std::move(apiKey), model = std::move(model), myRequest, sx, sy, border]() mutable {",
    "provider = std::move(provider), apiKey = std::move(apiKey), model = std::move(model), myRequest, sx, sy, border]() mutable {",
    1,
    "Capture thread capture",
)
text = replace_exact(
    text,
    "AIClient::translateImage(pixels, width, height, apiKey, model)",
    "AIClient::translateImage(provider, pixels, width, height, apiKey, model)",
    1,
    "Capture translate call",
)
text = text.replace('std::wstring(L"Gemini 翻译失败。")', 'std::wstring(L"AI 翻译失败。")')
cap_path.write_text(text, encoding="utf-8")

ai_path = Path("Src/AIClient.h")
text = ai_path.read_text(encoding="utf-8-sig")
if "#include <cmath>" not in text:
    text = text.replace("#include <algorithm>\n", "#include <algorithm>\n#include <cmath>\n#include <cwctype>\n", 1)
ai_path.write_text(text, encoding="utf-8")

print("AI provider call-site migration completed.")
