#include "config_manager.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CloseWindow CodexWin32CloseWindow
#define ShowCursor CodexWin32ShowCursor
#define DrawTextA CodexWin32DrawTextA
#define DrawTextW CodexWin32DrawTextW
#define DrawTextExA CodexWin32DrawTextExA
#define DrawTextExW CodexWin32DrawTextExW
#define Rectangle CodexWin32Rectangle
#include <windows.h>
#undef DrawTextEx
#undef DrawText
#undef Rectangle
#undef DrawTextExW
#undef DrawTextExA
#undef DrawTextW
#undef DrawTextA
#undef ShowCursor
#undef CloseWindow

#include "resource.h"
#endif

namespace {

struct Palette {
    Color background = {14, 17, 22, 255};
    Color sidebar = {20, 24, 31, 255};
    Color panel = {25, 30, 38, 255};
    Color panelRaised = {31, 37, 47, 255};
    Color border = {52, 61, 74, 255};
    Color text = {236, 240, 247, 255};
    Color muted = {144, 155, 172, 255};
    Color accent = {103, 180, 255, 255};
    Color accentSoft = {37, 74, 107, 255};
    Color success = {93, 211, 158, 255};
    Color danger = {255, 118, 118, 255};
    Color field = {17, 21, 27, 255};
};

struct AppState {
    ConfigData data;
    ConfigManager manager;
    std::string configPath;
    std::string status = "Ready to apply a provider";
    Color statusColor = {144, 155, 172, 255};
    int selected = 0;
    int editField = -1;
    std::string draft;
    bool dirty = false;
    double statusUntil = 0.0;
};

Font gAppFont = {};
bool gAppFontLoaded = false;
bool gAppFontUsesSdf = false;
Shader gAppFontShader = {};
bool gAppFontShaderLoaded = false;
constexpr float kUiTextScale = 1.28f;
constexpr int kFontAtlasSize = 64;
constexpr int kRoundedSegments = 24;

constexpr const char* kSdfFragmentShader = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

out vec4 finalColor;

void main() {
    float distanceFromOutline = texture(texture0, fragTexCoord).a - 0.5;
    float distanceChangePerFragment = length(vec2(dFdx(distanceFromOutline), dFdy(distanceFromOutline)));
    float alpha = smoothstep(-distanceChangePerFragment, distanceChangePerFragment, distanceFromOutline);
    finalColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
)GLSL";

float scaledTextSize(int size) {
    return static_cast<float>(size) * kUiTextScale;
}

unsigned int readBigEndian32(const unsigned char* data) {
    return (static_cast<unsigned int>(data[0]) << 24U) |
           (static_cast<unsigned int>(data[1]) << 16U) |
           (static_cast<unsigned int>(data[2]) << 8U) |
           static_cast<unsigned int>(data[3]);
}

unsigned int readBigEndian16(const unsigned char* data) {
    return (static_cast<unsigned int>(data[0]) << 8U) |
           static_cast<unsigned int>(data[1]);
}

void writeBigEndian32(unsigned char* data, unsigned int value) {
    data[0] = static_cast<unsigned char>((value >> 24U) & 0xffU);
    data[1] = static_cast<unsigned char>((value >> 16U) & 0xffU);
    data[2] = static_cast<unsigned char>((value >> 8U) & 0xffU);
    data[3] = static_cast<unsigned char>(value & 0xffU);
}

bool extractTrueTypeFace(const unsigned char* collection, int collectionSize,
                         unsigned int fontOffset, std::vector<unsigned char>& face) {
    if (collection == nullptr || collectionSize < 12 || fontOffset > static_cast<unsigned int>(collectionSize - 12)) {
        return false;
    }

    const unsigned int tableCount = readBigEndian16(collection + fontOffset + 4);
    const unsigned int recordsEnd = fontOffset + 12U + tableCount * 16U;
    if (recordsEnd > static_cast<unsigned int>(collectionSize)) {
        return false;
    }

    unsigned int faceEnd = recordsEnd;
    for (unsigned int index = 0; index < tableCount; ++index) {
        const unsigned char* record = collection + fontOffset + 12U + index * 16U;
        const unsigned int tableOffset = readBigEndian32(record + 8);
        const unsigned int tableLength = readBigEndian32(record + 12);
        if (tableOffset < fontOffset || tableOffset > static_cast<unsigned int>(collectionSize) ||
            tableLength > static_cast<unsigned int>(collectionSize) - tableOffset) {
            return false;
        }
        faceEnd = std::max(faceEnd, tableOffset + tableLength);
    }

    face.assign(collection + fontOffset, collection + faceEnd);
    for (unsigned int index = 0; index < tableCount; ++index) {
        const unsigned char* sourceRecord = collection + fontOffset + 12U + index * 16U;
        unsigned char* faceRecord = face.data() + 12U + index * 16U;
        writeBigEndian32(faceRecord + 8, readBigEndian32(sourceRecord + 8) - fontOffset);
    }
    return true;
}

Font loadMicrosoftYaHei(bool useSdf, bool& usesSdf) {
    usesSdf = false;
#ifdef _WIN32
    int dataSize = 0;
    unsigned char* fileData = LoadFileData("C:/Windows/Fonts/msyh.ttc", &dataSize);
    if (fileData == nullptr || dataSize <= 0) {
        return {};
    }

    std::vector<unsigned char> faceData;
    const unsigned char* fontData = fileData;
    int fontDataSize = dataSize;
    if (dataSize >= 16 && std::memcmp(fileData, "ttcf", 4) == 0) {
        const unsigned int fontCount = readBigEndian32(fileData + 8);
        const bool validCollection = fontCount > 0 && fontCount <= 128 &&
            12U + fontCount * 4U <= static_cast<unsigned int>(dataSize);
        if (!validCollection || !extractTrueTypeFace(fileData, dataSize,
                                                       readBigEndian32(fileData + 12), faceData)) {
            UnloadFileData(fileData);
            return {};
        }
        fontData = faceData.data();
        fontDataSize = static_cast<int>(faceData.size());
    }

    Font font = {};
    if (useSdf) {
        font.baseSize = kFontAtlasSize;
        font.glyphPadding = 0;
        font.glyphs = LoadFontData(fontData, fontDataSize, kFontAtlasSize, nullptr, 0,
                                   FONT_SDF, &font.glyphCount);
        if (font.glyphs != nullptr) {
            Image atlas = GenImageFontAtlas(font.glyphs, &font.recs, font.glyphCount,
                                            kFontAtlasSize, 0, 1);
            font.texture = LoadTextureFromImage(atlas);
            UnloadImage(atlas);
        }
    } else {
        font = LoadFontFromMemory(".ttf", fontData, fontDataSize, kFontAtlasSize, nullptr, 0);
    }
    UnloadFileData(fileData);
    if (IsFontValid(font)) {
        SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
        usesSdf = useSdf;
        return font;
    }
    if (font.glyphs != nullptr) {
        UnloadFont(font);
    }
    return {};
#else
    return {};
#endif
}

Color withAlpha(Color color, unsigned char alpha) {
    color.a = alpha;
    return color;
}

void drawText(const std::string& text, int x, int y, int size, Color color) {
    if (gAppFontLoaded) {
        const float textSize = scaledTextSize(size);
        if (gAppFontUsesSdf && gAppFontShaderLoaded) {
            BeginShaderMode(gAppFontShader);
        }
        DrawTextEx(gAppFont, text.c_str(), {static_cast<float>(x), static_cast<float>(y)},
                   textSize, 0.0f, color);
        if (gAppFontUsesSdf && gAppFontShaderLoaded) {
            EndShaderMode();
        }
    } else {
        const int textSize = static_cast<int>(scaledTextSize(size));
        DrawText(text.c_str(), x, y, textSize, color);
    }
}

int measureText(const std::string& text, int size) {
    if (gAppFontLoaded) {
        return static_cast<int>(MeasureTextEx(gAppFont, text.c_str(), scaledTextSize(size), 0.0f).x);
    }
    return MeasureText(text.c_str(), static_cast<int>(scaledTextSize(size)));
}

void drawLabel(const std::string& text, int x, int y, Color color) {
    drawText(text, x, y, 13, color);
}

void drawPanel(Rectangle rectangle, Color color, Color outline = {0, 0, 0, 0}) {
    DrawRectangleRounded(rectangle, 0.08f, kRoundedSegments, color);
    if (outline.a > 0) {
        DrawRectangleRoundedLines(rectangle, 0.08f, kRoundedSegments, outline);
    }
}

bool clicked(Rectangle rectangle) {
    return CheckCollisionPointRec(GetMousePosition(), rectangle) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool hovered(Rectangle rectangle) {
    return CheckCollisionPointRec(GetMousePosition(), rectangle);
}

std::string shortenPath(const std::string& value, std::size_t maxLength = 62) {
    if (value.size() <= maxLength) {
        return value;
    }
    return "..." + value.substr(value.size() - maxLength + 3);
}

bool isDefaultProvider(const Provider& provider) {
    return provider.builtIn || provider.id == "openai";
}

bool isProviderActive(const AppState& app, const Provider& provider) {
    if (isDefaultProvider(provider)) {
        return app.data.activeProvider.empty() || app.data.activeProvider == "openai";
    }
    return provider.id == app.data.activeProvider;
}

bool isFieldEditable(const Provider& provider, int field) {
    return !isDefaultProvider(provider) || field == 2;
}

std::string providerSubtitle(const Provider& provider) {
    if (isDefaultProvider(provider)) {
        return "ChatGPT account login";
    }
    return provider.id;
}

std::string providerValue(const Provider& provider, int field) {
    if (isDefaultProvider(provider)) {
        switch (field) {
            case 0: return "Default";
            case 1: return "openai";
            case 2: return provider.model;
            case 3: return "Built-in OpenAI endpoint";
            case 4: return "ChatGPT account login";
            case 5: return "responses";
            default: return {};
        }
    }
    switch (field) {
        case 0: return provider.name;
        case 1: return provider.id;
        case 2: return provider.model;
        case 3: return provider.baseUrl;
        case 4: return provider.envKey;
        case 5: return provider.wireApi;
        default: return {};
    }
}

void setProviderValue(Provider& provider, int field, const std::string& value) {
    if (!isFieldEditable(provider, field)) {
        return;
    }
    switch (field) {
        case 0: provider.name = value; break;
        case 1: provider.id = value; break;
        case 2: provider.model = value; break;
        case 3: provider.baseUrl = value; break;
        case 4: provider.envKey = value; break;
        case 5: provider.wireApi = value; break;
        default: break;
    }
}

void commitEdit(AppState& app) {
    if (app.editField >= 0 && app.selected >= 0 && app.selected < static_cast<int>(app.data.providers.size())) {
        Provider& provider = app.data.providers[app.selected];
        if (providerValue(provider, app.editField) != app.draft) {
            setProviderValue(provider, app.editField, app.draft);
            app.dirty = true;
        }
    }
    app.editField = -1;
    app.draft.clear();
}

void beginEdit(AppState& app, int field) {
    if (app.selected < 0 || app.selected >= static_cast<int>(app.data.providers.size())) {
        return;
    }
    if (!isFieldEditable(app.data.providers[app.selected], field)) {
        return;
    }
    if (app.editField != field) {
        commitEdit(app);
        app.editField = field;
        app.draft = providerValue(app.data.providers[app.selected], field);
    }
}

void captureTextInput(AppState& app) {
    if (app.editField < 0) {
        return;
    }
    if (IsKeyPressed(KEY_ENTER)) {
        commitEdit(app);
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        app.editField = -1;
        app.draft.clear();
        return;
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_A)) {
        app.draft.clear();
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_V)) {
        const char* clipboard = GetClipboardText();
        if (clipboard != nullptr) {
            app.draft += clipboard;
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !app.draft.empty()) {
        app.draft.pop_back();
    }
    int character = GetCharPressed();
    while (character > 0) {
        if (character >= 32 && character <= 126 && app.draft.size() < 180) {
            app.draft.push_back(static_cast<char>(character));
        }
        character = GetCharPressed();
    }
}

void showStatus(AppState& app, const std::string& message, Color color) {
    app.status = message;
    app.statusColor = color;
    app.statusUntil = GetTime() + 5.0;
}

void saveAndApply(AppState& app) {
    commitEdit(app);
    if (app.data.providers.empty()) {
        showStatus(app, "Add a provider before applying", Palette{}.danger);
        return;
    }
    if (app.selected >= 0 && app.selected < static_cast<int>(app.data.providers.size())) {
        const Provider& provider = app.data.providers[app.selected];
        app.data.activeProvider = isDefaultProvider(provider) ? "" : provider.id;
        if (!provider.model.empty()) {
            app.data.model = provider.model;
        }
    }
    std::string error;
    if (app.manager.save(app.configPath, app.data, error)) {
        app.dirty = false;
        if (app.data.activeProvider.empty()) {
            showStatus(app, "Applied Default; ChatGPT login is active", Palette{}.success);
        } else {
            showStatus(app, "Applied " + app.data.activeProvider + " to config.toml", Palette{}.success);
        }
    } else {
        showStatus(app, error, Palette{}.danger);
    }
}

void addProvider(AppState& app) {
    commitEdit(app);
    Provider provider;
    provider.id = "new-provider";
    int suffix = 2;
    auto idExists = [&app](const std::string& id) {
        return std::any_of(app.data.providers.begin(), app.data.providers.end(), [&id](const Provider& existing) {
            return existing.id == id;
        });
    };
    while (idExists(provider.id)) {
        provider.id = "new-provider-" + std::to_string(suffix++);
    }
    provider.name = "New Provider";
    provider.model = app.data.model.empty() ? "gpt-5" : app.data.model;
    provider.baseUrl = "https://api.example.com/v1";
    provider.envKey = "API_KEY";
    provider.wireApi = "responses";
    app.data.providers.push_back(provider);
    app.selected = static_cast<int>(app.data.providers.size()) - 1;
    app.dirty = true;
    showStatus(app, "New provider added", Palette{}.accent);
}

void deleteProvider(AppState& app) {
    commitEdit(app);
    if (app.selected < 0 || app.selected >= static_cast<int>(app.data.providers.size())) {
        return;
    }
    const Provider& selectedProvider = app.data.providers[app.selected];
    if (isDefaultProvider(selectedProvider)) {
        showStatus(app, "Default provider cannot be removed", Palette{}.danger);
        return;
    }
    const std::string removedName = selectedProvider.name.empty() ? selectedProvider.id : selectedProvider.name;
    const bool removedActive = selectedProvider.id == app.data.activeProvider;
    app.data.providers.erase(app.data.providers.begin() + app.selected);
    app.selected = std::clamp(app.selected, 0, static_cast<int>(app.data.providers.size()) - 1);
    if (removedActive) {
        app.data.activeProvider.clear();
        app.data.model = app.data.providers.front().model;
    }
    app.dirty = true;
    if (removedActive) {
        showStatus(app, "Removed " + removedName + "; Default will be used after save", Palette{}.accent);
    } else {
        showStatus(app, "Removed " + removedName, Palette{}.muted);
    }
}

void drawButton(Rectangle rectangle, const std::string& text, const Palette& palette,
                bool primary = false, bool enabled = true) {
    const bool isHovered = enabled && hovered(rectangle);
    const Color fill = !enabled ? palette.panel :
                       (primary ? (isHovered ? palette.accent : Color{77, 153, 226, 255})
                                : (isHovered ? palette.panelRaised : palette.panel));
    drawPanel(rectangle, fill, !enabled ? withAlpha(palette.border, 120) :
              (primary ? withAlpha(palette.accent, 160) : palette.border));
    const int textWidth = measureText(text, 14);
    const int textHeight = static_cast<int>(scaledTextSize(14));
    drawText(text, static_cast<int>(rectangle.x + (rectangle.width - textWidth) / 2),
             static_cast<int>(rectangle.y + (rectangle.height - textHeight) / 2 - 1), 14,
             !enabled ? palette.muted : (primary ? palette.background : palette.text));
}

void drawSidebar(AppState& app, const Palette& palette, int height) {
    DrawRectangle(0, 0, 304, height, palette.sidebar);
    DrawRectangle(303, 0, 1, height, palette.border);
    drawText("CODEX", 30, 28, 13, palette.accent);
    drawText("API SWITCHER", 30, 49, 23, palette.text);
    drawText("Local provider profiles", 30, 82, 13, palette.muted);
    drawLabel("PROVIDERS", 30, 128, palette.muted);

    int y = 154;
    for (std::size_t index = 0; index < app.data.providers.size(); ++index) {
        const Provider& provider = app.data.providers[index];
        const Rectangle item = {18, static_cast<float>(y), 268, 64};
        const bool selected = static_cast<int>(index) == app.selected;
        if (selected) {
            drawPanel(item, palette.accentSoft, withAlpha(palette.accent, 100));
            DrawRectangle(static_cast<int>(item.x), static_cast<int>(item.y), 3, static_cast<int>(item.height), palette.accent);
        } else if (hovered(item)) {
            drawPanel(item, withAlpha(palette.panelRaised, 180));
        }
        drawText(provider.name.empty() ? provider.id : provider.name, 34, y + 13, 16, palette.text);
        drawText(providerSubtitle(provider), 34, y + 37, 12, palette.muted);
        if (isProviderActive(app, provider)) {
            DrawCircle(264, y + 30, 5, palette.success);
        }
        if (clicked(item)) {
            commitEdit(app);
            app.selected = static_cast<int>(index);
        }
        y += 72;
    }

    const Rectangle addButton = {18, static_cast<float>(height - 104), 268, 42};
    drawButton(addButton, "+  Add provider", palette);
    if (clicked(addButton)) {
        addProvider(app);
    }
    const Rectangle deleteButton = {18, static_cast<float>(height - 54), 268, 34};
    const bool canDelete = app.selected >= 0 && app.selected < static_cast<int>(app.data.providers.size()) &&
                           !isDefaultProvider(app.data.providers[app.selected]);
    drawButton(deleteButton, "Remove selected", palette, false, canDelete);
    if (canDelete && clicked(deleteButton)) {
        deleteProvider(app);
    }
}

void drawField(AppState& app, const Palette& palette, const std::string& label, int field, Rectangle rectangle) {
    drawLabel(label, static_cast<int>(rectangle.x), static_cast<int>(rectangle.y - 20), palette.muted);
    const bool active = app.editField == field;
    const bool isHovered = hovered(rectangle);
    const bool editable = app.selected >= 0 && app.selected < static_cast<int>(app.data.providers.size()) &&
                          isFieldEditable(app.data.providers[app.selected], field);
    const Color fill = active ? Color{20, 33, 46, 255} : (editable ? palette.field : palette.panelRaised);
    const Color outline = active ? palette.accent :
                          (editable && isHovered ? withAlpha(palette.accent, 120) : palette.border);
    drawPanel(rectangle, fill, outline);
    std::string value;
    if (active) {
        value = app.draft;
    } else if (app.selected >= 0 && app.selected < static_cast<int>(app.data.providers.size())) {
        value = providerValue(app.data.providers[app.selected], field);
    }
    if (value.empty()) {
        value = "Not set";
    }
    drawText(value, static_cast<int>(rectangle.x + 14), static_cast<int>(rectangle.y + 13), 15,
             value == "Not set" ? palette.muted : palette.text);
    if (active && static_cast<int>(GetTime() * 2) % 2 == 0) {
        const int cursorX = static_cast<int>(rectangle.x + 14 + measureText(value, 15));
        DrawRectangle(cursorX, static_cast<int>(rectangle.y + 11), 1, 19, palette.accent);
    }
    if (editable && clicked(rectangle)) {
        beginEdit(app, field);
    }
}

void drawMainPanel(AppState& app, const Palette& palette, int width, int height) {
    const int left = 340;
    drawText("Configuration", left, 32, 26, palette.text);
    drawText("Choose which Codex provider should be active", left, 66, 14, palette.muted);

    const Rectangle saveButton = {static_cast<float>(width - 224), 28, 190, 44};
    drawButton(saveButton, "Save & Apply", palette, true);
    if (app.dirty) {
        drawText("UNSAVED", width - 224, 12, 11, palette.accent);
    }
    if (clicked(saveButton)) {
        saveAndApply(app);
    }

    const Rectangle pathPanel = {left, 104, static_cast<float>(width - left - 34), 52};
    drawPanel(pathPanel, palette.panel, palette.border);
    drawLabel("CONFIG FILE", left + 16, 116, palette.muted);
    drawText(shortenPath(app.configPath), left + 16, 133, 14, palette.text);

    if (app.selected < 0 || app.selected >= static_cast<int>(app.data.providers.size())) {
        drawText("No provider selected", left, 216, 18, palette.muted);
        return;
    }
    Provider& provider = app.data.providers[app.selected];
    const bool providerActive = isProviderActive(app, provider);
    const bool defaultProvider = isDefaultProvider(provider);
    const Rectangle settings = {left, 182, static_cast<float>(width - left - 34), 298};
    drawPanel(settings, palette.panel, palette.border);
    const std::string providerName = provider.name.empty() ? "Provider" : provider.name;
    drawText(providerName, left + 22, 205, 19, palette.text);
    const int badgeX = left + 22 + measureText(providerName, 19) + 14;
    if (providerActive) {
        drawPanel({static_cast<float>(badgeX), 202, 78, 24},
                   withAlpha(palette.success, 38), withAlpha(palette.success, 100));
        drawText("ACTIVE", badgeX + 11, 208, 11, palette.success);
    } else if (defaultProvider) {
        drawPanel({static_cast<float>(badgeX), 202, 84, 24},
                   withAlpha(palette.accent, 34), withAlpha(palette.accent, 100));
        drawText("BUILT-IN", badgeX + 9, 208, 11, palette.accent);
    }
    drawText(defaultProvider ? "Codex built-in OpenAI provider | ChatGPT account login"
                             : "Provider identity and transport settings",
             left + 22, 235, 13, palette.muted);

    drawField(app, palette, "DISPLAY NAME", 0, {left + 22.0f, 268, 270, 46});
    drawField(app, palette, "PROVIDER ID", 1, {left + 312.0f, 268, 270, 46});
    drawField(app, palette, "MODEL", 2, {left + 22.0f, 346, 270, 46});
    drawField(app, palette, "BASE URL", 3, {left + 312.0f, 346, 270, 46});
    drawField(app, palette, defaultProvider ? "AUTHENTICATION" : "API KEY ENVIRONMENT VARIABLE", 4,
              {left + 22.0f, 424, 270, 46});
    drawField(app, palette, "WIRE API", 5, {left + 312.0f, 424, 270, 46});

    const Rectangle applyButton = {left, 500, 210, 42};
    drawButton(applyButton, providerActive ? "Currently active" : "Use this provider", palette,
               !providerActive, !providerActive);
    if (!providerActive && clicked(applyButton)) {
        commitEdit(app);
        app.data.activeProvider = defaultProvider ? "" : provider.id;
        if (!provider.model.empty()) {
            app.data.model = provider.model;
        }
        app.dirty = true;
        showStatus(app, defaultProvider ? "Default selected. Save to use ChatGPT login"
                                        : "Provider selected. Save to apply it",
                   palette.accent);
    }
    drawText(defaultProvider ? "Uses the Codex ChatGPT account; no API key variable is needed."
                             : "API key name only; the secret stays in your environment.",
             left + 230, 514, 12, palette.muted);

    const int activityY = std::min(574, std::max(544, height - 96));
    const Rectangle activity = {left, static_cast<float>(activityY), static_cast<float>(width - left - 34), 64};
    drawPanel(activity, palette.panel, palette.border);
    DrawCircle(left + 25, static_cast<int>(activity.y + 32), 5, app.dirty ? palette.accent : app.statusColor);
    drawLabel("ACTIVITY", left + 42, static_cast<int>(activity.y + 12), palette.muted);
    drawText(app.dirty ? "Unsaved changes - save to apply" : app.status,
             left + 42, static_cast<int>(activity.y + 31), 14, palette.text);
}

void openConfigFolder(const std::string& path) {
#ifdef _WIN32
    const std::string command = "explorer.exe /select,\"" + path + "\"";
    std::system(command.c_str());
#else
    (void)path;
#endif
}

#ifdef _WIN32
void applyApplicationIcon() {
    HWND window = static_cast<HWND>(GetWindowHandle());
    if (window == nullptr) {
        return;
    }

    HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
    if (icon == nullptr) {
        return;
    }

    SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    SetClassLongPtrW(window, GCLP_HICON, reinterpret_cast<LONG_PTR>(icon));
    SetClassLongPtrW(window, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(icon));
}
#endif

} // namespace

