#include "UENode2Chat.h"
#include "UENode2ChatCommands.h"

#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditorModule.h"
#include "EdGraph/EdGraph.h"

#include "Framework/Commands/UICommandList.h"
#include "Misc/Compression.h"
#include "Misc/Base64.h"
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"

#include "BlueprintEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "HAL/PlatformApplicationMisc.h"
#include "K2Node_CallFunction.h"
#include "Modules/ModuleManager.h"
#include "EdGraphSchema_K2.h"
#include <GraphEditorModule.h>
#include "LevelEditor.h"
#include "GraphEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Timeline.h"
#include "Editor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_FunctionEntry.h"


#define LOCTEXT_NAMESPACE "FUENode2ChatModule"
void FUENode2ChatModule::StartupModule()
{
    FUENode2ChatCommands::Register();

    CommandList = MakeShareable(new FUICommandList);
    CommandList->MapAction(
        FUENode2ChatCommands::Get().CopyDSL,
        FExecuteAction::CreateRaw(this, &FUENode2ChatModule::CopySelectedNodesAsDSL)
    );
    CommandList->MapAction(
        FUENode2ChatCommands::Get().PasteDSL,
        FExecuteAction::CreateRaw(this, &FUENode2ChatModule::PasteNodesFromDSL)
    );

    // Bind into every Blueprint Editor instance as it opens
    FBlueprintEditorModule& BlueprintEditorModule =
        FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");

    BlueprintEditorOpenedHandle = BlueprintEditorModule.OnBlueprintEditorOpened().AddLambda(
        [this](EBlueprintType)
        {
            // Get the currently active Blueprint Editor
            UAssetEditorSubsystem* AssetEditorSubsystem =
                GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();

            TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
            for (UObject* Asset : EditedAssets)
            {
                IAssetEditorInstance* EditorInstance =
                    AssetEditorSubsystem->FindEditorForAsset(Asset, false);

                FBlueprintEditor* BPEditor =
                    static_cast<FBlueprintEditor*>(EditorInstance);

                if (BPEditor)
                {
                    BPEditor->GetToolkitCommands()->Append(CommandList.ToSharedRef());
                }
            }
        });

    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: StartupModule complete"));
}

void FUENode2ChatModule::ShutdownModule()
{
    FBlueprintEditorModule* BlueprintEditorModule =
        FModuleManager::GetModulePtr<FBlueprintEditorModule>("Kismet");

    if (BlueprintEditorModule)
    {
        BlueprintEditorModule->OnBlueprintEditorOpened().Remove(BlueprintEditorOpenedHandle);
    }

    FUENode2ChatCommands::Unregister();

    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: ShutdownModule complete"));
}

void FUENode2ChatModule::BuildAndCopyDSL(
    const FGraphPanelSelectionSet& Selection, UEdGraph* Graph)
{
    TArray<UEdGraphNode*> Nodes;
    TMap<UEdGraphNode*, int32> IndexMap;

    int32 Idx = 0;
    for (UObject* Obj : Selection)
    {
        if (UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
        {
            Nodes.Add(Node);
            IndexMap.Add(Node, Idx++);
        }
    }

    if (Nodes.Num() == 0) return;

    auto Esc = [](const FString& S) -> FString {
        FString O = S;
        O.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
        O.ReplaceInline(TEXT("\""), TEXT("\\\""));
        O.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        return O;
        };

    const FString BP = Graph->GetOuter() ? Graph->GetOuter()->GetName() : TEXT("");
    const FString GN = Graph->GetName();

    FString Out;
    Out += FString::Printf(TEXT("BP:%s Graph:%s\n"), *BP, *GN);
    Out += TEXT("---\n");

    for (UEdGraphNode* Node : Nodes)
    {
        const int32 Id = IndexMap[Node];

        // Human-readable title instead of internal object name
        FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
        Title.ReplaceInline(TEXT("\n"), TEXT(" "));
        Title.ReplaceInline(TEXT("\r"), TEXT(""));
        Out += FString::Printf(TEXT("[%d] %s (%s)\n"), Id, *Title, *Node->GetClass()->GetName());


        // Node-specific semantic info
        if (UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(Node))
        {
            UClass* PC = CF->FunctionReference.GetMemberParentClass(nullptr);
            FString ParentName = PC ? PC->GetName() : BP;
            Out += FString::Printf(TEXT("  fn: %s::%s\n"),
                *ParentName,
                *CF->FunctionReference.GetMemberName().ToString());
        }
        else if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(Node))
            Out += FString::Printf(TEXT("  get: %s\n"), *VG->GetVarName().ToString());
        else if (UK2Node_VariableSet* VS = Cast<UK2Node_VariableSet>(Node))
            Out += FString::Printf(TEXT("  set: %s\n"), *VS->GetVarName().ToString());
        else if (UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node))
            Out += FString::Printf(TEXT("  event: %s\n"), *CE->CustomFunctionName.ToString());
        else if (UK2Node_Event* EV = Cast<UK2Node_Event>(Node))
        {
            const UFunction* Sig = EV->FindEventSignatureFunction();
            Out += FString::Printf(TEXT("  event: %s\n"), Sig ? *Sig->GetName() : TEXT("?"));
        }
        else if (UK2Node_MacroInstance* MA = Cast<UK2Node_MacroInstance>(Node))
            Out += FString::Printf(TEXT("  macro: %s\n"), *MA->GetMacroGraph()->GetName());
        else if (UK2Node_Timeline* TL = Cast<UK2Node_Timeline>(Node))
            Out += FString::Printf(TEXT("  timeline: %s\n"), *TL->TimelineName.ToString());
        else if (Cast<UK2Node_IfThenElse>(Node))
        {
            // no type-specific line needed, class name is enough for paste
        }
        else if (UK2Node_PromotableOperator* PO = Cast<UK2Node_PromotableOperator>(Node))
        {
            UFunction* ResolvedFn = PO->GetTargetFunction();
            UClass* PC = ResolvedFn ? ResolvedFn->GetOuterUClass() : nullptr;
            FString ParentName = PC ? PC->GetName() : BP;
            FString FnName = ResolvedFn ? ResolvedFn->GetName() : PO->FunctionReference.GetMemberName().ToString();
            Out += FString::Printf(TEXT("  fn: %s::%s\n"), *ParentName, *FnName);
        }
        if (!Node->NodeComment.IsEmpty())
            Out += FString::Printf(TEXT("  comment: %s\n"), *Esc(Node->NodeComment));

        // Only output pins that are connected or have non-default values
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin) continue;
            const bool bConnected = Pin->LinkedTo.Num() > 0;
            const bool bHasDefault = !Pin->DefaultValue.IsEmpty()
                && Pin->DefaultValue != Pin->AutogeneratedDefaultValue;
            const bool bIsExec = Pin->PinType.PinCategory == TEXT("exec");

            // Always include exec pins if connected, and all connected/default pins
            if (!bConnected && !bHasDefault) continue;


            const FString Dir = Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out");
            const FString Cat = Pin->PinType.PinCategory.ToString();

            Out += FString::Printf(TEXT("  pin %s [%s:%s]"),
                *Pin->PinName.ToString(), *Dir, *Cat);

            if (bHasDefault)
                Out += FString::Printf(TEXT(" = %s"), *Esc(Pin->DefaultValue));

            if (bConnected && Pin->Direction == EGPD_Output)
            {
                Out += TEXT(" -> ");
                for (UEdGraphPin* LP : Pin->LinkedTo)
                {
                    if (!LP || !LP->GetOwningNode()) continue;
                    const int32* LIdx = IndexMap.Find(LP->GetOwningNode());
                    // Mark external connections with * so LLM knows they exist
                    Out += LIdx
                        ? FString::Printf(TEXT("[%d].%s "), *LIdx, *LP->PinName.ToString())
                        : FString::Printf(TEXT("[ext].%s "), *LP->PinName.ToString());
                }
            }

            Out += TEXT("\n");
        }

        Out += TEXT("\n");
    }

    FPlatformApplicationMisc::ClipboardCopy(*Out);
    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Copied %d nodes to clipboard"), Nodes.Num());
}


