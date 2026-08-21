#pragma once

#include <string>
#include <vector>

struct Provider {
    std::string id;
    std::string name;
    std::string model;
    std::string baseUrl;
    std::string envKey;
    std::string wireApi;
    bool builtIn = false;
    bool requiresOpenAIAuth = false;
};

struct ConfigData {
    std::string activeProvider;
    std::string model;
    std::vector<Provider> providers;
};

class ConfigManager {
public:
    static std::string defaultPath();
    static ConfigData defaults();

    bool load(const std::string& path, ConfigData& data, std::string& error) const;
    bool save(const std::string& path, const ConfigData& data, std::string& error) const;

private:
    static std::string trim(const std::string& value);
    static std::string parseTomlString(const std::string& value);
    static std::string quoteTomlString(const std::string& value);
};
