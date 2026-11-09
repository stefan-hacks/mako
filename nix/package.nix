# Mako, built from this repository on top of nixpkgs' Nautilus recipe, so it
# keeps the same dependencies, wrapping and glycin setup.
{
  lib,
  nautilus,
  json-glib,
  libcloudproviders,
  gst_all_1,
  src,
  version,
}:

nautilus.overrideAttrs (old: {
  pname = "mako-files";
  inherit src version;

  # nixpkgs' patches target its own (newer) release; Mako doesn't need them.
  patches = [ ];

  # No API docs, so no devdoc output (nixpkgs moves share/doc there).
  outputs = [ "out" ];
  postFixup = "";

  buildInputs = (old.buildInputs or [ ]) ++ [
    json-glib # tags.json and the File Flier import
    libcloudproviders
    # Quick Look plays videos through GStreamer.
    gst_all_1.gst-plugins-base
    gst_all_1.gst-plugins-good
    gst_all_1.gst-plugins-bad
    gst_all_1.gst-libav
  ];

  # The Devel profile gives Mako its own app id (org.gnome.Nautilus.Devel),
  # so it runs next to GNOME Files instead of replacing it.
  mesonFlags = [
    "-Dprofile=Devel"
    "-Ddocs=false"
    "-Dtests=none"
    "-Dextensions=false"
    "-Dcloudproviders=enabled"
  ];

  # Lets xdg-desktop-portal send other apps' open/save dialogs to Mako.
  postInstall = (old.postInstall or "") + ''
    mkdir -p $out/share/xdg-desktop-portal/portals
    cat > $out/share/xdg-desktop-portal/portals/mako.portal <<EOF
    [portal]
    DBusName=org.gnome.Nautilus.Devel
    Interfaces=org.freedesktop.impl.portal.FileChooser;
    EOF
  '';

  # Tests are off and the upstream update script tracks GNOME releases.
  doCheck = false;
  passthru = (old.passthru or { }) // {
    updateScript = null;
    desktopId = "org.gnome.Nautilus.Devel.desktop";
  };

  meta = (old.meta or { }) // {
    description = "Fast, hotkey-first fork of GNOME Files (Nautilus)";
    homepage = "https://github.com/tommkotzu/mako";
    license = lib.licenses.gpl3Plus;
    mainProgram = "nautilus";
    platforms = lib.platforms.linux;
  };
})