void FUENode2ChatModule::CopySelectedNodesAsDSL()
{
    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: CopySelectedNodesAsDSL() fired!"));

    // ------------------------------------------------------------
    // 1. Get focused Blueprint Editor and its active graph
    // ------------------------------------------------------------
    TSharedPtr<SGraphEditor> GraphEditor = nullptr;

    UAssetEditorSubsystem* AssetEditorSS = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    for (UObject* Asset : AssetEditorSS->GetAllEditedAssets())
    {
        IAssetEditorInstance* Inst = AssetEditorSS->FindEditorForAsset(Asset, false);
        FBlueprintEditor* BPEditor = static_cast<FBlueprintEditor*>(Inst);
        if (BPEditor)
        {
            if (UEdGraph* ActiveGraph = BPEditor->GetFocusedGraph())
            {
                const FGraphPanelSelectionSet Selection = BPEditor->GetSelectedNodes();
                if (Selection.Num() > 0)
                {
                    BuildAndCopyDSL(Selection, ActiveGraph);
                    return;
                }
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: No active Blueprint Editor with selection found."));
}

void FUENode2ChatModule::PasteNodesFromDSL()
{
    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: PasteNodesFromDSL() fired!"));

    // ------------------------------------------------------------
    // 1. Get clipboard text
    // ------------------------------------------------------------
    FString ClipboardText;
    FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

    if (ClipboardText.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Clipboard is empty."));
        return;
    }

    // Quick sanity check — our DSL always starts with "BP:"
    if (!ClipboardText.StartsWith(TEXT("BP:")))
    {
        UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Clipboard does not contain Node2Chat data."));
        return;
    }

    // ------------------------------------------------------------
    // 2. Get active Blueprint Editor and focused graph
    // ------------------------------------------------------------
    FBlueprintEditor* BPEditor = nullptr;
    UEdGraph* ActiveGraph = nullptr;

    UAssetEditorSubsystem* AssetEditorSS = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    for (UObject* Asset : AssetEditorSS->GetAllEditedAssets())
    {
        if (!Asset->IsA<UBlueprint>()) 
            continue; // skip non-blueprints
        IAssetEditorInstance* Inst = AssetEditorSS->FindEditorForAsset(Asset, false);
        FBlueprintEditor* Candidate = static_cast<FBlueprintEditor*>(Inst);
        if (Candidate)
        {
            UEdGraph* Graph = Candidate->GetFocusedGraph();
            if (Graph)
            {
                BPEditor = Candidate;
                ActiveGraph = Graph;
                break;
            }
        }
    }

    if (!BPEditor || !ActiveGraph)
    {
        UE_LOG(LogTemp, Warning, TEXT("Node2Chat: No active Blueprint Editor found."));
        return;
    }

    UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(ActiveGraph);
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Could not find Blueprint for active graph."));
        return;
    }

    // ------------------------------------------------------------
    // 3. Parse DSL lines
    // ------------------------------------------------------------
    TArray<FString> Lines;
    ClipboardText.ParseIntoArrayLines(Lines, false);

    // Each node block looks like:
    // [0] Print String (K2Node_CallFunction)
    //   fn: KismetSystemLibrary::PrintString
    //   pin exec [in:exec] -> [1].exec
    //   pin InString [in:string] = Hello

    struct FParsedPin
    {
        FString Name;
        FString Direction; // "in" or "out"
        FString Category;
        FString DefaultValue;
        TArray<TPair<int32, FString>> Links; // NodeId, PinName — within selection only
    };

    struct FParsedNode
    {
        int32 Id = -1;
        bool bUpdate = false;  // true if [Id!]
        FString Title;
        FString Class;
        FString FunctionParent;
        FString FunctionName;
        FString VarName;       // for get/set
        FString EventName;
        FString MacroName;
        FString TimelineName;
        FString Comment;
        TArray<FParsedPin> Pins;
    };

    TArray<FParsedNode> ParsedNodes;
    FParsedNode* CurrentNode = nullptr;

    for (const FString& RawLine : Lines)
    {
        const FString Line = RawLine.TrimStartAndEnd();

        if (Line.IsEmpty() || Line == TEXT("---"))
            continue;

        // Skip header line "BP:X Graph:Y"
        if (Line.StartsWith(TEXT("BP:")))
            continue;

        // comment: text
        if (Line.StartsWith(TEXT("comment: ")))
        {
            CurrentNode->Comment = Line.Mid(9);
            continue;
        }

        // Node header: [0] Title (ClassName)
        if (Line.StartsWith(TEXT("[")))
        {
            ParsedNodes.AddDefaulted();
            CurrentNode = &ParsedNodes.Last();

            // Parse [Id] Title (Class)
            int32 CloseBracket;
            if (Line.FindChar(']', CloseBracket))
            {
                FString IdStr = Line.Mid(1, CloseBracket - 1);
                // Check for update flag
                if (IdStr.EndsWith(TEXT("!")))
                {
                    CurrentNode->bUpdate = true;
                    IdStr = IdStr.LeftChop(1);
                }
                CurrentNode->Id = FCString::Atoi(*IdStr);

                FString Rest = Line.Mid(CloseBracket + 2); // skip "] "
                int32 OpenParen;
                if (Rest.FindLastChar('(', OpenParen))
                {
                    CurrentNode->Title = Rest.Left(OpenParen).TrimEnd();
                    FString ClassAndRest = Rest.Mid(OpenParen + 1);

                    int32 CloseParen;
                    if (ClassAndRest.FindChar(')', CloseParen))
                        CurrentNode->Class = ClassAndRest.Left(CloseParen);
                    else
                        CurrentNode->Class = ClassAndRest.LeftChop(1);

                    //UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Parsed node id=%d class='%s' title='%s'"),
                        //CurrentNode->Id, *CurrentNode->Class, *CurrentNode->Title);

                }
                else
                {
                    CurrentNode->Title = Rest;
                }
            }
            UE_LOG(LogTemp, Warning,
                TEXT("Node2Chat: Parsed [%d%s] title='%s' class='%s'"),
                CurrentNode->Id,
                CurrentNode->bUpdate ? TEXT("!") : TEXT(""),
                *CurrentNode->Title,
                *CurrentNode->Class);
            continue;
        }

        if (!CurrentNode) continue;

        // fn: ParentClass::FunctionName
        if (Line.StartsWith(TEXT("fn: ")))
        {
            FString FnStr = Line.Mid(4);
            FString Left, Right;
            if (FnStr.Split(TEXT("::"), &Left, &Right))
            {
                CurrentNode->FunctionParent = Left;
                CurrentNode->FunctionName = Right;
            }
            else
            {
                CurrentNode->FunctionName = FnStr;
            }
            continue;
        }

        // get: VarName / set: VarName
        if (Line.StartsWith(TEXT("get: ")) || Line.StartsWith(TEXT("set: ")))
        {
            CurrentNode->VarName = Line.Mid(5).TrimStartAndEnd();;
            continue;
        }

        // event: EventName
        if (Line.StartsWith(TEXT("event: ")))
        {
            CurrentNode->EventName = Line.Mid(7);
            continue;
        }

        // macro: MacroName
        if (Line.StartsWith(TEXT("macro: ")))
        {
            CurrentNode->MacroName = Line.Mid(7);
            continue;
        }

        // timeline: TimelineName
        if (Line.StartsWith(TEXT("timeline: ")))
        {
            CurrentNode->TimelineName = Line.Mid(10);
            continue;
        }

        // pin PinName [dir:category] = default -> [NodeId].PinName
        if (Line.StartsWith(TEXT("pin ")))
        {
            FParsedPin Pin;
            FString Rest = Line.Mid(4);

            // Extract pin name (up to first '[')
            int32 OpenBracket;
            if (Rest.FindChar('[', OpenBracket))
            {
                Pin.Name = Rest.Left(OpenBracket).TrimEnd();
                Rest = Rest.Mid(OpenBracket + 1);

                // Extract dir:category up to ']'
                int32 CloseBr;
                if (Rest.FindChar(']', CloseBr))
                {
                    FString TypeStr = Rest.Left(CloseBr);
                    FString Dir, Cat;
                    if (TypeStr.Split(TEXT(":"), &Dir, &Cat))
                    {
                        Pin.Direction = Dir;
                        Pin.Category = Cat;
                    }
                    Rest = Rest.Mid(CloseBr + 1).TrimStart();
                }
            }

            // Default value: = value (before ->)
            if (Rest.StartsWith(TEXT("= ")))
            {
                Rest = Rest.Mid(2);
                int32 ArrowIdx = Rest.Find(TEXT("->"));
                if (ArrowIdx != INDEX_NONE)
                {
                    Pin.DefaultValue = Rest.Left(ArrowIdx).TrimEnd();
                    Rest = Rest.Mid(ArrowIdx + 3).TrimStart();
                }
                else
                {
                    Pin.DefaultValue = Rest.TrimEnd();
                    Rest = TEXT("");
                }
            }
            else if (Rest.StartsWith(TEXT("-> ")))
            {
                Rest = Rest.Mid(3);
            }

            // Links: [NodeId].PinName [NodeId].PinName ...
            // Skip [ext] links
            // 
            // Parse all link tokens — handle both space and trailing space
            FString LinkRest = Rest.TrimEnd(); // strip trailing whitespace/newline
            while (!LinkRest.IsEmpty())
            {
                LinkRest.TrimStartInline();
                if (LinkRest.StartsWith(TEXT("->")))
                {
                    LinkRest = LinkRest.Mid(2);
                    LinkRest.TrimStartInline();
                }
                if (LinkRest.IsEmpty()) break;

                // Skip [ext].PinName
                if (LinkRest.StartsWith(TEXT("[ext]")))
                {
                    int32 SpaceIdx = LinkRest.Find(TEXT(" "));
                    LinkRest = SpaceIdx != INDEX_NONE ? LinkRest.Mid(SpaceIdx + 1) : TEXT("");
                    continue;
                }

                if (!LinkRest.StartsWith(TEXT("["))) break;


                int32 CloseBr = INDEX_NONE;
                int32 Dot = INDEX_NONE;
                LinkRest.FindChar(']', CloseBr);
                if (CloseBr != INDEX_NONE)
                    LinkRest.FindChar('.', Dot);
                if (CloseBr == INDEX_NONE || Dot == INDEX_NONE || Dot != CloseBr + 1) break;

                FString IdPart = LinkRest.Mid(1, CloseBr - 1);
                int32 LinkedId = FCString::Atoi(*IdPart);

                // Pin name starts after Dot, ends at next space or end
                FString AfterDot = LinkRest.Mid(Dot + 1).TrimEnd();
                int32 SpaceIdx = AfterDot.Find(TEXT(" "));
                FString PinPart = SpaceIdx != INDEX_NONE
                    ? AfterDot.Left(SpaceIdx)
                    : AfterDot;

                PinPart.TrimEndInline(); // strip any \r or whitespace

                Pin.Links.Add(TPair<int32, FString>(LinkedId, PinPart));
                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Parsed link -> [%d].%s"), LinkedId, *PinPart);


                int32 TokenLen = Dot + 1 + (SpaceIdx != INDEX_NONE ? SpaceIdx : AfterDot.Len());
                LinkRest = LinkRest.Mid(TokenLen);
                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: LinkRest after advance: '%s'"), *LinkRest);
            }

            CurrentNode->Pins.Add(Pin);
            continue;
        }
    }

    if (ParsedNodes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Node2Chat: No nodes parsed from clipboard."));
        return;
    }

    // Helper: find existing node in graph by class + title
auto FindExistingNode = [&](const FParsedNode& Parsed) -> UEdGraphNode*
{
    for (UEdGraphNode* Node : ActiveGraph->Nodes)
    {
        if (!Node) continue;
        if (Node->GetClass()->GetName() != Parsed.Class) continue;
        const FString ExistingTitle = Node->GetNodeTitle(
            ENodeTitleType::FullTitle).ToString();
        UE_LOG(LogTemp, Warning, TEXT("Node2Chat: FindExisting checking '%s' vs '%s'"),
            *ExistingTitle, *Parsed.Title);
        if (ExistingTitle == Parsed.Title)
            return Node;
    }
    return nullptr;
};

TMap<int32, UEdGraphNode*> CreatedNodes;


const int32 GridStepY = 150;

// Try to get cursor position in graph space
TSharedPtr<SGraphEditor> GraphEdWidget;
TArray<TSharedRef<SWindow>> Windows;
FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
FVector2D PasteOrigin;
int32 GridX;
int32 GridY;

TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
if (ActiveWindow.IsValid())
{
    TArray<TSharedRef<SWidget>> Stack;
    Stack.Add(ActiveWindow->GetContent());

    while (Stack.Num() > 0)
    {
        TSharedRef<SWidget> Current = Stack.Pop();
        if (Current->GetTypeAsString() == TEXT("SGraphEditor"))
        {
            GraphEdWidget = StaticCastSharedRef<SGraphEditor>(Current);
            break;
        }
        FChildren* Children = Current->GetChildren();
        if (Children)
            for (int32 i = 0; i < Children->Num(); i++)
                Stack.Add(Children->GetChildAt(i));
    }
}
if (GraphEdWidget.IsValid())
{
    const FVector2D GraphCursor = GraphEdWidget->GetPasteLocation();

    PasteOrigin = GraphCursor;
    GridX = (int32)PasteOrigin.X;
    GridY = (int32)PasteOrigin.Y;
}
for (const FParsedNode& Parsed : ParsedNodes)
{
    UEdGraphNode* ResultNode = nullptr;

    // --- UPDATE existing node ---
    if (Parsed.bUpdate)
    {
        ResultNode = FindExistingNode(Parsed);
        if (ResultNode)
        {
            ResultNode->Modify();
            UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Updating [%d] '%s'"), Parsed.Id, *Parsed.Title);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Node2Chat: Update target [%d] '%s' not found — will create new"),
                Parsed.Id, *Parsed.Title);
            // Fall through to create block below
            // Don't set bUpdate to false here, just let ResultNode stay null
            // and fall into the create logic
        }
    }
    if (!ResultNode)
    // --- CREATE new node ---
    {
        const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

        if (Parsed.Class == TEXT("K2Node_CallFunction") && !Parsed.FunctionName.IsEmpty())
        {
            UClass* ParentClass = nullptr;
            if (!Parsed.FunctionParent.IsEmpty())
            {
                FString FullPath = FString::Printf(TEXT("/Script/Engine.%s"), *Parsed.FunctionParent);
                ParentClass = FindObject<UClass>(nullptr, *FullPath);
                if (!ParentClass)
                {
                    // Try other common packages
                    TArray<FString> Packages = {
                        TEXT("/Script/Engine."),
                        TEXT("/Script/GameplayAbilities."),
                        TEXT("/Script/UMG."),
                        TEXT("/Script/AIModule.")
                        };
                    for (const FString& Pkg : Packages)
                    {
                        ParentClass = FindObject<UClass>(nullptr, *(Pkg + Parsed.FunctionParent));
                        if (ParentClass) break;
                    }
                }
                // Final fallback - iterator
                if (!ParentClass)
                {
                    for (TObjectIterator<UClass> It; It; ++It)
                    {
                        if (It->GetName() == Parsed.FunctionParent)
                        {
                            ParentClass = *It;
                            break;
                        }
                    }
                }
            }
            if (!ParentClass)
                ParentClass = Blueprint->GeneratedClass; // fallback for BP-defined functions
            UFunction* Function = ParentClass
                ? ParentClass->FindFunctionByName(FName(*Parsed.FunctionName))
                : nullptr;

            if (Function)
            {
                UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(ActiveGraph);
                CallNode->SetFromFunction(Function);
                ActiveGraph->AddNode(CallNode, false, false);
                CallNode->CreateNewGuid();
                CallNode->PostPlacedNewNode();
                CallNode->AllocateDefaultPins();
                ResultNode = CallNode;
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("Node2Chat: Function %s::%s not found, skipping."),
                    *Parsed.FunctionParent, *Parsed.FunctionName);
            }
        }
        else if (Parsed.Class == TEXT("K2Node_FunctionEntry"))
        {
            // Find existing FunctionEntry in graph
            for (UEdGraphNode* Node : ActiveGraph->Nodes)
            {
                if (Node && Node->GetClass()->GetName() == TEXT("K2Node_FunctionEntry"))
                {
                    ResultNode = Node;
                    ResultNode->Modify();
                    break;
                }
            }
            if (!ResultNode)
                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: FunctionEntry not found in graph"));
        }
        else if (Parsed.Class == TEXT("K2Node_FunctionResult"))
        {
            for (UEdGraphNode* Node : ActiveGraph->Nodes)
            {
                if (Node && Node->GetClass()->GetName() == TEXT("K2Node_FunctionResult"))
                {
                    ResultNode = Node;
                    ResultNode->Modify();
                    break;
                }
            }
            if (!ResultNode)
                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: FunctionResult not found in graph"));
        }
        else if (Parsed.Class == TEXT("K2Node_PromotableOperator") && !Parsed.FunctionName.IsEmpty())
        {
            UClass* ParentClass = nullptr;
            if (!Parsed.FunctionParent.IsEmpty())
            {
                FString FullPath = FString::Printf(TEXT("/Script/Engine.%s"), *Parsed.FunctionParent);
                ParentClass = FindObject<UClass>(nullptr, *FullPath);
                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: FindObject result: %s"), ParentClass ? TEXT("found") : TEXT("null"));

                if (!ParentClass)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: KismetMathLibrary load failed, trying iterator"));
                    for (TObjectIterator<UClass> It; It; ++It)
                    {
                        if (It->GetName() == Parsed.FunctionParent)
                        {
                            ParentClass = *It;
                            UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Found class via iterator: %s"), *It->GetPathName());
                            break;
                        }
                    }
                }
            }

            if (ParentClass)
            {
                UFunction* Function = ParentClass->FindFunctionByName(FName(*Parsed.FunctionName));
                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: FindFunctionByName '%s' result: %s"),
                    *Parsed.FunctionName, Function ? TEXT("found") : TEXT("null"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: ParentClass still null after all lookups"));
            }

            UFunction* Function = ParentClass
                ? ParentClass->FindFunctionByName(FName(*Parsed.FunctionName))
                : nullptr;
            if (!Function)
            {
                // UE5 renamed float functions to double — try remapping
                FString Remapped = Parsed.FunctionName;
                Remapped.ReplaceInline(TEXT("_FloatFloat"), TEXT("_DoubleDouble"));
                Remapped.ReplaceInline(TEXT("_FloatInt"), TEXT("_DoubleInt"));
                Remapped.ReplaceInline(TEXT("_IntFloat"), TEXT("_IntDouble"));
                Function = ParentClass->FindFunctionByName(FName(*Remapped));
                if (Function)
                    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Remapped '%s' to '%s'"),
                        *Parsed.FunctionName, *Remapped);
            }
            if (Function)
            {
                UK2Node_PromotableOperator* OpNode = NewObject<UK2Node_PromotableOperator>(ActiveGraph);
                OpNode->SetFromFunction(Function);
                ActiveGraph->AddNode(OpNode, false, false);
                OpNode->CreateNewGuid();
                OpNode->PostPlacedNewNode();
                OpNode->AllocateDefaultPins();
                OpNode->Modify();
                ResultNode = OpNode;
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("Node2Chat: PromotableOperator function %s::%s not found, skipping."),
                    *Parsed.FunctionParent, *Parsed.FunctionName);
            }
        }
        else if (Parsed.Class == TEXT("K2Node_VariableGet") && !Parsed.VarName.IsEmpty())
        {
            UK2Node_VariableGet* VarNode = NewObject<UK2Node_VariableGet>(ActiveGraph);
            VarNode->VariableReference.SetSelfMember(FName(*Parsed.VarName));

            FProperty* Prop = Blueprint->GeneratedClass
                ? Blueprint->GeneratedClass->FindPropertyByName(FName(*Parsed.VarName))
                : nullptr;
            UE_LOG(LogTemp, Warning, TEXT("Node2Chat: VarGet '%s' — class prop %s"),
                *Parsed.VarName, Prop ? TEXT("found") : TEXT("not found"));
            if (Prop)
            {
                VarNode->VariableReference.SetFromField<FProperty>(Prop, true);
            }
            else
            {
                UEdGraph* SearchGraph = FBlueprintEditorUtils::GetTopLevelGraph(ActiveGraph);
                FBPVariableDescription* LocalVar = FBlueprintEditorUtils::FindLocalVariable(
                    Blueprint, SearchGraph, FName(*Parsed.VarName), nullptr);

                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: SearchGraph: '%s'"), *SearchGraph->GetName());
                UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Local var '%s' — %s"),
                    *Parsed.VarName, LocalVar ? TEXT("found") : TEXT("not found"));
                // Dump all local vars for debugging
                TArray<UK2Node_FunctionEntry*> FunctionEntryNodes;
                SearchGraph->GetNodesOfClass<UK2Node_FunctionEntry>(FunctionEntryNodes);
                for (UK2Node_FunctionEntry* EntryNode : FunctionEntryNodes)
                {
                    for (const FBPVariableDescription& LocalVarDesc : EntryNode->LocalVariables)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Found local var: '%s' guid=%s"),
                            *LocalVarDesc.VarName.ToString(), *LocalVarDesc.VarGuid.ToString());
                    }
                }
                if (LocalVar)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Local var guid: %s"),
                        *LocalVar->VarGuid.ToString());
                    VarNode->VariableReference.SetLocalMember(
                        FName(*Parsed.VarName), ActiveGraph->GetName(), LocalVar->VarGuid);
                }
            }

            ActiveGraph->AddNode(VarNode, false, false);
            VarNode->CreateNewGuid();
            VarNode->PostPlacedNewNode();
            VarNode->AllocateDefaultPins();
            VarNode->ReconstructNode();
            VarNode->Modify();
            ResultNode = VarNode;
        }
        else if (Parsed.Class == TEXT("K2Node_VariableSet") && !Parsed.VarName.IsEmpty())
        {
            UK2Node_VariableSet* VarNode = NewObject<UK2Node_VariableSet>(ActiveGraph);
            VarNode->VariableReference.SetSelfMember(FName(*Parsed.VarName));

            FProperty* Prop = Blueprint->GeneratedClass
                ? Blueprint->GeneratedClass->FindPropertyByName(FName(*Parsed.VarName))
                : nullptr;

            if (Prop)
            {
                VarNode->VariableReference.SetFromField<FProperty>(Prop, true);
            }
            else
            {
                FBPVariableDescription* LocalVar = nullptr;
                UEdGraph* SearchGraph = FBlueprintEditorUtils::GetTopLevelGraph(ActiveGraph);
                TArray<UK2Node_FunctionEntry*> FunctionEntryNodes;
                SearchGraph->GetNodesOfClass<UK2Node_FunctionEntry>(FunctionEntryNodes);
                for (UK2Node_FunctionEntry* EntryNode : FunctionEntryNodes)
                {
                    if (!EntryNode) continue;
                    for (FBPVariableDescription& VarDesc : EntryNode->LocalVariables)
                    {
                        if (VarDesc.VarName.ToString().TrimStartAndEnd() == Parsed.VarName)
                        {
                            LocalVar = &VarDesc;
                            break;
                        }
                    }
                    if (LocalVar) break;
                }

                if (LocalVar)
                {
                    VarNode->VariableReference.SetLocalMember(
                        FName(*Parsed.VarName), SearchGraph->GetName(), LocalVar->VarGuid);
                }
            }

            ActiveGraph->AddNode(VarNode, false, false);
            VarNode->CreateNewGuid();
            VarNode->PostPlacedNewNode();
            VarNode->AllocateDefaultPins();
            VarNode->ReconstructNode();
            VarNode->Modify();
            ResultNode = VarNode;
        }
        else if (Parsed.Class == TEXT("K2Node_CustomEvent") && !Parsed.EventName.IsEmpty())
        {
            UK2Node_CustomEvent* EventNode = UK2Node_CustomEvent::CreateFromFunction(
                FVector2D(GridX, GridY), ActiveGraph, Parsed.EventName, nullptr, false);
            ResultNode = EventNode;
        }
        else if (Parsed.Class == TEXT("K2Node_MacroInstance") && !Parsed.MacroName.IsEmpty())
        {
            // Search all graphs in the Blueprint for a matching macro
            UEdGraph* MacroGraph = nullptr;

            // Check standard library macros first
            TArray<UBlueprint*> BlueprintLibraries;
            UBlueprint* StandardLib = Cast<UBlueprint>(
                StaticLoadObject(UBlueprint::StaticClass(), nullptr,
                    TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")));
            if (!StandardLib)
                StandardLib = Cast<UBlueprint>(
                    StaticLoadObject(UBlueprint::StaticClass(), nullptr,
                        TEXT("/Engine/EngineMacros/StandardMacros.StandardMacros")));
            UE_LOG(LogTemp, Warning, TEXT("Node2Chat: StandardLib %s"),
                StandardLib ? TEXT("found") : TEXT("NOT FOUND - both paths failed"));
            if (StandardLib)
                BlueprintLibraries.Add(StandardLib);
///
            if (StandardLib)
            {
                BlueprintLibraries.Add(StandardLib);
                for (UEdGraph* G : StandardLib->MacroGraphs)
                    if (G) UE_LOG(LogTemp, Warning, TEXT("Node2Chat: StandardLib macro: '%s'"), *G->GetName());
            }
            ////
            // Also check the current Blueprint's own macros
            BlueprintLibraries.Add(Blueprint);

            for (UBlueprint* Lib : BlueprintLibraries)
            {
                if (!Lib) continue;
                for (UEdGraph* G : Lib->MacroGraphs)
                {
                    if (G && G->GetName() == Parsed.MacroName)
                    {
                        MacroGraph = G;
                        break;
                    }
                }
                if (MacroGraph) break;
            }

            if (MacroGraph)
            {
                UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(ActiveGraph);
                MacroNode->SetMacroGraph(MacroGraph);
                ActiveGraph->AddNode(MacroNode, false, false);
                MacroNode->CreateNewGuid();
                MacroNode->PostPlacedNewNode();
                MacroNode->AllocateDefaultPins();
                MacroNode->Modify();
                ResultNode = MacroNode;
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("Node2Chat: Macro graph '%s' not found, skipping."), *Parsed.MacroName);
                // Log all available macro graphs for debugging
                if (StandardLib)
                {
                    for (UEdGraph* G : StandardLib->MacroGraphs)
                        if (G) UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Available macro: '%s'"), *G->GetName());
                }
            }
        }
        else if (Parsed.Class == TEXT("K2Node_IfThenElse"))
        {
            UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Creating K2Node_IfThenElse"));
            UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(ActiveGraph);
            ActiveGraph->AddNode(BranchNode, false, false);
            BranchNode->CreateNewGuid();
            BranchNode->PostPlacedNewNode();
            BranchNode->AllocateDefaultPins();
            BranchNode->Modify();
            ResultNode = BranchNode;
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Node2Chat: Unhandled class '%s' for node [%d], skipping."),
                *Parsed.Class, Parsed.Id);
        }

    }
    if (ResultNode)
    {
        if (!Parsed.Comment.IsEmpty())
            ResultNode->NodeComment = Parsed.Comment;
        UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Adding [%d] '%s' to CreatedNodes"),
            Parsed.Id, *ResultNode->GetClass()->GetName());
        CreatedNodes.Add(Parsed.Id, ResultNode);
    }
}


