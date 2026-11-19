#!/bin/bash
# hyprshare bash completion

_hyprshare_completions() {
    local cur prev commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    commands="status doctor check support discovery backend compat app clipboard inbox peer session share config sync clipboard-sync record mobile remote shell"

    subcommands=(
        "inbox:status history open announce receive serve put extract"
        "session:list show delete prune verify probe inspect serve fetch export import receive"
        "peer:self list inspect pair invite export import add remove"
        "share:file folder clipboard screen"
        "support:bundle"
        "discovery:status browse"
        "backend:list"
        "compat:show"
        "app:launch"
        "clipboard:read write receive"
        "config:show set get"
        "sync:start stop status add remove list"
        "clipboard-sync:start stop status push pull history"
        "record:start stop pause resume status"
        "mobile:serve send"
        "remote:serve status"
        "shell:"
    )

    if [[ ${COMP_CWORD} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${commands}" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 2 ]]; then
        local cmd="${COMP_WORDS[1]}"
        for sub in "${subcommands[@]}"; do
            if [[ "${sub%%:*}" == "${cmd}" ]]; then
                COMPREPLY=( $(compgen -W "${sub#*:}" -- ${cur}) )
                break
            fi
        done
    fi

    return 0
}

complete -F _hyprshare_completions hyprshare
