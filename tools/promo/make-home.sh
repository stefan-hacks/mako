#!/bin/bash
# Builds the demo home for promo recordings from a folder of deep-sea media.
# usage: make-home.sh SRC_DIR [HOME_DIR]
set -e
SRC=${1:?source folder}; H=${2:-/tmp/mako-home}
rm -rf "${H:?}"; mkdir -p "$H"/{Desktop,Downloads,Music,Templates,Public}
P="$H/Pictures/Deep Sea"; mkdir -p "$P"/{Sharks,Bettas,Reef,Moodboard}
cp_(){ cp "$SRC/$1" "$2"; }
cp_ 282e5eeba75da9e4cc9cc9a0cd7a5abc.jpg "$P/Sharks/great-white.jpg"
cp_ 407e12f82f59d39e7a48d0d4ac5725e2.jpg "$P/Sharks/mako-shark.jpg"
cp_ b426e4cdcc6dcfcb9a8ff34728d446a6.jpg "$P/Sharks/shark-surface.jpg"
cp_ download.png                         "$P/Sharks/hammerhead.png"
cp_ 24cac3b4e8c6a1595e9c2adf04edfaf5.jpg "$P/Bettas/betta-closeup.jpg"
cp_ 38d07c385505bfa89fb65332abbd26ba.jpg "$P/Bettas/betta-blue.jpg"
cp_ 4d8e10bb53fe7e3f0c0912da50dc96df.jpg "$P/Bettas/betta-red.jpg"
cp_ 90844585b94ff67fc7060f6edc1ae3ae.jpg "$P/Bettas/betta-violet.jpg"
cp_ aa13f61f1c2227cb0b1563b88eb9375f.jpg "$P/Bettas/betta-lilac.jpg"
cp_ 62dc35ac9f69d096bc2605ce9ac93837.jpg "$P/Reef/coral.jpg"
cp_ c5c05a893f7faf477225fc46353457f5.jpg "$P/Reef/jellyfish.jpg"
cp_ fae14701023547aee3e0621474c5b821.jpg "$P/Reef/crab-taxi.jpg"
cp_ 2a8d402bac9c93851bc61a5868858e18.jpg "$P/Reef/nautilus-octopus.jpg"
cp_ bcc1983c66c130999e7b5f9a73788737.jpg "$P/Reef/plankton-chart.jpg"
# Moodboard: everything flat, for multi-file Quick Look
cp "$P"/Sharks/* "$P"/Bettas/* "$P"/Reef/* "$P/Moodboard/"

V="$H/Videos"; mkdir -p "$V/Sardine Run frames"
cp "$SRC"/*.mp4 "$V/sardine-school.mp4"
ffmpeg -loglevel error -i "$V/sardine-school.mp4" -vf fps=4,scale=360:-2 "$V/Sardine Run frames/frame_%04d.jpg"

D="$H/Documents/Expedition"; mkdir -p "$D"
for p in "Abyssal Plain" "Coral Triangle" "Kelp Forest" "Mariana Trench" "Mid-Atlantic Ridge" "Sargasso Sea" "Great Barrier Reef" "Monterey Canyon"; do
  mkdir -p "$D/Dives/$p"
  for n in 1 2 3; do printf 'dive %s, %s\ndepth: %sm\n' "$n" "$p" "$((RANDOM%4000+50))" > "$D/Dives/$p/log-$n.txt"; done
done
cat > "$D/README.md" <<'M'
# Deep Sea Expedition 2026

Eight dive sites, three logs each. Species photos live in ~/Pictures/Deep Sea.

- Sharks: great white, mako, hammerhead
- Reef: coral, jellyfish, crabs
- Bettas for the aquarium side project
M
cat > "$D/species.csv" <<'M'
species,depth_m,site,count
Isurus oxyrinchus,150,Mid-Atlantic Ridge,3
Carcharodon carcharias,250,Monterey Canyon,1
Sphyrna mokarran,80,Coral Triangle,5
Aurelia aurita,20,Kelp Forest,140
Nautilus pompilius,400,Great Barrier Reef,2
M
cat > "$D/gear.json" <<'M'
{
  "camera": "RED Komodo 6K",
  "housing": "Nauticam",
  "lights": ["Keldan 8X", "Keldan 8X"],
  "max_depth_m": 120
}
M
printf 'Budget\n======\nboat  12000\ngear   8400\nfuel   2100\n' > "$D/budget.txt"
for n in 01 02 03 04 05 06; do printf 'Field notes %s\n' "$n" > "$D/field-notes-$n.md"; done

# Benchmark folders
B="$H/Bench"; mkdir -p "$B/10k files" "$B/50k files" "$B/tree 20k"
( cd "$B/10k files" && seq -f "sample_%05g.txt" 1 10000 | xargs touch )
( cd "$B/50k files" && seq -f "sample_%05g.txt" 1 50000 | xargs touch )
( cd "$B/tree 20k" && for a in $(seq -w 1 20); do for b in $(seq -w 1 10); do mkdir -p "site$a/dive$b"; ( cd "site$a/dive$b" && seq -f "shot_%03g.raw" 1 100 | xargs touch ); done; done )

# Ratings (0-10 like baloo) and tags
x(){ python3 -I -c 'import os,sys; os.setxattr(sys.argv[1], sys.argv[2], sys.argv[3].encode())' "$1" "$2" "$3"; }
r(){ x "$1" user.baloo.rating "$2"; }
t(){ x "$1" user.xdg.tags "$2"; }
r "$P/Sharks/mako-shark.jpg" 10; r "$P/Sharks/great-white.jpg" 8; r "$P/Sharks/hammerhead.png" 6
r "$P/Bettas/betta-violet.jpg" 10; r "$P/Bettas/betta-blue.jpg" 8; r "$P/Reef/nautilus-octopus.jpg" 10; r "$P/Reef/jellyfish.jpg" 6
t "$P/Sharks/mako-shark.jpg" "Favorites,Print"; t "$P/Reef/nautilus-octopus.jpg" "Favorites"; t "$P/Bettas/betta-violet.jpg" "Favorites"
t "$P/Sharks/great-white.jpg" "Print"; t "$P/Reef/plankton-chart.jpg" "Research"; t "$D/species.csv" "Research"; t "$D/README.md" "Research"
mkdir -p "$H/.config/nautilus"
cat > "$H/.config/nautilus/tags.json" <<'M'
{ "tags" : [
 { "name" : "Favorites", "color" : [ 246, 211, 45 ] },
 { "name" : "Print", "color" : [ 53, 132, 228 ] },
 { "name" : "Research", "color" : [ 38, 162, 105 ] } ] }
M
cat > "$H/.config/user-dirs.dirs" <<'M'
XDG_DESKTOP_DIR="$HOME/Desktop"
XDG_DOWNLOAD_DIR="$HOME/Downloads"
XDG_DOCUMENTS_DIR="$HOME/Documents"
XDG_MUSIC_DIR="$HOME/Music"
XDG_PICTURES_DIR="$HOME/Pictures"
XDG_VIDEOS_DIR="$HOME/Videos"
XDG_TEMPLATES_DIR="$HOME/Templates"
XDG_PUBLICSHARE_DIR="$HOME/Public"
M
echo "built $H"