// After CreatedNodes is fully populated, compute layout

// Step 1: build dependency depth (column) per node
// depth 0 = no inputs from within selection (leftmost)
TMap<int32, int32> NodeDepth;
for (const FParsedNode& P : ParsedNodes)
NodeDepth.Add(P.Id, 0);

// Iterate to propagate depth from output->input links
bool bChanged = true;
while (bChanged)
{
    bChanged = false;
    for (const FParsedNode& P : ParsedNodes)
    {
        for (const FParsedPin& Pin : P.Pins)
        {
            if (Pin.Direction != TEXT("out")) continue;
            for (const TPair<int32, FString>& Link : Pin.Links)
            {
                int32 TargetDepth = NodeDepth[P.Id] + 1;
                if (NodeDepth.Contains(Link.Key) && NodeDepth[Link.Key] < TargetDepth)
                {
                    NodeDepth[Link.Key] = TargetDepth;
                    bChanged = true;
                }
            }
        }
    }
}

// Step 2: group nodes by depth column
TMap<int32, TArray<int32>> Columns; // depth -> list of node ids
for (const FParsedNode& P : ParsedNodes)
Columns.FindOrAdd(NodeDepth[P.Id]).Add(P.Id);

// Step 3: assign positions
const int32 ColumnSpacing = 350;  // horizontal distance between columns
const int32 RowSpacing = 220;     // vertical distance between nodes in same column

