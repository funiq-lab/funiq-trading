#pragma once

#include <string>
#include <vector>
#include <utility>
#include <tinyxml2.h>

using namespace tinyxml2;

class XMLConfigEx
{
public:
	static XMLConfigEx* instance();
	virtual ~XMLConfigEx();

    bool LoadXML(const char* fileName);

public:
    tinyxml2::XMLElement* meta_node_{nullptr};

    tinyxml2::XMLElement* auth_node_{nullptr};
    tinyxml2::XMLElement* nic_node_{nullptr};
    tinyxml2::XMLElement* queue_node_{nullptr};

    tinyxml2::XMLElement* option_node_{nullptr};
private:
	static XMLConfigEx* instance_;
    tinyxml2::XMLDocument doc;

    XMLConfigEx();
};
