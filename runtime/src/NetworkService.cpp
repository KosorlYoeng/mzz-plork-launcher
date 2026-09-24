#include "NetworkService.h"

#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

namespace mzzplork {

namespace {
class WinsockNetworkService : public INetworkService {
public:
    ServiceResult Initialize() override {
        WSADATA wsaData;
        int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (rc != 0) {
            initialized_ = false;
            return ServiceResult::Failed("WSAStartup failed with code " + std::to_string(rc));
        }
        WSACleanup();  // this was only a capability probe; TcpServerConnection manages its own Winsock lifetime
        initialized_ = true;
        return ServiceResult::Ok("Winsock available");
    }
    bool IsInitialized() const override { return initialized_; }

private:
    bool initialized_ = false;
};
}

std::unique_ptr<INetworkService> MakeWinsockNetworkService() {
    return std::make_unique<WinsockNetworkService>();
}

}
