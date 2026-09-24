#pragma once
#include <optional>
#include <string>

#include "ErrorHandling.h"

namespace mzzplork {

// Unlike Network/Resources/GTA integration/ServerConnection, a local disk
// cache doesn't depend on any system that hasn't been built yet -- it's
// genuinely implementable now, so it is a real implementation rather than
// a NotImplemented stub.
class ICacheService {
public:
    virtual ~ICacheService() = default;
    virtual ServiceResult Initialize(const std::string& appDataRoot) = 0;
    virtual bool Has(const std::string& key) const = 0;
    virtual bool Put(const std::string& key, const std::string& data) = 0;
    virtual std::optional<std::string> Get(const std::string& key) const = 0;
    virtual bool Remove(const std::string& key) = 0;
};

// Keys are sanitized to a safe filename before touching disk -- caller
// input (a resource id, say) should never be trusted as a raw path
// component.
class FileCacheService : public ICacheService {
public:
    ServiceResult Initialize(const std::string& appDataRoot) override;
    bool Has(const std::string& key) const override;
    bool Put(const std::string& key, const std::string& data) override;
    std::optional<std::string> Get(const std::string& key) const override;
    bool Remove(const std::string& key) override;

private:
    std::string PathFor(const std::string& key) const;
    std::string cacheDir_;
};

}
