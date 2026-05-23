# CLAUDE.md

This project prioritizes correctness, maintainability, readability, portability, predictable performance, and design clarity.

When editing code, follow these instructions strictly. Do not treat them as suggestions.

Do not merely improve how the code looks; improve how clearly the code expresses the underlying system.

## Quality philosophy

Code quality is not only formatting. Treat code as a system of ideas.

Good code should be:
- correct
- simple
- explicit
- locally understandable
- globally coherent
- easy to change safely
- hard to misuse
- easy to test
- honest about failure
- respectful of the reader

Do not merely make code pass. Make the design clearer.

Prefer code that reveals intent through structure, naming, and data flow.

Avoid cleverness, hidden coupling, temporal dependencies, and implicit state.

A good change should make future changes easier, not merely solve the immediate task.

## Core rules

Preserve behavior exactly unless explicitly asked to change it.

Prefer small, deliberate, reviewable changes over broad rewrites.

Do not introduce clever code when straightforward code is sufficient.

Do not optimize prematurely. Prefer readable, correct code first. Optimize only when there is a clear reason.

Do not leave code in a worse state than you found it.

Avoid code smells aggressively:
- dense blocks with mixed purposes
- duplicated logic
- unclear ownership
- hidden side effects
- long functions
- deeply nested conditionals
- vague names
- unnecessary abstraction
- unnecessary mutation
- unsafe allocation patterns
- comments that restate obvious code
- functions that do more than one conceptual task
- functions whose names require reading the body to understand
- functions that both decide policy and perform low-level mechanics
- data structures with unclear ownership
- APIs that require callers to remember hidden ordering rules
- flags that alter behavior in unrelated ways
- repeated conditionals that should be one concept
- comments explaining confusing code instead of improving the code
- error handling that loses the cause of failure
- cleanup paths that differ subtly from each other
- mutation spread across distant code
- special cases that are not named as concepts
- code that is only understandable by knowing historical context

If a change risks altering behavior, stop and explain the risk before making it.

## Design quality

Before editing, understand the role of the code in the surrounding system.

Preserve architectural boundaries. Do not make a low-level module know about high-level policy.

Do not introduce dependencies in the wrong direction.

Keep data flow obvious. A reader should be able to tell where data comes from, who owns it, who mutates it, and where it goes.

Prefer explicit inputs and outputs over hidden global state.

Avoid temporal coupling where correctness depends on calling functions in a fragile order. If ordering matters, make it obvious in the API, the names, or the structure.

Make invalid states difficult or impossible to represent when practical.

Do not spread one concept across many unrelated places without a clear reason.

When adding abstraction, ensure it removes complexity rather than merely moving complexity.

## Simplicity and elegance

Elegance means fewer moving parts, clearer relationships, and less surprise.

Do not confuse elegance with terseness.

Prefer boring, transparent code over dense, impressive code.

Every abstraction must justify its existence.

Every helper function should have a name that makes the caller easier to read.

Every data structure should match the problem domain.

Every branch should represent a real distinction in behavior.

Remove accidental complexity when doing so does not increase risk.

## Interfaces

Design interfaces so that correct usage is natural and incorrect usage is difficult.

Function names should describe observable behavior, not implementation details.

Parameter order should be predictable and conventional.

Avoid functions with ambiguous boolean parameters. Prefer enums or clearly named helper functions when call sites would otherwise be unclear.

Document ownership transfer, mutation, and lifetime expectations at the interface boundary.

A function should not surprise the caller by mutating inputs unless the name, type, or documentation makes that clear.

Return values should have clear meaning. Do not use magic sentinel values unless they are already established in the project.

Distinguish between recoverable errors, programmer errors, and impossible states.

## Invariants

Identify and preserve invariants.

If a function relies on a non-obvious invariant, document it.

If a data structure has required internal relationships, keep those relationships centralized and easy to audit.

Do not duplicate invariant-maintaining logic in multiple places unless unavoidable.

Validate invariants at module boundaries when practical.

Use assertions for internal assumptions that indicate programmer error, not for normal runtime failures.

## C style

Follow GNU C style unless the local project style clearly differs.

Use GNU-style spacing for function calls and control flow where consistent with the surrounding file:

```c
foo (bar, baz);

if (condition)
    {
        do_work ();
    }
```

Use braces for nontrivial conditionals and loops.

Prefer clear vertical structure over compactness.

