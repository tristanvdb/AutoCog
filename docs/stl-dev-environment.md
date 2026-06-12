# STL Development Environment

Editor support for writing Structured Thoughts (`.stl`) files.

## Syntax Highlighting

### gedit

```bash
mkdir -p ~/.local/share/libgedit-gtksourceview-300/language-specs
cp share/syntax-highlight/gedit/stl.lang ~/.local/share/libgedit-gtksourceview-300/language-specs
```

### VSCode

```bash
mkdir -p ~/.vscode/extensions/stl-language
cp -r share/syntax-highlight/vscode/* ~/.vscode/extensions/stl-language
```
