# ZeroGPS

App del **Cap LoRa-1262 (LoRa + GPS)** para el **M5Stack Cardputer Zero** (Raspberry Pi CM0,
pantalla de 320×170, Debian 13 arm64). **Sólo para España** (mapas del IGN). Un menú con cuatro
funciones:

- **Mapa**: teselas del **IGN** (topográfico MTN, satélite PNOA o IGN Base, tecla `M`), tu posición, el track que se graba y el
  abierto desde Tracks.
- **Satélites**: el cielo (círculos de elevación, norte arriba) con los satélites por
  constelación, y fix, coordenadas, altitud, precisión aproximada y DOP.
- **Tracks**: grabar GPX (sigue aunque cambies de pantalla), listar `~/Tracks`, abrir, borrar.
- **Mapas offline**: descargar una zona (radio o zona ya hecha, zoom mín./máx., tarjeta) con
  aviso de tamaño y espacio libre.

En el lanzador se llama **IGN GPS** (la app de M5Stack para el cap, paquete
`m5cardputerzero-cap-lora-1262-gps`, ya viene instalada y se llama «GPS»). Qué hace y cómo se usa está en `README.md`; aquí va lo que hace
falta para tocarla.

Es la app hermana de **ZeroRF-NFC** (Cap CC1101+NFC): los dos caps comparten ranura y **no se
pueden poner a la vez**, por eso hay una app por cap. El GNSS va por serie y el LoRa (SX1262)
queda para más adelante.

No es una app de Android: de `~/CLAUDE.md` valen el estilo (comentarios, documentación y
commits en español explicando el porqué; pocas dependencias; lógica pura con pruebas), que el
repositorio es **público** (ni secretos ni datos personales) y la entrega; lo de Gradle, firma
y GrapheneOS no aplica.

## Compilar y comprobar

`Tools/compilar.sh` usa Docker si lo hay y, si no, las herramientas de la máquina.

```sh
Tools/compilar.sh pruebas   # simulador (SDL2) + ctest; tiene que pasar
Tools/compilar.sh deb       # dist/ZeroGPS_<versión>_rev1_arm64.deb
Tools/compilar.sh simulador # abre el simulador en este escritorio (X11)
```

- La primera vez descarga LVGL 9.5 y el *sysroot* a `.cache/`. Enlazar/copiar el de otra app del
  aparato (`../ZeroTVBGone/.cache`) ahorra la descarga.
- **Probar sin pantalla**: simulador bajo `Xvfb` (`SDL_RENDER_DRIVER=software`), teclas con
  `xdotool key --window`, captura con `import -window root` (pantalla en `322x170+286+118` con
  `-screen 0 900x600x24`).
- **El aparato** es `cpzero` en Tailscale; suele estar apagado, Bruno avisa. Hasta probarlo se
  dice «sin probar en el aparato» en el «Estado».

## De dónde viene

- **La base** es `../ZeroRF-NFC` (a su vez de la plantilla oficial): CMake, simulador, registro,
  *bindings* reactivos, barra de título, aviso, ayuda y el **enrutado de teclas por pantalla**
  (`BaseViewModel::set_page_handlers`) se conservan. Un arreglo en esas piezas comunes conviene
  llevarlo a las tres apps del aparato. Se quitó todo lo de RF/NFC y el menú pasó a Map / Tracks.
- **Núcleo GPS** (software puro, con pruebas) en `src/model/`: `gps_nmea` (parsea RMC/GGA/GSA/GSV
  con checksum; el cielo se arma con los GSV completos por talker+señal, junta L1/L5 del mismo
  satélite y olvida lo que no se renueva en 5 GGA), `gps_gpx` (leer/escribir GPX 1.1) y `map_tiles` (matemática Web Mercator lon/lat↔
  tesela/píxel, teselas que cubren una zona, URL del IGN y ruta de caché).
- **Mapas del IGN**: WMTS con el conjunto **`GoogleMapsCompatible`** (Web Mercator EPSG:3857,
  256 px) → teselas z/x/y estándar. Endpoint MTN: `ign.es/wmts/mapa-raster` (JPEG). Licencia
  **CC-BY 4.0**: hay que mostrar en pantalla «© Instituto Geográfico Nacional de España». El
  identificador de la capa es `MTN` (confirmado en el *GetCapabilities*, 20-nov-2026). Teselas
  **online con wifi + opción de descargar una zona para uso offline** (caché local); nada de
  descargar en masa.

## Cómo está hecha

