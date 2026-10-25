# Developer Environment

## Terminolgy

host: Computer running docker.  Physical connections are made from
the host to the dev board.

dev board: ESP32 development board

container: Docker container used for development.  Contains the
ESP-IDF toolset.

IDE: Integrated Development Environment.  CLion instance running
on the host that will connect to the container for development.

## Hardware Setup to Laptop

Connect the USB 3.0 SuperSpeed 4-Port Hub to the front-left USB-C port.

- Connect hub USB1 to **S** (Serial) on the ESP32 dev board.
- Connect hub USB2 to **V** (Volt) on the ESP32 dev board.

Use the buttons on the hub to disable USB1 and USB2.

Wait 30 seconds.

Use the buttons on the hub to enable
USB1 and USB2; the ESP32 dev board should have a red light.

On the host, verify that the ESP dev board is seen:

```console
$ dmesg | tail -n 20 | grep USB
[159528.424091] usb 1-1.1: USB disconnect, device number 8
[159530.816063] usb 1-1.1: new full-speed USB device number 9 using xhci_hcd
[159530.916971] usb 1-1.1: New USB device found, idVendor=1a86, idProduct=55d3, bcdDevice= 4.45
[159530.916987] usb 1-1.1: New USB device strings: Mfr=0, Product=2, SerialNumber=3
[159530.916993] usb 1-1.1: Product: USB Single Serial
[159530.925207] cdc_acm 1-1.1:1.0: ttyACM0: USB ACM device
$
```

Optionally if ESP-IDF is installed on the host:
```console
$ esptool --port /dev/ttyACM0
```


## Setup the Docker Environment

The development container uses the **prebuilt** image
`marcpawl/esp-idf:20261025` (ESP-IDF v6.1 plus the project tooling: gh,
PyGithub, esp-clang). The tag is pinned in `docker-compose.yml`, is never
moved, and is the same image CI pulls, so there is nothing to build on a
fresh machine — just pull it.

Pull the image and start the container:

```console
$ ./scripts/dev.py pull
...
$ docker compose up -d
...
$ ./scripts/dev.py shell esptool --port /dev/ttyACM0 chip-id
esptool v5.4.0
Connected to ESP32-S3 on /dev/ttyACM0:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
MAC:                a4:cb:8f:d9:83:68

Stub flasher running.

WARNING: ESP32-S3 has no chip ID. Reading MAC address instead.
MAC:                a4:cb:8f:d9:83:68

Hard resetting via RTS pin...
$
```

The container is now ready for use with ESP-IDF.

Configure the environment:

```console
$ rm -rf mcgowan/build_esp32s3
$ ./scripts/dev.py shell -C mcgowan idf.py -B build_esp32s3 fullclean
$ ./scripts/dev.py shell -C mcgowan idf.py -B build_esp32s3 set-target esp32s3
$
```

### Updating / publishing the dev image

The `Dockerfile` is kept so the image can still be rebuilt, but a rebuild is
**not** part of the normal workflow. When the `Dockerfile` changes (or the
base ESP-IDF image moves), publish a new image under a **new** date-stamped
tag — never move an existing tag, since CI and every developer pin it:

```console
# 1. Build locally from the committed Dockerfile.
$ docker build \
    --build-arg UID=1000 --build-arg GID=1000 --build-arg DIALOUT_GID=20 \
    -t marcpawl/esp-idf:YYYYMMDD .

# 2. Sanity-check it (build, tests, format inside the container).
$ ...

# 3. Publish, then bump the tag in exactly two places:
#      docker-compose.yml  (image: ...)
#      scripts/dev.py      (DEV_IMAGE)
$ docker push marcpawl/esp-idf:YYYYMMDD
```

`scripts/test_dev.py` asserts that `DEV_IMAGE` and the compose `image:` agree,
so `./scripts/dev.py self-check` catches a half-done bump.

To try a Dockerfile change without touching the pinned image, build it under a
throw-away tag and run it directly with the compose service's mounts:

```console
$ docker build -t esp32-sensors-dev:local .
$ docker run --rm -it --init -v "$PWD:/project" -v /dev:/dev \
    -u "$(id -u):$(id -g)" --group-add "$(getent group dialout | cut -d: -f3)" \
    --entrypoint sh \
    -e HOME=/tmp -e CCACHE_DIR=/tmp/.cache/ccache \
    -e GIT_CONFIG_COUNT=1 \
    -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/opt/esp/idf \
    esp32-sensors-dev:local -c '. $IDF_PATH/export.sh; exec bash -l'
```

(`docker compose build` also works, but it overwrites the locally cached copy
of the pinned tag, so `docker compose pull` afterwards to restore it.)

