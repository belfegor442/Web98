# Pendientes Web98

## Estado actual (2026-10-26)
- Instalador NSIS Win98 (`installer/web98.nsi`) + empaquetador (`scripts/package.ps1`) COMPLETOS.
- **`package.ps1 -Test` PASA**: instalación silenciosa 300 ficheros, verificación, desinstalación limpia.
- Dos bugs raíz resueltos (ver Notas técnicas): `FileOpen` modo `'a'` escribe en pos 0, y `SetWindowTextW(p $StatusH, ...)` con `$StatusH` vacío corrompe `$INSTDIR`.
- **Tareas 1-5 CERRADAS (2026-10-23)**: walkthrough GUI completo + tests unitarios verdes (289). Un bug nuevo encontrado y resuelto en Tarea 3 (ver abajo).
- **Re-verificación independiente 2026-10-26** (todo reproducido desde cero):
  - `web98_unit.exe` recompilado (cl manual, sin CEF) → **289 passed, 0 failed**.
  - Desinstalador GUI en **ambos modos**: `_?=$INSTDIR` (Confirm → Remove → Complete → Finish, solo queda `Uninstall.exe` — comportamiento esperado del modo automatizable) y **modo normal sin `_?`** (original sale exit=0 al instante, GUI desde `%TEMP%`, **`INSTDIR` auto-borrado COMPLETO incluido `Uninstall.exe`**, accesos + HKCU borrados, 0 restos, 0 procesos residuales).
  - **Smoke CDP: SÍ EJECUTABLE** (la hipótesis "no ejecutable" era falsa; Tarea 4 corregida): `node test/e2e/smoke.js` → **32/32 PASS** en 3 corridas (1 flake transitorio de cierre de settings, verde en 2 reintentos), y la build CEF instalada con `--remote-debugging-port=9333` responde `GET /json/version` (Chrome/154, `webSocketDebuggerUrl`) y `/json/list` con las páginas de la app — CEF 154 hereda el switch de command line aunque `settings.remote_debugging_port` no se fije en código.

## Tarea 1 — Compilar y probar el instalador (ALTA)
- [x] `package.ps1 -Test` pasa: install 300 ficheros → verify 7 required → uninstall → dir removed.

## Tarea 2 — Test UI visual instalador (ALTA)
- [x] Walkthrough automatizado completo (`meshtest\full-walkthrough.ps1`) — TODO VERDE, capturas `walk-01..07`:
  - Banner izquierdo 164px gradiente + icono + "Web98 Setup"; fuentes MS Sans Serif; botones [Cancel][Back][Next] arriba-derecha; sin "Nullsoft Install System" (walk-01).
  - License: Next deshabilitado inicialmente, sigue deshabilitado tras "I do not accept", habilitado tras "I accept" (walk-02). `fLicenseLeave` tiene MessageBox+Abort como red de seguridad (nsi:289-292), inalcanzable en flujo normal con Next deshabilitado.
  - Dir: default INSTDIR correcto; Browse… abre diálogo (localizado "Buscar carpeta" en ES) y rellena el edit (no error/cancelled); segunda Browse + Cancel → edit intacto (walk-03). *Nota: PrintWindow devuelve negro en el diálogo SHBrowseForFolder — asserts programáticos cubren el comportamiento.*
  - Options: checkboxes desktop/start por defecto marcados; Next relabeled "Install"; click Install → `msctls_progress32` visible y completado (walk-04/05).
  - **Cancel durante extracción: DISEÑO CAMBIADO** — ya no hay confirmación; Cancel queda DESHABILITADO durante extract y se re-habilita al terminar (nsi WIP). Verificado: disabled en busy, enabled en Finish.
  - Finish: texto "Finish", Back oculto, Cancel re-habilitado, Launch por defecto marcado; al desmarcar → web98 NO arranca al salir (walk-07); artefactos: web98.exe, Uninstall.exe, install.log `install-done`, accesos directos, HKCU InstallDir.
  - Back de Options → Dir conserva el path custom editado; volver a default correcto.
  - *No ejercitado: checkbox Launch MARCADO → arranque desde Finish (el arranque real se verificó aparte en Tarea 4).*

