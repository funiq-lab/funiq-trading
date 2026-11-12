#include "xml_config_ex.h"
#include <iostream>
#include <mutex>

using namespace std;

XMLConfigEx* XMLConfigEx::instance_ = nullptr;
static std::mutex instance_mutex;  // 添加线程安全

XMLConfigEx::XMLConfigEx()
{
}

XMLConfigEx::~XMLConfigEx()
{
}

XMLConfigEx* XMLConfigEx::instance()
{
	std::lock_guard<std::mutex> lock(instance_mutex);  // 线程安全
	if (!instance_)
	{
		instance_ = new XMLConfigEx();
	}
	return instance_;
}

bool XMLConfigEx::LoadXML(const char* fileName)
{
	
	tinyxml2::XMLError error = doc.LoadFile(fileName);
	if (error != tinyxml2::XMLError::XML_SUCCESS)
	{
		std::cerr << "LoadFile error "<<error;
		return false;
	}

	tinyxml2::XMLElement* root = doc.RootElement();
	if (root == nullptr)
	{
		std::cerr << "Root not found ";
		return false;
	}
    //meta_multi
    meta_node_ = root->FirstChildElement("meta");

	tinyxml2::XMLElement* config = root->FirstChildElement("config");
	if (config != nullptr)
	{
		auth_node_ = config->FirstChildElement("AuthInfo");
        tinyxml2::XMLElement* cache_node = config->FirstChildElement("Cache");
        if (cache_node)
        {
            nic_node_ = cache_node->FirstChildElement("RecvThreads");
            queue_node_ = cache_node->FirstChildElement("QueueGroup");
        }
        option_node_ = config->FirstChildElement("option");
	}
	else
	{
        std::cerr << "SysPath node not found";
        return false;
	}
	return true;
}
