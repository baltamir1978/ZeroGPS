# IGN GPS (ZeroGPS)

GPS con los mapas del **Instituto Geográfico Nacional** para el **M5Stack Cardputer Zero**
(Raspberry Pi CM0, pantalla de 320×170, Linux) con el **Cap LoRa-1262** (LoRa SX1262 + GNSS
AT6668). En el lanzador y en la tienda se llama **IGN GPS**; el repositorio y el paquete,
`zerogps`.

> **Sólo para España.** Los mapas son del IGN: fuera de lo que cubre (España) no hay mapa.
> La interfaz está en español.

![Mapa topográfico](screenshot/map.png) ![Satélite](screenshot/satellite.png)
![Satélites](screenshot/sky.png) ![Menú](screenshot/home.png)

## Qué hace

- **Mapa**: tu posición sobre el mapa del IGN, con el track que estás grabando (rojo) y el que
  hayas abierto desde Tracks (morado). Arriba, la posición, la altitud, el nivel de zoom
  (`z14`) y los satélites usados. Si falta una tesela a un zoom, amplía la de un zoom menor en
  vez de dejar un hueco.
- **Recuerda dónde estabas**: el mapa se abre donde lo dejaste y, mientras el GNSS busca
  satélites (unos 13 s al encenderlo), enseña en gris tu última posición conocida.
- **Satélites**: el cielo visto desde arriba (norte arriba; círculos de horizonte, 30° y 60°
  de elevación) con cada satélite en su sitio y el color de su constelación (GPS, Galileo,
  GLONASS, BeiDou, QZSS, SBAS): **relleno** si se usa para la posición, hueco si sólo se ve,
  tenue si no se oye. Al lado: tipo de fix (2D/3D), satélites usados/vistos, latitud,
  longitud, altitud, velocidad, **precisión aproximada** (HDOP × ~4 m: el GNSS no da el error
  real), HDOP y VDOP, hora UTC y cuántos se usan de cada constelación. Recién encendido el
  GNSS oye satélites sin saber aún dónde están: salen como «+N sin situar».
- **Tracks**: graba tu recorrido en **GPX** (sigue grabando aunque salgas del mapa), lista los
  guardados en `~/Tracks`, los abre en el mapa y los borra.
- **Mapas offline**: descarga una zona entera para usarla en el monte sin cobertura. Eliges el
  mapa (o **los tres**, lo de por defecto), la zona (alrededor de donde tengas el mapa, con un
  radio de 1 a 50 km, o una zona ya hecha: Guadarrama, Gredos, Picos de Europa, Sierra Nevada,
  Pirineo central, Cazorla, Montseny, Madrid), el **zoom mínimo y máximo** (por defecto, todo:
  del 5 al máximo de cada mapa) y **dónde guardarla** (la tarjeta del sistema o una
  tarjeta/pincho montado). Antes de bajar nada ves **cuántas teselas son, cuánto ocuparán y
  cuánto espacio libre hay**: avisa si va justo y no deja empezar si no cabe. Lo que ya tengas
  no se vuelve a bajar, así que una descarga cortada se retoma repitiéndola.
- **Modo online** (tecla `O` en el mapa): con wifi, baja sólo las teselas que faltan en
  pantalla. Viene apagado.

Trae de serie las teselas de la Sierra de Guadarrama (zooms 12 a 14) para tener mapa sin
descargar nada.

![Mapas offline](screenshot/download.png) ![Tracks](screenshot/tracks.png)

## Mapas

Se elige con la tecla `M` en el mapa (y en la descarga, con la fila «Mapa»):

| Mapa | Qué es | Zoom con detalle | Tesela media |
|---|---|---|---|
| **Topográfico** | MTN, el mapa topográfico nacional: curvas de nivel, caminos, fuentes | hasta 17 | ~20-25 KB |
| **Satélite** | Ortofoto aérea PNOA (25 cm por píxel) | hasta 19 | ~15-17 KB |
| **IGN Base** | Cartografía simplificada, tipo callejero | hasta 18 | ~3-10 KB |

Cada mapa se guarda en su carpeta, así que se pueden tener los tres de la misma zona. Por
encima del zoom con detalle se amplía el último nivel. Como referencia, los tres mapas con
todo el zoom en un radio de 5 km son unas 53.000 teselas y ~840 MB, casi todo del satélite a
z19.

## Teclas

En el teclado del Cardputer, `,` `.` `+` `-` sólo están en la capa de símbolos: por eso los
atajos son letras.

