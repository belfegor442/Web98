; Web98 Installer â€” Windows 98 look & font (NSIS 3.12)
; Custom nsDialogs wizard: MS Sans Serif via WM_SETFONT, no manifest
; (classic 3D controls on modern Windows), banner left, buttons top-right.
; Build via scripts\package.ps1 (stages files, banner.bmp, files.nsh).

Unicode true
!include LogicLib.nsh
!include WinMessages.nsh
!include nsDialogs.nsh

Name "Web98"
OutFile "..\dist\Web98 Setup 0.1.0 x64.exe"
Icon "..\build\icon.ico"
InstallDir "$LOCALAPPDATA\Programs\Web98"
InstallDirRegKey HKCU "Software\Web98" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText " "
ShowInstDetails nevershow
ShowUninstDetails nevershow
VIProductVersion "0.1.0.0"
VIAddVersionKey "ProductName" "Web98"
VIAddVersionKey "FileDescription" "Web98 Setup"
VIAddVersionKey "FileVersion" "0.1.0.0"
VIAddVersionKey "LegalCopyright" "Web98"

!include "files.nsh"

Var Banner
Var BannerImg
Var FReg
Var FBold
Var DirEdit
Var Bar
Var BarPos
Var StatusH
Var AcceptYes
Var AcceptNo
Var OptDesktop
Var OptStartMenu
Var LaunchBox
Var DesktopOn
Var LogH

; ---------------------------------------------------------- layout macro
; Called after nsDialogs::Create in every page. Moves content panel right
; of a full-height banner, moves buttons to top-right, applies fonts.
!macro LayoutInit
  ${If} $FReg == ""
    System::Call 'gdi32::CreateFontW(i -11, i 0, i 0, i 0, i 400, i 0, i 0, i 0, i 1, i 0, i 0, i 0, i 0, w "MS Sans Serif") p.r1'
    StrCpy $FReg $1
    System::Call 'gdi32::CreateFontW(i -11, i 0, i 0, i 0, i 700, i 0, i 0, i 0, i 1, i 0, i 0, i 0, i 0, w "MS Sans Serif") p.r1'
    StrCpy $FBold $1
  ${EndIf}
  System::Call '*(i, i, i, i) p.r4'
  System::Call 'user32::GetClientRect(p $HWNDPARENT, p $4)'
  System::Call '*$4(i.r5, i.r6, i.r7, i.r8)'
  StrCpy $R2 $7
  StrCpy $R3 $8
  ; hide NSIS separator + version text
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i -1) p.r1'
  System::Call 'user32::ShowWindow(p $1, i 0)'
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1028) p.r1'
  System::Call 'user32::ShowWindow(p $1, i 0)'
  ; banner static (created once, resized after)
  ${If} $Banner == ""
    System::Call 'user32::CreateWindowExW(i 0, w "Static", w "", i 0x5000000E, i 0, i 0, i 164, i $R3, p $HWNDPARENT, p 0, p 0, p 0) p.r1'
    StrCpy $Banner $1
  ${EndIf}
  System::Call 'user32::SetWindowPos(p $Banner, p 0, i 0, i 0, i 164, i $R3, i 0x0006)'
  ${If} $Banner != "0"
  ${AndIf} $BannerImg != "0"
  ${AndIf} $BannerImg != ""
    System::Call 'user32::SendMessageW(p $Banner, i 0x0172, i 0, p $BannerImg)'
  ${EndIf}
  ; move nsDialogs panel (#32770 child of main) right of banner
  System::Call 'user32::FindWindowExW(p $HWNDPARENT, p 0, w "#32770", p 0) p.r9'
  IntOp $R4 $R2 - 182
  IntOp $R5 $R3 - 43
  System::Call 'user32::SetWindowPos(p $9, p 0, i 175, i 36, i $R4, i $R5, i 0x0004)'
  ; buttons top-right: y=8, [Cancel][Back][Next]
  IntOp $1 $R2 - 89
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r9'
  System::Call 'user32::SetWindowPos(p $9, p 0, i $1, i 8, i 0, i 0, i 0x0005)'
  System::Call 'user32::SendMessageW(p $9, i 0x0030, p $FReg, i 0)'
  IntOp $1 $R2 - 170
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 3) p.r9'
  System::Call 'user32::SetWindowPos(p $9, p 0, i $1, i 8, i 0, i 0, i 0x0005)'
  System::Call 'user32::SendMessageW(p $9, i 0x0030, p $FReg, i 0)'
  IntOp $1 $R2 - 251
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 2) p.r9'
  System::Call 'user32::SetWindowPos(p $9, p 0, i $1, i 8, i 0, i 0, i 0x0005)'
  System::Call 'user32::SendMessageW(p $9, i 0x0030, p $FReg, i 0)'
  ; re-enable Next (license page may have disabled it)
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 1)'
!macroend

