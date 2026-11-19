# ESP-IDF locations for login shells.
#
# sshd starts a session with an almost empty environment rather than the
# container's, and `~/.bashrc` sources `export.sh`, which looks for the
# toolchain under `$IDF_TOOLS_PATH` (defaulting to `$HOME/.espressif`).
# Without these, `ssh -p 2222 localhost` fails with "ESP-IDF Python virtual
# environment ... not found".
#
# Sourced by `/etc/profile` (through the bind mount in docker-compose.yml),
# so it runs before `~/.bashrc`. Values already in the environment (a
# `docker compose run` command started by the ESP-IDF entrypoint) are kept.
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-/opt/esp}"
export IDF_PATH="${IDF_PATH:-/opt/esp/idf}"