**Run as the host user.** `scripts/dev.py` starts the container with
`--user "$(id -u):$(id -g)"`, so everything the build writes into the
bind-mounted project belongs to whoever ran the command and the checkout never
changes owner. That is also the only thing that works in CI: a GitHub Actions
runner (uid 1001) owns its checkout but, as a non-root user, cannot `chown` it
(`chown: Operation not permitted`).

The host user has no passwd entry in the image, so `dev.py` picks an
environment it can write (`_container_environment`): `HOME=/tmp` and
`CCACHE_DIR=/tmp/.cache/ccache` (never `/home/dev`, which is `750` — a foreign
uid cannot read it, `HOME` would fall back to `/`, and `ccache` would fail with
`Permission denied`). It also pins `safe.directory` to `/opt/esp/idf`
(root-owned, so `git` would otherwise abort with *dubious ownership* when
`idf.py` runs git in the IDF component submodules). Because such a user cannot
read the image's login files either, the command runs as
`sh -c ". $IDF_PATH/export.sh; <command>"`, or `idf.py` would not be found.

The `sh` must be there: `docker compose run dev <args>` replaces the compose
`command: ["/bin/bash", "-l"]`, so the entrypoint would otherwise run
`/bin/sh -c <args>` — and `/bin/sh` in the image is **dash**, which does not
accept arguments after `.`. A bare `dev -c ". $X; cmd"` thus made dash source a
file named `. $X; cmd` and failed with exit 127
(`/project/. $IDF_PATH/export.sh; scripts/dev.py build: No such file or
directory`). The `;` matters for the same reason: `.` takes every following
word as an argument to the file it sources.

Running `dev.py` as root is the one exception: root's IDs would leave every
artifact root-owned, so the container then runs as the image's own user
`1000:1000` (which keeps `/home/dev`), `dev.py` prints a note, and the checkout
is given to that user first.

It also makes sure the container can write the project before a command that
writes into it, since the container cannot create its build tree inside a
directory owned by someone else (CMake: "Unable to (re)create the private
pkgRedirects directory"). It reads the owner of the mount's *source* and
verifies the result with a throwaway container running as the container user,
which creates and removes one probe file in each tracked directory of the
project. When the container user already owns the checkout (the normal case,
and the CI case) nothing is chowned and only the verification runs.

On the host the source is the checkout itself, wherever it lives. Only inside
the container is it read at `<HOST_ROOT>/project` (`HOST_ROOT` is `/dev` in
`dev.py`), where the dev service's second mount (`/dev:/dev`) exposes it, so
that mount must stay in `docker-compose.yml`.

If you add a new `docker compose run --rm dev …` example to this document,
always wrap it in `./scripts/dev.py shell …` (or run `scripts/dev.py build`,
`flash`, …) instead of calling `docker compose run` by hand: a container that
writes the project without `dev.py`'s ownership check is exactly how the CMake
"pkgRedirects" failure comes back.

## CLion

Version: CLion 2026.2

### CLion IDE Usage

Change the CLion registry as described in
[SUPPORT-A-258](https://youtrack.jetbrains.com/articles/SUPPORT-A-258) to not
download the IDE when connecting to dev containers.

Open a CLion project at the root of this project.

Then use **File | Remote Development...**:

1. Select **Dev Containers** in the left bar.
2. Select the existing container if it exists. Otherwise:
   - Select **New Dev Container**.
   - Select the **From Local Project** tab.
     - Select **Docker** in the pull-down.
     - Select the devcontainer file for **Path to devcontainer.json**.
     - Select the **Build container and continue** button.
       - In **Building Dev Container**, select the **Continue** button.
   - Select the **Start IDE and Connect** button.

A new CLion window will appear using the container as the development
environment.

### CLion Configuration Notes

**Docker toolchain:** CLion's Docker toolchain integration is first-class and
officially supported. Configure it under **Settings | Build, Execution,
Deployment | Toolchains → Add → Docker**.

- Point CLion at the standard Docker socket (`unix:///var/run/docker.sock`).
- Select the pinned dev image `marcpawl/esp-idf:20261025` (the one
  `docker-compose.yml` uses; pull it first with `./scripts/dev.py pull`).
- CLion will build and run commands inside the container transparently.

**ESP-IDF environment in CLion:** Inside the Docker container, ESP-IDF's
environment is already sourced by the image entrypoint, so the environment-file
mechanism is typically unnecessary. If CLion complains about missing `idf.py`,
point its **Environment file** setting at `/opt/esp/idf/export.sh` (path inside
the image) or leave it blank and rely on the container's entrypoint.

**Serial monitor in CLion:** Pass the serial device through to the container
with `--device=/dev/ttyUSB0` (add this to the CLion Docker toolchain's container
options). Alternatively, use CLion's built-in terminal to run `idf.py monitor`.

**Recommended workflow:** Use CLion for editing, code navigation, and CMake
configuration; use CLion's built-in terminal (`Alt+F12`) for `idf.py`
operations (build, flash, monitor, menuconfig).
