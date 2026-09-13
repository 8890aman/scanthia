; Scanthia installer — NSIS (build via: makensis installer.nsi)
!include "MUI2.nsh"

Name "Scanthia"
OutFile "Scanthia-Setup.exe"
InstallDir "$PROGRAMFILES64\Scanthia"
RequestExecutionLevel admin
ShowInstDetails show

!define MUI_ABORTWARNING
!define MUI_ICON   "src\app\icons\Scanthia.ico"
!define MUI_UNICON "src\app\icons\Scanthia.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "resources\installer-header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "resources\installer-wizard.bmp"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "DISCLAIMER.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "dist\Scanthia\*"

    CreateShortcut "$SMPROGRAMS\Scanthia.lnk" "$INSTDIR\Scanthia.exe"
    CreateShortcut "$DESKTOP\Scanthia.lnk"  "$INSTDIR\Scanthia.exe"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; scanthia:// URL protocol — lets RIS/HIS launch the viewer with a
    ; study: scanthia://study/<uid> or scanthia://open?path=<dir>
    WriteRegStr HKCR "scanthia" "" "URL:Scanthia Protocol"
    WriteRegStr HKCR "scanthia" "URL Protocol" "$\"$\""
    WriteRegStr HKCR "scanthia\DefaultIcon" "" "$INSTDIR\Scanthia.exe,0"
    WriteRegStr HKCR "scanthia\shell\open\command" "" \
        "$\"$INSTDIR\Scanthia.exe$\" $\"%1$\""

    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scanthia" \
        "DisplayName" "Scanthia"
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scanthia" \
        "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scanthia" \
        "DisplayIcon" "$INSTDIR\Scanthia.exe"
    WriteRegDWORD HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scanthia" \
        "NoModify" 1
    WriteRegDWORD HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scanthia" \
        "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Scanthia.lnk"
    Delete "$DESKTOP\Scanthia.lnk"
    RMDir /r "$INSTDIR"
    DeleteRegKey HKCR "scanthia"
    DeleteRegKey HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Scanthia"
    ; NOTE: user data under %APPDATA%\Scanthia (study database,
    ; annotations, model library) is preserved on uninstall.
SectionEnd
