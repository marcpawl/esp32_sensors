# syntax=docker/dockerfile:1

# ESP32 Solar-Powered Thermometer Logger — development container.
#
# NOTE: this image is normally NOT built. `docker-compose.yml` pins the
# prebuilt, published image `marcpawl/esp-idf:20261025`, and both developers
# and CI pull it. Build this Dockerfile only when changing it, then publish the
# result under a NEW date-stamped tag (see Developer.md).
#
# Extends the official Espressif ESP-IDF image (v6.1) with a non-root user that
# maps to the host UID/GID so that (a) files created by the build are owned by
# the host user, and (b) the serial `dialout` group grants access to the ESP32
# USB device for `idf.py flash monitor`.
#
# That non-root user is uid/gid 1000. The container itself is run as the
# *calling* user (`scripts/dev.py` passes `--user "$(id -u):$(id -g)"`), so a
# host user with a different uid still gets build artifacts it owns; for such a
# uid `dev.py` also pins HOME/CCACHE_DIR, because it has no passwd entry here.
# The UID/GID args below therefore only affect the (rare) rebuild.
#
# The base image ships an existing user `ubuntu` (UID/GID 1000) that is already
# a member of `dialout`. We reuse it when it matches the requested UID/GID, and
# fall back to creating a user otherwise, so the build works for any host.
#
# The container is intentionally kept close to the upstream image so the
# ESP-IDF toolchain, Python venv, and env vars ($IDF_PATH, $PATH) keep working
# out of the box. Add project-specific tooling in the marked section below.
#
# Project tooling added on top of the base image:
#   - GitHub CLI (`gh`) from GitHub's official apt repository.
#   - PyGithub (`import github`) in the ESP-IDF Python venv.
#   - Node.js (pinned LTS, official tarball) — the base image ships none.
#   - Continue CLI (`cn`), the command line coding agent used in this repo.

FROM espressif/idf:release-v6.1

# Host user that the container user should map to. Override at build time with:
#   docker build --build-arg UID=$(id -u) --build-arg GID=$(id -g) .
ARG UID=1000
ARG GID=1000
ARG USERNAME=dev
# GID of the host `dialout` group; serial devices (/dev/ttyUSB*, /dev/ttyACM*)
# are typically owned by this group on Linux. Check with: getent group dialout
ARG DIALOUT_GID=20

USER root

# ---------------------------------------------------------------------------
# Base system packages (apt).
# Note: the ESP-IDF image is Ubuntu-based and already includes the toolchain,
# CMake, Ninja, Git, and Python. Add extra apt packages here as needed.
# ---------------------------------------------------------------------------
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        udev \
        usbutils \
        picocom \
        socat \
        ca-certificates \
        curl \
        vim \
        netcat-openbsd screen \
        psmisc \
        # libbsd is required by the ESP-IDF `linux` target (host unit tests);
        # the IDF build emits a warning / link error without it.
        libbsd-dev \
        # System python3 is also usable inside the container.
        python3-pip \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# PyGithub (`import github`): Python wrapper over the GitHub REST API.
#
# Installed into the ESP-IDF venv, because `python3` on PATH inside the
# container IS that venv, and it is created with
# `include-system-site-packages = false` — so an apt package or anything in
# /usr/lib/python3/dist-packages would not be importable.
#
# The venv interpreter is used directly (rather than sourcing export.sh, which
# needs a bash login context). Its name follows the image's IDF version and
# Python version, e.g. /opt/esp/python_env/idf6.1_py3.12_env.
RUN PYTHON="$(ls -d "${IDF_PATH%/idf}"/python_env/idf*_py*/bin/python | head -n 1)" \
    && test -x "$PYTHON" \
    && "$PYTHON" -m pip install --no-cache-dir PyGithub \
    && "$PYTHON" -c "from github import Github; import importlib.metadata as m; print('PyGithub', m.version('PyGithub'))"

