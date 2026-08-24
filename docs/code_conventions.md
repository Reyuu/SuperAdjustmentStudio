## Indentation
Use K&R style indentation style.

Bad:
```c++
if (shown == 0)
{
    return;
}
```

Good:
```c++
if (shown == 0) {
    return;
}

```

## Conditionals
Expand if statements to their full form. Do not use single-line if statements with no braces.

Bad:
```c++
if (shown == 0)
    return;
```

Good:
```c++
if (shown == 0) {
    return;
}

```

## Tenary operators
Permitted for quick operations, but if your the tenary operator is longer than 64 characters, consider making it an if statement instead.

## Pointers
We use left-aligned pointer signs.

Bad:
```c++
void *veryImportantPointer = std::make_shared<void*>();
```

Good:
```c++
void* veryImportantPointer = std::make_shared<void*>();
```
