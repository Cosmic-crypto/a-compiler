# 📘 A Programming Language — Full Specification

A is a lightweight, indentation-based scripting language that compiles directly into C.
This document specifies all features of the language based on the compiler source code.

## Table of Contents
- Lexical Structure
- Data Types
- Variables
- Control Flow
- Functions
- Lists and Dictionaries
- Expressions & Statements
- Printing
- Standard Library
- Indent-Based Scoping
- Compiler Errors
- Compilation Modes
- Summary

---

# 1. Lexical Structure

### Comments
```a
# This is a comment
```

Everything after `#` on a line is ignored.

---

# 2. Data Types

| A Type | C Equivalent | Notes |
|--------|--------------|--------|
| int | int | Default = 0 |
| bool | bool | Values: true, false |
| float | float | |
| string | char* | Raw C string pointer |
| list | List struct | Dynamic list of ints |
| const modifier | const | Works on standard types |

---

# 3. Variables

### Declaration Examples
```a
int x = 5
float y = 3.14
bool flag = true
string s = "hello"
list L
const int z = 9
```

### Default Initialization

| Type | Default Value |
|------|----------------|
| int | 0 |
| string | NULL |
| list | new_list() |
| bool, float | uninitialized (C default) |

---

# 4. Control Flow

### If
```a
if condition:
    ...
```

Compiles to:
```c
if (condition) {
```

### Elif
```a
elif condition:
```

Compiles to:
```c
} else if (condition) {
```

### Else
```a
else:
```

Compiles to:
```c
} else {
```

### While
```a
while condition:
    ...
```

Compiles to:
```c
while (condition) {
```

### For
```a
for i = A to B:
```

Compiles to:
```c
for (int i = A; i <= B; i++) {
```

---

# 5. Functions

### Declaration
```a
func myfunc:
    print("hi")
```

Compiles to:
```c
void myfunc() {
    printf("%s\n", "hi");
}
```

### Key Rules
- No parameters  
- No return values  
- Functions end based on indentation unless in raw mode  
- `func main` is ignored because the compiler generates its own `main()`  

---

# 6. Lists and Dictionaries

### List Declaration
```a
list L
```

Internal representation:
```c
typedef struct { int* data; int size; int cap; } List;
```

### Append
```a
append(L, 5)
```

List indexing:
```a
print(L[0])
```

Becomes:
```c
L.data[0]
```

### Dictionaries
Provided via stdlib:

```
Dict { keys[], vals[], size }
```

Functions:
```
dset(d, k, v)
dget(d, k)
```

---

# 7. Expressions & Statements

Everything not recognized as a block/function/keyword becomes raw C:

```a
x = x + 1
```

Becomes:
```c
x = x + 1;
```

---

# 8. Printing

```a
print(expr)
```

| Type | Output C Code |
|------|----------------|
| string | `printf("%s\n", expr);` |
| bool | `printf("%s\n", (expr)?"true":"false");` |
| float | `printf("%f\n", expr);` |
| int/default | `printf("%d\n", (int)(expr));` |

Examples:
```a
print("hi")
print(flag)
print(x)
```

---

# 9. Standard Library

Automatically included in generated C:

- stdio  
- stdlib  
- string  
- math  
- time  
- setjmp  
- list/dict implementations  
- append, new_list, slice_arr  
- dset, dget  

### Time Replacement

| A Code | C Code |
|--------|--------|
| `time.now()` | `(int)time(NULL)` |
| `date.now()` | `(int)time(NULL)` |
| `clock.now()` | `((double)clock() / CLOCKS_PER_SEC)` |

---

# 10. Indent-Based Scoping

### Modes

| Mode | Behavior |
|------|-----------|
| optimized | auto-closes blocks from indentation |
| debug | optimized + auto-runs |
| raw | requires manual `end` keyword |

### Auto-close example
```a
if x > 0:
    print("hi")
print("bye")
```

Compiles to:
```c
if (x > 0) {
    printf("%s\n", "hi");
}
printf("%s\n", "bye");
```

---

# 11. Compiler Errors

Only occur in **raw mode**.

Example:
```
[COMPILER ERROR] Syntax Error: Missing 'end'.
  -> Unclosed block started at line X
```

---

# 12. Compilation Modes

### Command-line
```
./compiler <file> [mode]
```

### Modes
- optimized (default)  
- debug  
- raw  

### GCC Flags

| Mode | Flags |
|-------|--------|
| optimized | -Ofast -w |
| debug | -Ofast |
| raw | -O0 |

Output binary: `program`

---

# Summary

A is a:

- Python-like indentation language  
- With C-like declarations  
- List & dict stdlib utilities  
- Auto-printing system  
- Lightweight transpiler to C
- Super-fast same/similar performance to C
- Optional strict/raw mode  
- Optional debug mode

Its entire runtime maps directly onto raw C.
This was completley done with AI (Gemini to be exact) over 2-3 days with 1-2hrs a day so if you find any errors in the code it's not me, I really don't know how to code in C.
please do add changes and fixes to my code and check out [Gemini](https://aistudio.google.com/)!
