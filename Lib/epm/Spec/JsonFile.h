#pragma once

#include <eacp/Core/Utils/Files.h>

#include <Miro/Json.h>
#include <Miro/Reflect.h>

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
} // namespace epm
