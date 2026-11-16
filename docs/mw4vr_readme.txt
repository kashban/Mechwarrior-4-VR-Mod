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


INSTALL
-------
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
mw4vr_start.cmd/.ps1   start the game without the administrator prompt, and again if it does not come up
mw4vr_cockpit_map.ini  which MechWarrior 5 cockpit each mech gets (only with converted cockpits)
mw4vr_mechs.ini        written by the mod: what it remembers for each mech
mw4vr.log              written by the mod: what it did; send it along when you report a fault


KEYS (in a battle)
------------------
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

Other
  Ctrl+F12                     a picture of both eyes into mw4vr_shots\

The seat, the sizes and the parts' places are remembered - the seat and sizes for each mech.


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

  Ctrl+F11                     the cockpits on / off
  Ctrl+Alt+F11 (Shift: back)   the next cockpit; remembered for the mech you sit in
  Ctrl+Alt+arrows              your eye's place in the cockpit
  Ctrl+Alt+PgUp / PgDn         the cockpit's size
  Ctrl+Alt+1..5                looking at a monitor: it shows that part of the HUD
  Ctrl+Alt+0                   the monitor you look at goes dark
  Ctrl+Alt+9                   this cockpit's monitors off / on (off: all of the HUD floats)
  Ctrl+Alt+Shift+9             the chosen part leaves its monitor and floats
  A chosen part on a monitor:  Ctrl+Alt+Shift + arrows move what the monitor shows, PgUp / PgDn
                               nearer / more, Ins / Del longer / shorter (for long weapon lists)

The clan cockpits have monitors; in the others the HUD's parts float in the cockpit's windows.


TREES (optional, if the package has a "trees" folder)
-----------------------------------------------------
Fourteen kinds of trees and shrubs for MW4Engine's terrain clutter, made from Poly Haven's plant
models (CC0). Copy the folder "Content" from "trees" over the game's and add the text of
clutter_trees.txt to the end of Content\Clutter\clutter.txt. Too many for your PC: set a kind's
"enabled = 0" or lower its density there.


IF SOMETHING IS WRONG
---------------------
Nothing in the headset        Is the OpenXR runtime 32-bit capable and active? mw4vr.log tells what
                              failed. enabled=1 in mw4vr.ini?
The picture is one frame late or smeared      taa=0 in mw4hook.ini.
Things vanish at the picture's edge           fov=130 in mw4hook.ini.
The game dies at start-up     Use dinput.dll and mw4vr_start.cmd.
Windows asks for administrator rights         Start with mw4vr_start.cmd, not the exe.
The cockpit is too large or small, you sit wrong      Ctrl+Ins / Del and Ctrl+arrows, then Ctrl+F10.
A new build of MW4Engine      Parts of MW4VR switch themselves off until MW4VR is updated.

Known limits: no motion controllers; menus and films are a screen in front of you; with the arrow
keys' side view the mech's own hull can cut into a MechWarrior 5 cockpit.


THANKS AND LICENCE
------------------
MW4Engine DX11 (mw4hook.dll) is the work of its author; MW4VR only builds on it. MechWarrior is a
trademark of Microsoft; the game and MekPak belong to their owners. Trees: Poly Haven (CC0).
MW4VR: Copyright (c) 2026 Kashban, PolyForm Noncommercial License 1.0.0 (see LICENSE) - free for any
noncommercial use.



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


INSTALLATION
------------
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
mw4vr_start.cmd/.ps1   startet das Spiel ohne Administrator-Abfrage und erneut, falls es nicht hochkommt
mw4vr_cockpit_map.ini  welches MechWarrior-5-Cockpit jeder Mech bekommt (nur mit umgewandelten Cockpits)
mw4vr_mechs.ini        schreibt der Mod: was er sich je Mech merkt
mw4vr.log              schreibt der Mod: was er getan hat; bei Fehlermeldungen mitschicken


TASTEN (im Gefecht)       Strg = Ctrl, Pos1 = Home, Bild auf/ab = PgUp/PgDn, Einfg/Entf = Ins/Del
------------------
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

Sonstiges
  Strg+F12                     Bild beider Augen nach mw4vr_shots\

Sitz, Größen und Plätze der Teile werden gemerkt - Sitz und Größen je Mech.


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
                               Waffenlisten)

Die Clan-Cockpits haben Monitore; in den anderen schweben die Teile des HUD in den Cockpitfenstern.


BÄUME (optional, falls das Paket einen Ordner "trees" hat)
----------------------------------------------------------
Vierzehn Arten Bäume und Sträucher für den Geländebewuchs von MW4Engine, gemacht aus Pflanzenmodellen
von Poly Haven (CC0). Ordner "Content" aus "trees" über den des Spiels kopieren und den Text von
clutter_trees.txt ans Ende von Content\Clutter\clutter.txt anfügen. Zu viel für deinen PC: dort bei
einer Art "enabled = 0" setzen oder ihre Dichte senken.


WENN ETWAS NICHT STIMMT
-----------------------
Nichts im Headset             OpenXR-Laufzeit 32-Bit-fähig und aktiv? mw4vr.log nennt den Fehler.
                              enabled=1 in mw4vr.ini?
Bild hängt ein Bild nach oder schmiert        taa=0 in mw4hook.ini.
Dinge verschwinden am Bildrand                fov=130 in mw4hook.ini.
Spiel stürzt beim Start ab    dinput.dll und mw4vr_start.cmd benutzen.
Windows fragt nach Administratorrechten       Mit mw4vr_start.cmd starten, nicht mit der exe.
Cockpit zu groß oder klein, Sitz falsch       Strg+Einfg / Entf und Strg+Pfeile, dann Strg+F10.
Neuer Build von MW4Engine     Teile von MW4VR schalten sich ab, bis MW4VR angepasst ist.

Bekannte Grenzen: keine Bewegungs-Controller; Menüs und Filme sind eine Leinwand vor dir; beim
Seitenblick per Pfeiltasten kann der eigene Mech in ein MechWarrior-5-Cockpit ragen.


DANK UND LIZENZ
---------------
MW4Engine DX11 (mw4hook.dll) ist das Werk seines Autors; MW4VR baut nur darauf auf. MechWarrior ist
eine Marke von Microsoft; Spiel und MekPak gehören ihren Eigentümern. Bäume: Poly Haven (CC0).
MW4VR: Copyright (c) 2026 Kashban, PolyForm Noncommercial License 1.0.0 (siehe LICENSE) - frei für
jede nichtkommerzielle Nutzung.
