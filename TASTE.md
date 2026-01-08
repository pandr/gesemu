# Code Style Guide

This project follows a direct, sparse coding style. Keep it simple.

## Philosophy

- **Direct over abstract** - Global state, straightforward logic, no unnecessary layers
- **Single-file implementation** - Keep related code together
- **Sparse commenting** - Comment only where needed (section headers, non-obvious behavior)
- **No over-engineering** - Don't add features, abstractions, or "improvements" beyond requirements
- **Minimal patterns** - Use idioms that favor minimal code (e.g., BCD: `v % 10; v /= 10`)

## Formatting

- **Indentation**: 4 spaces, no tabs
- **Spacing**: Spaces around operators (`*r1 = *r2`, `flag = 0`)
- **Blank lines**: Single blank line between sections
- **Braces**: K&R style (opening brace on same line for functions/control flow)

## Control Flow

- **Reduce nesting** - Use early `continue`/`return` to flatten deeply nested code
- **Example**:
  ```cpp
  // Good - flat
  if (error)
      continue;
  do_work();

  // Avoid - nested
  if (!error) {
      do_work();
  }
  ```

## Comments

- **Section headers** - Group related globals: `// CPU state`, `// Display`
- **Opcode mnemonics** - Label switch cases: `case 0xA:  // ANNN: LD I, addr`
- **Quirky behavior** - Note non-obvious specs: `// CHIP-8: copy before shift`
- **Don't comment the obvious** - Code should be self-documenting where possible

## What to Avoid

- Abstractions for single-use code
- Helper functions for straightforward operations
- Defensive programming for impossible states
- Magic number defines for well-known specs
- Docstrings, excessive type annotations

## Command Line

Keep argument parsing simple, inline. Add to README when adding flags.
