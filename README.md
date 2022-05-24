# Rem (Terminal Code Editor)

## ⚡ ᕙ(`▿´)ᕗ ⚡

Rem is a terminal code editor for personal use that does most things terminal editors do, but it's a very basic implementation, so there's not too much bloat

## Features
- Save the contents of any file
- Syntax highlighting for select languages
- Notifies you if a file is writeable or not
- Allows for querying (searching) file contents

## Usage
How to create an executable (if building from source):
```bash
make
```

Available Commands:
```
Ctrl-X (^X) | Exits the Rem editor
Ctrl-Q (^Q) | Search (Query) for specific characters or strings
Ctrl-S (^S) | Save file contents
```

You can check the help menu multiple ways. Pick your favorite!
```bash
./rem help
./rem -h
./rem --help
```

## Example(s)

Open existing files:
```bash
./rem file.c
```

Create a new file (remember to save):
## Example(s)
```bash
./rem
```