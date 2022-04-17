# Rem (Really Epic & Makeable) 
###### *Yes, named after THAT Rem*

REM is a terminal code editor created to improve my C knowledge and have something I can use that isn't Vim or Nano (or the many other terminal editors). My goal was to create a simple terminal editor, useful for beginners. At the moment, you can open a file, edit it, and save the file on disk.

I have a future feature list to eventually add syntax highlighting, a search feature, better code quality, ability to create files, and overall improvements to the structure.

## Usage
```bash
make
```

If you don't want to get your hands dirty with a bunch of commands, there's an installation script called `install.sh`. Here's how to use it:
```bash
# Only required if the file doesn't have executable permissions
chmod +x install.sh

./install.sh
```

Once the project is compiled, run the following command to set an alias on your local system:
```bash
# Example if using the build script
alias rem="/opt/rem/rem"
```
or you can just run `rem.sh` to run the editor, if you don't want to go through the trouble of setting up a new alias or variable.

```bash
# Only required if the file doesn't have executable permissions
chmod +x rem.sh

./rem.sh
```

### Mini FAQ
Q. Will there be any actual releases for this project or a project website?

A. Yes! I have a domain ready for the website, but need to build it. As for the release, a "real" release will be created once I am happy with the project.