# ffind

**ffind** is a fast, multi-threaded file search utility written in C. It searches directories for files matching a given pattern and leverages multiple CPU cores to accelerate searches compared to the standard `find` command.

---

## Features

- Multi-threaded directory traversal using pthreads
- Efficient and real-time search output
- Works with any file extension or substring search
- Lightweight with minimal dependencies
- Easy to build with a Makefile

---

## Installation

Clone the repository and build with Make:

```bash
git clone https://github.com/<your-username>/ffind.git
cd ffind
make
```

This will create the executable at `./build/ffind`

## Usage
```bash
./build/ffind <path> <search_term>
```