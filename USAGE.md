# Installation


### Option 1: Install from Release (.deb)

Download the `.deb` package from the Releases page and install it:

    sudo apt install ./ffind_<version>_<arch>.deb

### Option 2: Build from Source

**Prerequisites**
- Linux kernel 5.10+ (recommended)
- liburing development library
- build-essential (on Debian/Ubuntu)

**Install dependencies on Debian/Ubuntu :**

```shell
sudo apt-get install build-essential liburing-dev pkg-config
```

**Compile and Install :**
```shell
make          # build into ./build/ffind
sudo make install
```

This installs ffind into /usr/bin/ffind by default.

**Uninstall :**
```shell
sudo make uninstall
```

**Clean build files :**
```shell
make clean
```


# Usage
```shell
ffind <path> <search_term>
```

Examples:
```shell
# Search for all files containing "config" in your home directory
ffind ~ config

# Search for all header files containing "net" in /usr/include
ffind /usr/include net

# Find all Markdown files in the current directory
ffind . .md
```
