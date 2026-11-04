# MIDI Monitor

MIDI Monitor est un utilitaire Windows natif de diagnostic MIDI 1.0. Il affiche sans les
modifier les messages reçus depuis une entrée choisie explicitement par l'utilisateur :
horodatage, octets hexadécimaux et interprétation lisible. Il ne route, ne fusionne et ne
retransmet aucun message.

L'application est écrite en C++17 avec Win32, WinMM, GDI et les contrôles communs Windows.
Elle n'emploie aucune bibliothèque tierce et l'exécutable Release utilise le runtime C++
statique.

Le projet est distribué sous [licence MIT](LICENSE).

## Prérequis et compilation

- Windows 10 ou Windows 11 x64 ;
- CMake 3.20 ou plus récent ;
- Visual Studio Build Tools avec le composant **Développement Desktop en C++** et un SDK
  Windows récent.

Dans un terminal « Developer PowerShell for VS » à la racine du projet :

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

La compilation copie automatiquement le programme dans
`dist/windows/MIDI Monitor.exe`. L'application est de sous-système Windows : aucune console
n'est ouverte et aucun privilège administrateur n'est normalement nécessaire.

L'icône Windows multi-résolution est générée depuis `assets/MIDIMonitor.png` avec :

```powershell
.\tools\GenerateIcon.ps1
```

## Utilisation

1. Lancez MIDI Monitor.
2. Choisissez explicitement une entrée dans **Entrée MIDI**. Aucun périphérique n'est choisi
   arbitrairement au démarrage.
3. Les messages apparaissent dans l'ordre de réception. Le journal suit la dernière ligne tant
   que la vue est en bas ; remonter dans la liste suspend le suivi jusqu'au retour en bas.
4. Cliquez sur un en-tête pour trier la vue par horodatage, octets HEX, type, canal, paramètre ou
   valeur. Un second clic inverse l'ordre. Ce tri ne modifie jamais l'historique ni le CSV.
5. La liste **Filtre** permet d'isoler les notes, Control Change, Pitch Bend, SysEx, aftertouch,
   Program Change, messages système temps réel, messages système communs ou erreurs.
6. `Ctrl+C` copie les lignes sélectionnées sous forme tabulée, avec une colonne par champ.
7. **Réinitialiser** vide l'historique, les compteurs, la file en attente et remet l'origine
   temporelle à zéro, sans déconnecter le port.
8. **Sauvegarder CSV** exporte tout l'historique encore conservé, pas seulement les lignes
   visibles.

**Actualiser** ferme proprement l'entrée active, réénumère les périphériques et demande une
nouvelle sélection. Cela évite de réutiliser un identifiant WinMM devenu périmé après un
branchement ou débranchement.

## Test avec loopMIDI et Max/MSP

1. Démarrez loopMIDI et créez un port virtuel.
2. Lancez MIDI Monitor et sélectionnez ce port dans **Entrée MIDI**.
3. Dans Max/MSP, dirigez `noteout`, `ctlout` ou `midiout` vers le même port.
4. Envoyez des Note On/Off sur plusieurs canaux, puis des rafales de notes.
5. Comparez dans le tableau le canal (1 à 16), le numéro/nom de note, la vélocité, les octets
   HEX et le nombre de messages.
6. Exportez le CSV et vérifiez que chaque ligne du journal encore conservée y apparaît.

loopMIDI et Max/MSP ne sont pas nécessaires aux tests automatisés et ne sont pas distribués
avec ce projet.

## Messages pris en charge

- Note Off, Note On, Polyphonic Aftertouch, Control Change, Program Change, Channel Pressure
  et Pitch Bend 14 bits ;
- System Exclusive via quatre buffers WinMM réutilisables de 4096 octets ;
- MIDI Time Code Quarter Frame, Song Position Pointer, Song Select et Tune Request ;
- Timing Clock, Start, Continue, Stop, Active Sensing et System Reset ;
- les statuts réservés ou les messages incomplets sont indiqués sans inventer d'octets.

La convention d'affichage est **note MIDI 60 = C4**. Un Note On de vélocité zéro est présenté
comme Note Off, mais le statut original `0x9n` reste conservé dans les octets HEX et le CSV.
Le Pitch Bend est affiché de 0 à 16383, centre 8192.