; ---------------------------------------------------------- pump + progress
!macro AdvFile
  IntOp $BarPos $BarPos + 1
  System::Call 'user32::SendMessageW(p $Bar, i 0x402, p $BarPos, p 0)'
  System::Call '*(i, i, i, i, i, i, i) p.r1'
  ${Do}
    System::Call 'user32::PeekMessageW(p $1, p 0, i 0, i 0, i 1) i.r2'
    ${If} $2 = 0
      ${Break}
    ${EndIf}
    System::Call 'user32::TranslateMessage(p $1)'
    System::Call 'user32::DispatchMessageW(p $1)'
  ${Loop}
!macroend

; NOTE: NSIS FileOpen mode 'a' writes at position 0 (observed overwrite, not
; append) â€” open ONE handle at install start and write sequentially instead.
!macro LogLine txt
  FileWrite $LogH "${txt}$\r$\n"
!macroend

Function .onInit
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=banner.bmp "banner.bmp"
  File /oname=terms.txt "terms.txt"
  System::Call 'user32::LoadImageW(p 0, w "$PLUGINSDIR\banner.bmp", i 0, i 0, i 0, i 0x0010) p.r1'
  StrCpy $BannerImg $1
  ; silent mode: custom pages never run, so do the install here
  IfSilent doInstall
  Goto initDone
doInstall:
  StrCpy $BarPos 0
  StrCpy $DesktopOn 1
  Call DoInstall
initDone:
FunctionEnd

; ============================================================ WELCOME
Page custom fWelcome
Function fWelcome
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  !insertmacro LayoutInit
  ${NSD_CreateLabel} 0 4 100% 14u "Welcome to the Web98 Setup"
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FBold, i 0)'
  ${NSD_CreateLabel} 0 24u 100% 60u "This wizard will install the Web98 retro-styled web browser on your computer.$\r$\n$\r$\nClick Next to continue, or Cancel to exit Setup."
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FReg, i 0)'
  nsDialogs::Show
FunctionEnd

; ============================================================ LICENSE
Page custom fLicense fLicenseLeave
Function fLicense
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  !insertmacro LayoutInit
  ${NSD_CreateLabel} 0 0 100% 12u "Terms and Conditions of Use"
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FBold, i 0)'
  ${NSD_CreateText} 0 14u 100% 76u ""
  Pop $0
  ${NSD_AddStyle} $0 0x00000844
  ${NSD_AddStyle} $0 0x00200000
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FReg, i 0)'
  ; load full T&C (UTF-16LE) straight into the edit — avoids NSIS 8k string limit
  System::Call 'kernel32::CreateFileW(w "$PLUGINSDIR\terms.txt", i 0x80000000, i 0, p 0, i 3, i 0, p 0) p.r8'
  System::Call 'kernel32::GetFileSize(p $8, p 0) i.r2'
  IntOp $R3 $2 + 2
  System::Call 'kernel32::VirtualAlloc(p 0, i $R3, i 0x3000, i 4) p.r4'
  System::Call 'kernel32::ReadFile(p $8, p $4, i $2, *i .r5, p 0)'
  System::Call 'kernel32::CloseHandle(p $8)'
  System::Call '*$4(w.r6)'
  ${If} $6 = 0xFEFF
    IntOp $R4 $4 + 2
  ${Else}
    StrCpy $R4 $4
  ${EndIf}
  System::Call 'user32::SendMessageW(p $0, i 0x000C, i 0, p $R4)'
  ${NSD_CreateRadioButton} 0 92u 100% 10u "I &accept the terms of the license"
  Pop $AcceptYes
  ${NSD_CreateRadioButton} 0 102u 100% 10u "I &do not accept the terms of the license"
  Pop $AcceptNo
  System::Call 'user32::SendMessageW(p $AcceptYes, i 0x0030, p $FReg, i 0)'
  System::Call 'user32::SendMessageW(p $AcceptNo, i 0x0030, p $FReg, i 0)'
  ${NSD_OnClick} $AcceptYes fOnAccept
  ${NSD_OnClick} $AcceptNo fOnDecline
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 0)'
  nsDialogs::Show
FunctionEnd

Function fOnAccept

  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 1)'
FunctionEnd

Function fOnDecline

  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 0)'
FunctionEnd

Function fLicenseLeave

  ${NSD_GetState} $AcceptYes $R0
  ${If} $R0 != ${BST_CHECKED}
    MessageBox MB_ICONEXCLAMATION|MB_OK "You must accept the terms and conditions to continue."
    Abort
  ${EndIf}
FunctionEnd

