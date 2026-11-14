# Developer Environment

## Terminolgy

host: Computer running docker. Physical connections are made from the host to
the dev board.

dev board: ESP32 development board

container: Docker container used for development. Contains the ESP-IDF toolset.

IDE: Integrated Development Environment. CLion instance running on the host
that will connect to the container for development.

## Hardware Setup to Laptop

Connect the USB 3.0 SuperSpeed 4-Port Hub to the front-left USB-C port.

- Connect hub USB1 to **S** (Serial) on the ESP32 dev board.
- Connect hub USB2 to **V** (Volt) on the ESP32 dev board.

Use the buttons on the hub to disable USB1 and USB2.

Wait 30 seconds.

Use the buttons on the hub to enable USB1 and USB2; the ESP32 dev board should
have a red light.

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
`marcpawl/esp-idf:20261114` (ESP-IDF v6.1 plus the project tooling: gh,
PyGithub, esp-clang, and `pycodestyle`). The
tag is pinned in `docker-compose.yml`, is never moved, and is the same image CI
pulls, so there is nothing to build on a fresh machine — just pull it.

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

### GitHub CLI (`gh`) in the container: no SSH login

The image ships the [GitHub CLI](https://cli.github.com/) (`gh`), but **the
host's SSH keys do not log it in**. `gh` is an API client: `git push` uses
`~/.ssh`, while `gh pr create`, `gh release`, `gh api`, and the rest call the
GitHub REST API, which accepts only an OAuth token or a personal access token.
So there are two independent ways to get an authenticated `gh` inside the
container, and both are wired up:

**1. Pass the token through the environment (recommended).**
`docker-compose.yml` declares `GH_TOKEN:` and `GITHUB_TOKEN:` with no value,
which makes compose take them from your host environment when they are set:

```console
$ export GH_TOKEN=$(gh auth token)     # host: the token gh is logged in with
$ ./scripts/dev.py shell gh auth status
github.com
  ✓ Logged in to github.com account <you> (GH_TOKEN)
```

`GH_TOKEN` takes precedence over any stored credential, and this path works
even when the host keeps its token in a **desktop keyring** — which the
container cannot reach, and which is the default on a desktop Linux install.
Because the value is never written to a file, it also stays out of the
committed `docker-compose.yml`. Note that an *unset* host variable passes
nothing (compose keeps the variable absent rather than making it empty), so a
machine without the variable simply gets an unauthenticated `gh`.

**2. Share the host's `gh` state.** `docker-compose.yml` bind-mounts the host's
`~/.config/gh` (read/write) at both possible container homes, like
`~/.ssh`, so when `gh auth login` stored the token in
`hosts.yml` the container reads the same login:

```console
$ ./scripts/dev.py shell gh auth status
```

Read/write on purpose: `gh auth login` inside the container then persists to
the host, so the container and the host keep a single login instead of two. If
the host stored its token in the keyring instead, `hosts.yml` holds only the
account name and this mount alone authenticates nothing — use path 1, or run
`gh auth login` on the host (or in the container, which then writes
`hosts.yml`).

If the host has no `~/.config/gh` at all (CI, a fresh machine), Docker creates
that directory as root when it satisfies the mount, so the container can read
it but not write it. `gh` still runs — it simply reports that you are not
logged in — and path 1 is the one to use there.

Check the host first — a token can expire or be revoked, and `gh` then reports
`The token in keyring is invalid`:

```console
$ gh auth status          # host
$ gh auth login -h github.com    # host, if it says the token is invalid
```

Only a token needs to be passed: the container reaches `github.com` over its
normal network access, and `GH_HOST` / enterprise hosts are not configured
here.

### SSH keys and pushing to GitHub from the container

The host's `~/.ssh` is bind-mounted (read-only) into the container at the same
place `ssh` looks for it, so `git push` works from inside the container with
the host's keys — nothing is copied into the image and there is no
per-developer key setup:

```console
$ ./scripts/dev.py shell ssh -T git@github.com
Hi <you>! You've successfully authenticated, but GitHub does not provide shell access.
```

It is mounted **read-only**: the container can authenticate but cannot modify
or delete the host's private keys or rewrite `known_hosts`. The one consequence
is that a host key missing from the host's `known_hosts` cannot be added from
the container — `ssh` fails with "Host key verification failed" rather than
prompting. Record it on the host first:

```console
$ ssh-keyscan github.com >> ~/.ssh/known_hosts
```

`ssh` insists that `~/.ssh` be `0700` and private keys `0600`, and that they be
owned by the running user. Those are the host's modes (the mount carries them),
which is why the container must run as your uid — see "How the container user
is handled" below. Like `~/.ssh`, the mount is declared twice in
`docker-compose.yml`, once per possible container `HOME`.

### How the container user is handled

The container is run as **your own** UID/GID (`scripts/dev.py` passes
`--user "$(id -u):$(id -g)"`), so every file the build writes into the
bind-mounted checkout belongs to you on the host. No `chown` and no
user-namespace remapping is involved: a bind mount does not translate uids, so
the uid the container runs as *is* the uid the host sees.

The consequence is that the image's own user (uid 1000) is not used, so a
different uid has no passwd entry inside the image and `$HOME` resolves to `/`.
Anything caching there breaks — ccache reports
`ccache: error: Permission denied`. `scripts/dev.py` therefore sets `HOME=/tmp`
and `CCACHE_DIR=/tmp/.cache/ccache` for every uid except 1000 (uid 1000 owns
`/home/dev` and keeps it, including the persisted cache volume). GitHub's
runners are uid 1001, so CI depends on this.

### Updating / publishing the dev image

The `Dockerfile` is kept so the image can still be rebuilt, but a rebuild
is **not** part of the normal workflow. When the `Dockerfile` changes (or the
base ESP-IDF image moves), publish a new image under a **new** date-stamped tag
— never move an existing tag, since CI and every developer pin it:

```console
# 1. Build locally from the committed Dockerfile.
$ docker build \
    --build-arg UID=1000 --build-arg GID=1000 --build-arg DIALOUT_GID=20 \
    -t marcpawl/esp-idf:YYYYMMDD .

# 2. Sanity-check it (build, tests, format and Python style in the container).
$ docker run --rm -v "$PWD:/project" -w /project marcpawl/esp-idf:YYYYMMDD \
    bash -lc 'python3 -m pycodestyle --version && ./scripts/dev.py build'

# 3. Publish, then bump the tag in exactly two places:
#      docker-compose.yml  (image: ...)
#      scripts/dev.py      (DEV_IMAGE)
$ docker push marcpawl/esp-idf:YYYYMMDD
```

> **Pushing is a host command, outside the container.** It needs your Docker
> Hub credentials, which live on the host — do not run `docker push` from
> inside `./scripts/dev.py shell` or an AI agent.

`scripts/test_dev.py` asserts that `DEV_IMAGE` and the compose `image:` agree,
so `./scripts/dev.py self-check` catches a half-done bump.

To try a Dockerfile change without touching the pinned image, build it under a
throw-away tag and run it directly with the compose service's mounts:

```console
$ ./scripts/dev.py image            # docker build -t esp32-sensors-dev:local .
$ docker run --rm -it --init -v "$PWD:/project" -v /dev:/dev \
    -u "$(id -u):$(id -g)" --group-add "$(getent group dialout | cut -d: -f3)" \
    esp32-sensors-dev:local bash -l
```

(`docker compose build` also works, but it overwrites the locally cached copy
of the pinned tag, so `docker compose pull` afterwards to restore it.)

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
- Select the pinned dev image `marcpawl/esp-idf:20261114` (the one
  `docker-compose.yml` uses; pull it first with `./scripts/dev.py pull`).
- CLion will build and run commands inside the container transparently.

**ESP-IDF environment in CLion:** Inside the Docker container, ESP-IDF's
environment is already sourced by the image entrypoint, so the environment-file
mechanism is typically unnecessary. If CLion complains about missing `idf.py`,
point its **Environment file** setting at `/opt/esp/idf/export.sh` (path inside
the image) or leave it blank and rely on the container's entrypoint.

**Serial monitor in CLion:** Pass the serial device through to the container
with `--device=/dev/ttyUSB0` (add this to the CLion Docker toolchain's
container options). Alternatively, use CLion's built-in terminal to run
`idf.py monitor`.

**Recommended workflow:** Use CLion for editing, code navigation, and CMake
configuration; use CLion's built-in terminal (`Alt+F12`) for `idf.py`
operations (build, flash, monitor, menuconfig).
