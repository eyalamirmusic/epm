#include "BuildProtocol.h"

#include <epm/Spec/JsonFile.h>

namespace epm
{
namespace
{
struct ProtocolSchema
{
    int schema = 0;

    MIRO_REFLECT(schema)
};
} // namespace

BuildSource buildSourceFor(const SourceSnapshot& snapshot)
{
    return {snapshot.commit, snapshot.sourceId, snapshot.directory.str()};
}

SourceSnapshot snapshotOf(const BuildSource& source)
{
    return {source.commit, source.sourceId, eacp::FilePath {source.directory}};
}

std::optional<int> buildProtocolSchemaOf(const eacp::FilePath& path)
{
    const auto loaded = loadJsonFile<ProtocolSchema>(path);

    if (!loaded)
        return std::nullopt;

    return loaded->schema;
}
} // namespace epm