## Tarea 3 — Test UI desinstalador (MEDIA)
- [x] Flujo GUI Confirm ("Uninstall") → Remove ("Removing files…") → Done ("Uninstall Complete"/"Finish") verificado DOS veces:
  - Modo `_?=$INSTDIR` (automatizado por pid): `walk-09`, `walk-10`, solo queda `Uninstall.exe` (lock del proceso en sitio), accesos + HKCU borrados.
  - Modo NORMAL sin `_?` (doble-click de usuario) con setup silencioso previo: `tarea3-normal.ps1` TODO VERDE, `walk-11..13`: original entrega a copia en `%TEMP%` (exit 0), páginas avanzan, **INSTDIR borrado COMPLETO incluido Uninstall.exe**, sin procesos residuales, accesos + HKCU borrados.
- [x] `RMDir /r "$INSTDIR"` y auto-borrado: **SÍ en modo normal** (handoff a %TEMP% desbloquea el exe en sitio); con `_?` queda el exe (esperado, es el modo de automatización — no es bug).
- [x] Accesos directos (Escritorio + Menú Inicio) y clave HKCU\Software\Web98 borrados — verificado en ambos modos.
- **BUG (encontrado y RESUELTO en working tree)**: `un.fRemove` deshabilitaba id1/id3 ANTES de `nsDialogs::Show` → `Show` nunca retornaba → `un.fRemoveLeave` (única que llama `un.DoRemove`) jamás corría → desinstalador GUI colgado en "Removing files…" con barra en 0 y nada borrado (bug en HEAD, no solo en WIP). Fix: los `EnableWindow(0)` se movieron dentro de `un.fRemoveLeave` (patrón espejo del `DoInstall` del instalador: deshabilitar solo DESPUÉS de que `Show` retorna). Rebuild + walkthrough verdes.

## Tarea 4 — Test funcional post-instalación (MEDIA)
- [x] Instalación real → `web98.exe` lanza, proceso Responding, ventana clase `Web98Frame` con título (walk-08), health check OK; cierre por WM_CLOSE no surtió efecto → force-kill (observación: revisar manejo de WM_CLOSE/close-confirm a futuro).
- [x] **Smoke CDP EJECUTADO y VERDE (2026-10-26)** — la hipótesis "no ejecutable en esta máquina" era incorrecta:
  - App Electron (la main real, `src/main/main.js`): `node test/e2e/smoke.js` → **32/32 PASS, `ALL E2E CHECKS PASSED`, exit 0** (3 corridas; 1 flake transitorio en el cierre de la ventana de settings, verde en los 2 reintentos).
  - Build CEF instalada: `web98.exe --remote-debugging-port=9333` → `GET http://127.0.0.1:9333/json/version` responde (Chrome/154, `webSocketDebuggerUrl`); `GET /json/list` lista `Web98 Home` + `Web98`. No hace falta fijar `settings.remote_debugging_port` en código: CEF 154 hereda el switch de command line.

## Tarea 5 — Baja prioridad
- [x] Tests unitarios para `sanitizeId` y `parseStoredSession`: **`web98_unit` 289 passed / 0 failed**. `sanitizeId` extendido (13 checks nuevos: residuales, prefijos/símbolos, duplicados, empty→id, etc.) + suite `runSession()` para `parseStoredSession` (multi-window, caps 8/40, legacy `{urls,active}`, active no-finito, roundtrip `buildStoredSession`). Linkage OK: `session.cpp` añadido a `WEB98_UNIT_SRCS` en `native/CMakeLists.txt`; en esta máquina se compila con `meshtest\build-unit.ps1` (cl manual, sin CEF — `find_package(CEF)` arriba de CMakeLists impide cmake completo).
- [x] DPI considerado: todo verificado a 96dpi (capturas); instalador NSIS clásico fijo 96dpi por diseño → sin quirks en >96dpi, sin cambio de código.

