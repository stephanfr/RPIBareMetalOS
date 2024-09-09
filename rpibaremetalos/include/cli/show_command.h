// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli
{
    class ShowCommandDispatcher : public CLICommandDispatcher
    {
    public:
        ShowCommandDispatcher()
            : CLICommandDispatcher("show")
        {
        }

        virtual void DispatchCommand(CommandParser &parser,
                                     CLISessionContext &context) override;

    private:
        void ShowDiagnostics(CommandParser &parser,
                             CLISessionContext &context);

    };
} // namespace cli
