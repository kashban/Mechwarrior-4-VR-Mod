MW4VR - MechWarrior 4: Mercenaries in VR
=========================================
(Deutsche Fassung weiter unten.)

Plays MechWarrior 4: Mercenaries in a VR headset: real stereo, head tracking inside the cockpit, a HUD
with depth. Single player only.


WHAT YOU NEED
-------------
- MechWarrior 4: Mercenaries with MekTek MekPak 3.1c
- MW4Engine DX11 (mw4hook.dll), build 20261107_ea16ef2. With another build parts of MW4VR switch
  themselves off (mw4vr.log tells, lines beginning "abi:").
- A VR headset with a 32-bit OpenXR runtime. Tested with a Quest 3 over Virtual Desktop (VDXR) only.
- A fast graphics card: every frame is drawn twice.

Where to get them:
  MW4Engine DX11         https://www.moddb.com/mods/mw4-mercenaries-dx11-renderer-overhaul   (v1.0.7)
  MechWarrior 5: Mercenaries, for its cockpits only (optional):
      Epic Games Store   https://store.epicgames.com/p/mechwarrior-5
      Steam              https://store.steampowered.com/app/784080


INSTALL
-------
The easy way: unpack the package anywhere and start MW4VR_Setup.cmd. Its window asks for the game's
folder and does the steps below; it can also install the trees and export and convert your
MechWarrior 5 cockpits. Settings you already have are kept.

A newer version: unpack its package and start MW4VR_Setup.cmd again - the button reads "Update". The
program files are replaced; your settings stay and only get what is new (new keys in mw4vr.ini, new
mechs, cockpit settings you have not changed).

By hand:
1. In mw4hook.ini set:
       antihack=0
       fov=130
       vsync=0
       taa=0
       mode=windowed
       resolution=1300x1300      (any square size; MW4VR renders at its own, see render_height)
2. Copy the files of the package's "game" folder into the game's folder.
3. Connect the headset, then start the game with mw4vr_start.cmd.
4. In the cockpit: sit as you will play and press Ctrl+Home.

antihack=0 means: do not play online with this mod.

To remove MW4VR delete d3d11.dll and dinput.dll from the game's folder.


THE FILES
---------
d3d11.dll              the mod
mw4vr.ini              its settings, a comment at each key; most are read again while the game runs
dinput.dll             cures the game dying or hanging at start-up on PCs with certain USB devices
                       (it leaves "other HID devices" out of the game's device list). Not needed if you
                       never had that trouble.
mw4vr_start.cmd/.ps1   start the game without the administrator prompt
mw4vr_cockpit_map.ini  which MechWarrior 5 cockpit each mech gets (only with converted cockpits)
mw4vr_mechs.ini        written by the mod: what it remembers for each mech
mw4vr.log              written by the mod: what it did; send it along when you report a fault


KEYS (in a battle)
------------------
  Ctrl+F1                      all keys as a page in the headset; the game waits until you close it
                               (Ctrl+F1 again or Esc; its size: help_size in mw4vr.ini)

View and seat
  Ctrl+Home                    recentre the view
  Ctrl+Up / Down               seat forward / back
  Ctrl+Right / Left            seat higher / lower
  Ctrl+Ins / Del               the cockpit's size (with Shift: the world's)
  Ctrl+F10                     this cockpit fits: remember it for this mech
  Ctrl+PgUp / PgDn / End       cross-hair and target marks farther / nearer / far

The HUD's parts: 1 radar, 2 target, 3 damage, 4 lance list, 5 weapons, 6 top strip
  Ctrl+Alt+1..6                put that part where you look
  Ctrl+Alt+Shift+1..6          choose a part, then with Ctrl+Alt+Shift held:
      arrows                   move it
      PgUp / PgDn              larger / smaller
      Ins / Del                farther / nearer
      0                        hide it
  Ctrl+Alt+7                   all parts back to their automatic places
  The part the keys change has a yellow frame for some seconds.

Other
  Ctrl+F12                     a picture of both eyes into mw4vr_shots\