int main() {
    AppState app;
    app.configPath = ConfigManager::defaultPath();
    std::string loadError;
    app.manager.load(app.configPath, app.data, loadError);
    if (!loadError.empty()) {
        app.status = loadError;
        app.statusColor = Palette{}.danger;
    }
    for (std::size_t index = 0; index < app.data.providers.size(); ++index) {
        if (app.data.providers[index].id == app.data.activeProvider) {
            app.selected = static_cast<int>(index);
            break;
        }
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(1200, 760, "Codex API Switcher");
    SetWindowMinSize(960, 640);
#ifdef _WIN32
    applyApplicationIcon();
#endif
    gAppFontShader = LoadShaderFromMemory(nullptr, kSdfFragmentShader);
    gAppFontShaderLoaded = gAppFontShader.id > 0;
    gAppFont = loadMicrosoftYaHei(gAppFontShaderLoaded, gAppFontUsesSdf);
    gAppFontLoaded = IsFontValid(gAppFont);
    if (gAppFontShaderLoaded && !gAppFontUsesSdf) {
        UnloadShader(gAppFontShader);
        gAppFontShaderLoaded = false;
    }
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    const Palette palette;
    while (!WindowShouldClose()) {
        captureTextInput(app);
        const int width = GetScreenWidth();
        const int height = GetScreenHeight();

        BeginDrawing();
        ClearBackground(palette.background);
        drawSidebar(app, palette, height);
        drawMainPanel(app, palette, width, height);
        drawText("Ctrl+S  Apply", 340, height - 28, 12, palette.muted);
        EndDrawing();

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S)) {
            saveAndApply(app);
        }
        if (IsKeyPressed(KEY_F2)) {
            openConfigFolder(app.configPath);
        }
    }
    commitEdit(app);
    if (gAppFontLoaded) {
        UnloadFont(gAppFont);
    }
    if (gAppFontShaderLoaded) {
        UnloadShader(gAppFontShader);
    }
    CloseWindow();
    return 0;
}
