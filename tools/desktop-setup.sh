#!/bin/bash
# Desktop integration for Mako on GNOME:
#  - Super+F opens a new Mako window every time (GNOME's own "Home folder"
#    key only brings back a window that already shows home).
#  - Mako starts hidden at login, so even the first window opens at once.
# usage: desktop-setup.sh install | uninstall   [KEY, default <Super>f]
set -e
MK=org.gnome.settings-daemon.plugins.media-keys
K=/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings
S=$MK.custom-keybinding:$K/mako/
LAUNCH=${MAKO_LAUNCHER:-$HOME/.local/bin/nautilus-flier}
AUTOSTART=$HOME/.config/autostart/mako.desktop
KEY=${2:-<Super>f}

list_without_mako() {
    gsettings get $MK custom-keybindings | python3 -c "
import ast, sys
s = sys.stdin.read().strip().removeprefix('@as ')
print([p for p in ast.literal_eval(s) if not p.endswith('/mako/')])"
}

case "$1" in
install)
    # Free the key if GNOME's home-folder shortcut uses it.
    if gsettings get $MK home | grep -qiF "'$KEY'"; then
        gsettings get $MK home > "$HOME/.config/mako-home-key.bak"
        gsettings set $MK home "[]"
    fi
    gsettings set $MK custom-keybindings "$(list_without_mako | python3 -c "
import ast, sys; l = ast.literal_eval(sys.stdin.read()); l.append('$K/mako/'); print(l)")"
    gsettings set "$S" name 'Mako: new window'
    gsettings set "$S" command "$LAUNCH --new-window"
    gsettings set "$S" binding "$KEY"
    mkdir -p "$(dirname "$AUTOSTART")"
    cat > "$AUTOSTART" <<EOF
[Desktop Entry]
Type=Application
Name=Mako (background)
Comment=Starts Mako without a window so new windows open at once
Exec=$LAUNCH --gapplication-service
NoDisplay=true
X-GNOME-Autostart-Phase=Applications
EOF
    echo "$KEY opens a new Mako window; Mako starts in the background at login."
    ;;
uninstall)
    gsettings set $MK custom-keybindings "$(list_without_mako)"
    gsettings reset-recursively "$S" || true
    if [ -e "$HOME/.config/mako-home-key.bak" ]; then
        gsettings set $MK home "$(cat "$HOME/.config/mako-home-key.bak")"
        rm -f "$HOME/.config/mako-home-key.bak"
    fi
    rm -f "${AUTOSTART:?}"
    echo "Removed the Mako key and the login item."
    ;;
*)
    echo "usage: $0 install | uninstall [KEY]"; exit 1;;
esac
