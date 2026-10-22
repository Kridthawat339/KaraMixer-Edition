; KaraMixer Edition - Inno Setup script
; Build with (from repository root, after the app is built and deployed):
;   iscc /DAppVersion=4.0.0 _iss_setup\karamixer-x86.iss
; Expected layout:  deploy\x86\  (KaraMixer.exe + Qt/BASS dlls, see .github\workflows\build-windows.yml)

#ifndef AppVersion
  #define AppVersion "4.0.0"
#endif
#define MyAppName "KaraMixer Edition"
#define MyAppPublisher "Buai Music"
#define MyAppExeName "KaraMixer.exe"
#define DeployDir "..\deploy\x86"

[Setup]
AppId={{E92A4B7D-1C60-47F3-A8D5-6B0C3F9E5A18}
AppName={#MyAppName}
AppVersion=v.{#AppVersion} Pro
AppVerName={#MyAppName} v.{#AppVersion} Pro
VersionInfoVersion={#AppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\KaraMixer Edition (32-bit)
DefaultGroupName={#MyAppName} (32-bit)
AllowNoIcons=yes
LicenseFile=..\gpl-3.0.rtf
OutputDir=..\dist
OutputBaseFilename=KaraMixer-v{#AppVersion}-Pro-x86-setup
SetupIconFile=..\karamixer.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma
SolidCompression=yes
UsePreviousAppDir=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\VC_redist.x86.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated; Check: VC2017RedistNeedsInstall
Filename: "{app}\{#MyAppExeName}"; Flags: nowait postinstall skipifsilent; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"

[Dirs]
Name: "{app}\SoundFonts"; Flags: uninsneveruninstall
Name: "{app}\VST"; Flags: uninsneveruninstall

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\NOTICE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\gpl-3.0.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\..\VC_redist.x86.exe"; DestDir: "{tmp}"; Flags: ignoreversion deleteafterinstall; Check: VC2017RedistNeedsInstall

[Code]
function VC2017RedistNeedsInstall: Boolean;
var
  Version: String;
begin
  if RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x86', 'Version', Version) then
  begin
    Log('VC Redist Version check : found ' + Version);
    Result := True;   // always run the (small, quiet) runtime installer: the program needs a recent runtime (14.40+)
  end
  else
    Result := True;
end;
