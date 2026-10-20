# Pendientes Web98

## Estado actual (2026-10-20)
- Instalador NSIS Win98 (`installer/web98.nsi`) + empaquetador (`scripts/package.ps1`) COMPLETOS.
- **`package.ps1 -Test` PASA**: instalación silenciosa 300 ficheros, verificación, desinstalación limpia.
- Dos bugs raíz resueltos hoy (ver Notas técnicas): `FileOpen` modo `'a'` escribe en pos 0, y `SetWindowTextW(p $StatusH, ...)` con `$StatusH` vacío corrompe `$INSTDIR`.

## Tarea 1 — Compilar y probar el instalador (ALTA)
- [x] `package.ps1 -Test` pasa: install 300 ficheros → verify 7 required → uninstall → dir removed.

## Tarea 2 — Test UI visual instalador (ALTA)
- [ ] Ejecutar `dist\Web98 Setup 0.1.0 x64.exe` (GUI) y verificar aspecto Win98:
  - Banner izquierdo 164px con gradiente + icono + texto "Web98 Setup".
  - Fuente MS Sans Serif en todos los controles (títulos bold, cuerpo regular).
  - Botones [Cancel][Back][Next] arriba-derecha (y=8); sin texto "Nullsoft Install System"; sin separador inferior.
  - Página License: Next deshabilitado hasta marcar "I accept"; click "I do not accept" → aviso + Abort.
  - Página Dir: Browse… abre SelectFolderDialog — verificar que devuelve ruta (no "error"/"cancelled") y llena el edit.
  - Página Options: checkbox desktop/start; click Install → extracción con barra de progreso `msctls_progress32` visible y avanzando (si CreateWindowExW devuelve 0 = clase comctl32 v5 no disponible → fallback a texto estático).
  - Página Finish: Back oculto, texto "Finish", checkbox Launch → arranca web98.exe.
- [ ] Navegación Back entre páginas (Welcome/License/Dir/Options) conserva valores.
- [ ] Cancel durante extracción → mensaje de confirmación funciona (pump de mensajes en AdvFile).

## Tarea 3 — Test UI desinstalador (MEDIA)
- [ ] Ejecutar `Uninstall.exe` (GUI): Confirm (botón "Uninstall") → Remove (borrado) → Done ("Finish").
- [ ] Verificar que `RMDir /r "$INSTDIR"` puede borrar el Uninstall.exe en ejecución (FILE_SHARE_DELETE; si falla → borrar exe manualmente vía GetTempPath + rename antes del borrado).
- [ ] Verificar borrado de accesos directos (Escritorio + Menú Inicio) y clave HKCU\Software\Web98.

## Tarea 4 — Test funcional post-instalación (MEDIA)
- [ ] Instalar en director real, lanzar web98.exe, correr smoke CDP (`%TEMP%\smoke.js`) para health check.

## Tarea 5 — Baja prioridad
- [ ] Tests unitarios para `sanitizeId` y `parseStoredSession` (verificar linkage en web98_unit primero).
- [ ] Considerar DPI awareness si hay queks en pantallas >96dpi (instalador es 96dpi-fijo por diseño NSIS clásico).

## Notas técnicas clave (para quien continúe)
- **BUG 1 (resuelto)**: `FileOpen $h file 'a'` escribe en posición 0 (sobrescribe) — abrir UN handle con `'w'` al inicio y usar `FileWrite` secuencial.
- **BUG 2 (resuelto)**: `System::Call 'user32::SetWindowTextW(p $StatusH, w $StatusText)'` con `$StatusH` = cadena vacía (modo `/S`, página nunca creada) corrompe `$INSTDIR` a "0" (nunca se ejecutó ningún StrCpy sobre $INSTDIR — corrupción por el propio System::Call). Fix: envolver en `${If} $StatusH != ""`. Nunca pasar vars vacías como punteros `p` a System::Call.
- NSIS 3.12 en `C:\Program Files (x86)\NSIS\makensis.exe` (NO en PATH).
- System::Call: outputs SOLO `.r0..9` (`.r $var` sale vacío — bug sintaxis); structs usar tipo `i` (4B), `l` lee 8B y mezcla campos; pasar structs como puntero plano `p $addr`, leer con `*$addr(i.r1, ...)`.
- Registros NSIS: solo `$0-$9` y `$R0-$R9` (no existen `$S*`).
- Modo `/S` (silencioso): páginas custom NO corren — toda la lógica de instalación vive en `Function DoInstall` llamada desde `.onInit` vía `IfSilent`.
- NSIS exige ≥1 Section aunque no se ejecute (warning 8000 aceptable en ambos instalador y desinstalador).
- `installer/staged/`, `installer/files.nsh`, `installer/banner.bmp` se GENERAN con package.ps1 (están en .gitignore; no commitear).
- Para compilar directo con makensis primero hay que correr package.ps1 (genera files.nsh + banner.bmp).
- Repo: `https://github.com/belfegor442/Web98`, rama `master`. Tras cada cambio: build nativo + `web98_unit.exe` (195 tests) + sintaxis JS + smoke.
