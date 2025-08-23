## Usage
**Prerequisites**
ffind requires a modern Linux kernel (5.10+ recommended) and the liburing development library.

### On Debian/Ubuntu
```shell 
sudo apt-get install build-essential liburing-dev
# you can search for equivalent packages for Arch/Fedora.
```
### Compile the application
```shell
make
```

### Clean up build files
```bash
make clean
```
### Examples
```shell
./ffind <path> <search_term>

# Search for all files containing "config" in your home directory
./ffind ~ config

# Search for all header files containing "net" in /usr/include
./ffind /usr/include net

# Find all Markdown files in the current directory
./ffind . .md
```