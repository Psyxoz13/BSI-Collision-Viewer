#include "model/reflection.h"

#include <windows.h>

#include <format>

namespace
{
    constexpr uint32_t ObjectIndexOffset = 0x04;
    constexpr uint32_t ObjectNameOffset = 0x18;
    constexpr uint32_t ObjectClassOffset = 0x20;
    constexpr uint32_t FieldNextOffset = 0x28;
    constexpr uint32_t StructSuperOffset = 0x34;
    constexpr uint32_t StructChildrenOffset = 0x38;
    constexpr uint32_t PropertyElementSizeOffset = 0x30;
    constexpr uint32_t PropertyOffsetOffset = 0x48;
    constexpr uint32_t PropertyTypeDataOffset = 0x58;
    constexpr uint32_t NameEntryTextOffset = 0x10;
    constexpr int MaximumHierarchyDepth = 64;
    constexpr int MaximumFieldsPerStruct = 4096;
    constexpr int MaximumArrayElements = 4096;

    std::string nameEntry(uint32_t entries, int index)
    {
        uint32_t entry = read<uint32_t>(entries + 4 * index);
        return entry == 0 ? "" : readString(entry + NameEntryTextOffset);
    }

    bool isNameTable(uint32_t entries)
    {
        return nameEntry(entries, 0) == "None" && nameEntry(entries, 1) == "ByteProperty";
    }

    bool isObjectTable(uint32_t objects)
    {
        int validSamples = 0;
        for (uint32_t index = 1; index <= 16; index++)
        {
            uint32_t object = read<uint32_t>(objects + 4 * index);
            if (object == 0)
            {
                continue;
            }
            if (read<uint32_t>(object + ObjectIndexOffset) != index)
            {
                return false;
            }
            validSamples++;
        }
        return validSamples >= 8;
    }

    bool looksLikeArrayHeader(uint32_t pointer, uint32_t count, uint32_t capacity)
    {
        return pointer >= 0x10000 && count >= 10000 && count <= 4'000'000 && capacity >= count && capacity <= count * 4;
    }
}

std::pair<uint32_t, uint32_t> locateTables()
{
    auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    auto headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(headers);
    uint32_t names = 0;
    uint32_t objects = 0;
    for (int s = 0; s < headers->FileHeader.NumberOfSections; s++)
    {
        if (!(sections[s].Characteristics & IMAGE_SCN_MEM_WRITE))
        {
            continue;
        }
        uint32_t start = reinterpret_cast<uint32_t>(base) + sections[s].VirtualAddress;
        for (uint32_t page = 0; page < sections[s].Misc.VirtualSize && !(names && objects); page += 0x1000)
        {
            std::vector<uint32_t> words;
            try
            {
                words = readArray<uint32_t>(start + page, 1024 + 2);
            }
            catch (const MemoryError&)
            {
                continue;
            }
            for (uint32_t i = 0; i < 1024; i++)
            {
                if (!looksLikeArrayHeader(words[i], words[i + 1], words[i + 2]))
                {
                    continue;
                }
                try
                {
                    uint32_t address = start + page + 4 * i;
                    if (!names && isNameTable(words[i]))
                    {
                        names = address;
                    }
                    else if (!objects && isObjectTable(words[i]))
                    {
                        objects = address;
                    }
                }
                catch (const MemoryError&)
                {
                }
            }
        }
    }
    if (!names || !objects)
    {
        throw UnsupportedGame("the engine's name or object table was not found");
    }
    return {names, objects};
}

Reflection::Reflection(std::pair<uint32_t, uint32_t> namesAndObjects)
    : namesAddress(namesAndObjects.first), objectsAddress(namesAndObjects.second)
{
}

const std::string& Reflection::name(int index)
{
    {
        std::lock_guard lock(mutex_);
        if (auto known = names_.find(index); known != names_.end())
        {
            return known->second;
        }
    }
    std::string text = index >= 0 && index < read<int32_t>(namesAddress + 4) ? nameEntry(read<uint32_t>(namesAddress), index) : "";
    std::lock_guard lock(mutex_);
    return names_.emplace(index, std::move(text)).first->second;
}

const std::string& Reflection::nameOf(uint32_t object)
{
    return name(read<int32_t>(object + ObjectNameOffset));
}

bool Reflection::isDefaultObject(uint32_t object)
{
    return nameOf(object).starts_with("Default__");
}

uint32_t Reflection::classOf(uint32_t object)
{
    return read<uint32_t>(object + ObjectClassOffset);
}

bool Reflection::isA(uint32_t object, const char* className)
{
    return derives(classOf(object), className);
}

bool Reflection::derives(uint32_t structure, const char* baseName)
{
    for (int depth = 0; structure != 0 && depth < MaximumHierarchyDepth; depth++, structure = read<uint32_t>(structure + StructSuperOffset))
    {
        if (nameOf(structure) == baseName)
        {
            return true;
        }
    }
    return false;
}

Property Reflection::property(uint32_t structure, const char* name)
{
    std::pair<uint32_t, std::string> key(structure, name);
    {
        std::lock_guard lock(mutex_);
        if (auto known = properties_.find(key); known != properties_.end())
        {
            return known->second;
        }
    }
    uint32_t owner = structure;
    for (int depth = 0; owner != 0 && depth < MaximumHierarchyDepth; depth++, owner = read<uint32_t>(owner + StructSuperOffset))
    {
        uint32_t field = read<uint32_t>(owner + StructChildrenOffset);
        for (int n = 0; field != 0 && n < MaximumFieldsPerStruct; n++, field = read<uint32_t>(field + FieldNextOffset))
        {
            if (nameOf(field) == name)
            {
                Property found{read<uint32_t>(field + PropertyOffsetOffset), read<uint32_t>(field + PropertyTypeDataOffset)};
                std::lock_guard lock(mutex_);
                properties_.emplace(std::move(key), found);
                return found;
            }
        }
    }
    throw UnsupportedGame(std::format("property {} was not found", name));
}

bool Reflection::flag(uint32_t object, const char* name)
{
    Property found = property(classOf(object), name);
    return (read<uint32_t>(object + found.offset) & found.typeData) != 0;
}

std::vector<Reflection::Element> Reflection::elements(uint32_t owner, uint32_t ownerStructure, const char* arrayName)
{
    Property array = property(ownerStructure, arrayName);
    uint32_t data = read<uint32_t>(owner + array.offset);
    uint32_t elementSize = read<uint32_t>(array.typeData + PropertyElementSizeOffset);
    uint32_t elementStructure = read<uint32_t>(array.typeData + PropertyTypeDataOffset);
    int count = std::clamp(read<int32_t>(owner + array.offset + 4), 0, MaximumArrayElements);
    std::vector<Element> elements;
    for (int i = 0; i < count; i++)
    {
        elements.push_back({data + i * elementSize, elementStructure});
    }
    return elements;
}

std::vector<uint32_t> Reflection::allObjects()
{
    return readArray<uint32_t>(read<uint32_t>(objectsAddress), read<int32_t>(objectsAddress + 4));
}