The seat, the sizes and the parts' places are remembered - the seat and sizes for each mech, the seat apart for
the game's cockpit and for a MechWarrior 5 one. Seen from outside (the third-person view) the HUD's parts have
places of their own, the same for every mech; what you move out there leaves the cockpits alone.
Esc in a battle: the game's menu is one flat screen straight ahead (its size: menu_scale in mw4vr.ini).


MECHWARRIOR 5 COCKPITS (optional)
---------------------------------
If you own MechWarrior 5: Mercenaries you can sit in its cockpits. No MechWarrior 5 file is part of
this package, and none may be passed on: you convert your own copy. You need UE Viewer (umodel),
Python 3 with numpy and Pillow, and about 6 GB of disk space. From the package's "cockpit_tools":

1. Put umodel into a folder "umodel" beside the scripts, then export the cockpits (about 2 GB):
       powershell -File export_cockpits.ps1 -Paks <...\MW5Mercs\Content\Paks>
2. Convert them (about 4 GB):
       python convert_all.py mw5export <the game's folder>\mw4vr_cockpits
3. In mw4vr.ini: mw5_on=1 and mw5_cockpit=<a cockpit's name> (used where nothing else is chosen).

Every mech has a cockpit to start with (mw4vr_cockpit_map.ini): its own where MechWarrior 5 has it,
otherwise one with a similar face. At the start of a battle the mod shows the mech's and the cockpit's
name.

Cockpits of MechWarrior 5 mods: the setup can export those too ("Also the cockpits of the MechWarrior 5
mods installed there"; by hand: export_cockpits.ps1 -Mods <...\MW5Mercs\Mods>). They are often plainer
than the game's own - painted in one colour a part, without pictures. MechWarrior 5 draws such painted
metal with patches, worn paint and gloss of its own; the mod does the like ([vr] mw5_paint=0..1 how
strongly, 0: plain colours; mw5_paint_wear=0..1 how much paint is worn off). Cockpits converted before
v11.42 lack the mark for it: delete their .mwc and run the setup again. Even so they stay coarse beside
the game's own, so mw4vr_cockpit_map.ini names none of them: choose one with Ctrl+Alt+F11, or write it
into that file. A line there may name several cockpits, the best first (kodiak=kodiak,atlas): the first
there is a file of is used.

  Ctrl+F11                     the cockpits on / off
  Ctrl+Alt+F11 (Shift: back)   the next cockpit; remembered for the mech you sit in
  Ctrl+Alt+arrows              your eye's place in the cockpit
  Ctrl+Alt+PgUp / PgDn         the cockpit's size
  Ctrl+Alt+1..5                looking at a monitor: it shows that part of the HUD
  Ctrl+Alt+0                   the monitor you look at goes dark
  Ctrl+Alt+9                   this cockpit's monitors off / on (off: all of the HUD floats)
  Ctrl+Alt+Shift+9             the chosen part leaves its monitor and floats
  A chosen part on a monitor:  Ctrl+Alt+Shift + arrows move what the monitor shows, PgUp / PgDn
                               nearer / more, Ins / Del longer / shorter (for long weapon lists),
                               Home / End turn the picture by 5 degrees left / right (a monitor
                               that hangs askew or is no rectangle; screen_<n>_turn in its .ini)

The clan cockpits have monitors; in the others the HUD's parts float in the cockpit's windows.


TREES (optional, if the package has a "trees" folder)
-----------------------------------------------------
Fourteen kinds of trees and shrubs for MW4Engine's terrain clutter, made from Poly Haven's plant
models (CC0). Copy the folder "Content" from "trees" over the game's and add the text of
clutter_trees.txt to the end of Content\Clutter\clutter.txt. Too many for your PC: set a kind's
"enabled = 0" or lower its density there.


TOO FEW PICTURES A SECOND
-------------------------
The game draws every picture twice, so a battle with many trees and enemies is heavy; how heavy
depends on your PC. What helps, strongest first:
- The streaming quality of your headset's software (Virtual Desktop: "VR Graphics Quality") sets the
  size of the pictures. Tested on Medium; go lower on a weaker PC.
- clutter_density in mw4hook.ini: 1.0 = all the grass and trees, 0.5 = half, 0 = none. Read again
  when the file is saved, so you can try it during a battle.
- Fewer enemies and smaller maps in the instant action's settings.
A cable or a good 5/6 GHz WiFi link to the PC: short freezes are mostly the network, not the game.


IF SOMETHING IS WRONG
---------------------
Nothing in the headset        Is the OpenXR runtime 32-bit capable and active? mw4vr.log tells what
                              failed. enabled=1 in mw4vr.ini?
The picture is one frame late or smeared      taa=0 in mw4hook.ini.
Things vanish at the picture's edge           fov=130 in mw4hook.ini.
The game dies at start-up     Is the mod's dinput.dll in the game's folder?
Windows asks for administrator rights         Start with mw4vr_start.cmd, not the exe.
The cockpit is too large or small, you sit wrong      Ctrl+Ins / Del and Ctrl+arrows, then Ctrl+F10.
A new build of MW4Engine      Parts of MW4VR switch themselves off until MW4VR is updated.

Known limits: no motion controllers; menus and films are a screen in front of you; with the arrow
keys' side view the mech's own hull can cut into a MechWarrior 5 cockpit.


SHARE YOUR SETTINGS, REPORT FAULTS
----------------------------------
Each mech wants its seat, sizes and HUD places set once - and there are more than a hundred mechs.
If you have set some up well, please share them, so the next version ships them for everyone:
  mw4vr_mechs.ini                  seat, sizes and the HUD's places for each mech (the game's own cockpits
                                   and MechWarrior 5 ones)
  mw4vr_cockpits\<cockpit>.ini     a MechWarrior 5 cockpit's eye place, size, monitors, parts on its glass
