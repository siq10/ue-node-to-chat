#pragma once
#include "Modules/ModuleManager.h"
#include "Framework/Commands/UICommandList.h"
#include "BlueprintEditorModule.h"

class FUENode2ChatModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    void CopySelectedNodesAsDSL();
    void BuildAndCopyDSL(const FGraphPanelSelectionSet& Selection, UEdGraph* Graph);
    void PasteNodesFromDSL();
    FVector2D GetPasteOrigin(UEdGraph* Graph);
private:

    TSharedPtr<FUICommandList> CommandList;
    FDelegateHandle BlueprintEditorOpenedHandle;
};