# ue-node-to-chat
dsl import export for llm interaction workflow

BlueprintDSL Format Reference
A compact text representation of Unreal Engine 5 Blueprint nodes for LLM editing. Usage in graph window of the editor:

`ctrl-shift-1` for copy, `ctrl-shift-2` for paste

Shorten textual representation for blueprint nodes by >50%, thus increasing the context capabilities of the chat interaction.

How to install:
1) Add folder to plugins, start project, enable plugin `UEnode2chat` from `edit->plugins`.


How to use:
1) Copy contents of `DSL_rules.txt` into any ai chat window
2) Select nodes from graph.
3) Use shortcut `ctrl-shift-1` to copy nodes from ue env.
4) Paste contents into ai chat window. Instruct AI to respond in same format.
5) Paste edited nodes from AI chat back into the editor using `ctrl-shift-2`.
