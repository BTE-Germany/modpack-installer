; Wraps the deployed Qt application into a single portable executable.
;
; Qt cannot be linked statically from the official binary packages, so a real
; one-file build is not available. Instead the payload is compressed into this
; stub, unpacked into a temporary directory on launch, started, and removed
; again as soon as the application exits. Nothing is installed and no registry
; keys are written.
;
; Expected defines:
;   PAYLOAD_DIR   directory holding the windeployqt output
;   OUT_FILE      path of the executable to produce
;   VERSION       human readable version, e.g. 1.4.2 or 1.4.2-rc.1
;   VERSION_QUAD  four part version for the VERSIONINFO resource, e.g. 1.4.2.0
;   ICON_FILE     optional path to the .ico used for the stub

Unicode true
Name "BTE Germany Modpack Installer"
OutFile "${OUT_FILE}"

; No wizard: the stub only unpacks and launches.
SilentInstall silent
; The application writes to %APPDATA%, so it must not ask for elevation.
RequestExecutionLevel user
SetCompressor /SOLID lzma

!ifdef ICON_FILE
  Icon "${ICON_FILE}"
!endif

VIProductVersion "${VERSION_QUAD}"
VIAddVersionKey "ProductName" "BTE Germany Modpack Installer"
VIAddVersionKey "FileDescription" "BTE Germany Modpack Installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "BuildTheEarth Germany e.V."
VIAddVersionKey "LegalCopyright" "BuildTheEarth Germany e.V."

Section
    ; $PLUGINSDIR is created in %TEMP% and deleted automatically on exit.
    InitPluginsDir
    SetOutPath "$PLUGINSDIR\app"
    File /r "${PAYLOAD_DIR}\*"

    ClearErrors
    ExecWait '"$PLUGINSDIR\app\bteginstaller.exe"'
    IfErrors 0 +2
        MessageBox MB_ICONSTOP|MB_OK "Der Installer konnte nicht gestartet werden."

    ; SetOutPath made the payload directory this process' working directory, and
    ; Windows refuses to remove a directory that is in use - leaving empty
    ; folders behind in %TEMP% on every run. Step out before the cleanup runs.
    SetOutPath "$TEMP"
SectionEnd
