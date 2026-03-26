#pragma once

#include "cli/CliCommand.h"

namespace qodex::cli {

class HelpCommand final : public CliCommand {
public:
    [[nodiscard]] QStringList commandPath() const override;
    [[nodiscard]] QString summary() const override;
    [[nodiscard]] QString usage() const override;
    [[nodiscard]] QString description() const override;
    int run(const QStringList &arguments, const CliContext &context) const override;
};

}  // namespace qodex::cli
