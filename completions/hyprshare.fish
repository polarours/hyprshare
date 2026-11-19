# fish completion for hyprshare

function __hyprshare_needs_command
    set cmd (commandline -opc)
    if test (count $cmd) -eq 1
        return 0
    end
    return 1
end

function __hyprshare_using_command
    set cmd (commandline -opc)
    if test (count $cmd) -gt 1
        if test "$cmd[2]" = "$argv[1]"
            return 0
        end
    end
    return 1
end

# Subcommands
complete -c hyprshare -f -n __hyprshare_needs_command -a status -d 'Show system status'
complete -c hyprshare -f -n __hyprshare_needs_command -a doctor -d 'Run health diagnostics'
complete -c hyprshare -f -n __hyprshare_needs_command -a check -d 'Quick health check'
complete -c hyprshare -f -n __hyprshare_needs_command -a support -d 'Support bundle'
complete -c hyprshare -f -n __hyprshare_needs_command -a discovery -d 'Device discovery'
complete -c hyprshare -f -n __hyprshare_needs_command -a backend -d 'Backend management'
complete -c hyprshare -f -n __hyprshare_needs_command -a compat -d 'Compatibility profiles'
complete -c hyprshare -f -n __hyprshare_needs_command -a app -d 'Application launch'
complete -c hyprshare -f -n __hyprshare_needs_command -a clipboard -d 'Clipboard operations'
complete -c hyprshare -f -n __hyprshare_needs_command -a inbox -d 'Inbox file sharing'
complete -c hyprshare -f -n __hyprshare_needs_command -a peer -d 'Peer management'
complete -c hyprshare -f -n __hyprshare_needs_command -a session -d 'Session management'
complete -c hyprshare -f -n __hyprshare_needs_command -a share -d 'Content sharing'
complete -c hyprshare -f -n __hyprshare_needs_command -a config -d 'Configuration management'
complete -c hyprshare -f -n __hyprshare_needs_command -a sync -d 'File synchronization'
complete -c hyprshare -f -n __hyprshare_needs_command -a clipboard-sync -d 'Clipboard synchronization'
complete -c hyprshare -f -n __hyprshare_needs_command -a record -d 'Screen recording'
complete -c hyprshare -f -n __hyprshare_needs_command -a mobile -d 'Mobile device integration'
complete -c hyprshare -f -n __hyprshare_needs_command -a remote -d 'Remote control'
complete -c hyprshare -f -n __hyprshare_needs_command -a shell -d 'Interactive shell'

# Inbox subcommands
complete -c hyprshare -f -n __hyprshare_using_command inbox -a status -d 'Show inbox status'
complete -c hyprshare -f -n __hyprshare_using_command inbox -a history -d 'Show receive history'
complete -c hyprshare -f -n __hyprshare_using_command inbox -a open -d 'Open inbox directory'
complete -c hyprshare -f -n __hyprshare_using_command inbox -a announce -d 'Announce inbox service'
complete -c hyprshare -f -n __hyprshare_using_command inbox -a receive -d 'Receive files'
complete -c hyprshare -f -n __hyprshare_using_command inbox -a serve -d 'Serve files'
complete -c hyprshare -f -n __hyprshare_using_command inbox -a put -d 'Send files to inbox'
complete -c hyprshare -f -n __hyprshare_using_command inbox -a extract -d 'Extract received archives'

# Session subcommands
complete -c hyprshare -f -n __hyprshare_using_command session -a list -d 'List sessions'
complete -c hyprshare -f -n __hyprshare_using_command session -a show -d 'Show session details'
complete -c hyprshare -f -n __hyprshare_using_command session -a delete -d 'Delete session'
complete -c hyprshare -f -n __hyprshare_using_command session -a prune -d 'Prune old sessions'
complete -c hyprshare -f -n __hyprshare_using_command session -a verify -d 'Verify session integrity'
complete -c hyprshare -f -n __hyprshare_using_command session -a probe -d 'Probe remote session'
complete -c hyprshare -f -n __hyprshare_using_command session -a inspect -d 'Inspect remote session'
complete -c hyprshare -f -n __hyprshare_using_command session -a serve -d 'Serve session files'
complete -c hyprshare -f -n __hyprshare_using_command session -a fetch -d 'Fetch remote session'
complete -c hyprshare -f -n __hyprshare_using_command session -a export -d 'Export session'
complete -c hyprshare -f -n __hyprshare_using_command session -a import -d 'Import session'
complete -c hyprshare -f -n __hyprshare_using_command session -a receive -d 'Receive session'