for (auto& ColEntry : Columns)
{
    const int32 Depth = ColEntry.Key;
    TArray<int32>& ColNodes = ColEntry.Value;

    for (int32 i = 0; i < ColNodes.Num(); i++)
    {
        UEdGraphNode* Node = CreatedNodes.FindRef(ColNodes[i]);
        if (!Node) continue;

        Node->NodePosX = GridX + Depth * ColumnSpacing;
        Node->NodePosY = GridY + i * RowSpacing;
    }
}
    // ------------------------------------------------------------
    // 5. Wire pins between created nodes
    // ------------------------------------------------------------
        for (const FParsedNode& Parsed : ParsedNodes)
        {
            UEdGraphNode* SourceNode = CreatedNodes.FindRef(Parsed.Id);
            if (!SourceNode) continue;

            for (const FParsedPin& PinData : Parsed.Pins)
            {
                // Apply default values regardless of direction
                if (!PinData.DefaultValue.IsEmpty())
                {
                    UEdGraphPin* Pin = SourceNode->FindPin(FName(*PinData.Name));
                    if (Pin)
                        Pin->DefaultValue = PinData.DefaultValue;
                }

                // Output pin wiring
                if (PinData.Direction == TEXT("out"))
                {
                    UEdGraphPin* OutputPin = FindPinFuzzy(SourceNode, PinData.Name, EGPD_Output);
                    if (!OutputPin)
                    {
                        UE_LOG(LogTemp, Warning,
                            TEXT("Node2Chat: Output pin '%s' not found on [%d], available pins:"),
                            *PinData.Name, Parsed.Id);
                        for (UEdGraphPin* P : SourceNode->Pins)
                            if (P) UE_LOG(LogTemp, Warning, TEXT("  - '%s' dir=%d"),
                                *P->PinName.ToString(), (int32)P->Direction);
                        continue;
                    }
                    for (const TPair<int32, FString>& Link : PinData.Links)
                    {
                        UEdGraphNode* TargetNode = CreatedNodes.FindRef(Link.Key);
                        if (!TargetNode) continue;

                        UEdGraphPin* InputPin = FindPinFuzzy(TargetNode, Link.Value, EGPD_Input);
                        if (!InputPin)
                        {
                            UE_LOG(LogTemp, Warning,
                                TEXT("Node2Chat: Input pin '%s' not found on [%d], available pins:"),
                                *Link.Value, Link.Key);
                            for (UEdGraphPin* P : TargetNode->Pins)
                                if (P) UE_LOG(LogTemp, Warning, TEXT("  - '%s'"), *P->PinName.ToString());
                            continue;
                        }

                        UEdGraphPin* From = OutputPin;
                        UEdGraphPin* To = InputPin;
                        if (From->Direction == EGPD_Input)
                            Swap(From, To);

                        const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
                        const FPinConnectionResponse Response = K2Schema->CanCreateConnection(From, To);
                        if (Response.Response != CONNECT_RESPONSE_DISALLOW)
                        {
                            K2Schema->TryCreateConnection(From, To);
                        }
                        else
                        {
                            UE_LOG(LogTemp, Warning,
                                TEXT("Node2Chat: Cannot connect [%d].%s -> [%d].%s : %s"),
                                Parsed.Id, *PinData.Name,
                                Link.Key, *Link.Value,
                                *Response.Message.ToString());
                        }
                    }
                }
                // Input pin wiring — catches value pins on VariableSet not covered by output side
                else if (PinData.Direction == TEXT("in") && PinData.Links.Num() > 0)
                {
                    UEdGraphPin* InputPin = FindPinFuzzy(SourceNode, PinData.Name, EGPD_Input);
                    if (InputPin && InputPin->LinkedTo.Num() == 0)
                    {
                        for (const TPair<int32, FString>& Link : PinData.Links)
                        {
                            UEdGraphNode* SourceLinkedNode = CreatedNodes.FindRef(Link.Key);
                            if (!SourceLinkedNode) continue;
                            UEdGraphPin* OutputPin = FindPinFuzzy(SourceLinkedNode, Link.Value, EGPD_Output);
                            if (!OutputPin) continue;
                            const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
                            const FPinConnectionResponse Response = K2Schema->CanCreateConnection(OutputPin, InputPin);
                            if (Response.Response != CONNECT_RESPONSE_DISALLOW)
                                K2Schema->TryCreateConnection(OutputPin, InputPin);
                        }
                    }
                }
            }
        }

    // ------------------------------------------------------------
    // 6. Notify editor and compile
    // ------------------------------------------------------------
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    BPEditor->RefreshEditors();

    UE_LOG(LogTemp, Warning, TEXT("Node2Chat: Pasted %d nodes into graph."), CreatedNodes.Num());
}

