#pragma once

#include "core/common.h"

#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

struct Property
{
    uint32_t offset;
    uint32_t typeData;
};

class Reflection
{
public:
    struct Element
    {
        uint32_t address;
        uint32_t structure;
    };

    Reflection(std::pair<uint32_t, uint32_t> namesAndObjects);

    const uint32_t namesAddress;
    const uint32_t objectsAddress;

    const std::string& nameOf(uint32_t object);
    bool isDefaultObject(uint32_t object);
    uint32_t classOf(uint32_t object);
    bool isA(uint32_t object, const char* className);
    bool derives(uint32_t structure, const char* baseName);
    Property property(uint32_t structure, const char* name);
    uint32_t offsetOf(uint32_t object, const char* name) { return property(classOf(object), name).offset; }
    bool flag(uint32_t object, const char* name);
    std::vector<Element> elements(uint32_t owner, uint32_t ownerStructure, const char* arrayName);
    std::vector<uint32_t> allObjects();

    template <class T>
    T field(uint32_t object, const char* name) { return read<T>(object + offsetOf(object, name)); }

    template <class T>
    T elementField(Element element, const char* name) { return read<T>(element.address + property(element.structure, name).offset); }

    template <class T>
    std::vector<T> elementArray(Element element, const char* name)
    {
        return readCountedArray<T>(element.address + property(element.structure, name).offset);
    }

private:
    const std::string& name(int index);

    std::mutex mutex_;
    std::unordered_map<int, std::string> names_;
    std::map<std::pair<uint32_t, std::string>, Property> properties_;
    std::set<std::pair<uint32_t, std::string>> absent_;
};

std::pair<uint32_t, uint32_t> locateTables();