Avoid one-line conditionals unless the surrounding code strongly prefers them and the body is trivial.

Avoid mixing declarations, initialization, checks, actions, and returns without visual separation.

## Function body structure

When possible, structure function bodies in this order:

1. declarations
2. initialization / setup
3. validation and early exits
4. main functionality
5. cleanup
6. return

Example:

```c
static int
do_task (struct context *ctx, const char *name)
{
    struct item *item;
    char *copy;
    int result;

    item = NULL;
    copy = NULL;
    result = -1;

    if (ctx == NULL || name == NULL)
        return -1;

    copy = xstrdup (name);
    if (copy == NULL)
        goto out;

    item = item_create (copy);
    if (item == NULL)
        goto out;

    result = process_item (ctx, item);

out:
    item_destroy (item);
    free (copy);

    return result;
}
```

For short functions, this structure may be compressed only when readability is not harmed.

Avoid this:

```c
if (ai_turn) {
    struct move m;
    int score = search_root(g, AI_DEFAULT_DEPTH, &m);
    char uci[6];
    move_to_uci(&m, uci);
    printf("%s (engine) plays %s  [score = %d]\n",
           g->turn == COLOR_WHITE ? "white" : "black", uci, score);
    struct undo_state _unused;
    make_move(g, &m, &_unused);
    return 1;
}
```

Prefer this:

```c
if (ai_turn)
    {
        struct move m;
        struct undo_state undo;
        char uci[6];
        int score;

        score = search_root (g, AI_DEFAULT_DEPTH, &m);
        move_to_uci (&m, uci);

        printf ("%s (engine) plays %s  [score = %d]\n",
                g->turn == COLOR_WHITE ? "white" : "black", uci, score);

        make_move (g, &m, &undo);

        return 1;
    }
```

## Declarations and initialization

Group declarations at the start of a block when practical.

Do not interleave declarations and actions in dense code.

Declare variables in the narrowest reasonable scope, but not at the cost of readability.

Prefer separate declaration and assignment when it makes the execution phases clearer:

```c
int score;

score = search_root (g, AI_DEFAULT_DEPTH, &m);
```

This is acceptable when the variable exists only for the immediate check:

```c
int rc = parse_move (input, &move);
if (rc < 0)
    return -1;
```

Avoid vague temporary names such as `tmp`, `data`, `val`, `ret`, or `buf` unless the meaning is obvious in a very small scope.

Use names that describe purpose, not type alone.

## Control flow

Prefer early returns for invalid input and error cases when they reduce nesting.

Avoid deep nesting. If a function becomes heavily nested, refactor into clearer phases or helper functions.

Keep the main path easy to read.

Avoid hidden control flow through macros unless already established in the project.

Use `goto` for cleanup in C when it improves safety and avoids duplicated cleanup code.

Use one cleanup label where practical:

```c
out:
    free (buffer);
    return result;
```

## Error handling

Check all fallible operations.

Do not ignore return values from functions that can fail.

Handle allocation failures explicitly.

Preserve the original error cause when possible.

Do not collapse distinct error cases if doing so makes debugging harder.

Avoid printing errors from deep utility functions unless that is already the project convention. Prefer returning an error and letting the caller decide.

Validate external inputs at boundaries.

Inside internal functions, either validate nullable inputs explicitly or make non-null preconditions clear through naming, comments, assertions, or project convention.

Do not add meaningless NULL checks that hide programmer errors or make impossible states appear valid.

## Memory allocation

Be strict about allocation safety.

Check `malloc`, `calloc`, `realloc`, `strdup`, and project-specific allocation wrappers unless they are documented to abort on failure.

Never assign `realloc` directly to the original pointer unless losing the original pointer is impossible or harmless.

Avoid this:

```c
ptr = realloc (ptr, new_size);
if (ptr == NULL)
    return -1;
```

Prefer this:

```c
void *new_ptr;

new_ptr = realloc (ptr, new_size);
if (new_ptr == NULL)
    return -1;

ptr = new_ptr;
```

After `realloc`, treat only the returned pointer as authoritative.

Do not assume shrinking an allocation preserves the pointer. Allocators may move blocks because of size classes, alignment, internal metadata, or platform-specific behavior.

Clearly document ownership transfer when it is not obvious.

Every allocation must have a clear owner and a clear cleanup path.

Free resources in the reverse order of acquisition when practical.

