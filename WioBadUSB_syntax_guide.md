# WioBadUSB Payload Syntax Guide
### Wio Wireless — WioBadUSB

Payloads are plain `.txt` files stored on the SD card. One command per line.
Keys and command names are **case-insensitive**.

---

## Commands

### `REM` / `//`
Comment. Line is ignored entirely.
```
REM This is a comment
// This is also a comment
```

---

### `STRING <text>`
Types the text literally via the keyboard.
```
STRING Hello World!
STRING powershell -Command "..."
```

---

### `STRINGLN <text>`
Types the text and then presses Enter.
```
STRINGLN notepad
```
Equivalent to `STRING notepad` followed by `ENTER`.

---

### `DELAY <ms>`
Pauses execution for the given number of milliseconds.
```
DELAY 500
DELAY 2000
```

---

### `DEFAULTDELAY <ms>` / `DEFAULT_DELAY <ms>`
Sets an automatic pause inserted **after every subsequent command**.
Useful for slow target systems. Set to `0` to disable.
```
DEFAULTDELAY 100
```

---

### `REPEAT <n>`
Repeats the **previous command** n times.
```
STRING a
REPEAT 4
REM  types "aaaaa" total (1 original + 4 repeats)
```

---

### Chord Lines — Key Presses
Any line that is **not** one of the named commands above is treated as a **chord**: all listed key names are pressed simultaneously and then released together. List keys separated by spaces.

```
CTRL ALT DELETE
GUI r
GUI ALT m h
SHIFT F10
CONTROL SHIFT ESCAPE
ALT F4
CTRL c
CTRL SHIFT N
```

> **Single characters** (letters, numbers, symbols) can appear in chords too:
> ```
> GUI d
> ALT F4
> CTRL z
> ```

---

## Key Reference

### Modifiers
| Token(s) | Key |
|---|---|
| `CTRL` `CONTROL` | Left Ctrl |
| `RCTRL` `RIGHTCTRL` `RIGHTCONTROL` | Right Ctrl |
| `SHIFT` | Left Shift |
| `RSHIFT` `RIGHTSHIFT` | Right Shift |
| `ALT` | Left Alt |
| `RALT` `RIGHTALT` `ALTGR` | Right Alt / AltGr |
| `GUI` `WINDOWS` `COMMAND` `META` | Left GUI / Win / Cmd |
| `RGUI` `RIGHTGUI` `RIGHTWINDOWS` | Right GUI |

### Navigation & Editing
| Token(s) | Key |
|---|---|
| `ENTER` `RETURN` | Enter |
| `TAB` | Tab |
| `SPACE` | Space |
| `BACKSPACE` `BKSP` | Backspace |
| `DELETE` `DEL` | Delete |
| `INSERT` `INS` | Insert |
| `HOME` | Home |
| `END` | End |
| `PAGEUP` `PAGE_UP` `PGUP` | Page Up |
| `PAGEDOWN` `PAGE_DOWN` `PGDN` | Page Down |
| `ESCAPE` `ESC` | Escape |

### Arrow Keys
| Token(s) | Key |
|---|---|
| `UP` `UPARROW` | ↑ |
| `DOWN` `DOWNARROW` | ↓ |
| `LEFT` `LEFTARROW` | ← |
| `RIGHT` `RIGHTARROW` | → |

### Lock Keys
| Token(s) | Key |
|---|---|
| `CAPSLOCK` `CAPS_LOCK` | Caps Lock |
| `NUMLOCK` `NUM_LOCK` | Num Lock |
| `SCROLLLOCK` `SCROLL_LOCK` | Scroll Lock |

### System Keys
| Token(s) | Key |
|---|---|
| `PRINTSCREEN` `PRINT_SCREEN` `PRTSC` | Print Screen |
| `PAUSE` `BREAK` | Pause / Break |
| `MENU` `APP` `CONTEXT_MENU` | Menu / App key |

### Function Keys
`F1` through `F24`

### Numpad
| Token(s) | Key |
|---|---|
| `NUM0`–`NUM9` / `KP0`–`KP9` | Numpad 0–9 |
| `NUMENTER` `KP_ENTER` | Numpad Enter |
| `NUMPLUS` `KP_PLUS` | Numpad + |
| `NUMMINUS` `KP_MINUS` | Numpad − |
| `NUMDIV` `KP_DIV` | Numpad / |
| `NUMDOT` `KP_DOT` | Numpad . |

### Single Characters
Any individual printable character — letters `a-z` / `A-Z`, digits `0-9`, and standard symbols — can be used directly as a chord token.

---

## Example Payload
```
REM Create greetings text file from WioBadUSB [MacOS]
DEFAULTDELAY 150
GUI r
DELAY 500
STRING notepad
ENTER
DELAY 1000
STRING Hello from WioBadUSB!
ENTER
CTRL s
DELAY 500
STRING WioBadUSBGreetings.txt
ENTER
```
