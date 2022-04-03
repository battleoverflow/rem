# Rem
(*Yes, named after THAT Rem*)

A terminal based code editor built in C.

## Usage
```bash
$ make
```

If you don't want to get your hands dirty with a bunch of commands, there's an installation script called `install.sh`. Here's how to use it:

```bash
# Only required if the file doesn't have executable permissions
$ chmod +x install.sh

$ ./install.sh
```

Once the project is compiled, run the following command to set an alias on your local system:

```bash
# Example if using the build script
$ alias rem="/opt/rem/rem"
```
or you can just run `rem.sh` to run the editor if you don't want to go through the trouble of setting up a new alias or variable.

```bash
# Only required if the file doesn't have executable permissions
$ chmod +x rem.sh

$ ./rem.sh
```