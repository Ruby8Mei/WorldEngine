# INOP audio assets

INOP checks this folder at startup and uses the first supported file found for each cue.

- `button-click.wav`, `button-click.flac`, or `button-click.mp3`
- `background-music.wav`, `background-music.flac`, or `background-music.mp3`
- `processing.wav`, `processing.flac`, or `processing.mp3`

WAV, FLAC, and MP3 playback is provided by miniaudio. Files are optional. A missing or unreadable file leaves its cue silent and does not stop INOP.

Background music and processing cues loop. The button cue is a reusable one shot. No audio content ships with INOP.
