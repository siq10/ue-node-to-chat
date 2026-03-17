#include "UENode2ChatCommands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "FUENode2ChatCommands"

FUENode2ChatCommands::FUENode2ChatCommands()
    : TCommands<FUENode2ChatCommands>(
        TEXT("UENode2Chat"),
        NSLOCTEXT("Contexts", "UENode2Chat", "Blueprint DSL Plugin"),
        NAME_None,
        FEditorStyle::GetStyleSetName()
    )
{
}

void FUENode2ChatCommands::RegisterCommands()
{
    UI_COMMAND(CopyDSL, "Copy DSL", "Copy selected Blueprint nodes as DSL", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::One));
    UI_COMMAND(PasteDSL, "Paste DSL", "Paste selected Blueprint nodes as graph nodes in bp graph", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::Two));

}

#undef LOCTEXT_NAMESPACE
