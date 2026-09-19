[Icons]
Name: "{group}\INOP"; Filename: "{app}\inop.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\INOP"; Filename: "{app}\inop.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\inop.exe"; WorkingDir: "{app}"; Description: "Launch INOP"; Flags: nowait postinstall skipifsilent