# Peer subcommands
complete -c hyprshare -f -n __hyprshare_using_command peer -a self -d 'Show device identity'
complete -c hyprshare -f -n __hyprshare_using_command peer -a list -d 'List trusted peers'
complete -c hyprshare -f -n __hyprshare_using_command peer -a inspect -d 'Inspect peer card'
complete -c hyprshare -f -n __hyprshare_using_command peer -a pair -d 'Pair with peer'
complete -c hyprshare -f -n __hyprshare_using_command peer -a invite -d 'Manage peer invitations'
complete -c hyprshare -f -n __hyprshare_using_command peer -a export -d 'Export peer card'
complete -c hyprshare -f -n __hyprshare_using_command peer -a import -d 'Import peer card'
complete -c hyprshare -f -n __hyprshare_using_command peer -a add -d 'Add trusted peer'
complete -c hyprshare -f -n __hyprshare_using_command peer -a remove -d 'Remove trusted peer'

# Share subcommands
complete -c hyprshare -f -n __hyprshare_using_command share -a file -d 'Share a file'
complete -c hyprshare -f -n __hyprshare_using_command share -a folder -d 'Share a folder'
complete -c hyprshare -f -n __hyprshare_using_command share -a clipboard -d 'Share clipboard'
complete -c hyprshare -f -n __hyprshare_using_command share -a screen -d 'Share screen'

# Sync subcommands
complete -c hyprshare -f -n __hyprshare_using_command sync -a start -d 'Start sync service'
complete -c hyprshare -f -n __hyprshare_using_command sync -a stop -d 'Stop sync service'
complete -c hyprshare -f -n __hyprshare_using_command sync -a status -d 'Show sync status'
complete -c hyprshare -f -n __hyprshare_using_command sync -a add -d 'Add sync rule'
complete -c hyprshare -f -n __hyprshare_using_command sync -a remove -d 'Remove sync rule'
complete -c hyprshare -f -n __hyprshare_using_command sync -a list -d 'List sync rules'

# Clipboard-sync subcommands
complete -c hyprshare -f -n __hyprshare_using_command clipboard-sync -a start -d 'Start clipboard sync'
complete -c hyprshare -f -n __hyprshare_using_command clipboard-sync -a stop -d 'Stop clipboard sync'
complete -c hyprshare -f -n __hyprshare_using_command clipboard-sync -a status -d 'Show clipboard sync status'
complete -c hyprshare -f -n __hyprshare_using_command clipboard-sync -a push -d 'Push to clipboard'
complete -c hyprshare -f -n __hyprshare_using_command clipboard-sync -a pull -d 'Pull from clipboard'
complete -c hyprshare -f -n __hyprshare_using_command clipboard-sync -a history -d 'Show clipboard history'

# Record subcommands
complete -c hyprshare -f -n __hyprshare_using_command record -a start -d 'Start recording'
complete -c hyprshare -f -n __hyprshare_using_command record -a stop -d 'Stop recording'
complete -c hyprshare -f -n __hyprshare_using_command record -a pause -d 'Pause recording'
complete -c hyprshare -f -n __hyprshare_using_command record -a resume -d 'Resume recording'
complete -c hyprshare -f -n __hyprshare_using_command record -a status -d 'Show recording status'

# Global options
complete -c hyprshare -f -s j -l json -d 'Output in JSON format'
complete -c hyprshare -f -s d -l dry-run -d 'Plan without executing'
complete -c hyprshare -f -l stage -d 'Stage for later serving'
complete -c hyprshare -f -l tls -d 'Enable TLS'
complete -c hyprshare -f -l tls-cert -r -d 'TLS certificate file'
complete -c hyprshare -f -l tls-key -r -d 'TLS key file'
complete -c hyprshare -f -l tls-ca -r -d 'TLS CA certificate file'
complete -c hyprshare -f -l insecure -d 'Disable certificate verification'