; ============================================================ DESTINATION
Page custom fDir fDirLeave
Function fDir
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  !insertmacro LayoutInit
  ${NSD_CreateLabel} 0 0 100% 12u "Choose Install Location"
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FBold, i 0)'
  ${NSD_CreateLabel} 0 16u 100% 20u "Setup will install Web98 in the following folder. Click Next to continue, or click Browse to choose a different folder."
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FReg, i 0)'
  ${NSD_CreateText} 0 40u 100% 12u "$INSTDIR"
  Pop $DirEdit
  System::Call 'user32::SendMessageW(p $DirEdit, i 0x0030, p $FReg, i 0)'
  ${NSD_CreateButton} 0 56u 80u 14u "&Browse..."
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FReg, i 0)'
  ${NSD_OnClick} $0 fOnBrowse
  nsDialogs::Show
FunctionEnd

Function fOnBrowse

  nsDialogs::SelectFolderDialog "Select the folder where Setup will install Web98" $INSTDIR
  Pop $R0
  ${If} $R0 != "error"
  ${AndIf} $R0 != "cancelled"
  ${AndIf} $R0 != ""
    ${NSD_SetText} $DirEdit $R0
  ${EndIf}
FunctionEnd

Function fDirLeave

  ${NSD_GetText} $DirEdit $R0
  ${If} $R0 == ""
    MessageBox MB_ICONEXCLAMATION|MB_OK "Please select an install location."
    Abort
  ${EndIf}
  StrCpy $INSTDIR $R0
FunctionEnd

; ============================================================ OPTIONS + INSTALL
Page custom fOptions fOptionsLeave
Function fOptions
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  !insertmacro LayoutInit
  ${NSD_CreateLabel} 0 0 100% 12u "Installation Options"
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FBold, i 0)'
  ${NSD_CreateCheckbox} 0 20u 100% 10u "Create a &desktop shortcut"
  Pop $OptDesktop
  ${NSD_CreateCheckbox} 0 32u 100% 10u "Create a &Start Menu shortcut"
  Pop $OptStartMenu
  System::Call 'user32::SendMessageW(p $OptDesktop, i 0x0030, p $FReg, i 0)'
  System::Call 'user32::SendMessageW(p $OptStartMenu, i 0x0030, p $FReg, i 0)'
  ${NSD_Check} $OptDesktop
  ${NSD_Check} $OptStartMenu
  ; progress bar + status inside panel (hidden until install), pixel coords
  System::Call 'user32::FindWindowExW(p $HWNDPARENT, p 0, w "#32770", p 0) p.r9'
  System::Call 'user32::CreateWindowExW(i 0, w "msctls_progress32", w "", i 0x50000000, i 8, i 90, i 380, i 14, p $9, p 0, p 0, p 0) p.r1'
  StrCpy $Bar $1
  ${NSD_CreateLabel} 8 108 380 20 ""
  Pop $StatusH
  System::Call 'user32::SendMessageW(p $StatusH, i 0x0030, p $FReg, i 0)'
  System::Call 'user32::ShowWindow(p $Bar, i 0)'
  System::Call 'user32::ShowWindow(p $StatusH, i 0)'
  ; Next = Install
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::SetWindowTextW(p $1, w "&Install")'
  nsDialogs::Show
FunctionEnd

Function fOptionsLeave

  StrCpy $BarPos 0
  ${NSD_GetState} $OptDesktop $R0
  ${If} $R0 == ${BST_CHECKED}
    StrCpy $DesktopOn 1
  ${Else}
    StrCpy $DesktopOn 0
  ${EndIf}
  Call DoInstall
FunctionEnd

; shared install logic: progress UI (harmless when silent/hidden), extraction,
; registry, uninstaller, shortcuts
Function DoInstall
  SetOutPath "$INSTDIR"
  FileOpen $LogH "$INSTDIR\install.log" w
  !insertmacro LogLine "start"
  System::Call 'user32::ShowWindow(p $Bar, i 5)'
  System::Call 'user32::ShowWindow(p $StatusH, i 5)'
  ${If} $StatusH != "0"
  ${AndIf} $StatusH != ""
    System::Call 'user32::SetWindowTextW(p $StatusH, w "Installing files...")'
  ${EndIf}
  System::Call 'user32::SendMessageW(p $Bar, i 0x406, i 0, i ${TOTALFILES})'
  ; disable Next + Back during extract
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 0)'
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 3) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 0)'
  ; extraction (File instructions from files.nsh)
  !insertmacro InstallFiles
  !insertmacro LogLine "files-done"
  ; registry + uninstaller + shortcuts
  WriteRegStr HKCU "Software\Web98" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Web98"
  CreateShortCut "$SMPROGRAMS\Web98\Web98.lnk" "$INSTDIR\web98.exe" "" "$INSTDIR\web98.exe" 0
  CreateShortCut "$SMPROGRAMS\Web98\Uninstall Web98.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
  ${If} $DesktopOn == 1
    CreateShortCut "$DESKTOP\Web98.lnk" "$INSTDIR\web98.exe" "" "$INSTDIR\web98.exe" 0
  ${EndIf}
  !insertmacro LogLine "install-done"
  FileClose $LogH
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 1)'
FunctionEnd

