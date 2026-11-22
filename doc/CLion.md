# CLion IDE


## Version

Version: CLion 2026.3

## CLion IDE usage

CLion connects to the container using ssh.

Ensure that you have ssh keys in ~/.ssh.

### Prepare compile database
On the host prepare the container by having a clean build, 
with the compile commands database built.

```console
$ ./scripts/dev.py up
$ ./scripts/dev.py build
$ ./scripts/dev.py clion-compdb
$ ./scripts/dev.py clion-run-configs
```

### Offline backend archive

CLion installs the remote backend through the Toolbox agent, which downloads
a large `CLion-*.tar.gz` into the container. To avoid the downloadd you can
place it into your ~/Downloads folder, and place it into the container.

#### Download

[`CLion-2026.3.0.1.tar.gz`](https://download-cdn.jetbrains.com/cpp/CLion-2026.3.0.1.tar.gz?_gl=1*hqmpvq*_gcl_au*MTY1MDY1MTk3OS4xNzkwNjI3MTY2Li0uLS4xNzkwNzE1MjkyLjc4ODEyNDAyLjE3OTEwMzMxMzcuMTc5MTAzMzEzNg..*FPAU*MTY1MDY1MTk3OS4xNzkwNjI3MTY2*_ga*MTg4OTA5NTg0NC4xNzkwNjI3MTY2*_ga_9J976DJZ68*czE3OTUwMjg1MTUkbzI2JGcwJHQxNzk1MDI4NTM0JGo0MSRsMCRoMA..&_cl=Mjt7ImFsbEFsbG93ZWQiOnRydWUsImFsbG93U2FsZSI6ZmFsc2UsInJlZ2lvbiI6IkcwIiwiYW5zd2VyZWQiOnRydWUsInRpbWVzdGFtcCI6IjIwMjYtMDktMjhUMjA6MjY6MDUuNTA0WiJ9&Expires=1795114947&Signature=HhaZcIaq27pipi-AFDhfn~WZaU-~YZPu1lP6FK8loMyYCcsL5ea415C0HLbvtwydme43pBGuTSJ-1q28T8tne3ybLQLTdzRBXDJs3igLmNqJyxERexOG~yDZGsF70qwFUqfjeTIBKdMuo~hAjvmOQzZsEP94Nbgb4suMq~mZGhXgBlingQ1pw0pMVkLercE8ma0aoKbgiYHWmksXWQfmGcv03lin7t2E9zjs6XvZZJP1Thy5CtfPJTyFN-u5Tr48VA3GWCAJ6beXk4yvtvY6XxdQaG7-qtf9FomDQqNOmgUcxT~SWTvOGoTRuU5GJ0Hv~BtmZQT7Cjk2myyOMjBkMg__&Key-Pair-Id=K2PI9GMSMWJY0)

#### Install into container
```console
$ ./scripts/dev.py clion-archive
```
## Open CLion

1. Start CLion.
2. Close any open projects.
3. At the welcome screen, select **Remote Connections → ssh**.
4. Connect with `ssh -p 2222 dev@localhost`.
5. Set the project directory to `/project/.clion/compile_commands.json` and
   select **Open Project**.
6. Provide the path to your SSH key, `~/.ssh/id_rsa.pub`.

### CLion configuration notes

**ESP-IDF environment in CLion:** inside the Docker container, ESP-IDF's
environment is already sourced by the image entrypoint, so the
environment-file mechanism is typically unnecessary. If CLion complains about
missing `idf.py`, point its **Environment file** setting at
`/opt/esp/idf/export.sh` (a path inside the image) or leave it blank and rely
on the container's entrypoint.

**Recommended workflow:** use CLion for editing, code navigation, and CMake
configuration; use CLion's built-in terminal (`Alt+F12`) for `idf.py`
operations (build, flash, monitor, menuconfig).

## Build, run, and debug from the menus

This project includes pre-populated shared run/debug configurations in
`.idea/runConfigurations/` (see commit 1334d12). Use the run configuration
selector in the toolbar (or **Run > Run...** / **Run > Debug...**) to choose
one.

### Build (container)

- Select a **Build ... (container)** configuration (for example
  **Build firmware (container)** or a per-component build such as
  **Build logic component (container)**).
- Run it with **Run > Run '...’** (`Shift+F10`) or the toolbar Run button.
  These shell configurations execute `scripts/dev.py build ...` in the dev
  container.
- Other shared actions:
  - **Flash and monitor (container)**
  - **menuconfig (container)**
  - **clang-format check (container)**
  - **Host unit tests (container)**

### Run and debug Linux executables

- Select a **... (linux ELF, container)** configuration (for example
  **donny (linux ELF, container)** or a host test ELF such as
  **test-logic (linux ELF, container)**).
- **Run > Run '...’** (`Shift+F10`): build (before launch) and run the ELF in
  the container.
- **Run > Debug '...’** (`Shift+F9`): build (before launch) and debug with
  breakpoints. Set breakpoints in the gutter (`Ctrl+F8`), then use stepping
  and inspection controls in the **Debug** tool window (`F8`, `F7`, `Shift+F8`,
  `F9`).
- Selecting a Linux ELF configuration also activates the matching CMake
  profile in `.idea/cmake.xml`. See **Run > Edit Configurations...** to inspect
  the shared configurations.
