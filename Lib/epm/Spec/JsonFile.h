#pragma once

#include <eacp/Core/Utils/Files.h>

#include <Miro/Json.h>
#include <Miro/Reflect.h>

#include <cstdint>
#include <optional>
#include <string>

namespace epm
{
// nullopt when the file is missing or does not hold a JSON object.
inline std::optional<Miro::JSON> loadJsonObject(const eacp::FilePath& path)
{
    if (!eacp::Files::isFile(path))
        return std::nullopt;

    auto json = Miro::Json::getParsedValue(eacp::Files::readFile(path));

    if (!json.isObject())
        return std::nullopt;

    return json;
}

// nullopt when the file is missing or does not hold a JSON object. Keys the
// file leaves out keep the struct's defaults.
template <typename T>
std::optional<T> loadJsonFile(const eacp::FilePath& path)
{
    const auto json = loadJsonObject(path);

    if (!json)
        return std::nullopt;

    return Miro::createFromJSON<T>(*json);
}

template <typename T>
bool saveJsonFile(const eacp::FilePath& path, const T& value)
{
    const auto text = Miro::toJSONString(value, 2) + "\n";
    const auto directory = path.parentDirectory();

    // A bare file name has no parent, and createDirectories("") fails.
    // (Upstream candidate: eacp's createDirectories should accept it.)
    if (!directory.empty() && !eacp::Files::createDirectories(directory))
        return false;

    try
    {
        eacp::Files::writeFileAtomically(
            path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }
    catch (const std::exception&)
    {
        return false;
    }

    return true;
}
} // namespace epm