- **Capas** (`map_layers`): Topo `mapa-raster/MTN` (máx. z17, en la raíz de la caché para
  no perder lo ya bajado ni las de la app), Satélite `pnoa-ma/OI.OrthoimageCoverage` (z19,
  `pnoa/`), Base `ign-base/IGNBaseTodo` (z18, `base/`). Todas JPEG y GoogleMapsCompatible
  (comprobado en sus GetCapabilities el 20-nov-2026); los tamaños medios de tesela de
  `estimated_tile_bytes` se midieron ese día en Navacerrada. `IGNBaseOrto` (rótulos sobre
  la ortofoto) sería el «híbrido», pero es PNG con transparencia: haría falta libpng.

- **`platform::GpsSession`** (la tiene el viewmodel, `view_model().gps()`): GNSS por demanda
  (`acquire`/`release`; con 0 usuarios se para el hilo y se cortan los 5V del cap), grabación
  (`gps::TrackRecorder` alimentado desde el hilo del GNSS, GPX reescrito cada 60 s con
  temporal + rename), track abierto (overlay), vista del mapa y ajustes (`maps.conf`).
- **`TileProvider`**: JPEG → RGB565 con libjpeg, LRU de 64, recuerda las que faltan.
  Busca en: carpeta elegida, demás tarjetas (`/proc/mounts` → `storage::removable_mounts`),
  `assets/tiles` (Guadarrama z12-14, va en el `.deb`).
- **`TileDownloader`**: `curl` del sistema en tandas de 48 con `--parallel-max 3` y `-K`
  (reutiliza conexiones; sin enlazar libcurl). `.part` → rename si empieza por `FF D8`. Se
  para sin espacio (<64 MB) o tras 2 tandas sin nada bueno. Tope 100.000 teselas.
- **Mapa**: copia las teselas al buffer del canvas (sin `lv_draw`); si falta una, amplía la
  de hasta 4 zooms menos. Refresco por zoom (`map::map_refresh_ms`) y repintado sólo si algo
  se movió ≥2 px. Lo pidió Bruno: el zoom manda en el intervalo, por batería.
- **Teclas por pantalla**: `set_page_handlers(owner, …)` / `clear_page_handlers(owner)`.
  El dueño es necesario porque la pantalla nueva se construye antes de destruir la vieja (al
  ir de Tracks al mapa la vieja borraba las teclas de la nueva). Arreglo a llevar a
  ZeroRF-NFC si allí se salta de una herramienta a otra.

## Hardware (Cap LoRa-1262)

Detalle en la memoria `cardputer-caps-hardware`. Numeración **BCM** del CM0:

- **GNSS AT6668**: UART → `/dev/serial0` a **115200 8N1** (NMEA 0183). Es lo que usa ZeroGPS.
- **SX1262 (LoRa)**: SPI0 (`/dev/spidev0.1`, NSS=G7, RST=G26, BUSY=G22, DIO1=G23). Para más
  adelante.
- **5V del cap**: escribir `1` en `/sys/class/leds/ext_5v_out/brightness` antes de abrir el GNSS.
- **Antena obligatoria** (sobre todo para LoRa) o se daña el cap.

## Convenciones del aparato que la app respeta

- `.deb` con `.desktop` en `/usr/share/APPLaunch/applications/`; el lanzador cede `/dev/fb0` y el
  teclado (evdev).
- `Esc` corto «atrás», `Esc` mantenido sale; `Fn`+`H` ayuda, `Fn`+`J` captura.
- Flechas en `F`/`Z`/`X`/`C` (con o sin `Fn`), tratadas como flechas por el viewmodel.
- **Interfaz en español** (lo pidió Bruno el 20-nov-2026: «esta va a ir sola»). Es la
  excepción: las demás apps del aparato están en inglés.

## Versiones y entrega

- Versión en `project(... VERSION x.y.z)` de `CMakeLists.txt` y en `app-builder.json` (a la vez).
- El `.deb` se copia a `~/Documentos/ZeroGPS-<versión>.deb`, sustituyendo al anterior.
- Commit en `main` con mensaje en español que empieza por la versión, y push.
- **Tienda**: `app-builder.json` es la ficha (`czdev publish --deb dist/…deb`, desde
  `../AppBuilder`; sesión en `~/.czdev/credentials`). Título **IGN GPS** (`app_name`; el
  paquete sigue siendo `zerogps`), código `ZGPS`, categorías «Hardware & IoT» y «Lifestyle»
  (la lista es cerrada: «Navigation» no vale), capturas de **320×170 exactos** (las
  recortadas del Xvfb llevan desplazamiento de página: `convert … +repage`). La primera
  publicación la revisa una persona con un vídeo en el aparato en el PR; las siguientes
  versiones se aceptan solas. Icono: `Tools/generar_icono.py` (mapa plegado con chincheta).
