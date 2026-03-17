#pragma once

#include "Framework/Commands/Commands.h"

class FUENode2ChatCommands : public TCommands<FUENode2ChatCommands>
{
public:
    FUENode2ChatCommands();

    virtual void RegisterCommands() override;

public:
    TSharedPtr<FUICommandInfo> CopyDSL;
    TSharedPtr<FUICommandInfo> PasteDSL;

};
