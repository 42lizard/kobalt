#include "publication_source.h"
#include "publication/native_archive.h"
#include "publication/resource_server.h"
#include "support/strings.h"

namespace kobalt::native {
namespace {
std::unique_ptr<PublicationEndpoint> open(const void *, const void *, const QString &path, const QString &,
                                          QString *error) {
    auto server = std::make_unique<ResourceServer>();
    if (!server->open(path, [&path] {
            return openPlainArchive(path);
        })) {
        if (error)
            *error = strings::get("error.cannot_start_the_native_publication_service");
        return {};
    }
    return server;
}
}
const PublicationSource &plainPublicationSource() {
    static const PublicationSource source{plainArchiveAvailable, open};
    return source;
}
}
