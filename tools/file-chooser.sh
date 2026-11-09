#!/bin/bash
# Makes Mako the open/save dialog for other apps (browsers, Flatpaks, and
# anything else that uses the desktop portal).
#
# GNOME's portal sends file dialogs to stock Nautilus (org.gnome.Nautilus).
# Mako has the same file-chooser backend but runs as org.gnome.Nautilus.Devel,
# so this adds a portal file for it and prefers it for FileChooser. Nothing
# outside your home folder is changed. xdg-desktop-portal only reads portal
# files from one folder, so the system's portal files are linked next to
# Mako's.
#
# usage: file-chooser.sh install | uninstall
set -e
P=$HOME/.local/share/xdg-desktop-portal/portals
CONF=$HOME/.config/xdg-desktop-portal/gnome-portals.conf
DROPIN=$HOME/.config/systemd/user/xdg-desktop-portal.service.d/mako.conf

case "$1" in
install)
    mkdir -p "$P" "$(dirname "$CONF")" "$(dirname "$DROPIN")"
    for f in /usr/share/xdg-desktop-portal/portals/*.portal; do ln -sf "$f" "$P/"; done
    cat > "$P/mako.portal" <<'EOF'
[portal]
DBusName=org.gnome.Nautilus.Devel
Interfaces=org.freedesktop.impl.portal.FileChooser;
UseIn=gnome
EOF
    if [ -e "$CONF" ] && ! grep -q "Mako" "$CONF"; then cp "$CONF" "$CONF.before-mako"; fi
    cat > "$CONF" <<'EOF'
# Mako opens and saves files for other apps; everything else as GNOME ships it.
[preferred]
default=gnome;gtk;
org.freedesktop.impl.portal.Secret=gnome-keyring;
org.freedesktop.impl.portal.FileChooser=mako;gnome;
EOF
    # With XDG_DESKTOP_PORTAL_DIR set, the portal reads its config only from
    # that folder, so it needs a copy there too.
    cp "$CONF" "$P/gnome-portals.conf"
    cat > "$DROPIN" <<EOF
# Read portal backends from ~/.local/share, where Mako's is (system ones are linked there).
[Service]
Environment=XDG_DESKTOP_PORTAL_DIR=$P
EOF
    ;;
uninstall)
    rm -f "${DROPIN:?}" "${P:?}/mako.portal" "${P:?}/gnome-portals.conf"
    find "${P:?}" -maxdepth 1 -type l -name '*.portal' -delete
    if [ -e "$CONF.before-mako" ]; then mv "$CONF.before-mako" "$CONF"; else rm -f "${CONF:?}"; fi
    ;;
*)
    echo "usage: $0 install | uninstall"; exit 1;;
esac
systemctl --user daemon-reload
systemctl --user restart xdg-desktop-portal
echo "done ($1). Apps already open pick it up on their next dialog."
