from pathlib import Path

for path in [Path('Src/GeminiClient.h'), Path('Src/AIClient.h')]:
    text = path.read_text(encoding='utf-8-sig')
    old = 'StarCap/0.9.7'
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{path}: expected exactly one {old}, found {count}')
    path.write_text(text.replace(old, 'StarCap/0.9.8'), encoding='utf-8')

version = Path('Src/Version.h').read_text(encoding='utf-8-sig')
if 'STARCAP_DISPLAY_VERSION L"v0.9.8"' not in version:
    raise SystemExit('Version.h is not v0.9.8')
meta = Path('Doc/version.json').read_text(encoding='utf-8-sig')
if '0, 9, 8' not in meta:
    raise SystemExit('Doc/version.json is not v0.9.8')
print('StarCap v0.9.8 preview source finalized.')