Only these .ini files - never the .mwc cockpit files: those are MechWarrior 5 content.

Send them, and report faults or ask for features, as an issue on GitHub:
  https://github.com/kashban/Mechwarrior-4-VR-Mod/issues
For a fault please attach mw4vr.log and say which mech, map and headset.


THANKS AND LICENCE
------------------
MW4Engine DX11 (mw4hook.dll) is the work of its author; MW4VR only builds on it. MechWarrior is a
trademark of Microsoft; the game and MekPak belong to their owners. Trees: Poly Haven (CC0).
MW4VR: Copyright (c) 2026 Kashban, PolyForm Noncommercial License 1.0.0 (see LICENSE) - free for any
noncommercial use. Source and new versions: https://github.com/kashban/Mechwarrior-4-VR-Mod



=========================================
MW4VR - MechWarrior 4: Mercenaries in VR
=========================================

MechWarrior 4: Mercenaries im VR-Headset: echtes Stereo, Kopfbewegung im Cockpit, HUD mit Tiefe.
Nur Einzelspieler.


VORAUSSETZUNGEN
---------------
- MechWarrior 4: Mercenaries mit MekTek MekPak 3.1c
- MW4Engine DX11 (mw4hook.dll), Build 20261107_ea16ef2. Bei einem anderen Build schalten sich Teile
  von MW4VR selbst ab (steht in mw4vr.log, Zeilen mit "abi:").
- VR-Headset mit 32-Bit-OpenXR-Laufzeit. Getestet nur mit Quest 3 über Virtual Desktop (VDXR).
- Schnelle Grafikkarte: jedes Bild wird zweimal gezeichnet.

Bezugsquellen:
  MW4Engine DX11         https://www.moddb.com/mods/mw4-mercenaries-dx11-renderer-overhaul   (v1.0.7)
  MechWarrior 5: Mercenaries, nur für dessen Cockpits (optional):
      Epic Games Store   https://store.epicgames.com/p/mechwarrior-5
      Steam              https://store.steampowered.com/app/784080


INSTALLATION
------------
Der einfache Weg: Paket irgendwo entpacken und MW4VR_Setup.cmd starten. Das Fenster fragt nach dem
Spielordner und erledigt die Schritte unten; es kann auch die Bäume installieren und deine
MechWarrior-5-Cockpits exportieren und umwandeln. Vorhandene Einstellungen bleiben erhalten.