| Dónde | Tecla | Qué hace |
|---|---|---|
| Todas | `Fn`+`H` | Ayuda |
| | `Fn`+`J` | Captura de pantalla |
| | `T` | Claro / oscuro |
| | `Esc` / mantener `Esc` | Atrás / salir |
| Menú | `F` / `X` · `ENTER` | Elegir · abrir |
| Mapa | Flechas (`F` `Z` `X` `C`, con o sin `Fn`) | Mover el mapa |
| | `ENTER` | Centrar en mi posición |
| | `Q` / `A` | Acercar / alejar (también `+` `-` `.` `,` en la capa de símbolos) |
| | `M` | Cambiar de mapa: topográfico, satélite, IGN Base |
| | `R` | Grabar / parar track |
| | `O` | Modo online |
| | `D` | Descargar la zona que estás viendo |
| Tracks | `ENTER` | Grabar / parar, o abrir el track en el mapa |
| | `D` (dos veces) | Borrar el track |
| | `Q` | Quitar del mapa el track abierto |
| Mapas offline | `F`/`X` · `Z`/`C` · `ENTER` | Elegir fila · cambiar valor · descargar |

## Batería

La batería del Cardputer Zero es pequeña, así que la app intenta gastar lo mínimo:

- El GNSS (y los 5V del cap) **sólo está encendido** con el mapa o los satélites abiertos, o
  mientras grabas. Volver a tener posición tras encenderlo cuesta unos 13 s.
- El mapa se refresca **según el zoom**: cada segundo de cerca (z16-18), cada 2 s a z14-15,
  cada 5 s a z12-13 y cada 10 s más lejos. Y sólo se repinta si te has movido al menos 2
  píxeles: de lejos, aunque andes, el punto casi no se mueve.
- El hilo del GNSS duerme hasta que llegan datos; el track guarda un punto cada 5 m (o cada
  30 s si vas muy despacio) y el GPX se escribe en la tarjeta una vez por minuto.
- Nada de wifi salvo que tú lo pidas (descarga de zona o modo online).

## Instalar

En el aparato, desde la tienda del Cardputer Zero (**IGN GPS**, código `ZGPS`), o con el
`.deb`:

```sh
sudo apt install ./ZeroGPS_<versión>_rev1_arm64.deb
```

Hace falta el Cap LoRa-1262 puesto **con su antena** (sin ella se puede dañar el cap). El
GNSS va por `/dev/serial0` a 115200; el usuario tiene que estar en el grupo `dialout` (ya lo
está en el sistema del aparato).

## Dónde guarda las cosas

- Tracks: `~/Tracks/AAAA-MM-DD_hhmmss.gpx` (hora UTC del GPS).
- Teselas: `~/.local/share/zerogps/tiles/` (o `<tarjeta>/ZeroGPS/tiles/` si eliges otra
  tarjeta): el topográfico en `z/x/y.jpeg`, el satélite en `pnoa/z/x/y.jpeg` y el Base en
  `base/z/x/y.jpeg`. El mapa busca en todas las tarjetas.
- Ajustes: `~/.config/zerogps/maps.conf` (mapa elegido, modo online, carpeta de teselas,
  dónde se quedó el mapa y la última posición).

## Compilar

```sh
Tools/compilar.sh pruebas     # simulador de escritorio (SDL2) y pruebas
Tools/compilar.sh deb         # dist/ZeroGPS_<versión>_rev1_arm64.deb para el aparato
Tools/compilar.sh simulador   # abre el simulador en este escritorio (X11)
```

Usa Docker si lo hay y, si no, las herramientas de la máquina (las de `Tools/Dockerfile`). La
primera vez descarga LVGL 9.5 y el *sysroot* de M5Stack a `.cache/` (no se versiona). En el
simulador el GNSS es simulado (un paseo por el puerto de Navacerrada con un cielo de 11
satélites); en el aparato, `ZEROGPS_GNSS=sim` hace lo mismo. Las descargas usan el `curl` del
sistema (dependencia del paquete).

## Mapas y licencia

Las teselas vienen de los servicios WMTS del IGN, con el conjunto `GoogleMapsCompatible`:
`mapa-raster` (capa `MTN`), `pnoa-ma` (capa `OI.OrthoimageCoverage`) e `ign-base` (capa
`IGNBaseTodo`). Los datos son **© Instituto Geográfico Nacional de España** (la ortofoto, del
PNOA), con licencia **CC-BY 4.0**: la atribución se ve siempre en el mapa. La app pide las
teselas con moderación (como mucho 3 a la vez) y pone un tope de 100.000 teselas por descarga:
es para llevarte tus zonas, no para volcar el servicio.

El código es MIT (ver `LICENSE`). Las teselas incluidas en `assets/tiles` son datos del IGN
bajo CC-BY 4.0.

## Estado

**0.5.0** (20-nov-2026): mapa con los tres mapas del IGN, pantalla de satélites, tracks GPX,
mapas offline (los tres a la vez) y modo online; recuerda la última posición. En el aparato
ya se instala, coge posición (~13 s desde apagado) y responde al teclado; falta probar ahí
una grabación de track larga y una descarga grande. El LoRa (SX1262) queda para más adelante.
