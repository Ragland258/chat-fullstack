#pragma once
#include "const.h"
#include "Singleton.h"
struct SectionInfo
{
	SectionInfo(){}
	~SectionInfo() { section_data_.clear(); }

	SectionInfo(const SectionInfo& other) { section_data_ = other.section_data_; }
	SectionInfo& operator = (const SectionInfo& other)
	{
		if (this == &other)
			return *this;
		section_data_ = other.section_data_;
		return *this;
	}


	std::map<std::string, std::string > section_data_;

	std::string operator [] (const std::string& first)
	{
		if (section_data_.find(first) == section_data_.end())
			return "";
		else
			return section_data_[first];
	}

};

class ConfigMgr:public Singleton<ConfigMgr>
{
	friend class Singleton<ConfigMgr>;

public:
	~ConfigMgr(){}

	SectionInfo operator[](const std::string& first)
	{
		if (config_map_.find(first) == config_map_.end())
			return SectionInfo();
		else
			return config_map_[first];

	}

private:
	ConfigMgr();
	bool LoadConfig(const std::string& config);
	void PrintConfig();
private:
	std::map<std::string, SectionInfo> config_map_;
};

