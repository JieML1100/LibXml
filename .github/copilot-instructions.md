# Copilot Instructions

## General Execution Behavior

When the user asks for a large feature, a major module, or several small tasks that belong to the same overall objective, you should continue working autonomously until the objective is meaningfully completed.

Do not stop after a very small change or a partial implementation unless a real decision from the user is required.

Default behavior should be:
- break the request into logical subtasks,
- complete the subtasks in a reasonable order,
- continue from one subtask to the next without waiting for confirmation,
- keep going until the feature, module, or task group reaches a usable and coherent state.

## When To Continue Without Asking

Continue working without asking the user to confirm every step when:
- the next step is a reasonable continuation of the current task,
- the implementation details can be inferred from the codebase, existing patterns, or the user's request,
- the remaining work is part of the same feature or task group,
- a temporary assumption is low-risk and can be documented in the final summary.

In these cases, prefer making a reasonable choice and proceeding.

## When To Stop And Ask The User

Stop and ask the user only when their decision is actually needed.

Examples include:
- there are multiple valid product or design directions with materially different outcomes,
- the requirement is ambiguous enough that continuing would likely implement the wrong behavior,
- the change would affect public APIs, data formats, database schema, security behavior, or other high-impact interfaces,
- the task requires credentials, external access, secrets, paid services, or permissions not already provided,
- the codebase presents conflicting patterns and there is no clear safe default,
- continuing would risk destructive changes or large unintended refactors.

Do not stop merely because one small part of the work is finished.

## Expected Work Style For Large Tasks

For larger requests, follow this pattern by default:
1. Understand the overall goal.
2. Break it into concrete implementation steps.
3. Implement the steps in sequence.
4. Update related code where necessary.
5. Add or update tests when appropriate.
6. Run or describe relevant validation steps.
7. Continue refining until the result is reasonably complete.

The goal is to reduce unnecessary back-and-forth and avoid requiring the user to repeatedly say "continue".

## Partial Progress And Autonomy

If the task is too large to finish in a single pass, make as much meaningful progress as possible before stopping.

Before stopping, aim to complete a coherent chunk of work rather than only a tiny edit.

Do not pause only because:
- one file has been modified,
- one function has been added,
- one error has been fixed,
- one subtask has been started.

Instead, continue to the next obvious step unless user input is required.

## Assumptions

When necessary, make conservative and reasonable assumptions based on:
- existing repository patterns,
- naming conventions,
- nearby implementations,
- the user's stated goal.

Clearly summarize important assumptions after the work is done.

## Final Response Style

When reporting progress or completion:
- summarize what was implemented,
- mention any assumptions made,
- note anything that still needs user input,
- identify remaining risks or optional next steps only if relevant.

Do not ask for confirmation unless a true decision is required.