- Vídeos y capturas para publicar: **sin la ubicación** (los del Pixel la llevan en los
  metadatos: `ffmpeg -map_metadata -1 -an`). Los originales no se suben al repo
  (`*.mp4` en `.gitignore`).

## Estado

- **Tienda (20-nov-2026):** 0.5.0 enviada como **IGN GPS** en
  [CardputerZero/packages#568](https://github.com/CardputerZero/packages/pull/568); CI en verde
  (`validate`, `capture`), **esperando la revisión** de un mantenedor (primera publicación).
  Vídeo en el aparato enlazado en el PR, subido como release `video-0.5.0` de este repo (sin
  ubicación ni audio; se ve la zona de casa en el mapa: Bruno lo aceptó). Para publicar hubo
  que sincronizar el fork `baltamir1978/packages`: iba 49 commits por detrás con cambios en
  `.github/workflows` y el token de `czdev` (sólo `public_repo`) no puede crear ramas así
  (404). Se arregla con `gh repo sync baltamir1978/packages --source CardputerZero/packages`
  (el `gh` del portátil tiene ya el permiso `workflow`).

- **0.5.0 (20-nov-2026) — recuerda dónde estabas; descarga de los tres mapas; instalada, sin probar en el aparato.**
  - `maps.conf` guarda `view=lat,lon,zoom` (dónde se quedó el mapa) y `last=lat,lon` (última
    posición con fix). Se escribe al salir del mapa y de la app, y con fix al alejarse 200 m
    de lo guardado (o cada 5 min si se mueve algo), no a cada segundo. Sin fix, el mapa abre
    donde se quedó y pinta la última posición con el marcador en gris. Antes cada arranque
    abría en Navacerrada.
  - Descarga: fila «Mapa» con «Los tres» (por defecto; se bajan uno detrás de otro con el
    mismo descargador y el progreso suma los tres), zoom mín. 5 y máx. «máx.» (cada mapa
    hasta el suyo: 17/19/18) por defecto, radio 5 km por defecto: ~53.000 teselas y ~840 MB,
    casi todo el satélite a z19 (a 10 km serían ~210.000, por encima del tope). Lo pidió Bruno.
  - **GNSS: medido en el aparato** junto a la ventana: ~13 s hasta tener posición tras
    encender los 5V, igual tras horas apagado que tras cortar 15 s (no pierde casi nada al
    cortar); encendido, 1 s. Se probó dejarlo encendido unos minutos al salir y **Bruno no lo
    quiere**: 15 s le parecen bien. No volver a proponerlo.

- **0.4.1 (20-nov-2026) — zoom con Q / A.**
  - En el teclado del Cardputer (`linux,keymap*` del tca8418 en el device tree) la capa
    normal sólo tiene letras, números, Esc, Tab, Enter, espacio, mayúsculas, Ctrl y Alt:
    `, .` están en la capa de símbolos (sobre 7 y 8), `-` sobre la U y `+` sobre las
    mayúsculas. Bruno no conseguía hacer zoom: ahora `Q` acerca y `A` aleja. **Para
    cualquier atajo nuevo, usar letras.** La capa Fn trae las flechas (F/Z/X/C), Fn+H
    = `KEY_HELP` y Fn+J = `KEY_SYSRQ`.

- **0.4.0 (20-nov-2026) — pantalla de satélites; instalada, sin probar en el aparato.**
  - Entrada nueva «Satélites» en el menú (segunda): cielo con los satélites por
    constelación (relleno = usado, tenue = sin señal, «+N sin situar» los que aún no tienen
    elevación/azimut) y columna con fix 2D/3D, usados/vistos, lat/lon, altitud, velocidad,
    precisión ≈ HDOP × 4 m (el AT6668 no manda GST), HDOP/VDOP, hora UTC y leyenda
    usados/vistos por constelación. Lo pidió Bruno.
  - El parser lee GSA (modo de fix, DOP, usados; id de sistema de NMEA 4.10 o, con GN sin
    él, por número) y GSV; HDOP también de la GGA. 2 pruebas nuevas en `gps_nmea_test`.
  - La etiqueta del mapa muestra la altitud.
  - El simulador trae un cielo de 11 satélites que gira, para ver la pantalla sin el cap.
  - **Por mirar en el aparato:** qué talkers manda el AT6668 con fix (sin fix sólo mandaba
    `$GP…`; si manda las demás constelaciones como GN sin id, se reparten por número).

- **0.3.2 (20-nov-2026) — las teclas de letra llegan en el aparato; instalada, sin probar.**
  - En el aparato **sólo llegaban las teclas que traducía la plantilla** (flechas, F/X/T/Z/C,
    4–8, Esc, Enter…): `M`, `O`, `R`, `D` y el zoom no hacían nada, aunque en el simulador
    (SDL) sí. `map_evdev_key` traduce ahora todas las letras, los números y `, . - = +`. El
    teclado (`tca8418`) no tiene `KEY_MINUS`/`KEY_EQUAL`: su `-` y su `+` son `KEY_KPMINUS` y
    `KEY_KPPLUS`. **Arreglo a llevar a ZeroRF-NFC y ZeroTVBGone** si usan más letras.
  - **Primer fix en el aparato** junto a la ventana (5 satélites); el mapa salía en negro
    porque el modo online viene apagado y en Madrid no hay teselas incluidas (sólo
    Guadarrama): con `O` (que ahora llega) se bajan.

- **0.3.1 (20-nov-2026) — nombre y descripción; instalada en el aparato.**
  - En el lanzador (y en la barra de título del menú) pasa a llamarse **IGN GPS**: había
    dos entradas «GPS», la nuestra y la de M5Stack.
  - La descripción del paquete era la de ZeroTVBGone (venía de la plantilla); ahora es la suya.
  - **Comprobado en el aparato** (con la 0.3.0): `/dev/serial0` → `ttyS0`, sin consola
    encima (`console=tty1`), usuario en `dialout`; con los 5V encendidos el AT6668 manda
    NMEA a 115200 (RMC, VTG, GGA, GSA, GSV, GLL, ZDA, TXT). Con la antena
    puesta sigue mandando `$GPTXT,...,ANTENNA OPEN` (será que la antena es pasiva y la
    detección no ve consumo: no afecta, la app no lee los TXT). En interior: 3 satélites a
    29–32 dB y hora UTC al minuto y medio, sin fix todavía.
  - **Pendiente:** ver el mapa con teselas en el aparato y grabar un track; lo de las efemérides al cortar los 5V; LoRa.

- **0.3.0 (20-nov-2026) — elegir mapa e icono; sin probar en el aparato.**
  - Tres mapas del IGN: topográfico, satélite (PNOA) y IGN Base. `M` en el mapa los cambia
    (se recuerda en `maps.conf`); la descarga tiene fila «Mapa» con el zoom máximo de cada uno
    y su estimación de tamaño. Probado en el simulador con el modo online (satélite a z17).
  - Icono propio: mapa plegado con río, ruta y chincheta.

- **0.2.0 (20-nov-2026) — mapa real, tracks y mapas offline; sin probar en el aparato.**
  - Arreglada la compilación que dejó rota la sesión anterior (`TileProvider` incluía
    `asset_manager.h`).
  - **Mapa con teselas del IGN** (capa `MTN` confirmada en el GetCapabilities; GoogleMapsCompatible
    llega a z20, la app usa hasta z17 y amplía a z18). Paneo, centrar, zoom, sobrezoom.
  - **GNSS**: `poll()` en vez de lecturas con espera, reintenta abrir `/dev/serial0`, apaga
    los 5V al parar, GGA con calidad 0 marca sin posición. Simulado en Navacerrada a pie.
  - **Tracks** (grabar/parar/listar/abrir/borrar con confirmación) y **Mapas offline**
    (zonas ya hechas o radio, zooms, tarjeta, tamaño/espacio, progreso, cancelar, reanudar).
    Probado en el simulador con una descarga real de 39 teselas.
  - **Interfaz en español**, menú de tres entradas, ayuda por pantalla.
  - 5 pruebas en verde (`offline_zone` y `track_recorder` nuevas).
  - **Pendiente:** probar en el aparato (¿`/dev/serial0` libre de la consola? ¿permisos del
    grupo `dialout`? ¿llega el NMEA a 115200?); ¿pierde efemérides el AT6668 al cortar los
    5V? (si el arranque en frío es lento, mantenerlo encendido o mandar el comando de reposo);
    LoRa.
- **0.1.0 (20-nov-2026)** — esqueleto traído de ZeroRF-NFC, núcleo GPS (NMEA, GPX, teselas)
  con pruebas, lector GNSS y mapa con malla en lugar de teselas.
