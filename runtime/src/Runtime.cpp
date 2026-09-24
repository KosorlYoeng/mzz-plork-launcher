#include "Runtime.h"

#include "ClientRuntimeInterface.h"
#include "ServerConnectionStub.h"
#include "TcpServerConnection.h"

#include <windows.h>
#include <bcrypt.h>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")

namespace mzzplork {

namespace {

constexpr const char* kProtocolVersion = "1.0";
constexpr int kHeartbeatIntervalMs = 10000;  // comfortably under the server's default 45s timeout

std::string GenerateRandomId() {
    unsigned char bytes[16] = {};
    BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::ostringstream oss;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::hex << ((bytes[i] >> 4) & 0xF) << (bytes[i] & 0xF);
    }
    return oss.str();
}

}  // namespace

RuntimeDependencies MakeDefaultDependencies(const RuntimeConfig& config) {
    RuntimeDependencies deps;
    deps.cache = std::make_unique<FileCacheService>();
    deps.network = MakeWinsockNetworkService();
    deps.server = config.serverHost.empty() ? MakeNotImplementedServerConnection() : MakeTcpServerConnection();
    deps.resources = MakeFileResourceManager();
    deps.game = MakeGameIntegration(config.gtaInstallPathOverride);
    return deps;
}

ClientRuntime::ClientRuntime(std::string appDataRoot, RuntimeDependencies deps)
    : appDataRoot_(std::move(appDataRoot)), deps_(std::move(deps)) {}

ClientRuntime::ClientRuntime(std::string appDataRoot)
    : appDataRoot_(std::move(appDataRoot)) {}

RuntimeState ClientRuntime::LifecycleState() const { return lifecycle_.GetState(); }
ClientState ClientRuntime::SessionState() const { return clientState_.Get(); }
EventSystem& ClientRuntime::Events() { return events_; }

void ClientRuntime::SetState(ClientState state) {
    clientState_.Set(state);
    EmitServiceEvent("client-state", "ok", ToString(state));
}

std::string ClientRuntime::LoadOrCreateClientId() {
    std::string path = appDataRoot_ + "\\config\\client-id.txt";
    std::ifstream in(path);
    if (in) {
        std::string id;
        std::getline(in, id);
        if (!id.empty()) return id;
    }
    std::string id = GenerateRandomId();
    std::ofstream out(path, std::ios::trunc);
    out << id;
    return id;
}

