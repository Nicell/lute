#include "lute/ui/Platform.h"

namespace lute::ui
{

#if !defined(__APPLE__)
bool runNativeShell(std::shared_ptr<UiContext>, std::string* error)
{
    if (error)
        *error = "Lute UI native shell is currently implemented only on macOS";
    return false;
}
#endif

} // namespace lute::ui
