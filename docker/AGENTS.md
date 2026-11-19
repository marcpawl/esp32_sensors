# The Docker dev environment

The dev container is defined by the committed `docker/Dockerfile` +
`docker/docker-compose.yml`; the image itself is prebuilt and pulled
(`marcpawl/esp-idf:20261119`). Repository-wide guidance is in the root
`AGENTS.md`.

## Image

The pinned image matches the host toolchain (ESP-IDF v6.1, Xtensa GCC 15.2.0)
and is never moved. Pull it with `./scripts/dev.py pull`. Only rebuild when the
`docker/Dockerfile` changes: rebuild, publish a **new** date-stamped tag, bump the
pin in `docker/docker-compose.yml` and `scripts/dev.py` (`DEV_IMAGE`), and update the
docs. See `Developer.md`.

## Running commands

Use `./scripts/dev.py` (or `docker compose run --rm dev <cmd>`), never ad-hoc
`docker run`. `./scripts/dev.py shell` is the preferred way to run a container
command from the host: with no arguments it opens an interactive login shell,
with arguments it runs them in the container (shell syntax works).

The container runs as your UID/GID (`scripts/dev.py` passes
`--user "$(id -u):$(id -g)"`), so build artifacts are owned by you and no
chown or user-namespace remapping is involved. Both serial groups are joined
for board access: `dialout` for a USB-UART bridge, and `plugdev` because the
ESP32-S3's built-in USB-serial/JTAG node is often owned by that group instead.
`$HOME` is a directory that uid can actually write (see below), so ccache
works.

## SSH into the container

`./scripts/dev.py ssh` (or `./scripts/dev.py ssh ls -l`) connects from the host
into the long-running container, as the container user, from `/project`. With
no arguments it opens an interactive login shell; the ESP-IDF environment is
sourced exactly as for `shell`.

- The image's entrypoint starts `sshd` as the container user on the
  unprivileged port 2222; the command in the foreground is unchanged, so
  `docker compose run` behaves as before.
- `docker-compose.yml` publishes that port on `127.0.0.1` only. Set
  `DEV_SSH_PORT` when 2222 is taken or several checkouts run at once.
- Authentication is public-key only. The host's `~/.ssh` is already mounted
  read-only, so sshd reads the same `authorized_keys`/`id_*.pub` files and
  `scripts/dev.py ssh` uses the host's normal keys and agent. Nothing is
  generated or copied; a host with no key needs one (`ssh-keygen -t ed25519`).
- The Dev Container reuses the same service and therefore the same sshd, so
  `scripts/dev.py ssh` can enter the container CLion is attached to.

## ccache and non-1000 uids

A bind mount does not translate uids, so the container runs as the calling
user rather than remapping it. The image's own user is uid 1000; any other uid
(a normal host user, or GitHub's runner at 1001) has no passwd entry inside
the image, so `$HOME` resolves to `/` and ccache fails with
`ccache: error: Permission denied`. `scripts/dev.py` therefore exports
`HOME=/tmp` and `CCACHE_DIR=/tmp/.cache/ccache` for any uid other than 1000
(uid 1000 keeps `/home/dev`). Keep this in mind if you add another
`$HOME`-caching tool.

## Container-only builds

The root `CMakeLists.txt` and every subproject refuse to configure unless
`DOCKER_IMAGE=esp32_sensors` (set by the `docker/Dockerfile`), so a host invocation
fails fast. `sdkconfig` is generated and git-ignored — edit
`sdkconfig.defaults` instead.

## Editing the image

Editing the image, mounts, or user? Change `docker/docker-compose.yml` (and
`docker/Dockerfile`), **not** `devcontainer.json`. The Dev Container deliberately
inherits everything from the compose service so the CLI and the IDE cannot
drift apart.