Eine neuere Version: ihr Paket entpacken und MW4VR_Setup.cmd erneut starten - der Knopf heißt dann
"Update". Die Programmdateien werden ersetzt; deine Einstellungen bleiben und bekommen nur, was neu ist
(neue Schlüssel in mw4vr.ini, neue Mechs, Cockpit-Einstellungen, die du nicht geändert hast).

Von Hand:
1. In mw4hook.ini eintragen:
       antihack=0
       fov=130
       vsync=0
       taa=0
       mode=windowed
       resolution=1300x1300      (beliebige quadratische Größe; MW4VR rendert in eigener Größe,
                                  siehe render_height)
2. Dateien aus dem Ordner "game" des Pakets in den Spielordner kopieren.
3. Headset verbinden, dann das Spiel mit mw4vr_start.cmd starten.
4. Im Cockpit: so hinsetzen, wie du spielst, und Strg+Pos1 drücken.

antihack=0 heißt: mit diesem Mod nicht online spielen.

Entfernen: d3d11.dll und dinput.dll aus dem Spielordner löschen.


DIE DATEIEN
-----------
d3d11.dll              der Mod
mw4vr.ini              Einstellungen, Kommentar an jedem Schlüssel; die meisten werden im laufenden
                       Spiel neu gelesen
dinput.dll             behebt Abstürze und Hänger beim Spielstart auf PCs mit bestimmten USB-Geräten
                       (lässt "sonstige HID-Geräte" aus der Geräteliste des Spiels). Unnötig, wenn du
                       das Problem nie hattest.
mw4vr_start.cmd/.ps1   startet das Spiel ohne Administrator-Abfrage
mw4vr_cockpit_map.ini  welches MechWarrior-5-Cockpit jeder Mech bekommt (nur mit umgewandelten Cockpits)
mw4vr_mechs.ini        schreibt der Mod: was er sich je Mech merkt
mw4vr.log              schreibt der Mod: was er getan hat; bei Fehlermeldungen mitschicken


TASTEN (im Gefecht)       Strg = Ctrl, Pos1 = Home, Bild auf/ab = PgUp/PgDn, Einfg/Entf = Ins/Del
------------------
  Strg+F1                      alle Tasten als Seite im Headset (englisch); das Spiel wartet, bis du sie schließt
                               (nochmals Strg+F1 oder Esc; ihre Größe: help_size in mw4vr.ini)

Blick und Sitz
  Strg+Pos1                    Blick neu ausrichten
  Strg+Hoch / Runter           Sitz vor / zurück
  Strg+Rechts / Links          Sitz höher / tiefer
  Strg+Einfg / Entf            Größe des Cockpits (mit Umschalt: der Welt)
  Strg+F10                     dieses Cockpit passt: für diesen Mech merken
  Strg+Bild auf / ab / Ende    Fadenkreuz und Zielmarken weiter weg / näher / fern

Teile des HUD: 1 Radar, 2 Ziel, 3 Schaden, 4 Lanzenliste, 5 Waffen, 6 Kopfleiste
  Strg+Alt+1..6                Teil dorthin setzen, wo du hinsiehst
  Strg+Alt+Umschalt+1..6       Teil wählen, dann mit gehaltenem Strg+Alt+Umschalt:
      Pfeile                   verschieben
      Bild auf / ab            größer / kleiner
      Einfg / Entf             weiter weg / näher
      0                        ausblenden
  Strg+Alt+7                   alle Teile zurück an ihre automatischen Plätze
  Der Teil, den die Tasten gerade ändern, hat einige Sekunden einen gelben Rahmen.

Sonstiges
  Strg+F12                     Bild beider Augen nach mw4vr_shots\

Sitz, Größen und Plätze der Teile werden gemerkt - Sitz und Größen je Mech, der Sitz getrennt für das Cockpit
des Spiels und für ein MechWarrior-5-Cockpit. Von außen gesehen (Außenansicht) haben die HUD-Teile eigene
Plätze, für alle Mechs dieselben; was du dort verschiebst, lässt die Cockpits unberührt.
Esc im Gefecht: das Menü des Spiels ist ein flacher Schirm geradeaus (Größe: menu_scale in mw4vr.ini).


