# Pendientes Web98

## Estado actual (2026-10-20, HEAD local tras commit del instalador)
- Instalador NSIS Win98 escrito (`installer/web98.nsi`) + empaquetador (`scripts/package.ps1`).
- Verificado con probes: sintaxis System::Call outputs = `.r0..9` (sin `$`), structs tipo `i` (4 bytes), mecánica de layout (panel+banner+botones top-right) OK.
- Último run de `package.ps1 -Test` FALLÓ por falta de `!include nsDialogs.nsh` — YA CORREGIDO en web98.nsi + fix icono en banner (ToBitmap+DrawImage), pero NO se ha re-ejecutado.

## Tarea 1 — Compilar y probar el instalador (ALTA)
- [ ] Ejecutar: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package.ps1 -Test`
- [ ] Si makensis da errores, arreglar web98.nsi (compilación de 300 File instructions + LZMA ~250MB tarda varios minutos).
- [ ] Test silencioso incluido: instala en `%TEMP%\web98-install-test`, verifica archivos requeridos, desinstala silencioso, verifica borrado.

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
- NSIS 3.12 en `C:\Program Files (x86)\NSIS\makensis.exe` (NO en PATH).
- System::Call: outputs SOLO `.r0..9` (`.r $var` sale vacío — bug sintaxis); structs usar tipo `i` (4B), `l` lee 8B y mezcla campos; pasar structs como puntero plano `p $addr`, leer con `*$addr(i.r1, ...)`.
- Registros NSIS: solo `$0-$9` y `$R0-$R9` (no existen `$S*`).
- `installer/staged/`, `installer/files.nsh`, `installer/banner.bmp` se GENERAN con package.ps1 (están en .gitignore; no commitear).
- Para compilar directo con makensis primero hay que correr package.ps1 (genera files.nsh + banner.bmp).
- Repo: `https://github.com/belfegor442/Web98`, rama `master`. Tras cada cambio: build nativo + `web98_unit.exe` (195 tests) + sintaxis JS + smoke.
