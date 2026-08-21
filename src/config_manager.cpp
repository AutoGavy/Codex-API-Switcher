#include "config_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string stripInlineComment(const std::string& value) {
    bool quoted = false;
    bool escaped = false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char current = value[index];
        if (current == '\\' && quoted && !escaped) {
            escaped = true;
            continue;
        }
        if (current == '"' && !escaped) {
            quoted = !quoted;
        }
        if (current == '#' && !quoted) {
            return value.substr(0, index);
        }
        escaped = false;
    }
    return value;
}

bool isProviderSection(const std::string& section) {
    return startsWith(section, "model_providers.");
}

bool isReservedProviderId(const std::string& id) {
    return id == "openai" || id == "ollama" || id == "lmstudio";
}

std::string providerIdFromSection(const std::string& section) {
    return section.substr(std::string("model_providers.").size());
}

bool isSafeProviderId(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-';
    });
}

void setProviderValue(Provider& provider, const std::string& key, const std::string& value) {
    if (key == "name") {
        provider.name = value;
    } else if (key == "model") {
        provider.model = value;
    } else if (key == "base_url") {
        provider.baseUrl = value;
    } else if (key == "env_key") {
        provider.envKey = value;
    } else if (key == "wire_api") {
        provider.wireApi = value;
    } else if (key == "requires_openai_auth") {
        std::string normalized = value;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        provider.requiresOpenAIAuth = normalized == "true";
    }
}

Provider customProvider(const std::string& id, const std::string& name, const std::string& model,
                        const std::string& baseUrl, const std::string& envKey) {
    Provider provider;
    provider.id = id;
    provider.name = name;
    provider.model = model;
    provider.baseUrl = baseUrl;
    provider.envKey = envKey;
    provider.wireApi = "responses";
    return provider;
}

Provider builtInDefaultProvider(const std::string& model) {
    Provider provider;
    provider.id = "openai";
    provider.name = "Default";
    provider.model = model;
    provider.wireApi = "responses";
    provider.builtIn = true;
    provider.requiresOpenAIAuth = true;
    return provider;
}

} // namespace

std::string ConfigManager::defaultPath() {
#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile != nullptr && *userProfile != '\0') {
        return (std::filesystem::path(userProfile) / ".codex" / "config.toml").string();
    }
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return (std::filesystem::path(home) / ".codex" / "config.toml").string();
    }
#endif
    return "config.toml";
}

ConfigData ConfigManager::defaults() {
    ConfigData data;
    data.activeProvider.clear();
    data.model = "gpt-5";
    data.providers.push_back(builtInDefaultProvider(data.model));
    data.providers.push_back(customProvider(
        "openrouter", "OpenRouter", "openai/gpt-4o", "https://openrouter.ai/api/v1", "OPENROUTER_API_KEY"));
    return data;
}

bool ConfigManager::load(const std::string& path, ConfigData& data, std::string& error) const {
    std::ifstream input(path);
    if (!input.good()) {
        data = defaults();
        if (std::filesystem::exists(path)) {
            error = "Unable to read config.toml";
            return false;
        }
        error.clear();
        return true;
    }

    data = {};
    std::string line;
    std::string currentSection;
    int currentProviderIndex = -1;
    while (std::getline(input, line)) {
        const std::string cleaned = trim(stripInlineComment(line));
        if (cleaned.empty()) {
            continue;
        }
        if (cleaned.front() == '[' && cleaned.back() == ']') {
            currentSection = trim(cleaned.substr(1, cleaned.size() - 2));
            currentProviderIndex = -1;
            if (isProviderSection(currentSection)) {
                const std::string providerId = providerIdFromSection(currentSection);
                if (!isReservedProviderId(providerId)) {
                    Provider provider;
                    provider.id = providerId;
                    provider.name = providerId;
                    provider.wireApi = "responses";
                    data.providers.push_back(provider);
                    currentProviderIndex = static_cast<int>(data.providers.size()) - 1;
                }
            }
            continue;
        }

        const std::size_t separator = cleaned.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = trim(cleaned.substr(0, separator));
        const std::string value = parseTomlString(trim(cleaned.substr(separator + 1)));
        if (currentSection.empty()) {
            if (key == "model_provider") {
                data.activeProvider = value;
            } else if (key == "model") {
                data.model = value;
            }
        } else if (currentProviderIndex >= 0 &&
                   currentProviderIndex < static_cast<int>(data.providers.size())) {
            setProviderValue(data.providers[currentProviderIndex], key, value);
        }
    }

    if (input.bad()) {
        error = "Error while reading config.toml";
        return false;
    }
    if (data.model.empty()) {
        for (const Provider& provider : data.providers) {
            if (!provider.model.empty()) {
                data.model = provider.model;
                break;
            }
        }
    }
    if (data.model.empty()) {
        data.model = "gpt-5";
    }
    for (Provider& provider : data.providers) {
        if (provider.model.empty()) {
            provider.model = data.model;
        }
    }
    std::vector<Provider> customProviders = std::move(data.providers);
    data.providers.clear();
    data.providers.push_back(builtInDefaultProvider(data.model));
    for (Provider& provider : customProviders) {
        provider.builtIn = false;
        data.providers.push_back(std::move(provider));
    }
    if (data.activeProvider == "openai") {
        data.activeProvider.clear();
    }
    error.clear();
    return true;
}