MECHWARRIOR-5-COCKPITS (optional)
---------------------------------
Wer MechWarrior 5: Mercenaries besitzt, kann in dessen Cockpits sitzen. Das Paket enthält keine
MechWarrior-5-Datei, und keine darf weitergegeben werden: du wandelst deine eigene Kopie um. Nötig:
UE Viewer (umodel), Python 3 mit numpy und Pillow, etwa 6 GB Plattenplatz. Aus "cockpit_tools":

1. umodel in einen Ordner "umodel" neben die Skripte legen, dann Cockpits exportieren (ca. 2 GB):
       powershell -File export_cockpits.ps1 -Paks <...\MW5Mercs\Content\Paks>
2. Umwandeln (ca. 4 GB):
       python convert_all.py mw5export <Spielordner>\mw4vr_cockpits
3. In mw4vr.ini: mw5_on=1 und mw5_cockpit=<Name eines Cockpits> (gilt, wo nichts anderes gewählt ist).

Jeder Mech hat ein Cockpit vorab (mw4vr_cockpit_map.ini): sein eigenes, wo MechWarrior 5 es hat,
sonst eines mit ähnlichem Gesicht. Bei Gefechtsbeginn zeigt der Mod Mech- und Cockpit-Namen.

Cockpits aus MechWarrior-5-Mods: Das Setup kann auch die exportieren ("Also the cockpits of the
MechWarrior 5 mods installed there"; von Hand: export_cockpits.ps1 -Mods <...\MW5Mercs\Mods>). Sie sind
oft schlichter als die des Spiels - je Teil einfarbig lackiert, ohne Bilder. MechWarrior 5 zeichnet
solches lackiertes Metall mit eigenen Flecken, abgenutztem Lack und Glanz; der Mod macht es ähnlich
([vr] mw5_paint=0..1 wie stark, 0: reine Farben; mw5_paint_wear=0..1 wie viel Lack abgenutzt ist).
Vor v11.42 umgewandelten Cockpits fehlt die Markierung dafür: ihre .mwc löschen und das Setup noch
einmal starten. Auch so bleiben sie grob neben denen des Spiels, darum nennt mw4vr_cockpit_map.ini
keines davon: mit Strg+Alt+F11 wählen oder in die Datei eintragen. Eine Zeile dort darf mehrere
Cockpits nennen, das beste zuerst (kodiak=kodiak,atlas): benutzt wird das erste, für das es eine
Datei gibt.

  Strg+F11                     Cockpits an / aus
  Strg+Alt+F11 (Umschalt: zurück)   nächstes Cockpit; für den Mech gemerkt, in dem du sitzt
  Strg+Alt+Pfeile              Platz deines Auges im Cockpit
  Strg+Alt+Bild auf / ab       Größe des Cockpits
  Strg+Alt+1..5                mit Blick auf einen Monitor: er zeigt diesen Teil des HUD
  Strg+Alt+0                   der angesehene Monitor wird dunkel
  Strg+Alt+9                   Monitore dieses Cockpits aus / an (aus: das ganze HUD schwebt)
  Strg+Alt+Umschalt+9          der gewählte Teil verlässt seinen Monitor und schwebt
  Gewählter Teil auf Monitor:  Strg+Alt+Umschalt + Pfeile verschieben den gezeigten Ausschnitt,
                               Bild auf / ab näher / mehr, Einfg / Entf länger / kürzer (für lange
                               Waffenlisten), Pos1 / Ende drehen das Bild um 5 Grad nach links /
                               rechts (schief hängender oder nicht rechteckiger Monitor;
                               screen_<n>_turn in seiner .ini)

Die Clan-Cockpits haben Monitore; in den anderen schweben die Teile des HUD in den Cockpitfenstern.


BÄUME (optional, falls das Paket einen Ordner "trees" hat)
----------------------------------------------------------
Vierzehn Arten Bäume und Sträucher für den Geländebewuchs von MW4Engine, gemacht aus Pflanzenmodellen
von Poly Haven (CC0). Ordner "Content" aus "trees" über den des Spiels kopieren und den Text von
clutter_trees.txt ans Ende von Content\Clutter\clutter.txt anfügen. Zu viel für deinen PC: dort bei
einer Art "enabled = 0" setzen oder ihre Dichte senken.


ZU WENIG BILDER PRO SEKUNDE
---------------------------
Das Spiel zeichnet jedes Bild zweimal, ein Gefecht mit vielen Bäumen und Gegnern ist also schwer;
wie schwer, hängt von deinem PC ab. Was hilft, das Stärkste zuerst:
- Die Streaming-Qualität der Headset-Software (Virtual Desktop: "VR Graphics Quality") bestimmt die
  Bildgröße. Getestet mit Medium; auf schwächerem PC niedriger.
- clutter_density in mw4hook.ini: 1.0 = alles Gras und alle Bäume, 0.5 = die Hälfte, 0 = nichts.
  Wird beim Speichern der Datei neu gelesen, lässt sich also im Gefecht ausprobieren.
- Weniger Gegner und kleinere Karten in den Einstellungen der Instant Action.
Kabel oder gutes 5/6-GHz-WLAN zum PC: kurze Hänger kommen meist vom Netz, nicht vom Spiel.


WENN ETWAS NICHT STIMMT
-----------------------
Nichts im Headset             OpenXR-Laufzeit 32-Bit-fähig und aktiv? mw4vr.log nennt den Fehler.
                              enabled=1 in mw4vr.ini?
Bild hängt ein Bild nach oder schmiert        taa=0 in mw4hook.ini.
Dinge verschwinden am Bildrand                fov=130 in mw4hook.ini.
Spiel stürzt beim Start ab    Liegt die dinput.dll des Mods im Spielordner?
Windows fragt nach Administratorrechten       Mit mw4vr_start.cmd starten, nicht mit der exe.
Cockpit zu groß oder klein, Sitz falsch       Strg+Einfg / Entf und Strg+Pfeile, dann Strg+F10.
Neuer Build von MW4Engine     Teile von MW4VR schalten sich ab, bis MW4VR angepasst ist.

Bekannte Grenzen: keine Bewegungs-Controller; Menüs und Filme sind eine Leinwand vor dir; beim
Seitenblick per Pfeiltasten kann der eigene Mech in ein MechWarrior-5-Cockpit ragen.


EINSTELLUNGEN TEILEN, FEHLER MELDEN
-----------------------------------
Jeder Mech will Sitz, Größen und HUD-Plätze einmal eingestellt haben - und es gibt über hundert Mechs.
Wenn du welche gut eingestellt hast, teile sie bitte, damit die nächste Version sie für alle mitbringt:
  mw4vr_mechs.ini                  Sitz, Größen und HUD-Plätze je Mech (Spiel-Cockpits und
                                   MechWarrior-5-Cockpits)
  mw4vr_cockpits\<Cockpit>.ini     Augenplatz, Größe, Monitore und Glas-Anzeigen eines
                                   MechWarrior-5-Cockpits
Nur diese .ini-Dateien - niemals die .mwc-Cockpitdateien: die sind MechWarrior-5-Inhalt.

Schicken, Fehler melden oder Funktionen wünschen: als Issue auf GitHub:
  https://github.com/kashban/Mechwarrior-4-VR-Mod/issues
Bei Fehlern bitte mw4vr.log anhängen und Mech, Karte und Headset nennen.


DANK UND LIZENZ
---------------
MW4Engine DX11 (mw4hook.dll) ist das Werk seines Autors; MW4VR baut nur darauf auf. MechWarrior ist
eine Marke von Microsoft; Spiel und MekPak gehören ihren Eigentümern. Bäume: Poly Haven (CC0).
MW4VR: Copyright (c) 2026 Kashban, PolyForm Noncommercial License 1.0.0 (siehe LICENSE) - frei für
jede nichtkommerzielle Nutzung. Quelltext und neue Versionen: https://github.com/kashban/Mechwarrior-4-VR-Mod
