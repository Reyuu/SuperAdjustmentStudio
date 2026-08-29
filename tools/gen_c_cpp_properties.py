#!/usr/bin/env python3
import json
import os
import sys

root = sys.argv[1].rstrip('\\')
cl = sys.argv[2].replace('\\', '\\\\')

inc = [root + p for p in [
    '/src', '/src/ui_helpers', '/thirdparty/libmdbx',
    '/thirdparty/LExSDKv2/Src', '/thirdparty/LExSDKv2/Src/LESDK',
    '/thirdparty/imgui', '/thirdparty/imgui/backends',
    '/thirdparty/IconFontCppHeaders', '/thirdparty/kiero',
    '/thirdparty/spdlog/include', '/thirdparty/LExSDKv2/External',
    '/build/compile-commands',
]]
cfg = root + '/build/compile-commands/thirdparty/libmdbx/config-cmake.h'

data = {
    "configurations": [{
        "name": "Win32",
        "compileCommands": root + "/compile_commands.json",
        "includePath": inc,
        "defines": ["_DEBUG", "UNICODE", "_UNICODE",
                    'MDBX_CONFIG_H="%s"' % cfg],
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