Set pointers to `NULL` after `free` only when the pointer may be reused or when doing so prevents a realistic bug. Do not add meaningless nulling at end of scope.

## Pointers and ownership

Make pointer ownership obvious from names, comments, or function contracts.

Do not return pointers to stack memory.

Do not store borrowed pointers beyond their valid lifetime.

Do not cast away `const` unless there is a strong reason and the reason is documented.

Prefer `const` for input pointers that are not modified.

Make nullability explicit through validation, assertions, comments, or project convention.

## Comments

Follow GNU comment style.

Write complete sentences in comments. Start with a capital letter and end with a period.

Use comments to explain why, not what.

Good comments explain:
- invariants
- ownership and lifetime
- non-obvious algorithms
- portability concerns
- security assumptions
- surprising edge cases
- intentional deviations from normal style

Avoid comments like:

```c
/* Increment i. */
i++;
```

Prefer comments like:

```c
/* Keep the original pointer valid until realloc succeeds. */
new_ptr = realloc (ptr, new_size);
```

Place comments above the code they describe.

Use block comments for explanations:

```c
/* The move is converted before make_move because make_move mutates
   the game state used to determine the side to print. */
```

For multi-line comments, align continuation lines in GNU style:

```c
/* This buffer may be reallocated even when shrinking, because some
   allocators move allocations between size classes. */
```

Do not leave stale comments. Update or remove comments when changing nearby code.

Do not use comments to excuse confusing code. Prefer making the code clearer.

## Function design

A function should do one conceptual thing.

Keep functions short enough to understand without scrolling excessively.

Split functions when:
- there are multiple independent phases
- the function has repeated logic
- indentation becomes deep
- local variables become difficult to track
- comments are needed to divide the function into sections
- the name of a helper would clearly describe a subtask

Do not split functions merely to hide complexity. A helper must have a clear purpose.

Prefer descriptive function names using existing project naming conventions.

Avoid boolean parameters that make call sites unclear unless already established.

Avoid functions with surprising side effects.

## Data structures

Keep invariants clear.

Initialize structs deliberately.

Do not rely on partially initialized data unless the invariant is explicit.

Prefer designated initializers where they improve clarity and are compatible with the project style.

Avoid exposing struct internals unnecessarily.

Do not duplicate state unless there is a clear synchronization strategy.

## Performance

Prefer simple code unless performance matters in that path.

Do not introduce unnecessary allocations.

Avoid repeated expensive work in loops.

Avoid algorithmic regressions.

When optimizing, preserve readability and explain non-obvious choices.

Do not micro-optimize at the cost of maintainability without evidence.

## Portability

Write portable C unless the project intentionally targets a specific platform.

Avoid relying on undefined behavior, implementation-defined behavior, signed overflow, pointer aliasing violations, or object lifetime tricks.

Be careful with integer conversions, signed/unsigned comparisons, and size calculations.

Check for overflow before allocation size calculations when inputs may be large.

Prefer `sizeof *ptr` over spelling the pointed-to type:

```c
ptr = malloc (count * sizeof *ptr);
```

## Verification

For nontrivial changes, consider how the change would be tested.

Do not claim behavior is preserved unless the change is mechanical or you have reasoned through the affected paths.

Prefer changes that make testing easier.

When fixing a bug, identify the failing condition and ensure the new code prevents recurrence.

When changing parsing, memory ownership, move generation, search, or state mutation logic, be especially conservative and explain the behavioral risk.

If tests exist, run the relevant tests when practical. If tests are not run, say so.

## Formatting

Use blank lines to separate logical phases.

Do not add blank lines randomly.

Keep related statements together.

Avoid dense visual clumps.

Preserve surrounding formatting when editing existing code, unless the purpose of the edit is formatting cleanup.

Do not reformat unrelated code.

## Safety checklist before finishing

Before claiming completion, review the changed code for:

- behavior changes
- allocation failure handling
- ownership and lifetime bugs
- unchecked return values
- invalid pointer use
- integer overflow risks
- duplicated logic
- overly dense blocks
- excessive nesting
- unclear names
- stale or useless comments
- formatting inconsistent with GNU/project style
- architectural boundary violations
- unclear data flow
- hidden temporal coupling
- unnecessary broad rewrites
- unnecessary abstractions
- missed testing opportunities

After editing, summarize:
- what changed
- why it changed
- whether behavior was preserved
- any risks or follow-up work