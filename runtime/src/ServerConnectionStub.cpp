#include "ServerConnectionStub.h"

namespace mzzplork {

namespace {
class NotImplementedServerConnection : public IServerConnection {
public:
    bool Connect(const std::string&, unsigned short) override { return false; }
    void Disconnect() override {}
    bool IsConnected() const override { return false; }
    bool SendHandshake() override { return false; }
    bool Send(const std::string&, const std::string&) override { return false; }
    void SetMessageHandler(std::function<void(const ServerMessage&)> handler) override {
        handler_ = std::move(handler);
    }

private:
    std::function<void(const ServerMessage&)> handler_;
};
}

std::unique_ptr<IServerConnection> MakeNotImplementedServerConnection() {
    return std::make_unique<NotImplementedServerConnection>();
}

}