UEdGraphPin* FUENode2ChatModule::FindPinFuzzy(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection PreferredDir) const
{
    if (!Node) return nullptr;

    // Try exact match with preferred direction first
    if (PreferredDir != EGPD_MAX)
    {
        for (UEdGraphPin* P : Node->Pins)
        {
            if (!P) continue;
            if (P->Direction != PreferredDir) continue;
            if (P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
                return P;
        }
    }

    // Exact match any direction
    UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
    if (Pin) return Pin;

    // Case-insensitive
    for (UEdGraphPin* P : Node->Pins)
    {
        if (!P) continue;
        if (P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
            return P;
    }

    // Friendly name
    for (UEdGraphPin* P : Node->Pins)
    {
        if (!P) continue;
        if (P->PinFriendlyName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
            return P;
    }

    // Exec aliases
    static const TArray<FString> ExecAliases = {
        TEXT("exec"), TEXT("execute"), TEXT("then"), TEXT("in"), TEXT("out")
    };
    if (ExecAliases.Contains(PinName.ToLower()))
    {
        for (UEdGraphPin* P : Node->Pins)
        {
            if (!P) continue;
            if (P->PinType.PinCategory != TEXT("exec")) continue;
            if (PreferredDir != EGPD_MAX && P->Direction != PreferredDir) continue;
            FString PName = P->PinName.ToString().ToLower();
            if (ExecAliases.Contains(PName))
                return P;
        }
    }

    return nullptr;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUENode2ChatModule, UENode2Chat)