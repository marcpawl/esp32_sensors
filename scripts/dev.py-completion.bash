# Bash completion for scripts/dev.py (the ESP32 thermometer container task runner).
#
# Enable it in the current shell:
#     source scripts/dev.py-completion.bash
#
# Enable it for every new shell by adding this line to ~/.bashrc:
#     source /path/to/esp32_sensors/scripts/dev.py-completion.bash
#
# Then tab-complete subcommands, e.g.:
#     ./scripts/dev.py form<TAB>   ->  format  format-check  format-check-staged

_dev_py() {
    local cur prev commands
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # Locate dev.py from the words typed so far so the completion reflects the
    # script actually being invoked (works with ./scripts/dev.py or an abspath).
    local script="${COMP_WORDS[0]}"
    if [[ "$script" != *dev.py ]]; then
        script="scripts/dev.py"
    fi

    # Only complete the first argument (the subcommand); nothing is expected
    # after it, so fall back to file completion for later words.
    if [[ "$COMP_CWORD" -eq 1 ]]; then
        # Ask dev.py itself for the matching commands — keeps the command list
        # defined in exactly one place.
        commands="$(python3 "$script" complete "$cur" 2>/dev/null \
                    || python3 scripts/dev.py complete "$cur" 2>/dev/null)"
        if [[ -n "$commands" ]]; then
            COMPREPLY=( $(compgen -W "$commands" -- "$cur") )
            return 0
        fi
    fi

    COMPREPLY=()
    return 0
}

complete -o bashdefault -o default -F _dev_py dev.py
complete -o bashdefault -o default -F _dev_py ./scripts/dev.py
