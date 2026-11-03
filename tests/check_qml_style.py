#!/usr/bin/env python3
"""Theme/UI consistency gate for src/qml (skill 03 "Theme & component rules"). Fails on:

R1 bare Qt Quick Controls (Button, TextField, ComboBox, TextArea, Switch, CheckBox, RadioButton, SpinBox,
   Slider, ToolButton, RoundButton) without an explicit `background:` outside the Z* kit files — their Basic
   style paints QPalette colours that ignore the Zalo light/dark/system theme (black boxes on a white page).
R2 text or icons coloured `Theme.zaloBlue` inside an item whose own colour is `Theme.zaloBlueLight` (tonal):
   unreadable blue-on-dark-blue in dark mode; use `Theme.zaloBlueTonalText`.
R3 icon names that ZIcon does not draw (an unknown name renders nothing: an empty button).
R4 literal shortcut sequences (`sequence: "Ctrl+…"`): shortcuts come from ZaloClient.shortcuts.keys() — only
   "Esc" may be literal.
R5 `"transparent"` as the not-hovered end of a hover colour (a ternary with hoverColor / containsMouse /
   hovered): an animated change from #00000000 (black) to a light hover colour flashes dark grey in the light
   theme. Use ThemeManager.hoverClearColor / railHoverClearColor (the same colour with alpha 0).
Usage: check_qml_style.py <repo-root>
"""
import glob
import os
import re
import sys

CONTROLS = r"Button|TextField|ComboBox|TextArea|Switch|CheckBox|RadioButton|SpinBox|Slider|ToolButton|RoundButton"


def block(lines, start):
    """Lines of the item opened on `start` (brace matching)."""
    depth, out = 0, []
    for j in range(start, len(lines)):
        depth += lines[j].count("{") - lines[j].count("}")
        out.append(lines[j])
        if depth <= 0 and j > start:
            break
    return out


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    qml_dir = os.path.join(root, "src", "qml")
    icon_src = open(os.path.join(qml_dir, "ZIcon.qml"), encoding="utf-8").read()
    icons = set(re.findall(r'case "([a-z_0-9]+)"', icon_src))
    errors = []
    for path in sorted(glob.glob(os.path.join(qml_dir, "*.qml"))):
        name = os.path.basename(path)
        lines = open(path, encoding="utf-8").read().split("\n")
        for i, line in enumerate(lines):
            where = f"{name}:{i + 1}"
            # R1
            m = re.match(r"^\s*(" + CONTROLS + r")\s*\{", line)
            if m and not name.startswith("Z"):
                if not any("background:" in l for l in block(lines, i)):
                    errors.append(f"{where}: R1 bare {m.group(1)} (use ZButton/ZTextField/ZComboBox/ZSwitch/ZCheckBox/ZRadio)")
            # R2
            if re.match(r"^\s*color:.*zaloBlueLight", line):
                for k, l in enumerate(block(lines, i)[1:], start=i + 1):
                    if re.search(r"(?<!border\.)color:\s*Theme\.zaloBlue\s*(\}|$)", l):
                        errors.append(f"{name}:{k + 1}: R2 Theme.zaloBlue on a zaloBlueLight background (use Theme.zaloBlueTonalText)")
            # R3: ZIcon names, and icon props forwarded to ZIcon
            if re.search(r"\bZIcon\b", line) or re.search(r"^\s*(name|iconName|icon)\s*:", line):
                in_icon = re.search(r"\bZIcon\b", line) or any(re.search(r"\bZIcon\s*\{", lines[k]) for k in range(max(0, i - 3), i))
                for prop, value in re.findall(r'\b(name|iconName|icon)\s*:\s*([^;}\n]+)', line):
                    if prop == "name" and not in_icon:
                        continue
                    # Strings compared in a condition (`x === "ended" ? … : …`) are not icon names.
                    value = re.sub(r'[!=]==\s*"[^"]*"', "", value)
                    for literal in re.findall(r'"([a-z_0-9]+)"', value):
                        if literal not in icons:
                            errors.append(f"{where}: R3 unknown icon \"{literal}\" (ZIcon draws: see its case list)")
            # R4
            m = re.search(r'\bsequence:\s*"([^"]+)"', line)
            if m and m.group(1) != "Esc":
                errors.append(f"{where}: R4 literal shortcut \"{m.group(1)}\" (add it to ShortcutMap)")
            # R5
            if '"transparent"' in line and "?" in line and re.search(r"hoverColor|containsMouse|hovered", line):
                errors.append(f"{where}: R5 \"transparent\" as a hover end (use ThemeManager.hoverClearColor)")
    for e in errors:
        print(e)
    print(f"QML style: {len(errors)} problem(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
