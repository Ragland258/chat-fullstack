#include "ConfigMgr.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include <Windows.h>

namespace
{
    bool IsSensitiveConfigKey(std::string key)
    {
        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });

        return key.find("password") != std::string::npos ||
            key.find("passwd") != std::string::npos ||
            key.find("pass") != std::string::npos ||
            key.find("token") != std::string::npos ||
            key.find("secret") != std::string::npos ||
            key.find("auth") != std::string::npos ||
            key.find("key") != std::string::npos;
    }
}

ConfigMgr::ConfigMgr()
{
    // Use a service-specific name to avoid multi-project config collisions.
    if (!LoadIniFile("GateServer.ini"))
    {
        throw std::runtime_error(
            "failed to load GateServer.ini");
    }

    PrintConfig();
}

void ConfigMgr::PrintConfig()
{

    // Print loaded config for debugging, but never expose secrets.
    for (const auto& section_entry : config_map_)
    {
        const auto& section_name = section_entry.first;
        auto section_config = section_entry.second;
        std::cout << "[" << section_name << "]" << std::endl;
        for (const auto& key_value : section_config.section_data_)
        {
            if (IsSensitiveConfigKey(key_value.first))
            {
                std::cout
                    << key_value.first
                    << "=<hidden, len="
                    << key_value.second.size()
                    << ">"
                    << std::endl;
            }
            else
            {
                std::cout
                    << key_value.first
                    << "="
                    << key_value.second
                    << std::endl;
            }
        }
    }
}

bool ConfigMgr::LoadIniFile(const std::string& config)
{
    try
    {
        /*
         * 联合启动时各进程的工作目录不一定相同，因此优先从
         * 当前 EXE 所在目录查找配置，而不是依赖工作目录。
         */
        std::wstring executable_buffer(32768, L'\0');
        const DWORD executable_length = GetModuleFileNameW(
            nullptr,
            executable_buffer.data(),
            static_cast<DWORD>(executable_buffer.size()));

        if (executable_length == 0 ||
            executable_length == executable_buffer.size())
        {
            throw std::runtime_error(
                "failed to get GateServer executable path");
        }

        executable_buffer.resize(executable_length);

        const std::filesystem::path executable_path(
            executable_buffer);

        std::vector<std::filesystem::path> config_paths;
        config_paths.push_back(
            executable_path.parent_path() / config);

        const std::filesystem::path current_config_path =
            std::filesystem::current_path() / config;

        if (config_paths.empty()
            || config_paths.front() != current_config_path)
        {
            config_paths.push_back(current_config_path);
        }

        std::filesystem::path config_path;

        for (const auto& candidate : config_paths)
        {
            std::cerr
                << "[GateServer] checking config: "
                << candidate.string()
                << std::endl;

            std::error_code file_error;

            if (std::filesystem::is_regular_file(
                    candidate,
                    file_error))
            {
                config_path = candidate;
                break;
            }
        }

        if (config_path.empty())
        {
            std::cerr << "Config file not found. Checked:" << std::endl;
            for (const auto& candidate : config_paths)
            {
                std::cerr << "  " << candidate.string() << std::endl;
            }
            return false;
        }

        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(config_path.string(), pt);

        std::cerr
            << "Loaded config: "
            << config_path.string()
            << std::endl;

        config_map_.clear();

        for (const auto& section_pair : pt)
        {
            const std::string& section_name = section_pair.first;
            const boost::property_tree::ptree& section_tree = section_pair.second;

            SectionInfo section_info;

            for (const auto& key_value : section_tree)
            {
                const std::string& key = key_value.first;
                std::string value = key_value.second.get_value<std::string>();

                section_info.section_data_[key] = value;
            }

            config_map_[section_name] = section_info;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "LoadConfig failed: " << e.what() << std::endl;
        return false;
    }
}
