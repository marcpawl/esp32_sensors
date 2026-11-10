# syntax=docker/dockerfile:1

# ESP32 Solar-Powered Thermometer Logger — development container.
#
# NOTE: this image is normally NOT built. `docker-compose.yml` pins the
# prebuilt, published image `marcpawl/esp-idf:20261109b`, and both developers
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
#   - esp-clang, providing `clang-format` / `clang-tidy`.
#   - pycodestyle (pinned) for the Python style check.

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
# (e.g. `gh pr create`, `gh release create`).
#
# `gh` is an API client, not a git transport: it accepts no SSH key, so the
# host's `~/.ssh` does not log it in. Its credentials are an OAuth token or a
# PAT, supplied either by the host's `~/.config/gh` (bind-mounted by
# docker-compose.yml) or by passing GH_TOKEN/GITHUB_TOKEN through the
# environment, which takes precedence and works even when the host keeps its
# token in a desktop keyring the container cannot reach. Nothing is baked into
# this image: `auth login` is per-user, so the token lives in the bind-mounted
# home.
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

# ---------------------------------------------------------------------------
# pycodestyle: PEP 8 checker for this repository's Python, driven by
# `scripts/dev.py pycodestyle` (the first gate in the pre-commit hook, and a CI
# step). It covers `scripts/dev.py`, `scripts/test_dev.py`, and
# `.githooks/pre-commit` — the last is Python despite its missing `.py` suffix,
# so it is checked by shebang, not by extension.
#
# Installed into the ESP-IDF venv for the same reason as PyGithub: `python3` on
# PATH inside the container IS that venv, and the venv is created with
# `include-system-site-packages = false`, so a system-wide install would not be
# importable. `python3 -m pycodestyle` is how `dev.py` invokes it.
#
# The version is pinned so a rebuild cannot silently change the rules being
# enforced, and the install is self-checking: the final `--version` fails the
# build rather than deferring a broken install to the first lint run.
#
# Deliberately the LAST tooling layer. Everything above is cached, and the
# layers that follow are a few hundred kilobytes, so placing this here costs a
# trivial rebuild; sitting next to PyGithub would invalidate the ~3 GB
# esp-clang layer above it.
#
# Defaults are used (no setup.cfg): pycodestyle's PEP 8 rule set, with its
# 79-column limit.
# ---------------------------------------------------------------------------
ARG PYCODESTYLE_VERSION=2.15.0
RUN PYTHON="$(ls -d "${IDF_PATH%/idf}"/python_env/idf*_py*/bin/python | head -n 1)" \
    && test -x "$PYTHON" \
    && "$PYTHON" -m pip install --no-cache-dir \
        "pycodestyle==${PYCODESTYLE_VERSION}" \
    && "$PYTHON" -m pycodestyle --version \
    && test "$("$PYTHON" -m pycodestyle --version)" = "${PYCODESTYLE_VERSION}"

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
# Continue CLI (`cn`), SSH key, and GitHub CLI (`gh`) mount points, shared with
# the host.
#
# `docker-compose.yml` bind-mounts the host's `~/.continue`, `~/.ssh`, and
# `~/.config/gh` here so the host and the container see the same files: `cn`
# keeps its config, models, and sessions under $HOME/.continue, `git push`
# authenticates with the host's SSH keys under $HOME/.ssh, and `gh` reads its
# logged-in account from $HOME/.config/gh. (gh accepts no SSH key for its own
# commands — they call the REST API, which needs a token — so its state is
# separate from `~/.ssh`; see the GH_TOKEN passthrough in docker-compose.yml.)
#
# A bind mount needs its destination to exist; Docker would otherwise create it
# as root (and the mount would hide it), and the tool running without the mount
# would have to invent it. Creating it up front, owned by the container user,
# makes both paths work.
#
# Two locations, because the container's HOME depends on the uid it runs as
# (see scripts/dev.py): uid 1000 keeps /home/dev, any other uid is given
# HOME=/tmp so tools like ccache find a writable home. `cn` and `ssh` read from
# $HOME, so both are mount points in compose.
#
# `~/.ssh` is mounted READ-ONLY, so its permissions are the host's — SSH
# requires the private key to be 0600 and the directory 0700 or it refuses to
# use them, and a `.ssh` directory owned by another uid would fail. That is
# exactly why the container must run as the host uid (see scripts/dev.py).
#
# The directory is 0700 here as well, so `ssh` is happy when the mount is
# absent (then there is simply no key, and `git push` fails loudly).
#
# `~/.config/gh` needs no 0700: unlike ssh, `gh` does not refuse to run when
# the directory's mode or owner is "wrong", and `gh auth login` must be able to
# write there when the mount is absent. (Its `hosts.yml` does hold a token when
# `gh` stored one there, which is the same secret the mounted host copy already
# carries — see the read/write mount in docker-compose.yml.)
# ---------------------------------------------------------------------------
RUN mkdir -p /tmp/.continue /tmp/.config/gh \
    # World-writable + sticky, like /tmp itself: any uid may use it when the
    # host config is not mounted (the mount, when present, hides this dir, and
    # then carries the host's own modes — which is why the container must run
    # as the host uid; see scripts/dev.py).
    #
    # `/tmp/.config/gh` gets the same treatment on purpose. Left at the 0700
    # owned by uid 1000 that `install -d` below would give it, every other uid
    # would fail with EACCES ("failed to read configuration") instead of simply
    # starting unauthenticated, and that is the useful behaviour for the many
    # runs that have no `gh` login to share (CI, a bare `docker run`, a host
    # that never logged in).
    && chmod 1777 /tmp/.continue /tmp/.config/gh \
    # The remaining mount points belong to the container user. `install -d`
    # creates them with SSH's required 0700 (a loose mode or a foreign owner
    # makes ssh refuse the key), for both possible HOMEs. `-m 700` applies to
    # the leaf only, so each `.config` parent stays traversable at 755.
    && install -d -m 700 -o "${UID}" -g "${GID}" \
        "/home/${USERNAME}/.continue" "/home/${USERNAME}/.ssh" /tmp/.ssh \
        "/home/${USERNAME}/.config/gh"


# Persist bash history across container restarts (volume-mounted in compose).
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/home/${USERNAME}/.commandhistory/.bash_history" \
    && echo "${SNIPPET}" >> "/home/${USERNAME}/.bashrc" \
    && chown -R "${UID}:${GID}" "/home/${USERNAME}/.commandhistory"

RUN mkdir -p /tmp/esp_idf_activate_dev && chmod 777 /tmp/esp_idf_activate_dev

USER ${USERNAME}

WORKDIR /project
ENV DOCKER_IMAGE=esp32_sensors
