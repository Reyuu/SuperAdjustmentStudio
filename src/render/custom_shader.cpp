#include "custom_shader.h"
#include "custom_shader_catalog.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

static std::string trimAscii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(start, end - start);
}

static std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return (char)::tolower(c);
    });
    return value;
}

static bool startsWithIgnoreCase(const std::string& value, const char* prefix) {
    const std::string lowered = toLowerAscii(value.substr(0, std::strlen(prefix)));
    return lowered == toLowerAscii(std::string(prefix));
}

static bool isIdentifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (unsigned char c : value) {
        if (!std::isalpha(c) && c != '_') {
            return false;
        }
    }
    return true;
}

static std::vector<std::string> splitArguments(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            out.push_back(trimAscii(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    const std::string last = trimAscii(current);
    if (!last.empty() || !out.empty()) {
        out.push_back(last);
    }
    return out;
}

static bool parseFloatStrict(const std::string& text, float& out) {
    if (text.empty()) {
        return false;
    }
    try {
        size_t consumed = 0;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value)) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

static bool parseIntStrict(const std::string& text, int& out) {
    if (text.empty()) {
        return false;
    }
    try {
        size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

static float clampFloat(float value, float min, float max) {
    if (!(max > min)) {
        return value;
    }
    return std::clamp(value, min, max);
}

static std::string lineError(int lineNumber, const std::string& message) {
    return "line " + std::to_string(lineNumber) + ": " + message;
}

namespace {

struct AnnotationContext {
        int line = 0;
        std::string* error = nullptr;
};

bool failAnnotation(const AnnotationContext& context, const std::string& message) {
    *context.error = lineError(context.line, message);
    return false;
}

bool expectFloatArg(const AnnotationContext& context, const std::string& kind, const std::vector<std::string>& args, size_t index, float& out) {
    if (index >= args.size()) {
        return failAnnotation(context, kind + " is missing argument #" + std::to_string(index + 1));
    }
    if (!parseFloatStrict(args[index], out)) {
        return failAnnotation(context, kind + " argument #" + std::to_string(index + 1) + " is not a number: '" + args[index] + "'");
    }
    return true;
}

bool expectIntArg(const AnnotationContext& context, const std::string& kind, const std::vector<std::string>& args, size_t index, int& out) {
    if (index >= args.size()) {
        return failAnnotation(context, kind + " is missing argument #" + std::to_string(index + 1));
    }
    if (!parseIntStrict(args[index], out)) {
        return failAnnotation(context, kind + " argument #" + std::to_string(index + 1) + " is not an integer: '" + args[index] + "'");
    }
    return true;
}

bool requireArgCount(const AnnotationContext& context, const std::string& kind, const std::vector<std::string>& args, size_t minCount, size_t maxCount) {
    if (args[0].empty()) {
        return failAnnotation(context, kind + " needs a name as its first argument");
    }
    if (args.size() < minCount) {
        return failAnnotation(context, kind + " expects at least " + std::to_string(minCount) + " arguments, got " + std::to_string(args.size()));
    }
    if (args.size() > maxCount) {
        return failAnnotation(context, kind + " expects at most " + std::to_string(maxCount) + " arguments, got " + std::to_string(args.size()));
    }
    return true;
}

// radio / combo / listbox: name, items..., default index
bool parseItemKind(const AnnotationContext& context, const std::string& kind, CustomShaderParamKind paramKind, const std::vector<std::string>& args,
                   CustomShaderParam& param) {
    if (!requireArgCount(context, kind, args, 3, 512)) {
        return false;
    }
    const std::string indexText = args.back();
    const std::vector<std::string> items(args.begin() + 1, args.end() - 1);
    int defaultIndex = 0;
    if (!parseIntStrict(indexText, defaultIndex)) {
        return failAnnotation(context, kind + " needs an integer default index as its last argument, got '" + indexText + "'");
    }
    if (defaultIndex < 0 || defaultIndex >= (int)items.size()) {
        return failAnnotation(context, kind + " default index " + std::to_string(defaultIndex) + " is outside 0.." + std::to_string(items.size() - 1));
    }
    param.kind = paramKind;
    param.name = args[0];
    param.items = items;
    param.intDef = defaultIndex;
    return true;
}

bool parseAnnotation(const AnnotationContext& context, const std::string& kind, const std::vector<std::string>& args, CustomShaderParam& param) {
    param.name = args.empty() ? std::string() : args[0];

    if (kind == "slider" || kind == "drag") {
        if (!requireArgCount(context, kind, args, 4, 5)) {
            return false;
        }
        float min = 0.0f;
        float max = 1.0f;
        float def = 0.0f;
        float step = 0.0f;
        if (!expectFloatArg(context, kind, args, 1, min) || !expectFloatArg(context, kind, args, 2, max) || !expectFloatArg(context, kind, args, 3, def)) {
            return false;
        }
        if (!(max > min)) {
            return failAnnotation(context, kind + " needs max > min, got min=" + args[1] + " max=" + args[2]);
        }
        if (args.size() == 5 && !expectFloatArg(context, kind, args, 4, step)) {
            return false;
        }
        param.kind = (kind == "slider") ? CustomShaderParamKind::Slider : CustomShaderParamKind::Drag;
        param.min = min;
        param.max = max;
        param.def = clampFloat(def, min, max);
        param.step = step > 0.0f ? step : (max - min) / 100.0f;
        return true;
    }

    if (kind == "input") {
        if (!requireArgCount(context, kind, args, 2, 3)) {
            return false;
        }
        float def = 0.0f;
        float step = 0.0f;
        if (!expectFloatArg(context, kind, args, 1, def)) {
            return false;
        }
        if (args.size() == 3 && !expectFloatArg(context, kind, args, 2, step)) {
            return false;
        }
        param.kind = CustomShaderParamKind::Input;
        param.def = def;
        param.step = step > 0.0f ? step : 0.01f;
        return true;
    }

    if (kind == "dragint") {
        if (!requireArgCount(context, kind, args, 4, 4)) {
            return false;
        }
        int min = 0;
        int max = 1;
        int def = 0;
        if (!expectIntArg(context, kind, args, 1, min) || !expectIntArg(context, kind, args, 2, max) || !expectIntArg(context, kind, args, 3, def)) {
            return false;
        }
        if (max <= min) {
            return failAnnotation(context, kind + " needs max > min, got min=" + args[1] + " max=" + args[2]);
        }
        param.kind = CustomShaderParamKind::DragInt;
        param.min = (float)min;
        param.max = (float)max;
        param.intDef = std::clamp(def, min, max);
        return true;
    }

    if (kind == "inputint") {
        if (!requireArgCount(context, kind, args, 2, 3)) {
            return false;
        }
        int def = 0;
        int step = 0;
        if (!expectIntArg(context, kind, args, 1, def)) {
            return false;
        }
        if (args.size() == 3 && !expectIntArg(context, kind, args, 2, step)) {
            return false;
        }
        param.kind = CustomShaderParamKind::InputInt;
        param.intDef = def;
        param.step = step > 0 ? (float)step : 1.0f;
        return true;
    }

    if (kind == "toggle" || kind == "checkbox") {
        if (!requireArgCount(context, kind, args, 2, 2)) {
            return false;
        }
        float def = 0.0f;
        if (!expectFloatArg(context, kind, args, 1, def)) {
            return false;
        }
        param.kind = CustomShaderParamKind::Toggle;
        param.def = (def != 0.0f) ? 1.0f : 0.0f;
        param.min = 0.0f;
        param.max = 1.0f;
        return true;
    }

    if (kind == "radio") {
        return parseItemKind(context, kind, CustomShaderParamKind::Radio, args, param);
    }
    if (kind == "combo") {
        return parseItemKind(context, kind, CustomShaderParamKind::Combo, args, param);
    }
    if (kind == "listbox") {
        return parseItemKind(context, kind, CustomShaderParamKind::ListBox, args, param);
    }

    if (kind == "color") {
        if (!requireArgCount(context, kind, args, 4, 5)) {
            return false;
        }
        const size_t components = args.size() == 5 ? size_t(4) : size_t(3);
        std::array<float, 4> value = {0.0f, 0.0f, 0.0f, 1.0f};
        for (size_t i = 0; i < components; ++i) {
            if (!expectFloatArg(context, kind, args, i + 1, value[i])) {
                return false;
            }
        }
        param.kind = CustomShaderParamKind::Color;
        param.hasAlpha = args.size() == 5;
        param.colorDef = {clampFloat(value[0], 0.0f, 1.0f), clampFloat(value[1], 0.0f, 1.0f), clampFloat(value[2], 0.0f, 1.0f),
                          clampFloat(value[3], 0.0f, 1.0f)};
        return true;
    }

    return failAnnotation(context,
                          "unknown annotation '" + kind + "' (slider, drag, input, dragInt, inputInt, toggle, checkbox, radio, combo, listbox, color)");
}

} // namespace

bool parseCustomShaderAnnotations(const std::string& source, std::vector<CustomShaderParam>& outParams, int& outFloatCount, int& outIntCount,
                                  int& outColorCount, std::string& outError) {
    outParams.clear();
    outFloatCount = 0;
    outIntCount = 0;
    outColorCount = 0;
    outError.clear();

    size_t position = 0;
    int lineNumber = 0;
    while (position <= source.size()) {
        const size_t newline = source.find('\n', position);
        const std::string line = source.substr(position, newline == std::string::npos ? std::string::npos : newline - position);
        ++lineNumber;

        const size_t commentStart = line.find("//");
        if (commentStart != std::string::npos) {
            const std::string comment = trimAscii(line.substr(commentStart + 2));
            if (startsWithIgnoreCase(comment, "sas:")) {
                const std::string body = trimAscii(comment.substr(4));
                const size_t open = body.find('(');
                if (open == std::string::npos) {
                    outError = lineError(lineNumber, "expected '(' after the annotation kind: '" + body + "'");
                    return false;
                }
                const size_t close = body.rfind(')');
                if (close == std::string::npos || close < open) {
                    outError = lineError(lineNumber, "missing ')' in annotation: '" + body + "'");
                    return false;
                }
                const std::string kind = toLowerAscii(trimAscii(body.substr(0, open)));
                if (!isIdentifier(kind)) {
                    outError = lineError(lineNumber, "annotation kind must be a word, got '" + kind + "'");
                    return false;
                }

                const std::vector<std::string> args = splitArguments(body.substr(open + 1, close - open - 1));
                CustomShaderParam param;
                AnnotationContext context;
                context.line = lineNumber;
                context.error = &outError;
                if (!parseAnnotation(context, kind, args, param)) {
                    return false;
                }
                for (const auto& existing : outParams) {
                    if (existing.name == param.name) {
                        outError = lineError(lineNumber, "duplicate annotation name '" + param.name + "' (names key the stored values)");
                        return false;
                    }
                }
                outParams.push_back(param);
            }
        }

        if (newline == std::string::npos) {
            break;
        }
        position = newline + 1;
    }

    // per-family indices: float widgets share gParams in annotation order,
    // integer widgets share gIntParams, colors share gColors
    int floatIndex = 0;
    int intIndex = 0;
    int colorIndex = 0;
    for (auto& param : outParams) {
        if (param.usesFloatSlot()) {
            param.floatIndex = floatIndex++;
        } else if (param.usesIntSlot()) {
            param.intIndex = intIndex++;
        } else {
            param.colorIndex = colorIndex++;
        }
    }
    outFloatCount = floatIndex;
    outIntCount = intIndex;
    outColorCount = colorIndex;
    return true;
}

CustomShaderIncludeHandler::CustomShaderIncludeHandler(const std::filesystem::path& mainFile,
                                                       std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>& dependencies,
                                                       std::string& error)
    : dependencies_(dependencies), error_(error) {
    stack_.push_back(mainFile);
}

CustomShaderIncludeHandler::~CustomShaderIncludeHandler() = default;

HRESULT STDMETHODCALLTYPE CustomShaderIncludeHandler::Open(D3D_INCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentData, LPCVOID* data, UINT* dataSize) {
    if (!fileName) {
        error_ = "include without a file name";
        return E_FAIL;
    }

    const std::filesystem::path parent = stack_.empty() ? std::filesystem::path() : stack_.back();
    const std::string name(fileName);
    std::filesystem::path resolved;
    std::string resolveError;
    const std::filesystem::path anchor = parent.empty() ? std::filesystem::current_path() : parent;
    if (!resolveCustomShaderInclude(anchor, name, resolved, resolveError)) {
        error_ = resolveError;
        return E_FAIL;
    }

    std::ifstream in(resolved, std::ios::binary);
    if (!in) {
        error_ = "cannot read include '" + resolved.string() + "'";
        return E_FAIL;
    }
    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    char* copy = new char[contents.size() + 1];
    std::memcpy(copy, contents.c_str(), contents.size() + 1);
    *data = copy;
    // the byte count is the content length, without the terminating nul
    *dataSize = static_cast<UINT>(contents.size());

    std::error_code ec;
    const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(resolved, ec);
    bool known = false;
    for (auto& entry : dependencies_) {
        if (entry.first == resolved) {
            entry.second = ec ? std::filesystem::file_time_type() : stamp;
            known = true;
            break;
        }
    }
    if (!known) {
        dependencies_.emplace_back(resolved, ec ? std::filesystem::file_time_type() : stamp);
    }
    stack_.push_back(resolved);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CustomShaderIncludeHandler::Close(LPCVOID data) {
    delete[] static_cast<char*>(const_cast<void*>(data));
    if (stack_.size() > 1) {
        stack_.pop_back();
    }
    return S_OK;
}

namespace {

struct CbufferField {
        std::string type; // float4, int4, float2, float, int
        std::string name;
        int count = 1;
        int offset = 0;
        int size = 0;
};

std::string stripComments(const std::string& source) {
    std::string out;
    out.reserve(source.size());
    size_t i = 0;
    while (i < source.size()) {
        if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
                ++i;
            }
            i = (i + 1 < source.size()) ? i + 2 : source.size();
            out.push_back('\n');
            continue;
        }
        out.push_back(source[i]);
        ++i;
    }
    return out;
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// "float4 gParams[SAS_NUM_FLOATS]" -> type float4, name gParams, count resolved
bool parseFieldDeclaration(const std::string& declaration, const int& floatSlots, const int& intSlots, const int& colorSlots, CbufferField& out,
                           std::string& error) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : declaration) {
        if (isSpace(c) || c == '[' || c == ']' || c == ',') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            if (c == '[' || c == ']') {
                tokens.push_back(std::string(1, c));
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    if (tokens.size() < 2) {
        error = "cannot read the declaration '" + trimAscii(declaration) + "'";
        return false;
    }

    out.type = toLowerAscii(tokens[0]);
    if (out.type != "float4" && out.type != "int4" && out.type != "float2" && out.type != "float" && out.type != "int") {
        error = "unsupported cbuffer type '" + tokens[0] + "' (the contract uses float4/int4/float2/float/int)";
        return false;
    }

    size_t index = 1;
    if (index < tokens.size() && tokens[index] == "[") {
        // array: name is absent here, it follows the brackets
        if (index + 2 >= tokens.size() || tokens[index + 2] != "]") {
            error = "malformed array declaration '" + trimAscii(declaration) + "'";
            return false;
        }
        const std::string sizeToken = tokens[index + 1];
        if (sizeToken == "SAS_NUM_FLOATS") {
            out.count = floatSlots;
        } else if (sizeToken == "SAS_NUM_INTS") {
            out.count = intSlots;
        } else if (sizeToken == "SAS_NUM_COLORS") {
            out.count = colorSlots;
        } else {
            int literal = 0;
            if (!parseIntStrict(sizeToken, literal)) {
                error = "array size '" + sizeToken + "' must be a literal or one of the SAS_NUM_* macros";
                return false;
            }
            out.count = literal;
        }
        index += 3;
        if (index >= tokens.size()) {
            error = "array declaration '" + trimAscii(declaration) + "' has no name";
            return false;
        }
    }

    out.name = tokens[index];
    ++index;
    if (index < tokens.size() && tokens[index] == "[") {
        if (index + 2 >= tokens.size() || tokens[index + 2] != "]") {
            error = "malformed array declaration '" + trimAscii(declaration) + "'";
            return false;
        }
        const std::string sizeToken = tokens[index + 1];
        if (sizeToken == "SAS_NUM_FLOATS") {
            out.count = floatSlots;
        } else if (sizeToken == "SAS_NUM_INTS") {
            out.count = intSlots;
        } else if (sizeToken == "SAS_NUM_COLORS") {
            out.count = colorSlots;
        } else {
            int literal = 0;
            if (!parseIntStrict(sizeToken, literal)) {
                error = "array size '" + sizeToken + "' must be a literal or one of the SAS_NUM_* macros";
                return false;
            }
            out.count = literal;
        }
        index += 3;
    }
    if (index != tokens.size()) {
        error = "unexpected trailing tokens in '" + trimAscii(declaration) + "'";
        return false;
    }

    out.size = (out.type == "float4" || out.type == "int4") ? 16 * out.count : (out.type == "float2" ? 8 : 4);
    return true;
}

const CbufferField* findField(const std::vector<CbufferField>& fields, const char* name) {
    for (const auto& field : fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

std::string describeField(const CbufferField& field) {
    std::string text = field.type;
    if (field.count > 1) {
        text += "[" + std::to_string(field.count) + "]";
    }
    return text + " " + field.name;
}

std::string describeFields(const std::vector<CbufferField>& fields) {
    std::string out;
    for (const auto& field : fields) {
        if (!out.empty()) {
            out += ", ";
        }
        out += describeField(field);
    }
    return out;
}

int alignUp(int value, int alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

void writeFloat(unsigned char* base, int offset, float value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

void writeInt(unsigned char* base, int offset, int value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

// radio / combo / list box hand the shader the selected index
float indexOfSelection(const CustomShaderParam& param, const CustomShaderOptions& values) {
    const auto it = values.floats.find(param.name);
    const float raw = it != values.floats.end() ? it->second : (float)param.intDef;
    const int maxIndex = (int)param.items.size() - 1;
    return (float)std::clamp((int)std::lround(raw), 0, maxIndex);
}

bool expectType(const std::vector<CbufferField>& fields, const char* name, const char* type, int expectedCount, CbufferField& out, std::string& error) {
    const CbufferField* field = findField(fields, name);
    if (!field) {
        error = "missing " + std::string(name) + " in cbuffer CustomCB (found: " + describeFields(fields) + ")";
        return false;
    }
    if (field->type != type) {
        error = std::string(name) + " must be declared as " + type + ", but it is " + describeField(*field);
        return false;
    }
    if (expectedCount > 0 && field->count != expectedCount) {
        error = std::string(name) + " is declared as " + type + "[" + std::to_string(field->count) + "], expected " + type + "[" +
                std::to_string(expectedCount) + "] (that is the number of annotations SAS found)";
        return false;
    }
    out = *field;
    return true;
}

} // namespace

static bool collectConstantBufferFields(const std::string& source, int floatSlots, int intSlots, int colorSlots, std::vector<CbufferField>& fields,
                                        std::string& error) {
    fields.clear();

    const std::string code = stripComments(source);
    const size_t cbufferAt = toLowerAscii(code).find("cbuffer");
    if (cbufferAt == std::string::npos) {
        error = "no cbuffer found (the contract needs `cbuffer CustomCB : register(b0) { ... };`)";
        return false;
    }

    const size_t open = code.find('{', cbufferAt);
    const size_t close = code.find('}', open == std::string::npos ? cbufferAt : open);
    if (open == std::string::npos || close == std::string::npos) {
        error = "the cbuffer body is not closed";
        return false;
    }

    const std::string header = code.substr(cbufferAt, open - cbufferAt);
    const std::string loweredHeader = toLowerAscii(header);
    const size_t registerAt = loweredHeader.find("register");
    if (registerAt != std::string::npos) {
        const size_t openParen = loweredHeader.find('(', registerAt);
        const size_t closeParen = loweredHeader.find(')', registerAt);
        if (openParen == std::string::npos || closeParen == std::string::npos || closeParen < openParen) {
            error = "cannot read the cbuffer register (expected `register(b0)`)";
            return false;
        }
        std::string slotText = trimAscii(header.substr(openParen + 1, closeParen - openParen - 1));
        if (!slotText.empty() && (slotText[0] == 'b' || slotText[0] == 'B')) {
            slotText.erase(slotText.begin());
        }
        int slot = -1;
        if (!parseIntStrict(slotText, slot)) {
            error = "cannot read the cbuffer register slot '" + slotText + "'";
            return false;
        }
        if (slot != 0) {
            error = "the constant buffer must be declared as register(b0) - SAS only binds b0";
            return false;
        }
    }

    const std::string body = code.substr(open + 1, close - open - 1);
    size_t position = 0;
    while (position <= body.size()) {
        const size_t semicolon = body.find(';', position);
        const std::string declaration = body.substr(position, semicolon == std::string::npos ? std::string::npos : semicolon - position);
        if (!trimAscii(declaration).empty()) {
            CbufferField field;
            std::string fieldError;
            if (!parseFieldDeclaration(declaration, floatSlots, intSlots, colorSlots, field, fieldError)) {
                error = fieldError;
                return false;
            }
            fields.push_back(field);
        }
        if (semicolon == std::string::npos) {
            break;
        }
        position = semicolon + 1;
    }
    if (fields.empty()) {
        error = "the cbuffer declares no variables";
        return false;
    }

    int cursor = 0;
    for (auto& field : fields) {
        if (field.type == "float4" || field.type == "int4") {
            cursor = alignUp(cursor, 16);
        } else {
            cursor = alignUp(cursor, 4);
        }
        field.offset = cursor;
        cursor += field.size;
    }
    return true;
}

bool CustomShaderSlot::parseConstantBuffer(const std::string& source, int floatSlots, int intSlots, int colorSlots, Layout& outLayout, std::string& error) {
    // floatSlots already accounts for the integer widgets when gIntParams is absent
    std::vector<CbufferField> fields;
    if (!collectConstantBufferFields(source, floatSlots, intSlots, colorSlots, fields, error)) {
        return false;
    }

    Layout layout;
    CbufferField field;
    if (!expectType(fields, "gParams", "float4", floatSlots, field, error)) {
        return false;
    }
    layout.paramsOffset = field.offset;

    const CbufferField* intField = findField(fields, "gIntParams");
    if (intField) {
        if (intField->type != "int4") {
            error = "gIntParams must be declared as int4, but it is " + describeField(*intField);
            return false;
        }
        if (intField->count != intSlots) {
            error = "gIntParams is declared as int4[" + std::to_string(intField->count) + "], expected int4[" + std::to_string(intSlots) + "]";
            return false;
        }
        layout.intParamsOffset = intField->offset;
    }

    const CbufferField* colorField = findField(fields, "gColors");
    if (colorField) {
        if (colorField->type != "float4") {
            error = "gColors must be declared as float4, but it is " + describeField(*colorField);
            return false;
        }
        if (colorField->count != colorSlots) {
            error = "gColors is declared as float4[" + std::to_string(colorField->count) + "], expected float4[" + std::to_string(colorSlots) + "]";
            return false;
        }
        layout.colorsOffset = colorField->offset;
    } else if (colorSlots > 1) {
        error = "missing gColors in cbuffer CustomCB (you have color annotations, so declare float4 gColors[SAS_NUM_COLORS]; found: " + describeFields(fields) +
                ")";
        return false;
    }

    if (!expectType(fields, "gTime", "float", 1, field, error)) {
        return false;
    }
    layout.timeOffset = field.offset;

    if (!expectType(fields, "gResolution", "float2", 1, field, error)) {
        return false;
    }
    layout.resolutionOffset = field.offset;

    const CbufferField* flags = findField(fields, "gFlags");
    if (!flags) {
        flags = findField(fields, "gPad");
    }
    if (!flags) {
        error = "missing the trailing int flags word in cbuffer CustomCB (declare `int gFlags;`; found: " + describeFields(fields) + ")";
        return false;
    }
    if (flags->type != "int") {
        error = flags->name + " must be an int, but it is " + describeField(*flags);
        return false;
    }
    layout.flagsOffset = flags->offset;

    int cursor = 0;
    for (const auto& packed : fields) {
        cursor = packed.offset + packed.size;
    }
    layout.size = alignUp(cursor, 16);
    if (layout.size > 4096) {
        error = "the constant buffer needs " + std::to_string(layout.size) + " bytes, above the 4096 byte limit";
        return false;
    }
    outLayout = layout;
    return true;
}

namespace {
// injected when the author does not provide a VS entry point
const char* DEFAULT_VERTEX_SOURCE = R"(
void VS(uint id : SV_VertexID, out float4 outPos : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2((float)((id << 1) & 2u), (float)(id & 2u));
    outPos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
}
)";

// shader model 5 where the device offers it, shader model 4 as the floor
const char* VERTEX_PROFILE_SM5 = "vs_5_0";
const char* PIXEL_PROFILE_SM5 = "ps_5_0";
const char* VERTEX_PROFILE_SM4 = "vs_4_0";
const char* PIXEL_PROFILE_SM4 = "ps_4_0";
} // namespace

CustomShaderSlot::~CustomShaderSlot() {
    shutdown();
}

void CustomShaderSlot::shutdown() {
    pass_.shutdown();
    ready_ = false;
}

bool CustomShaderSlot::load(ID3D11Device* device, const std::filesystem::path& absPath, std::string& error) {
    error.clear();
    path_ = absPath;

    // dependencies drive the hot reload poll, so they are tracked even when the
    // compile below fails
    dependencies_.clear();
    std::error_code stampEc;
    dependencies_.emplace_back(absPath, std::filesystem::last_write_time(absPath, stampEc));
    if (stampEc) {
        dependencies_.back().second = std::filesystem::file_time_type();
    }

    std::ifstream in(absPath, std::ios::binary);
    if (!in) {
        error = "cannot read '" + absPath.string() + "'";
        lastError_ = error;
        // an unreadable file is gone for good: drop the program so the image
        // passes through untouched
        ready_ = false;
        pass_.shutdown();
        Logger->warn("custom_shader: {}", error);
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::vector<CustomShaderParam> params;
    int floatCount = 0;
    int intCount = 0;
    int colorCount = 0;
    std::string parseError;
    if (!parseCustomShaderAnnotations(source, params, floatCount, intCount, colorCount, parseError)) {
        error = absPath.filename().string() + ": " + parseError;
        lastError_ = error;
        // keep any working program so live edits never blank the image
        Logger->warn("custom_shader: {}", error);
        return false;
    }

    // which optional arrays the author declared decides the counts below, and
    // comments must not count, so look at the stripped source
    const std::string declared = stripComments(source);
    const bool hasIntParams = declared.find("gIntParams") != std::string::npos;

    // without a declared gIntParams the integer widgets take gParams slots after
    // the float ones, so they count towards SAS_NUM_FLOATS
    const int effectiveFloatCount = floatCount + (hasIntParams ? 0 : intCount);
    const int floatSlots = effectiveFloatCount > 0 ? effectiveFloatCount : 1;
    const int intSlots = intCount > 0 ? intCount : 1;
    const int colorSlots = colorCount > 0 ? colorCount : 1;
    const std::string withDefines = "#define SAS_NUM_FLOATS " + std::to_string(floatSlots) + "\n#define SAS_NUM_INTS " + std::to_string(intSlots) +
                                    "\n#define SAS_NUM_COLORS " + std::to_string(colorSlots) + "\n" + source;

    std::string includeError;
    CustomShaderIncludeHandler includeHandler(absPath, dependencies_, includeError);

    // hand the compiler the file name
    const std::string sourceName = absPath.filename().string();
    const char* sourceNamePtr = sourceName.c_str();

    // shader model 5 needs feature level 11_0; fall back to 4_0 so the panel
    // still works instead of failing every shader outright
    std::string vertexProfile = VERTEX_PROFILE_SM4;
    std::string pixelProfile = PIXEL_PROFILE_SM4;
    if (device && device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0) {
        vertexProfile = VERTEX_PROFILE_SM5;
        pixelProfile = PIXEL_PROFILE_SM5;
    } else {
        static bool warned = false;
        if (!warned) {
            warned = true;
            Logger->warn("custom_shader: this device does not support shader model 5, falling back to 4_0");
        }
    }
    const char* vertexProfilePtr = vertexProfile.c_str();
    const char* pixelProfilePtr = pixelProfile.c_str();

    // the author may or may not provide a VS entry point
    bool hasVertexShader = false;
    {
        ID3DBlob* probeBlob = nullptr;
        ID3DBlob* probeError = nullptr;
        hasVertexShader = SUCCEEDED(
            D3DCompile(withDefines.data(), withDefines.size(), sourceNamePtr, nullptr, &includeHandler, "VS", vertexProfilePtr, 0, 0, &probeBlob, &probeError));
        if (probeBlob) {
            probeBlob->Release();
        }
        if (probeError) {
            probeError->Release();
        }
    }

    Layout layout;
    {
        ID3DBlob* pixelBlob = nullptr;
        ID3DBlob* pixelError = nullptr;
        const HRESULT hr =
            D3DCompile(withDefines.data(), withDefines.size(), sourceNamePtr, nullptr, &includeHandler, "PS", pixelProfilePtr, 0, 0, &pixelBlob, &pixelError);
        if (FAILED(hr)) {
            std::string message = "compilation failed";
            if (pixelError && pixelError->GetBufferSize() > 0) {
                message = std::string(reinterpret_cast<const char*>(pixelError->GetBufferPointer()), pixelError->GetBufferSize());
            }
            // the include handler knows why an #include failed; fxc only says
            // "failed to open source file", so lead with the real reason
            if (!includeError.empty()) {
                message = includeError + " (" + message + ")";
            }
            if (pixelBlob) {
                pixelBlob->Release();
            }
            if (pixelError) {
                pixelError->Release();
            }
            error = absPath.filename().string() + ": " + message;
            lastError_ = error;
            Logger->error("custom_shader: {}", error);
            return false;
        }
        if (pixelError) {
            pixelError->Release();
        }
        pixelBlob->Release();
    }

    if (!parseConstantBuffer(source, floatSlots, intSlots, colorCount > 0 ? colorSlots : 0, layout, error)) {
        error = absPath.filename().string() + ": " + error;
        lastError_ = error;
        Logger->error("custom_shader: {}", error);
        return false;
    }

    if (!includeError.empty()) {
        error = absPath.filename().string() + ": " + includeError;
        lastError_ = error;
        Logger->error("custom_shader: {}", error);
        return false;
    }

    std::vector<PassShader> shaders;
    // an author VS wins, otherwise SAS injects the fullscreen triangle
    const std::string vertexSource = hasVertexShader ? withDefines : std::string(DEFAULT_VERTEX_SOURCE);
    shaders.push_back({vertexSource, "VS", vertexProfile});
    shaders.push_back({withDefines, "PS", pixelProfile});

    if (!pass_.initialize(device, shaders, layout.size, "PS", &includeHandler)) {
        pass_.shutdown();
        error = absPath.filename().string() + ": shader initialization failed (see the log for the compiler output)";
        lastError_ = error;
        Logger->error("custom_shader: {}", error);
        return false;
    }

    params_ = std::move(params);
    layout_ = layout;
    cbSize_ = layout.size;
    floatCount_ = floatCount;
    intCount_ = intCount;
    colorCount_ = colorCount;
    lastError_.clear();
    ready_ = true;
    Logger->info("custom_shader: loaded '{}' ({} floats, {} ints, {} colors, {} byte cbuffer)", absPath.filename().string(), floatCount, intCount, colorCount,
                 cbSize_);
    return true;
}

bool CustomShaderSlot::needsRecompile() const {
    for (const auto& dependency : dependencies_) {
        std::error_code ec;
        const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(dependency.first, ec);
        if (ec || stamp != dependency.second) {
            return true;
        }
    }
    return false;
}

void CustomShaderSlot::packConstants(void* dst, const CustomShaderOptions& values, float time, unsigned w, unsigned h, unsigned flags) const {
    if (!dst || cbSize_ == 0) {
        return;
    }
    unsigned char* out = static_cast<unsigned char*>(dst);
    std::memset(out, 0, cbSize_);

    for (const auto& param : params_) {
        if (param.kind == CustomShaderParamKind::Color) {
            if (layout_.colorsOffset < 0) {
                continue;
            }
            std::array<float, 4> color = param.colorDef;
            const auto it = values.colors.find(param.name);
            if (it != values.colors.end()) {
                color[0] = it->second[0];
                color[1] = it->second[1];
                color[2] = it->second[2];
            }
            const int offset = layout_.colorsOffset + param.colorIndex * 16;
            writeFloat(out, offset + 0, clampFloat(color[0], 0.0f, 1.0f));
            writeFloat(out, offset + 4, clampFloat(color[1], 0.0f, 1.0f));
            writeFloat(out, offset + 8, clampFloat(color[2], 0.0f, 1.0f));
            if (param.hasAlpha) {
                writeFloat(out, offset + 12, clampFloat(color[3], 0.0f, 1.0f));
            }
            continue;
        }

        if (param.usesIntSlot()) {
            int value = param.intDef;
            const auto it = values.ints.find(param.name);
            if (it != values.ints.end()) {
                value = it->second;
            }
            if (param.kind == CustomShaderParamKind::DragInt) {
                value = std::clamp(value, (int)param.min, (int)param.max);
            }
            if (layout_.intParamsOffset >= 0) {
                writeInt(out, layout_.intParamsOffset + param.intIndex * 16, value);
            } else {
                // no gIntParams declared: the integer widgets take gParams slots right after the float ones
                writeFloat(out, layout_.paramsOffset + (floatCount_ + param.intIndex) * 16, (float)value);
            }
            continue;
        }

        if (param.floatIndex < 0) {
            continue;
        }
        const int offset = layout_.paramsOffset + param.floatIndex * 16;

        switch (param.kind) {
            case CustomShaderParamKind::Slider: {
                const auto it = values.floats.find(param.name);
                const float value = clampFloat(it != values.floats.end() ? it->second : param.def, param.min, param.max);
                writeFloat(out, offset, value);
                break;
            }
            case CustomShaderParamKind::Drag: {
                const auto it = values.floats.find(param.name);
                const float value = clampFloat(it != values.floats.end() ? it->second : param.def, param.min, param.max);
                writeFloat(out, offset, value);
                break;
            }
            case CustomShaderParamKind::Input: {
                const auto it = values.floats.find(param.name);
                const float value = it != values.floats.end() ? it->second : param.def;
                writeFloat(out, offset, std::isfinite(value) ? value : 0.0f);
                break;
            }
            case CustomShaderParamKind::Toggle: {
                const auto it = values.floats.find(param.name);
                const float value = it != values.floats.end() ? it->second : param.def;
                writeFloat(out, offset, value != 0.0f ? 1.0f : 0.0f);
                break;
            }
            case CustomShaderParamKind::Radio: {
                writeFloat(out, offset, indexOfSelection(param, values));
                break;
            }
            case CustomShaderParamKind::Combo: {
                writeFloat(out, offset, indexOfSelection(param, values));
                break;
            }
            case CustomShaderParamKind::ListBox: {
                writeFloat(out, offset, indexOfSelection(param, values));
                break;
            }
            default: {
                break;
            }
        }
    }

    if (layout_.timeOffset >= 0) {
        writeFloat(out, layout_.timeOffset, std::isfinite(time) ? time : 0.0f);
    }
    if (layout_.resolutionOffset >= 0) {
        writeFloat(out, layout_.resolutionOffset, (float)w);
        writeFloat(out, layout_.resolutionOffset + 4, (float)h);
    }
    if (layout_.flagsOffset >= 0) {
        writeInt(out, layout_.flagsOffset, (int)flags);
    }
}

bool CustomShaderSlot::draw(ID3D11DeviceContext* context, const void* cbData, ID3D11ShaderResourceView* const* inputs, unsigned inputCount,
                            ID3D11RenderTargetView* target, unsigned width, unsigned height) {
    if (!ready_ || !cbData) {
        return false;
    }
    return pass_.draw(context, cbData, cbSize_, inputs, inputCount, target, width, height);
}