# ---------------------------------------------------------------------------
# GitHub CLI (`gh`), installed from GitHub's official apt repository.
# https://github.com/cli/cli/blob/trunk/docs/install_linux.md
# Adds /usr/bin/gh for authenticated repository work from inside the container
# (e.g. `gh pr create`, `gh release create`). `auth login` is per-user, so the
# token lives in the bind-mounted/persisted home, not in this image.
# ---------------------------------------------------------------------------
RUN mkdir -p -m 755 /etc/apt/keyrings \
    && curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
        -o /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/* \
    && gh --version

# ---------------------------------------------------------------------------
# Node.js: required by the Continue CLI below. The base image ships no Node
# runtime at all, and the ESP-IDF venv cannot provide one.
#
# The official static tarball is used (rather than NodeSource or a distro
# package) so the version is pinned by URL *and* SHA-256 and does not drift with
# the Ubuntu release. It is unpacked into /usr/local so `node`/`npm` are on PATH
# for *any* uid: the container is run as the calling user, and for a uid with no
# passwd entry `scripts/dev.py` sets HOME=/tmp — a per-user install (nvm/fnm,
# what the upstream Continue installer does) would need a writable HOME at build
# time and would be invisible to those users.
#
# To bump: set NODE_VERSION and the two SHA-256 values from
# https://nodejs.org/dist/v<version>/SHASUMS256.txt (only the architectures
# below are supported; add a branch for any other).
# ---------------------------------------------------------------------------
ARG NODE_VERSION=24.22.2
RUN ARCH="$(dpkg --print-architecture)" \
    && case "$ARCH" in \
        amd64) NODE_ARCH=x64 \
               NODE_SHA256=48cc0e21e048790e54c0cf5749c2725005313d652651c40b7a21feb9697aaf75 ;; \
        arm64) NODE_ARCH=arm64 \
               NODE_SHA256=eb187837a2230d4047161ac0be008584e16b46f726e9c9490ee35dc6b17d9ba8 ;; \
        *) echo "Unsupported architecture for Node.js: $ARCH" >&2; exit 1 ;; \
    esac \
    && TARBALL="node-v${NODE_VERSION}-linux-${NODE_ARCH}.tar.xz" \
    && curl -fsSL -o "/tmp/${TARBALL}" \
        "https://nodejs.org/dist/v${NODE_VERSION}/${TARBALL}" \
    && echo "${NODE_SHA256}  /tmp/${TARBALL}" | sha256sum --check --strict - \
    && tar -xJf "/tmp/${TARBALL}" -C /usr/local \
        --strip-components=1 --no-same-owner \
    && rm -f "/tmp/${TARBALL}" \
    && node --version \
    && npm --version

# ---------------------------------------------------------------------------
# Continue CLI (`cn`), the command line coding agent used on this repository.
# https://github.com/continuedev/continue/tree/main/extensions/cli
#
# Installed from npm into /usr/local (npm's default prefix as root) rather than
# a user prefix, so every uid that runs the container finds `cn` on PATH. The
# version is pinned exactly and the registry integrity is left to npm, so a
# rebuild cannot silently pick up a new release.
#
# `cn` keeps its configuration, sessions and models under `$HOME/.continue`, so
# it is per-user state, never part of this image: uid 1000 (the image user)
# keeps it in /home/dev, while any other uid gets HOME=/tmp from `dev.py` and so
# starts with a fresh config on each container. Point it elsewhere explicitly
# with `cn --config <path>` (see scripts/dev.py's environment) to share one.
# ---------------------------------------------------------------------------
ARG CONTINUE_CLI_VERSION=1.5.47
RUN npm install --global --no-fund --no-audit \
        "@continuedev/cli@${CONTINUE_CLI_VERSION}" \
    && test "$(cn --version)" = "${CONTINUE_CLI_VERSION}"

# ---------------------------------------------------------------------------
# esp-clang: the Espressif-maintained LLVM toolchain, which ships the
# `clang-format` / `clang-tidy` binaries used by the pre-commit hook
# (.githooks/pre-commit) and scripts/dev.py.
#
# It is *not* installed by `install.sh` (its tools.json entry is
# "install": "on_request"), so it must be requested explicitly. Installing it
# here also adds `.../esp-clang/bin` to PATH via $IDF_PATH/export.sh, so
# `clang-format` / `clang-tidy` resolve to the Espressif build.
#
# This pins the version to whatever the current ESP-IDF release manages
# (v6.1 -> esp-21.1.3_20260408), keeping formatting reproducible and identical
# to what the IDF toolchain ships. To use a different release, change the
# ESP-IDF base image (the version comes from tools.json).
# ---------------------------------------------------------------------------
RUN cd $IDF_PATH && \
    . "$IDF_PATH/export.sh" && \
      /opt/esp/idf/tools/idf_tools.py --non-interactive  install-python-env && \
     "$IDF_PATH/tools/idf_tools.py" --non-interactive install \
          esp-clang


# Ensure a `dialout` group exists with the requested GID (create/skip safely).
RUN getent group "${DIALOUT_GID}" >/dev/null 2>&1 \
    || groupadd --gid "${DIALOUT_GID}" dialout

# Create or reuse the container user so its UID/GID match the host.
# - If UID+GID 1000 already map to the stock `ubuntu` user, rename it to
#   ${USERNAME} so paths/home dir are consistent.
# - Otherwise create a fresh user with the requested IDs.
RUN if getent passwd "${UID}" >/dev/null 2>&1; then \
        existing="$(getent passwd "${UID}" | cut -d: -f1)"; \
        if [ "$existing" != "${USERNAME}" ]; then \
            usermod --login "${USERNAME}" --home "/home/${USERNAME}" \
                --move-home "$existing"; \
        fi; \
    else \
        useradd --uid "${UID}" --create-home --shell /bin/bash "${USERNAME}"; \
    fi \
    # Align the primary group / home ownership with the requested GID.
    && if getent group "${GID}" >/dev/null 2>&1; then \
        usermod --gid "${GID}" "${USERNAME}"; \
    fi \
    # Guarantee membership in the serial-relevant groups.
    && for g in "${DIALOUT_GID}" plugdev; do \
        getent group "$g" >/dev/null && usermod -aG "$g" "${USERNAME}"; \
    done \
    && mkdir -p "/home/${USERNAME}/.cache" "/home/${USERNAME}/.espressif" \
        "/home/${USERNAME}/.commandhistory" "/home/${USERNAME}/.continue" \
    && chown -R "${UID}:${GID}" "/home/${USERNAME}" \
    # Source the IDF environment for every interactive login shell.
    && echo "ESP_IDF_EXPORT_DEBUG=1" >> "/home/${USERNAME}/.bashrc" \
    && printf 'source %s/export.sh\n' "${IDF_PATH}" >> "/home/${USERNAME}/.bashrc"

# ---------------------------------------------------------------------------
# Continue CLI (`cn`) state directory, shared with the host.
#
# `docker-compose.yml` bind-mounts the host's `~/.continue` here so the host and
# the container see the same config, models, and sessions. A bind mount needs
# its destination to exist; Docker would otherwise create it as root (and the
# mount would hide it), and `cn` running without the mount would have to invent
# it. Creating it up front, owned by the container user, makes both paths work.
#
# Two locations, because the container's HOME depends on the uid it runs as
# (see scripts/dev.py): uid 1000 keeps /home/dev, any other uid is given
# HOME=/tmp so tools like ccache find a writable home. `cn` reads its state from
# $HOME/.continue, so both are mount points in compose.
# ---------------------------------------------------------------------------
RUN mkdir -p /tmp/.continue \
    && chown "${UID}:${GID}" "/home/${USERNAME}/.continue" /tmp/.continue \
    # World-writable + sticky, like /tmp itself: any uid may use it when the
    # host config is not mounted (the mount, when present, hides this dir).
    && chmod 1777 /tmp/.continue


# Persist bash history across container restarts (volume-mounted in compose).
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/home/${USERNAME}/.commandhistory/.bash_history" \
    && echo "${SNIPPET}" >> "/home/${USERNAME}/.bashrc" \
    && chown -R "${UID}:${GID}" "/home/${USERNAME}/.commandhistory"

RUN mkdir -p /tmp/esp_idf_activate_dev && chmod 777 /tmp/esp_idf_activate_dev

USER ${USERNAME}

WORKDIR /project
ENV DOCKER_IMAGE=esp32_sensors
