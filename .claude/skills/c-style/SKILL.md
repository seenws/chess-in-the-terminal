---
name: c-style
description: The mechanical C conventions for this codebase — GNU formatting, function-body structure, declaration/init discipline, control flow, error handling, memory-allocation safety, pointer ownership, and the pre-finish safety checklist. Use when writing or editing any C in this repo, especially make/unmake, movegen, search, parsing, or allocation code.
tools: Read, Edit, Grep
model: opus
---

# C style for CITT

Follow these conventions when writing or editing C in this repository. They are
rules, not suggestions. They exist to keep a long-lived, performance-critical
engine readable and safe to change. For *design*-level concerns (boundaries,
ownership, API shape) use the `architect` skill; for comments use
`comment-pragmatist`. This skill is the mechanical layer.

## Formatting (GNU style)

Use GNU-style spacing for calls and control flow:

```c
foo (bar, baz);

if (condition)
    {
        do_work ();
    }
```

- Braces for all nontrivial conditionals and loops.
- Prefer clear vertical structure over compactness; one-line conditionals only
  where the surrounding file already does it and the body is trivial.
- Blank lines separate logical phases; never add them randomly.
- Match the surrounding file's existing style. Do not reformat unrelated code.

## Function body structure

Order bodies as: declarations → setup → validation/early-exit → main work →
cleanup → return. Separate phases with blank lines.

```c
static int
do_task (struct context *ctx, const char *name)
{
    struct item *item;
    char        *copy;
    int          result;

    item   = NULL;
    copy   = NULL;
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

Short functions may compress this only when readability is not harmed.

## Declarations and initialization

- Group declarations at the start of a block; do not interleave declarations
  and actions in dense code.
- Declare in the narrowest reasonable scope, but not at the cost of clarity.
- Prefer separate declaration and assignment when it clarifies execution
  phases. The inline form is fine when the variable exists only for an
  immediate check (`int rc = parse_move (...); if (rc < 0) ...`).
- Avoid vague names (`tmp`, `data`, `val`, `ret`, `buf`) outside tiny scopes.
  Name for purpose, not type.

## Control flow

- Early returns for invalid input and error cases when they cut nesting.
- Refactor heavily nested functions into phases or helpers.
- Use `goto` for cleanup; prefer a single `out:` label.
- No hidden control flow through macros unless already established.

## Error handling

- Check every fallible operation; never ignore a return value that can fail.
- Handle allocation failure explicitly.
- Preserve the original cause of failure; do not collapse distinct error cases.
- Return errors and let the caller decide; do not print from deep utilities
  unless that is already the project convention.
- Validate external inputs at boundaries. Inside internal functions, make
  non-null preconditions clear (naming, assert, convention) — do not add
  meaningless NULL checks that mask programmer errors.
- Use `assert` for internal invariants (programmer error), not runtime failure.

## Memory allocation

Be strict. Check `malloc`/`calloc`/`realloc`/`strdup` and project wrappers
unless they are documented to abort.

Never assign `realloc` directly back to the original pointer:

```c
void *new_ptr;

new_ptr = realloc (ptr, new_size);
if (new_ptr == NULL)
    return -1;
ptr = new_ptr;
```

- After `realloc`, only the returned pointer is authoritative — even when
  shrinking (allocators may move blocks across size classes).
- `sizeof *ptr`, not the spelled-out type: `malloc (count * sizeof *ptr)`.
- Check for overflow before size calculations when inputs may be large.
- Every allocation has one owner and one cleanup path; free in reverse order
  of acquisition where practical.
- Set a freed pointer to NULL only when it may be reused or doing so prevents a
  realistic bug — not as ritual at end of scope.

## Pointers and ownership

- Make ownership obvious from names, comments, or the function contract.
- Never return pointers to stack memory; never store borrowed pointers past
  their lifetime.
- `const` for input pointers that are not modified; do not cast away `const`
  without a documented reason.
- A function must not surprise the caller by mutating inputs unless the name,
  type, or doc makes that clear.

## Function and data-structure design

- One conceptual task per function; split when there are independent phases,
  repeated logic, deep nesting, or a helper name would clarify a subtask.
- Do not split merely to hide complexity — a helper must earn its name.
- Avoid ambiguous boolean parameters; prefer enums or named helpers.
- Initialize structs deliberately; prefer designated initializers where they
  improve clarity. Do not rely on partial initialization unless the invariant
  is explicit.

## Portability

- Portable C99. Avoid undefined/implementation-defined behavior, signed
  overflow, aliasing violations, and lifetime tricks.
- Be careful with integer conversions and signed/unsigned comparisons.

## Safety checklist before finishing

Review changed code for: behavior changes, allocation-failure handling,
ownership/lifetime bugs, unchecked returns, invalid pointer use, integer
overflow, duplicated logic, dense blocks, excessive nesting, unclear names,
stale comments, formatting drift, boundary violations, hidden temporal
coupling, and missed testing opportunities.

Then summarize: what changed, why, whether behavior was preserved, and any
risks or follow-up. If a change risks altering behavior, stop and explain the
risk before making it. State plainly whether tests were run.
