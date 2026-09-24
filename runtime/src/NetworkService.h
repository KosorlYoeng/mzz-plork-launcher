#pragma once
#include <memory>

#include "ErrorHandling.h"

namespace mzzplork {

// A real, if simple, networking capability check: can this machine
// actually initialize Winsock. Distinct from IServerConnection, which is
// about a specific connection to a specific server -- this just answers
// "is networking available at all" before Runtime attempts anything that
// needs it.
class INetworkService {
public:
    virtual ~INetworkService() = default;
    virtual ServiceResult Initialize() = 0;
    virtual bool IsInitialized() const = 0;
};

std::unique_ptr<INetworkService> MakeWinsockNetworkService();

}
