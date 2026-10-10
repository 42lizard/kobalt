#include "publication/loopback.h"
#include <cerrno>

// Test hosts use their own loopback interface. Only the Kobo firmware 4
// device build needs to add a missing address.
namespace kobalt::nickel4 {
bool configureLoopback(int *error) {
    if (error)
        *error = ENOTSUP;
    return false;
}
}
