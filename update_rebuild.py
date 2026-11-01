with open('build.ps1', 'r', encoding='utf-8') as f:
    text = f.read()

text = text.replace(
    '& $msbuild (Join-Path $root "mod\\HikayeTurkRP.vcxproj") $props /m /v:minimal',
    '& $msbuild (Join-Path $root "mod\\HikayeTurkRP.vcxproj") $props /t:Rebuild /m /v:minimal'
)

with open('build.ps1', 'w', encoding='utf-8', newline='\r\n') as f:
    f.write(text)

print('Updated build.ps1 to use /t:Rebuild')

