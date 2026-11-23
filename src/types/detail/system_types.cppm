module;

#include <cstdint>
#include <functional>

export module arche:types.detail.system_types;

export namespace arche {

struct SystemContext;

using SystemFunction = std::function<void(SystemContext &)>;

enum class SystemScale : uint8_t { Narrow, Wide, Sequential };

} // namespace arche