; ============================================================ FINISH
Page custom fFinish fFinishLeave
Function fFinish
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  !insertmacro LayoutInit
  ; hide Back (cannot return past install)
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 3) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 0)'
  System::Call 'user32::ShowWindow(p $1, i 0)'
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::SetWindowTextW(p $1, w "&Finish")'
  ${NSD_CreateLabel} 0 4 100% 14u "Installation Complete"
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FBold, i 0)'
  ${NSD_CreateLabel} 0 24u 100% 30u "Web98 has been installed on your computer. Click Finish to close this wizard."
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FReg, i 0)'
  ${NSD_CreateCheckbox} 0 58u 100% 10u "&Launch Web98"
  Pop $LaunchBox
  ${NSD_Check} $LaunchBox
  System::Call 'user32::SendMessageW(p $LaunchBox, i 0x0030, p $FReg, i 0)'
  nsDialogs::Show
FunctionEnd

Function fFinishLeave
  ${NSD_GetState} $LaunchBox $R0
  ${If} $R0 == ${BST_CHECKED}
    Exec "$INSTDIR\web98.exe"
  ${EndIf}
FunctionEnd

; ============================================================ UNINSTALLER
Var UnBar
Var UnStatus

UninstPage custom un.fConfirm un.fConfirmLeave
Function un.fConfirm
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  !insertmacro LayoutInit
  ${NSD_CreateLabel} 0 4 100% 14u "Uninstall Web98"
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FBold, i 0)'
  ${NSD_CreateLabel} 0 24u 100% 40u "This wizard will remove Web98 from your computer.$\r$\n$\r$\nClick Uninstall to begin removing Web98, or click Cancel to exit."
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FReg, i 0)'
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::SetWindowTextW(p $1, w "&Uninstall")'
  nsDialogs::Show
FunctionEnd

Function un.fConfirmLeave
FunctionEnd

UninstPage custom un.fRemove un.fRemoveLeave
Function un.fRemove
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  !insertmacro LayoutInit
  ${NSD_CreateLabel} 0 4 100% 14u "Removing Web98"
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FBold, i 0)'
  ${NSD_CreateLabel} 8 30 380 14 "Removing files..."
  Pop $UnStatus
  System::Call 'user32::SendMessageW(p $UnStatus, i 0x0030, p $FReg, i 0)'
  System::Call 'user32::FindWindowExW(p $HWNDPARENT, p 0, w "#32770", p 0) p.r9'
  System::Call 'user32::CreateWindowExW(i 0, w "msctls_progress32", w "", i 0x50000000, i 8, i 50, i 380, i 14, p $9, p 0, p 0, p 0) p.r1'
  StrCpy $UnBar $1
  System::Call 'user32::SendMessageW(p $UnBar, i 0x406, i 0, i 3)'
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 0)'
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 3) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 0)'
  nsDialogs::Show
FunctionEnd

Function un.fRemoveLeave
  System::Call 'user32::SendMessageW(p $UnBar, i 0x402, i 1, i 0)'
  Delete "$SMPROGRAMS\Web98\Web98.lnk"
  Delete "$SMPROGRAMS\Web98\Uninstall Web98.lnk"
  RMDir "$SMPROGRAMS\Web98"
  Delete "$DESKTOP\Web98.lnk"
  System::Call 'user32::SendMessageW(p $UnBar, i 0x402, i 2, i 0)'
  RMDir /r "$INSTDIR"
  System::Call 'user32::SendMessageW(p $UnBar, i 0x402, i 3, i 0)'
  DeleteRegKey HKCU "Software\Web98"
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 1)'
FunctionEnd

UninstPage custom un.fDone
Function un.fDone
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  !insertmacro LayoutInit
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 3) p.r1'
  System::Call 'user32::EnableWindow(p $1, i 0)'
  System::Call 'user32::ShowWindow(p $1, i 0)'
  System::Call 'user32::GetDlgItem(p $HWNDPARENT, i 1) p.r1'
  System::Call 'user32::SetWindowTextW(p $1, w "&Finish")'
  ${NSD_CreateLabel} 0 4 100% 14u "Uninstall Complete"
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FBold, i 0)'
  ${NSD_CreateLabel} 0 24u 100% 30u "Web98 has been removed from your computer. Click Finish to close this wizard."
  Pop $0
  System::Call 'user32::SendMessageW(p $0, i 0x0030, p $FReg, i 0)'
  nsDialogs::Show
FunctionEnd

; NSIS requires at least one section; never executed (no instfiles page â€”
; all install/uninstall work happens in page functions).
Section "Web98"
SectionEnd

Section "Uninstall"
SectionEnd