int ClientRuntime::Run(std::istream& controlInput) {
    lifecycle_.TransitionTo(RuntimeState::Initializing);
    EmitStateEvent(ToString(lifecycle_.GetState()));
    SetState(ClientState::Starting);

    // 1. Load configuration.
    auto configLoad = LoadConfig(appDataRoot_);
    if (configLoad.result.status == ServiceStatus::Failed) {
        EmitServiceEvent("configuration", ToString(configLoad.result.status), configLoad.result.message);
        EmitErrorEvent("configuration load failed: " + configLoad.result.message);
        return static_cast<int>(ExitCode::FatalInitError);
    }
    config_ = configLoad.config;
    EmitServiceEvent("configuration", ToString(configLoad.result.status), configLoad.result.message);

    if (!deps_.cache) {
        deps_ = MakeDefaultDependencies(config_);
    }

    // 2. Initialize logging.
    logger_ = std::make_unique<Logger>(appDataRoot_ + "\\logs\\client.log", ParseLogLevel(config_.logLevel));
    logger_->Info("logging initialized at level " + config_.logLevel);
    EmitServiceEvent("logging", "ok", "log level = " + config_.logLevel);

    SetState(ClientState::Initializing);

    // 3. Initialize runtime services (client state store is already live; cache + network + game integration boundary).
    ServiceResult cacheResult = deps_.cache ? deps_.cache->Initialize(appDataRoot_) : ServiceResult::Failed("cache service missing");
    logger_->Info(std::string("cache service: ") + cacheResult.message);
    EmitServiceEvent("cache", ToString(cacheResult.status), cacheResult.message);
    if (cacheResult.status == ServiceStatus::Failed) {
        logger_->Error("runtime services failed to initialize: " + cacheResult.message);
        EmitErrorEvent("runtime services failed to initialize: " + cacheResult.message);
        return static_cast<int>(ExitCode::FatalInitError);
    }

    ServiceResult netResult = deps_.network ? deps_.network->Initialize() : ServiceResult::Failed("network service missing");
    logger_->Info(std::string("network service: ") + netResult.message);
    EmitServiceEvent("network", ToString(netResult.status), netResult.message);

    if (deps_.game) {
        ServiceResult gameInit = deps_.game->Initialize();
        logger_->Info(std::string("game integration: ") + gameInit.message);
        EmitServiceEvent("game-integration", ToString(gameInit.status), gameInit.message);
        ServiceResult detect = deps_.game->DetectGame();
        logger_->Info(std::string("game detection: ") + detect.message);
        EmitServiceEvent("game-detection", ToString(detect.status), detect.message);
    }

    // 4. Checking update. Substantive version-compatibility checking
    // happens during the hello handshake below (step CONNECTING); the
    // bootstrapper already owns checking/applying updates to the
    // installed client/resources before this process even starts (see
    // docs/resources.md). This state exists for observability and as the
    // seam for future client-side update logic, not yet doing
    // independent work of its own -- an honest, disclosed scope
    // boundary, not an oversight.
    SetState(ClientState::CheckingUpdate);

    // 5. Connecting.
    SetState(ClientState::Connecting);
    if (config_.serverHost.empty()) {
        logger_->Info("no server configured (serverHost empty) -- staying idle without a server connection");
        EmitServiceEvent("server-connection", "not_implemented", "no server configured");
    } else if (!deps_.server || !deps_.server->Connect(config_.serverHost, static_cast<unsigned short>(config_.serverPort))) {
        logger_->Error("failed to connect to " + config_.serverHost + ":" + std::to_string(config_.serverPort));
        EmitServiceEvent("server-connection", "failed", "could not connect to " + config_.serverHost);
    } else {
        connectedToServer_ = true;
        protocol_ = std::make_unique<ProtocolClient>(*deps_.server);
        protocol_->SetKickHandler([this](const std::string& reason) {
            kicked_ = true;
            kickReason_ = reason;
            if (disconnectRequested_) {
                // The server always answers a client-initiated "disconnect"
                // with a "kick" -- that's an acknowledgment, not a failure.
                if (logger_) logger_->Info("server acknowledged disconnect: " + reason);
                EmitServiceEvent("server-connection", "ok", "disconnect acknowledged: " + reason);
            } else {
                if (logger_) logger_->Warn("kicked by server: " + reason);
                EmitServiceEvent("server-connection", "failed", "kicked by server: " + reason);
            }
        });
        protocol_->SetEventSink([this](const std::string& eventType, const json::Object& data) {
            events_.Publish(eventType, data);
        });
        protocol_->Start();

        json::Object helloAck;
        if (!protocol_->SendHello(kProtocolVersion, helloAck) || !helloAck["ok"].AsBool(false)) {
            logger_->Error("server rejected hello handshake");
            EmitServiceEvent("server-connection", "failed", "protocol handshake rejected");
            connectedToServer_ = false;
        } else {
            logger_->Info("connected to " + config_.serverHost + ":" + std::to_string(config_.serverPort));
            EmitServiceEvent("server-connection", "ok", "connected and protocol-compatible");
        }
    }

    // 6. Authenticating.
    if (connectedToServer_) {
        SetState(ClientState::Authenticating);
        std::string clientId = LoadOrCreateClientId();
        json::Object authResult;
        if (protocol_->SendAuth(clientId, "", authResult) && authResult["ok"].AsBool(false)) {
            std::string playerId = authResult["playerId"].AsString();
            std::string displayName = authResult["displayName"].AsString();
            logger_->Info("authenticated as player " + playerId);
            EmitServiceEvent("authentication", "ok", "player " + playerId);

            // Multiplayer synchronization can only start once we know the
            // server-assigned playerId -- see docs/synchronization.md.
            // Subscribing here (rather than waiting for Ready) matters:
            // the server backfills the existing player list as "event"
            // messages immediately after auth_result, before resources
            // even start downloading.
            replication_ = std::make_unique<Replication>(entityManager_, *protocol_, events_, playerId, displayName);
            replication_->SetSyncIntervalMs(config_.replicationIntervalMs);
            replication_->SetInterpolationDurationMs(config_.interpolationDurationMs);

            // Observability for entity lifecycle and incoming state --
            // nothing else logs this, and it is the only way to confirm
            // synchronization actually happened (in production debugging,
            // or in the local dev/E2E test environment -- see
            // docs/synchronization.md).
            entityManager_.SetOnEntityCreated([this](Entity& entity) {
                std::string ownership = entity.IsLocal() ? "local" : "remote";
                logger_->Info("entity created: " + entity.Id() + " (" + ToString(entity.Type()) + ", " + ownership + ")");
                EmitServiceEvent("entity-created", "ok", entity.Id());
            });
            entityManager_.SetOnEntityDestroyed([this](const std::string& id, EntityType type) {
                logger_->Info("entity destroyed: " + id + " (" + std::string(ToString(type)) + ")");
                EmitServiceEvent("entity-destroyed", "ok", id);
            });
            events_.Subscribe("player_state", [this](const json::Object& data) {
                auto it = data.find("playerId");
                if (it == data.end()) return;
                NetworkState state = FromJson(data);
                std::ostringstream oss;
                oss << "player_state received for " << it->second.AsString()
                    << " pos=(" << state.position.x << "," << state.position.y << "," << state.position.z << ")"
                    << " rot=(" << state.rotation.pitch << "," << state.rotation.yaw << "," << state.rotation.roll << ")";
                logger_->Info(oss.str());
            });

            replication_->Start();
        } else {
            logger_->Error("authentication failed or timed out");
            EmitServiceEvent("authentication", "failed", "no auth_result received");
            connectedToServer_ = false;
        }
    }

    // 7. Downloading resources.
    bool manifestApplied = false;
    if (connectedToServer_) {
        SetState(ClientState::DownloadingResources);
        json::Object manifest;
        if (protocol_->RequestResourceManifest(manifest) && deps_.resources) {
            ServiceResult resInit = deps_.resources->Initialize(appDataRoot_);
            ServiceResult applied = resInit.status == ServiceStatus::Failed
                ? resInit
                : deps_.resources->ApplyManifest(manifest, config_.serverHost);
            logger_->Info(std::string("resource manager: ") + applied.message);
            EmitServiceEvent("resources", ToString(applied.status), applied.message);
            manifestApplied = applied.status != ServiceStatus::Failed;
        } else {
            logger_->Error("failed to retrieve resource manifest");
            EmitServiceEvent("resources", "failed", "no resource manifest received");
        }
    } else if (deps_.resources) {
        ServiceResult resInit = deps_.resources->Initialize(appDataRoot_);
        EmitServiceEvent("resources", ToString(resInit.status), resInit.message + " (no server connection -- nothing to download)");
    }

    // 8. Loading resources: dependency-ordered Cached -> Loaded for
    // everything that was actually verified (docs/resources.md). Loading
    // a resource here means confirming its bytes are still valid on disk
    // and making it available for use -- it does not run or interpret
    // its content, since there's no consumer for that yet (see
    // GameIntegration.h). Then notify the server, per the client flow:
    // Load resources -> Notify server -> Enter ready state.
    SetState(ClientState::LoadingResources);
    if (manifestApplied && deps_.resources) {
        auto summary = deps_.resources->LoadAll();
        std::string message = std::to_string(summary.loaded) + " loaded, " + std::to_string(summary.failed) + " failed";
        for (const auto& [id, err] : summary.errors) {
            logger_->Warn("resource \"" + id + "\" failed to load: " + err);
        }
        logger_->Info("resource loading: " + message);
        EmitServiceEvent("resource-loading", summary.failed > 0 && summary.loaded == 0 ? "failed" : "ok", message);

        if (connectedToServer_ && protocol_) {
            if (protocol_->SendResourcesReady(summary.loaded, summary.failed)) {
                logger_->Info("server notified: resources ready");
                EmitServiceEvent("resources-ready-notification", "ok", "server acknowledged");
            } else {
                logger_->Warn("server did not acknowledge resources-ready notification");
                EmitServiceEvent("resources-ready-notification", "failed", "no acknowledgment received");
            }
        }
    } else {
        EmitServiceEvent("resource-loading", "not_implemented", "no resources were downloaded/verified to load");
    }

    // 9. Ready. Never transitions to Playing: that would require
    // GameIntegration::Connect(), which is honestly NotImplemented.
    lifecycle_.TransitionTo(RuntimeState::Ready);
    EmitStateEvent(ToString(lifecycle_.GetState()));
    SetState(ClientState::Ready);
    logger_->Info("client runtime ready, idling");

    if (connectedToServer_) {
        heartbeatShouldStop_ = false;
        heartbeatThread_ = std::thread([this] {
            while (!heartbeatShouldStop_) {
                for (int waited = 0; waited < kHeartbeatIntervalMs && !heartbeatShouldStop_; waited += 100) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (heartbeatShouldStop_) break;
                if (protocol_) protocol_->SendHeartbeat();
            }
        });
    }

    if (replication_) {
        replicationShouldStop_ = false;
        replicationThread_ = std::thread([this] {
            // Ticks far more often than syncIntervalMs_ -- Replication::Tick()
            // itself throttles actual sends, this just keeps latency between
            // a local SetLocalPosition() call and the wire send low.
            while (!replicationShouldStop_) {
                replication_->Tick(Replication::NowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }

    std::string line;
    while (std::getline(controlInput, line)) {
        HandleControlCommand(line);
    }

    if (kicked_) {
        logger_->Warn("session ended because the server kicked this client: " + kickReason_);
    }

    return Shutdown();
}

int ClientRuntime::Shutdown() {
    lifecycle_.TransitionTo(RuntimeState::ShuttingDown);
    SetState(ClientState::Disconnecting);
    if (logger_) logger_->Info("shutting down");
    EmitStateEvent(ToString(lifecycle_.GetState()));

    heartbeatShouldStop_ = true;
    if (heartbeatThread_.joinable()) heartbeatThread_.join();

    replicationShouldStop_ = true;
    if (replicationThread_.joinable()) replicationThread_.join();

    if (connectedToServer_ && protocol_) {
        disconnectRequested_ = true;
        protocol_->SendDisconnect("client shutting down");
    }
    if (deps_.server && deps_.server->IsConnected()) {
        deps_.server->Disconnect();
    }
    if (deps_.game) {
        deps_.game->Shutdown();
    }

    lifecycle_.TransitionTo(RuntimeState::Stopped);
    SetState(ClientState::Stopped);
    if (logger_) logger_->Info("stopped");
    EmitStateEvent(ToString(lifecycle_.GetState()));

    return static_cast<int>(ExitCode::Success);
}

void ClientRuntime::HandleControlCommand(const std::string& line) {
    if (!replication_) return;

    std::istringstream iss(line);
    std::string command;
    iss >> command;

    if (command == "setpos") {
        Vector3 position;
        if (iss >> position.x >> position.y >> position.z) {
            replication_->SetLocalPosition(position);
        }
    } else if (command == "setrot") {
        Rotation3 rotation;
        if (iss >> rotation.pitch >> rotation.yaw >> rotation.roll) {
            replication_->SetLocalRotation(rotation);
        }
    }
    // Unrecognized commands are ignored -- reserved for future control
    // commands from the bootstrapper.
}

}



