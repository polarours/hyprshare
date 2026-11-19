#compdef hyprshare

# hyprshare zsh completion

_hyprshare() {
    local -a commands
    commands=(
        'status:Show system status'
        'doctor:Run health diagnostics'
        'check:Quick health check'
        'support:Support bundle'
        'discovery:Device discovery'
        'backend:Backend management'
        'compat:Compatibility profiles'
        'app:Application launch'
        'clipboard:Clipboard operations'
        'inbox:Inbox file sharing'
        'peer:Peer management'
        'session:Session management'
        'share:Content sharing'
        'config:Configuration management'
        'sync:File synchronization'
        'clipboard-sync:Clipboard synchronization'
        'record:Screen recording'
        'mobile:Mobile device integration'
        'remote:Remote control'
        'shell:Interactive shell'
    )

    local -a inbox_commands=(
        'status:Show inbox status'
        'history:Show receive history'
        'open:Open inbox directory'
        'announce:Announce inbox service'
        'receive:Receive files'
        'serve:Serve files'
        'put:Send files to inbox'
        'extract:Extract received archives'
    )

    local -a session_commands=(
        'list:List sessions'
        'show:Show session details'
        'delete:Delete session'
        'prune:Prune old sessions'
        'verify:Verify session integrity'
        'probe:Probe remote session'
        'inspect:Inspect remote session'
        'serve:Serve session files'
        'fetch:Fetch remote session'
        'export:Export session'
        'import:Import session'
        'receive:Receive session'
    )

    local -a peer_commands=(
        'self:Show device identity'
        'list:List trusted peers'
        'inspect:Inspect peer card'
        'pair:Pair with peer'
        'invite:Manage peer invitations'
        'export:Export peer card'
        'import:Import peer card'
        'add:Add trusted peer'
        'remove:Remove trusted peer'
    )

    local -a share_commands=(
        'file:Share a file'
        'folder:Share a folder'
        'clipboard:Share clipboard'
        'screen:Share screen'
    )

    local -a sync_commands=(
        'start:Start sync service'
        'stop:Stop sync service'
        'status:Show sync status'
        'add:Add sync rule'
        'remove:Remove sync rule'
        'list:List sync rules'
    )

    local -a clipboard_sync_commands=(
        'start:Start clipboard sync'
        'stop:Stop clipboard sync'
        'status:Show clipboard sync status'
        'push:Push to clipboard'
        'pull:Pull from clipboard'
        'history:Show clipboard history'
    )

    local -a record_commands=(
        'start:Start recording'
        'stop:Stop recording'
        'pause:Pause recording'
        'resume:Resume recording'
        'status:Show recording status'
    )

    local curcontext="$curcontext" state line
    _arguments -C \
        '1:command:->command' \
        '*::arg:->args'

    case $state in
        command)
            _describe 'command' commands
            ;;
        args)
            case $line[1] in
                inbox)
                    _describe 'inbox command' inbox_commands
                    ;;
                session)
                    _describe 'session command' session_commands
                    ;;
                peer)
                    _describe 'peer command' peer_commands
                    ;;
                share)
                    _describe 'share command' share_commands
                    ;;
                sync)
                    _describe 'sync command' sync_commands
                    ;;
                clipboard-sync)
                    _describe 'clipboard-sync command' clipboard_sync_commands
                    ;;
                record)
                    _describe 'record command' record_commands
                    ;;
            esac
            ;;
    esac
}

_hyprshare "$@"