## Notas técnicas clave (para quien continúe)
- **BUG 1 (resuelto)**: `FileOpen $h file 'a'` escribe en posición 0 (sobrescribe) — abrir UN handle con `'w'` al inicio y usar `FileWrite` secuencial.
- **BUG 2 (resuelto)**: `System::Call 'user32::SetWindowTextW(p $StatusH, w $StatusText)'` con `$StatusH` = cadena vacía (modo `/S`, página nunca creada) corrompe `$INSTDIR` a "0" (nunca se ejecutó ningún StrCpy sobre $INSTDIR — corrupción por el propio System::Call). Fix: envolver en `${If} $StatusH != ""`. Nunca pasar vars vacías como punteros `p` a System::Call.
- **BUG 3 (resuelto, Tarea 3)**: en `UninstPage custom`, deshabilitar id1 ANTES de `nsDialogs::Show` = deadlock (Show solo retorna al pulsar id1/id2/id3; `*Leave` nunca corre). Deshabilitar botones SOLO dentro de la función Leave, como hace `DoInstall`.
- NSIS uninstaller sin parámetro `_?` → se copia a `%TEMP%` y ejecuta desde ahí (el proceso ORIGINAL sale con code 0 al instante — NO es fallo; buscar la ventana por título, no por pid). Con `_?` corre en sitio, automatizable por pid, pero NO se auto-borra de `$INSTDIR`.
- `MainWindowHandle` de un proceso puede devolver `$null` → `$null -ne [IntPtr]::Zero` es TRUE y miente: normalizar a `[IntPtr]::Zero` antes de assert (helper `Wait-MainH` en walkthrough).
- `GetWindowText` cross-process devuelve STALE en Edits (lee la caché del caption) — usar WM_GETTEXT con buffer `GlobalAlloc`; algunos botones NSIS devuelven vacío vía WM_GETTEXT → fallback a `GetWindowText` (híbrido `Get-Text`).
- `taskkill` escribe en stderr → rompe `$ErrorActionPreference='Stop'` — usar `cmd /c "taskkill ... >nul 2>&1"`.
- NSIS 3.13 en `C:\Program Files (x86)\NSIS\Bin\makensis.exe` - el `makensis.exe` de la raíz es un STUB que lanza el worker y retorna antes (causa de locks/carreras); invocar `Bin\` directo (ya hace `package.ps1`).
- System::Call: outputs SOLO `.r0..9` (`.r $var` sale vacío — bug sintaxis); structs usar tipo `i` (4B), `l` lee 8B y mezcla campos; pasar structs como puntero plano `p $addr`, leer con `*$addr(i.r1, ...)`.
- Registros NSIS: solo `$0-$9` y `$R0-$R9` (no existen `$S*`).
- Modo `/S` (silencioso): páginas custom NO corren — toda la lógica de instalación vive en `Function DoInstall` llamada desde `.onInit` vía `IfSilent`; la de desinstalación en `un.DoRemove` desde `un.onInit` vía `IfSilent`.
- NSIS exige ≥1 Section aunque no se ejecute (warning 8000 aceptable en ambos instalador y desinstalador).
- `installer/staged/`, `installer/files.nsh`, `installer/banner.bmp` se GENERAN con package.ps1 (están en .gitignore; no commitear).
- Los scripts y capturas de los walkthroughs GUI viven en `%TEMP%\opencode\meshtest\` y `%TEMP%\opencode\*.ps1` (temp, NO commiteados): `full-walkthrough.ps1`, `tarea3-normal.ps1`, `drive-un.ps1`, `build-unit.ps1`, capturas `walk-01..13`.
- Para compilar directo con makensis primero hay que correr package.ps1 (genera files.nsh + banner.bmp).
- Repo: `https://github.com/belfegor442/Web98`, rama `master`. Tras cada cambio: build nativo + `web98_unit.exe` (289 tests) + sintaxis JS + smoke.