bool ConfigManager::save(const std::string& path, const ConfigData& data, std::string& error) const {
    if (data.model.empty()) {
        error = "A model is required";
        return false;
    }
    const bool defaultIsActive = data.activeProvider.empty() || data.activeProvider == "openai";
    bool activeProviderFound = defaultIsActive;
    for (const Provider& provider : data.providers) {
        if (provider.builtIn) {
            continue;
        }
        if (isReservedProviderId(provider.id)) {
            error = "Provider ID is reserved for a built-in Codex provider";
            return false;
        }
        if (!isSafeProviderId(provider.id)) {
            error = "Provider ID may only use letters, numbers, '_' and '-'";
            return false;
        }
        if (provider.baseUrl.empty()) {
            error = "Every custom provider needs a base URL";
            return false;
        }
        if (provider.id == data.activeProvider) {
            activeProviderFound = true;
        }
    }
    if (!activeProviderFound) {
        error = "The active provider is not defined";
        return false;
    }

    std::string original;
    {
        std::ifstream input(path, std::ios::binary);
        if (input.good()) {
            std::ostringstream buffer;
            buffer << input.rdbuf();
            original = buffer.str();
        }
    }

    std::vector<std::string> keptLines;
    std::istringstream source(original);
    std::string line;
    std::string currentSection;
    while (std::getline(source, line)) {
        const std::string cleaned = trim(stripInlineComment(line));
        if (!cleaned.empty() && cleaned.front() == '[' && cleaned.back() == ']') {
            currentSection = trim(cleaned.substr(1, cleaned.size() - 2));
        }
        if (currentSection == "model_providers" || isProviderSection(currentSection)) {
            continue;
        }
        if (currentSection.empty()) {
            const std::size_t separator = cleaned.find('=');
            if (separator != std::string::npos) {
                const std::string key = trim(cleaned.substr(0, separator));
                if (key == "model_provider" || key == "model") {
                    continue;
                }
            }
        }
        keptLines.push_back(line);
    }

    std::ostringstream output;
    output << "# Managed by Codex API Switcher\n";
    if (!defaultIsActive) {
        output << "model_provider = " << quoteTomlString(data.activeProvider) << "\n";
    }
    output << "model = " << quoteTomlString(data.model) << "\n\n";
    for (const std::string& keptLine : keptLines) {
        output << keptLine << '\n';
    }
    output << '\n';
    for (const Provider& provider : data.providers) {
        if (provider.id.empty() || provider.builtIn || isReservedProviderId(provider.id)) {
            continue;
        }
        output << "[model_providers." << provider.id << "]\n";
        if (!provider.name.empty()) {
            output << "name = " << quoteTomlString(provider.name) << '\n';
        }
        output << "base_url = " << quoteTomlString(provider.baseUrl) << '\n';
        if (!provider.envKey.empty()) {
            output << "env_key = " << quoteTomlString(provider.envKey) << '\n';
        }
        if (provider.requiresOpenAIAuth) {
            output << "requires_openai_auth = true\n";
        }
        if (!provider.wireApi.empty()) {
            output << "wire_api = " << quoteTomlString(provider.wireApi) << '\n';
        }
        output << '\n';
    }

    try {
        const std::filesystem::path target(path);
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path());
        }
        const std::filesystem::path temporary = target.string() + ".codex-switcher.tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.good()) {
            error = "Unable to write config.toml";
            return false;
        }
        file << output.str();
        file.flush();
        file.close();
        if (!file.good()) {
            error = "Error while writing config.toml";
            return false;
        }
#ifdef _WIN32
        if (MoveFileExA(temporary.string().c_str(), target.string().c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            error = "Unable to replace config.toml";
            return false;
        }
#else
        std::filesystem::rename(temporary, target);
#endif
    } catch (const std::filesystem::filesystem_error&) {
        error = "Unable to create the config directory";
        return false;
    }
    error.clear();
    return true;
}

std::string ConfigManager::trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string ConfigManager::parseTomlString(const std::string& value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return value;
    }
    std::string result;
    bool escaped = false;
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        const char current = value[index];
        if (escaped) {
            if (current == 'n') {
                result.push_back('\n');
            } else {
                result.push_back(current);
            }
            escaped = false;
        } else if (current == '\\') {
            escaped = true;
        } else {
            result.push_back(current);
        }
    }
    return result;
}

std::string ConfigManager::quoteTomlString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (const char current : value) {
        if (current == '\\' || current == '"') {
            escaped.push_back('\\');
        }
        if (current == '\n') {
            escaped += "\\n";
        } else {
            escaped.push_back(current);
        }
    }
    return '"' + escaped + '"';
}