## Horodatage et ordre

Le temps de réception est capturé avec `QueryPerformanceCounter` dès l'entrée du callback
WinMM, avant la mise en file. L'interface affiche `mm:ss.mmm` ; le CSV conserve la mesure en
millisecondes avec trois décimales. Cette mesure représente l'arrivée dans l'application, et
non l'instant électrique exact d'émission sur le câble MIDI.

`driver_timestamp_ms` conserve séparément le timestamp en millisecondes fourni par WinMM. Son
origine et sa précision appartiennent au pilote. Il peut différer du compteur haute résolution
de l'application. L'historique et le CSV conservent toujours l'ordre des callbacks reçus. Le tri
des colonnes ne concerne que la vue affichée.

## Limite essentielle : running status

WinMM fournit généralement les messages courts déjà normalisés par Windows, avec un statut
présent dans chaque message. MIDI Monitor affiche donc exactement ces messages et indique :

> Running status : non observable via WinMM (messages normalisés par Windows).

Deux messages WinMM successifs ayant le même statut ne sont jamais présentés comme preuve d'un
running status dans le flux physique d'origine. Le CSV porte la valeur `not_observable`.

Le module indépendant `MidiByteStreamParser` sait en revanche analyser une véritable suite
d'octets : état conservé entre blocs, running status réellement omis, messages incomplets,
System Common, Real-Time intercalé et frontières SysEx. Il conserve séparément les octets reçus
et le statut reconstruit. Ce module prépare l'ajout futur d'une source d'octets bruts ; il n'est
pas utilisé pour inventer une information absente de WinMM.

## Historique, charge et erreurs

L'historique conserve 100 000 messages. Une fois la limite atteinte, un avertissement est
affiché, les plus anciens messages sont remplacés et leur nombre reste visible. La file entre le
callback et l'interface contient au maximum 65 536 événements ; une saturation est explicitement
signalée avec le nombre de messages perdus. Les mises à jour sont traitées par lots sur le thread
graphique, au maximum toutes les 20 ms. Le corps du tableau ne dessine que les lignes visibles :
les messages hors écran restent dans l'historique sans devenir autant de contrôles Windows. Le
défilement conserve ainsi un coût constant, y compris lorsque le journal contient plusieurs
milliers de messages ou qu'un outil d'accessibilité Windows inspecte la fenêtre.

Les erreurs d'ouverture WinMM, de buffer SysEx, de réception et d'écriture CSV sont présentées à
l'utilisateur. Fermer l'application arrête et réinitialise l'entrée, libère les en-têtes SysEx,
puis ferme le périphérique.

## CSV

Le fichier utilise UTF-8 avec BOM, des fins de ligne Windows et le point-virgule, pour une
ouverture directe dans Excel en configuration française. Une ligne correspond à un message.
Les champs sont :

`index; elapsed_ms; driver_timestamp_ms; raw_hex; status_hex; data1; data2; message_type;
channel; note_number; note_name; velocity; value; description; running_status; source; port_name`

Les champs non applicables sont vides et les textes sont échappés selon les règles CSV. Le nom
proposé suit `MIDI_Monitor_YYYY-MM-DD_HHMMSS.csv` selon l'heure locale Windows.

## Tests

`MIDIMonitorTests` couvre notamment Note On/Off, vélocité zéro, Control/Program Change, Pitch
Bend, Clock, SysEx, messages incomplets, Real-Time intercalé, running status réel et explicitement
absent, annulation du running status et encodage/échappement CSV.

`WinMMIntegrationTests` recherche exclusivement un port dont le nom contient `loopMIDI`, ouvre
réellement son entrée et sa sortie avec WinMM, envoie `90 3C 64` puis `80 3C 00` et vérifie leur
réception dans cet ordre par le module de capture. Le test est marqué ignoré (et non échoué) si
loopMIDI n'est pas disponible.

`MainWindowStressTests` injecte 10 000 messages dans la file de l'interface, fait énumérer ses
contrôles par Windows UI Automation puis remonte dans le journal. Il protège ainsi contre les
blocages de la liste lors du défilement, sans dépendre d'un port MIDI externe.

Les essais matériels avec Max/MSP, loopMIDI ou une interface USB restent manuels, car leur
résultat dépend des logiciels et périphériques présents sur le poste.
