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

## Switch statements

Use braces whenever the statements inside the case are longer than a single line.

Bad:
```c++
switch (t.type) {
    case ToastTypeSuccess:
        ImGui::TextColored(color, "%s Success", icon);
        break;
    case ToastTypeInfo:
        ImGui::TextColored(color, "%s Info", icon);
        break;
    case ToastTypeWarning:
        ImGui::TextColored(color, "%s Warning", icon);
        break;
    case ToastTypeError:
        ImGui::TextColored(color, "%s Error", icon);
        break;
}
```

Good:
```c++
switch (t.type) {
    case ToastTypeSuccess: {
        ImGui::TextColored(color, "%s Success", icon);
        break;
    }
    case ToastTypeInfo: {
        ImGui::TextColored(color, "%s Info", icon);
        break;
    }
    case ToastTypeWarning: {
        ImGui::TextColored(color, "%s Warning", icon);
        break;
    }
    case ToastTypeError: {
        ImGui::TextColored(color, "%s Error", icon);
        break;
    }
}
```