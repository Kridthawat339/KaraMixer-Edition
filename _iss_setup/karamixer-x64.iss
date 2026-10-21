; KaraMixer Edition - Inno Setup script
; Build with (from repository root, after the app is built and deployed):
;   iscc /DAppVersion=4.0.0 _iss_setup\karamixer-x64.iss
; Expected layout:  deploy\x64\  (KaraMixer.exe + Qt/BASS dlls, see .github\workflows\build-windows.yml)

#ifndef AppVersion
  #define AppVersion "4.0.0"
#endif
#define MyAppName "KaraMixer Edition"
#define MyAppPublisher "Buai Music"
#define MyAppExeName "KaraMixer.exe"
#define DeployDir "..\deploy\x64"

[Setup]
AppId={{B6F1C2D4-7A35-4E98-9C1B-3D5E8A0F6721}
AppName={#MyAppName}
AppVersion=v.{#AppVersion} Pro
AppVerName={#MyAppName} v.{#AppVersion} Pro
VersionInfoVersion={#AppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\KaraMixer Edition
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=..\gpl-3.0.rtf
OutputDir=..\dist
OutputBaseFilename=KaraMixer-v{#AppVersion}-Pro-x64-setup
SetupIconFile=..\karamixer.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma
SolidCompression=yes
UsePreviousAppDir=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated; Check: VC2017RedistNeedsInstall
; YouTube (PRO) needs a virtual audio cable. They are third-party products with their own licences,
; so they are NOT bundled: the finish page offers to open the official download pages instead.
Filename: "https://vb-audio.com/Cable/"; Flags: shellexec nowait postinstall skipifsilent unchecked; Description: "Open the VB-Cable (virtual audio cable) download page - needed for the YouTube function (PRO)"
Filename: "{app}\{#MyAppExeName}"; Flags: nowait postinstall skipifsilent; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"

[Dirs]
Name: "{app}\SoundFonts"; Flags: uninsneveruninstall
Name: "{app}\VST"; Flags: uninsneveruninstall

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\NOTICE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\gpl-3.0.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\..\VC_redist.x64.exe"; DestDir: "{tmp}"; Flags: ignoreversion deleteafterinstall; Check: VC2017RedistNeedsInstall

[Code]
function VC2017RedistNeedsInstall: Boolean;
var
  Version: String;
begin
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', Version) then
  begin
    Log('VC Redist Version check : found ' + Version);
    Result := True;   // always run the (small, quiet) runtime installer: the program needs a recent runtime (14.40+)
  end
  else
    Result := True;
end;
