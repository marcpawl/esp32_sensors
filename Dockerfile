# syntax=docker/dockerfile:1

# ESP32 Solar-Powered Thermometer Logger — development container.
#
# Extends the official Espressif ESP-IDF image (v6.1) with a non-root user that
# maps to the host UID/GID so that (a) files created by the build are owned by
# the host user, and (b) the serial `dialout` group grants access to the ESP32
# USB device for `idf.py flash monitor`.
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
#   - esp-clang (`clang-format` / `clang-tidy`).
#
# NOTE: this image is normally NOT built locally. It is prebuilt and published
# as `marcpawl/esp-idf:YYYYMMDD`, and docker-compose.yml pins that tag as the
# `dev` service image, so developers and CI just pull it
# (`./scripts/dev.py pull`). Change this file only when the image itself must
# change; then build, publish under a NEW date-stamped tag (never move an
# existing tag) and bump the pin in docker-compose.yml + scripts/dev.py
# (DEV_IMAGE). See Developer.md, "Updating / publishing the dev image".

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
        "/home/${USERNAME}/.commandhistory" \
    && chown -R "${UID}:${GID}" "/home/${USERNAME}" \
    # Source the IDF environment for every interactive login shell.
    && echo "ESP_IDF_EXPORT_DEBUG=1" >> "/home/${USERNAME}/.bashrc" \
    && printf 'source %s/export.sh\n' "${IDF_PATH}" >> "/home/${USERNAME}/.bashrc"


# Persist bash history across container restarts (volume-mounted in compose).
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/home/${USERNAME}/.commandhistory/.bash_history" \
    && echo "${SNIPPET}" >> "/home/${USERNAME}/.bashrc" \
    && chown -R "${UID}:${GID}" "/home/${USERNAME}/.commandhistory"

RUN mkdir -p /tmp/esp_idf_activate_dev && chmod 777 /tmp/esp_idf_activate_dev

# The ESP-IDF tree is owned by root while we run as ${USERNAME}, so git would
# reject it as "dubious ownership" when the build inspects its submodules.
# Trust exactly that tree (not `*`, which would hide a bad project checkout).
# tools/idf.py also inherits this via GIT_CONFIG_* below.
#
# HOME and CCACHE_DIR are pinned so the build's ccache (IDF_CCACHE_ENABLE=1)
# always has a writable cache directory, even when the container is started
# with `docker compose run --user <some other uid>` (e.g. a CI runner).
RUN git config --global --add safe.directory /opt/esp/idf

ENV HOME=/home/${USERNAME} \
    CCACHE_DIR=/home/${USERNAME}/.cache/ccache \
    GIT_CONFIG_COUNT=1 \
    GIT_CONFIG_KEY_0=safe.directory \
    GIT_CONFIG_VALUE_0=/opt/esp/idf

USER ${USERNAME}

WORKDIR /project
ENV DOCKER_IMAGE=esp32_sensors
