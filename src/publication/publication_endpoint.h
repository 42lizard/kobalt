#pragma once

namespace kobalt::native {
// Owned on the GUI thread. The host keeps this endpoint alive until engine
// and chapter work drains and resource connections have been released.
class PublicationEndpoint {
public:
    virtual ~PublicationEndpoint() = default;
    virtual int port() const = 0;
    virtual bool active() const = 0;
};
}
