#!/usr/bin/env python3
import json
import os
import sys

root = sys.argv[1].rstrip('\\')
cl = sys.argv[2].replace('\\', '\\\\')

inc = [root + p for p in [
    '/src', '/src/ui_helpers',
    '/thirdparty/LExSDKv2/Src', '/thirdparty/LExSDKv2/Src/LESDK',
    '/thirdparty/LExSDKv2/External',
    '/thirdparty/imgui/backends',
    '/thirdparty/tracy/public',
    '/build/compile-commands',
]]

# auto-discover every thirdparty package instead of hardcoding one path per package,
# so adding/removing a submodule doesn't require touching this script
thirdparty = os.path.join(root, 'thirdparty')
if os.path.isdir(thirdparty):
    for name in sorted(os.listdir(thirdparty)):
        pkg = os.path.join(thirdparty, name)
        if not os.path.isdir(pkg) or name.startswith('.'):
            continue
        inc.append(pkg.replace('\\', '/'))
        for sub in ('include', 'public', 'single_include'):
            subdir = os.path.join(pkg, sub)
            if os.path.isdir(subdir):
                inc.append(subdir.replace('\\', '/'))

data = {
    "configurations": [{
        "name": "Win32",
        "compileCommands": root + "/compile_commands.json",
        "includePath": inc,
        "defines": ["_DEBUG", "UNICODE", "_UNICODE"],
        "intelliSenseMode": "windows-msvc-x64",
        "compilerPath": cl,
        "cppStandard": "c++20",
    }],
    "version": 4,
}

os.makedirs(root + '/.vscode', exist_ok=True)
out = root + '/.vscode/c_cpp_properties.json'
with open(out, 'w') as f:
    json.dump(data, f, indent=4)
print("c_cpp_properties.json written to " + out)
