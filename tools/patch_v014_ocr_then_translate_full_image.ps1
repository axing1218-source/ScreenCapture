$ErrorActionPreference = 'Stop'

# v0.8.14
# In the OCR result window, always translate from the complete screenshot image.
# This makes "OCR -> Translate" use the same visual-region/paragraph analysis as
# direct image translation, instead of translating fragmented OCR blocks.

$path = 'Src\WeShotOcrV2.h'
$src = Get-Content $path -Raw

$old = @'
            size_t blockChars = 0;
            for (const auto& b : geminiOcrBlocks) blockChars += b.source.size();
            const bool longImage = imageW > 0 && imageH > imageW * 2;
            const bool blocksLookIncomplete = !originalText.empty() &&
                blockChars * 10 < originalText.size() * 7;
            const bool preferImageTranslation = isLongScreenshotSource || longImage || blocksLookIncomplete;
'@
$new = @'
            // Always re-read the complete image for layout-aware translation.
            // OCR blocks are optimized for text extraction/copying, but can be split by visual
            // lines. Reusing those blocks for translation causes cramped, fragmented Chinese.
            // The full-image path returns paragraph/title regions and uses the same renderer as
            // direct screenshot translation.
            const bool preferImageTranslation = true;
'@
if (-not $src.Contains($old)) { throw 'v0.8.14 target not found: preferImageTranslation decision block' }
$src = $src.Replace($old, $new)

$src = $src.Replace(
    'L"正在翻译完整图片，长截图会从顶部处理到底部..."',
    'L"正在按完整图片版式翻译..."')

Set-Content $path $src -Encoding utf8

$verify = Get-Content $path -Raw
if (-not $verify.Contains('const bool preferImageTranslation = true;')) {
    throw 'v0.8.14 verification failed: full-image translation not forced'
}
if (-not $verify.Contains('正在按完整图片版式翻译')) {
    throw 'v0.8.14 verification failed: status text missing'
}
Write-Host 'v0.8.14 OCR-then-translate full-image layout fix applied.'
