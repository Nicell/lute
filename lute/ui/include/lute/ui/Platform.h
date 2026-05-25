#pragma once

#include "lute/ui/Context.h"

#include <memory>
#include <string>

namespace lute::ui
{

bool runNativeShell(std::shared_ptr<UiContext> context, std::string* error);

} // namespace lute::ui
