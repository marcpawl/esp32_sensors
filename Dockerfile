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
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Project-specific software installation.
# Add additional `RUN` commands here for tooling the project needs
# (e.g. python packages into the IDF venv, extra CLI utilities, formatters).
#
# Examples (kept commented out so the image stays lean until required):
#   RUN python -m pip install --no-cache-dir pre-commit
#   RUN . "$IDF_PATH/export.sh" && pip install --no-cache-dir esptool
# ---------------------------------------------------------------------------

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
    && printf 'source %s/export.sh\n' "${IDF_PATH}" >> "/home/${USERNAME}/.bashrc"

# Persist bash history across container restarts (volume-mounted in compose).
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/home/${USERNAME}/.commandhistory/.bash_history" \
    && echo "${SNIPPET}" >> "/home/${USERNAME}/.bashrc" \
    && chown -R "${UID}:${GID}" "/home/${USERNAME}/.commandhistory"

USER ${USERNAME}
WORKDIR /project
