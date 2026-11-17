; The Windows installer, built from the same folder as the zip (see make-installer.sh):
;   makensis /DVERSION=0.01 /DFILEVERSION=0.1.0.0 /DSOURCE=<the program's folder>
;            /DUNINSTALL_LIST=<uninstall-files.nsh> /DOUTFILE=<setup.exe> trackrideviz.nsi
;
; For the user alone (no administrator, %LOCALAPPDATA%\Programs) or for everyone (Program
; Files); a Start menu entry, a desktop one if chosen, and projects, gauges and templates opened
; with the program by a double click. The uninstaller takes away the files that were put there
; and nothing else, so a folder chosen by mistake is never emptied.

Unicode true
; a 64-bit installer for the 64-bit program where NSIS has it (MSYS2's has nothing else);
; a 32-bit one otherwise (Linux distributions' NSIS)
!if /FileExists "${NSISDIR}/Stubs/lzma_solid-amd64-unicode"
  Target amd64-unicode
!endif
SetCompressor /SOLID lzma

!define APPNAME "Race Track Visualizer"
!define EXENAME "trackrideviz.exe"
!define REGNAME "trackrideviz"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${REGNAME}"
!define PUBLISHER "coozoo"
!define WEBSITE "https://github.com/coozoo/trackrideviz"

!ifndef VERSION
  !define VERSION "0.0"
!endif
!ifndef FILEVERSION
  !define FILEVERSION "0.0.0.0"
!endif
!ifndef OUTFILE
  !define OUTFILE "trackrideviz-${VERSION}-setup.exe"
!endif

Name "${APPNAME}"
OutFile "${OUTFILE}"
BrandingText "${APPNAME} ${VERSION}"

!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_USE_PROGRAMFILES64
!define MULTIUSER_INSTALLMODE_INSTDIR "${APPNAME}"
; an update is offered for the same users as the version before
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "${UNINSTALL_KEY}"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME "InstallMode"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "${UNINSTALL_KEY}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "InstallLocation"
!include MultiUser.nsh
!include MUI2.nsh
!include x64.nsh

!define MUI_ICON "../trackrideviz.ico"
!define MUI_UNICON "../trackrideviz.ico"
!define MUI_ABORTWARNING

; free software: shown, nothing to agree to
!define MUI_LICENSEPAGE_BUTTON $(^NextBtn)
!define MUI_LICENSEPAGE_TEXT_BOTTOM "$(LicenseNote)"
!insertmacro MUI_PAGE_LICENSE "../LICENSE"
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Ukrainian"

LangString LicenseNote ${LANG_ENGLISH} "Free software under the GNU GPL version 3: use it, share it, change it."
LangString LicenseNote ${LANG_UKRAINIAN} "Вільна програма за GNU GPL версії 3: користуйтеся, поширюйте, змінюйте."
LangString SectionProgram ${LANG_ENGLISH} "${APPNAME}"
LangString SectionProgram ${LANG_UKRAINIAN} "${APPNAME}"
LangString SectionDesktop ${LANG_ENGLISH} "Desktop shortcut"
LangString SectionDesktop ${LANG_UKRAINIAN} "Ярлик на робочому столі"
LangString SectionFiles ${LANG_ENGLISH} "Open projects, gauges and templates with it"
LangString SectionFiles ${LANG_UKRAINIAN} "Відкривати нею проєкти, індикатори й шаблони"
LangString DescProgram ${LANG_ENGLISH} "The program, ffmpeg for the export, the gauges and templates that come with it."
LangString DescProgram ${LANG_UKRAINIAN} "Програма, ffmpeg для експорту, індикатори й шаблони, що постачаються з нею."
LangString DescDesktop ${LANG_ENGLISH} "A shortcut on the desktop as well as in the Start menu."
LangString DescDesktop ${LANG_UKRAINIAN} "Ярлик на робочому столі на додачу до меню «Пуск»."
LangString DescFiles ${LANG_ENGLISH} "A double click on a .rtvproj project opens it; on a .rtvgauge or .rtvtemplate file brings it into the gauge library."
LangString DescFiles ${LANG_UKRAINIAN} "Подвійне клацання по проєкту .rtvproj відкриває його; по файлу .rtvgauge чи .rtvtemplate — додає його до бібліотеки індикаторів."
LangString ProjectType ${LANG_ENGLISH} "${APPNAME} project"
LangString ProjectType ${LANG_UKRAINIAN} "Проєкт ${APPNAME}"
LangString GaugeType ${LANG_ENGLISH} "${APPNAME} gauge"
LangString GaugeType ${LANG_UKRAINIAN} "Індикатор ${APPNAME}"
LangString TemplateType ${LANG_ENGLISH} "${APPNAME} template"
LangString TemplateType ${LANG_UKRAINIAN} "Шаблон ${APPNAME}"
LangString Needs64 ${LANG_ENGLISH} "${APPNAME} needs a 64-bit Windows."
LangString Needs64 ${LANG_UKRAINIAN} "${APPNAME} потребує 64-бітної Windows."

VIProductVersion "${FILEVERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${APPNAME} setup"
VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "GNU GPL v3"

; A file type opened by the program: its ProgID and the suffix pointing to it.
!macro Associate SUFFIX PROGID DESCRIPTION
  WriteRegStr SHCTX "Software\Classes\.${SUFFIX}" "" "${PROGID}"
  WriteRegStr SHCTX "Software\Classes\${PROGID}" "" "${DESCRIPTION}"
  WriteRegStr SHCTX "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\${EXENAME},0"
  WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\${EXENAME}" "%1"'
!macroend

; ... taken away, the suffix only while it still points to it.
!macro Unassociate SUFFIX PROGID
  ReadRegStr $0 SHCTX "Software\Classes\.${SUFFIX}" ""
  StrCmp $0 "${PROGID}" 0 +2
    DeleteRegKey SHCTX "Software\Classes\.${SUFFIX}"
  DeleteRegKey SHCTX "Software\Classes\${PROGID}"
!macroend

Section "!$(SectionProgram)" SecProgram
  SectionIn RO
  ; a version before goes first, by its own uninstaller: files it had and this one has not
  ; would stay behind otherwise
  IfFileExists "$INSTDIR\uninstall.exe" 0 +3
    ExecWait '"$INSTDIR\uninstall.exe" /S /$MultiUser.InstallMode _?=$INSTDIR'
    Delete "$INSTDIR\uninstall.exe"
  SetOutPath "$INSTDIR"
  File /r "${SOURCE}/*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"

  WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayName" "${APPNAME}"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${EXENAME},0"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "URLInfoAbout" "${WEBSITE}"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "InstallMode" "$MultiUser.InstallMode"
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe" /$MultiUser.InstallMode'
  WriteRegStr SHCTX "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /$MultiUser.InstallMode /S'
  WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD SHCTX "${UNINSTALL_KEY}" "EstimatedSize" "$0"
SectionEnd

Section /o "$(SectionDesktop)" SecDesktop
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"
SectionEnd

Section "$(SectionFiles)" SecFiles
  !insertmacro Associate "rtvproj" "${REGNAME}.project" "$(ProjectType)"
  !insertmacro Associate "rtvgauge" "${REGNAME}.gauge" "$(GaugeType)"
  !insertmacro Associate "rtvtemplate" "${REGNAME}.template" "$(TemplateType)"
  ; Explorer shows the new icons without a restart
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecProgram} "$(DescProgram)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "$(DescDesktop)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecFiles} "$(DescFiles)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "$(Needs64)"
    Abort
  ${EndIf}
  SetRegView 64
  !insertmacro MULTIUSER_INIT
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

Section "Uninstall"
  ; what the installer put there, one by one (make-installer.sh lists it)
  !include "${UNINSTALL_LIST}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  !insertmacro Unassociate "rtvproj" "${REGNAME}.project"
  !insertmacro Unassociate "rtvgauge" "${REGNAME}.gauge"
  !insertmacro Unassociate "rtvtemplate" "${REGNAME}.template"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  DeleteRegKey SHCTX "${UNINSTALL_KEY}"
SectionEnd

Function un.onInit
  SetRegView 64
  !insertmacro MULTIUSER_UNINIT
FunctionEnd
