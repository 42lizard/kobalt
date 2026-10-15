#pragma once
#include <QObject>
#include <memory>
#include "native_archive.h"
#include "publication_endpoint.h"

namespace kobalt::native {
// Serves resources over local sockets on PocketBook, loopback TCP on other hosts.
class ResourceServer : public QObject, public PublicationEndpoint {
public:
    explicit ResourceServer(QObject *parent = nullptr);
    ~ResourceServer();
    bool open(const QString &path, const OpenArchive &openArchive);
    // PocketBook returns a process-local endpoint number instead of a TCP port.
    int port() const override;
    bool active() const override {
        return port() != 0;
    }

private:
    struct Data;
    std::unique_ptr<Data> data_;
};
}
