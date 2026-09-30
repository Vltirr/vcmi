# Barra de Focus de batalla

## Finalidad

Este documento describe el estado de la interfaz del recurso **Focus** en batalla,
las decisiones visuales ya validadas, los assets implicados y el flujo local usado
para modificar, compilar y comprobar la funcionalidad.

Los objetivos de la iteración han sido:

- hacer legible el progreso de Focus de ambos bandos;
- integrar la barra bajo la cola de iniciativa sin tapar héroes ni criaturas;
- conservar una estética compatible con la interfaz original de Heroes III;
- diferenciar con claridad tramos activos, tramos vacíos y puntos de carga;
- integrar el medallón mediante un adorno dorado y azul coherente con la barra;
- mantener visibles los remates dorados de los extremos sin recolocar los rombos
  cuya posición ya había sido validada.

## Repositorio y rama

- Código fuente: `D:\GitRepos\vcmi`
- Rama: `feature/battle-focus`
- Instrucciones del repositorio: `D:\GitRepos\vcmi\AGENTS.md`
- Código principal:
  - `client/battle/BattleFocusBar.cpp`
  - `client/battle/BattleFocusBar.h`
  - `client/battle/BattleWindow.cpp`
  - `client/battle/BattleWindow.h`
  - `client/battle/BattleFieldController.cpp`

El código fuente debe permanecer libre de builds, dependencias, logs y resultados
locales. Esos artefactos se mantienen en `D:\Juegos\VCMI-Builds`.

## Estado visual aprobado

La barra usa un marco de oro oscuro, una pista azul, relleno dorado para el
progreso, separadores internos, hasta cinco rombos de carga y un medallón engarzado
mediante un adorno azul y dorado. El 2026-09-30 el usuario autorizó variantes de
cuatro y cinco cargas con extensión moderada y rombos menores; el 2026-10-01
aceptó su resultado dentro del juego. La variante de tres conserva sus PNG y
todas sus coordenadas anteriores.

Revisión posterior del usuario: cinco cargas aceptadas visualmente, conservando
su tamaño; en cuatro, el segundo rombo coincide con el medallón y queda detrás.
Por petición del usuario, los rombos se dibujan ahora por encima del medallón,
con aceptación visual del 2026-10-01. Por petición expresa, `config/gameConfig.json` usa ahora `maxCharges: 5`
como valor base y comentarios de cada parámetro; también se actualizó la copia
de configuración del run nuevo, sin recompilar ni modificar el run anterior.

Las decisiones que deben considerarse fijas salvo petición expresa son:

- la posición de los rombos en la variante de tres cargas;
- el tamaño y la posición del medallón;
- la posición vertical de la media luna y los brazos del adorno;
- el orden relativo entre adorno, rombos y medallón;
- el anclaje de las barras bajo la cola de iniciativa;
- el renderizado de criaturas y héroes por encima de toda la interfaz de Focus.

La corrección anterior amplía únicamente el extremo de cada marco en **5 píxeles
lógicos**. El remate dorado queda junto a la punta exterior del último rombo, sin
ocultarlo y sin dejar una casilla final excesivamente larga. No se desplazó ningún
rombo para conseguirlo.

## Assets

Los assets normales están en:

`Mods/vcmi/Content/Sprites/battle/focus`

Las variantes 4x están en:

`Mods/vcmi/Content/Sprites4x/battle/focus`

| Asset | Función | Tamaño 1x | Tamaño 4x | Tamaño lógico de render |
| --- | --- | ---: | ---: | ---: |
| `FocusBarFrameDarkGold.png` | Marco activo de oro oscuro y fondo azul | 203x16 | 812x64 | 223x18 |
| `FocusBarFrame.png` | Marco anterior, conservado pero no cargado actualmente | 198x16 | 792x64 | n/a |
| `ProgressBar_Energy.png` | Relleno dorado del progreso | 188x8 | 752x32 | 206x9 |
| `Separator_ProgressBar_Energy.png` | Separadores de puntos dentro de la pista | 2x10 | 12x40 | 3x9 |
| `EnegyPoint_ProgressBar_Activ.png` | Rombo de carga alcanzada | 18x18 | 72x72 | 20x20 |
| `EnegyPoint_ProgressBar_NotActiv.png` | Rombo de carga pendiente | 18x18 | 72x72 | 20x20 |
| `FocusBarOrnament.png` | Brazos, panel azul y media luna que engarzan el medallón | 104x25 | 416x101 | 104x25 |
| `Icon_Energy_AddSection.png` | Medallón activo | 28x28 | 112x112 | 34x34 |
| `Icon_Energy.png` | Medallón inactivo de grafito y oro | 28x28 | 112x112 | 34x34 |

Variantes añadidas para cuatro y cinco cargas:

| Asset | Tamaño 1x | Tamaño 4x | Tamaño lógico |
| --- | ---: | ---: | ---: |
| `FocusBarFrameDarkGold4.png` | 239x16 | 956x64 | 263x18 |
| `ProgressBar_Energy4.png` | 224x8 | 896x32 | 246x9 |
| `FocusBarFrameDarkGold5.png` | 276x16 | 1104x64 | 303x18 |
| `ProgressBar_Energy5.png` | 261x8 | 1044x32 | 286x9 |

Los remates del marco se conservan píxel por píxel (6 píxeles en 1x, 24 en 4x).
Sólo se extiende el cuerpo central. Los rombos reutilizan sus PNG originales y
se escalan a 16x16 para cuatro cargas y 14x14 para cinco. El script reproducible
`D:\Juegos\VCMI-Builds\tooling\generate-focus-charge-variants.ps1` comprueba tamaños,
remates, hashes de todos los originales y backups, simetría y espacio mínimo.
La evidencia se guarda en `work\focus-charge-variants` del árbol externo.

La errata `EnegyPoint` pertenece a los nombres históricos de los archivos. No debe
corregirse sin actualizar también todas sus referencias.

Los recursos se cargan como imágenes builtin bajo `battle/focus/`, con
`EImageBlitMode::COLORKEY`, y se escalan con filtrado bilineal a sus tamaños
lógicos.

### Construcción del marco extendido

El marco de oro oscuro introducido en `5db2b7b3f` medía 198x16 en 1x y 792x64 en
4x. Para descubrir el remate exterior sin alterar la posición de los rombos se
extendió su cuerpo central:

- 1x: 198x16 -> 203x16, extensión de 5 píxeles;
- 4x: 792x64 -> 812x64, extensión de 20 píxeles físicos;
- tamaño lógico: 218x18 -> 223x18.

Se utilizó escalado horizontal de tres tramos: los remates de 6 píxeles en 1x y
24 píxeles en 4x se copiaron sin cambios y sólo se amplió la franja central. Los
dos remates se restauraron después píxel por píxel para evitar artefactos de
interpolación.

El script local reproducible es:

`D:\Juegos\VCMI-Builds\tooling\extend-focus-bar-frame.ps1`

El material de trabajo correspondiente está en:

`D:\Juegos\VCMI-Builds\work\focus-bar-extended-end`

## Geometría del widget

La geometría de tres cargas de `BattleFocusBar` es:

| Elemento | Geometría lógica |
| --- | --- |
| Widget completo | 227x53 |
| Anchura de composición original | 218 |
| Extensión del marco | 5 |
| Marco | 223x18 en `Y=7` |
| Pista | 206x9 en `X=6`, `Y=12` |
| Separador base | 3x9 |
| Separador de tramo activo | 2x9 |
| Separador de tramo vacío | 3x7 |
| Rombo | 20x20, centrado verticalmente sobre el marco |
| Medallón | 34x34 |
| Adorno | 104x25 en `Y=20` |
| Máximo de rombos visibles | 5 |

`STANDARD_WIDGET_WIDTH` permanece en 218 como base de tres cargas. Esto mantiene el adorno,
el medallón, la pista, los separadores y los rombos en las posiciones ya aprobadas,
mientras que `FRAME_WIDTH` pasa a 223.

En el atacante, el marco conserva su origen y crece 5 píxeles hacia la derecha.
En el defensor, el widget se ancla al mismo margen derecho y el marco crece 5
píxeles hacia la izquierda. La compensación de `trackX` del defensor y las fórmulas
especulares basadas en `WIDTH` conservan las coordenadas globales anteriores de
todo el contenido. Por tanto, la ampliación sólo mueve los remates exteriores.

Para cuatro/cinco cargas, `configureLayout` carga la variante de marco y relleno
y añade 40/80 píxeles lógicos: widget 267/307x53, marco 263/303x18 y pista
246/286x9. Cada punto, con seis puntos por carga, dispone de 10,25/9,53 píxeles
incluyendo separador, frente a 11,44 con tres. Medallón y adorno mantienen sus
tamaños y coordenadas verticales, centrados en la composición ampliada.
La variante sólo se recarga al cambiar la capacidad visual; los puntos nuevos
no recargan ni reescalan assets. El defensor se ancla con su `pos.w` real, no con
la anchura constante de tres. Las barras crecen hacia el centro y conservan
138/58 píxeles libres entre widgets en el campo de batalla lógico de 800 píxeles.

No debe volver a resolverse este problema desplazando el último checkpoint: esa
prueba hacía desaparecer visualmente la última casilla y fue descartada.

## Datos y comportamiento

`BattleFocusBar::updateState` recibe:

- si la regla de Focus está habilitada;
- los puntos actuales del bando;
- los puntos necesarios por carga;
- el número máximo de cargas.

Los valores de divisor se limitan a un mínimo de uno y los puntos actuales se
acotan entre cero y la capacidad total. La barra se deshabilita cuando las reglas
de Focus no están activas.

El relleno del atacante crece de izquierda a derecha. El del defensor usa un
recorte de la misma textura y crece de derecha a izquierda. El número de
separadores se limita por la capacidad y por una anchura mínima de segmento de
3 píxeles.

Se muestran como máximo cinco rombos representativos. Cada rombo se activa al
alcanzar su umbral correspondiente. Las capacidades tres, cuatro y cinco usan
las variantes y dimensiones descritas arriba. El renderer conserva un fallback:
capacidades menores usan la geometría de tres y mayores usan la de cinco, sin
ampliar el esquema válido `[3, 4, 5]`. El medallón activo se usa cuando hay al menos
un punto de Focus; con cero puntos se muestra el medallón inactivo.

El widget es exclusivamente visual y tiene la entrada deshabilitada.

## Orden de dibujo dentro de la barra

`BattleFocusBar::render` dibuja las capas en este orden:

1. marco de oro oscuro;
2. relleno de progreso;
3. separadores internos;
4. adorno que abraza el medallón;
5. medallón activo o inactivo;
6. rombos activos o inactivos.

Este orden garantiza que:

- el engarce del adorno se vea sobre la barra;
- `EnegyPoint_ProgressBar_Activ.png` y
  `EnegyPoint_ProgressBar_NotActiv.png` queden por encima del adorno;
- el medallón quede por encima de la media luna, sin que el borde del adorno tape
  su dibujo, y los rombos queden por encima del medallón para mostrar completo el
  segundo checkpoint en la variante de cuatro cargas;
- la parte superior del medallón pueda invadir visualmente la barra.

## Posición y orden dentro de la batalla

`BattleWindow::setPositionFocusBars` usa:

- margen horizontal: 64 píxeles;
- solape con la cola de iniciativa: 7 píxeles.

La barra atacante se ancla al margen izquierdo del campo y la defensora al margen
derecho. Cuando la cola de iniciativa es visible, las dos barras se colocan bajo
ella con el solape indicado.

`BattleFieldController::renderBattlefield` dibuja:

1. fondo y hexágonos;
2. barras de Focus;
3. `BattleRenderer`, que incluye criaturas y otros elementos del campo;
4. proyectiles.

Como resultado, las criaturas grandes y los héroes quedan por encima de las barras
y del medallón. Esto evita que la interfaz oculte sprites del campo de batalla.

## Evolución visual y decisiones descartadas

Durante la exploración se probaron soportes alados, piezas triangulares, nudos
angulares, brazos muy ornamentados, franjas azules horizontales y distintos
solapes entre marco y adorno. Muchas variantes se descartaron porque invadían la
pista, deformaban el medallón o no encajaban con el lenguaje visual original.

El adorno de producción actual sí está activo. Su diseño final mantiene:

- brazos sencillos de borde dorado;
- panel azul sólo alrededor del medallón, no como franja horizontal añadida;
- media luna abierta por arriba para permitir que el medallón sobresalga;
- ausencia de remaches laterales;
- una transición de 24 píxeles físicos en 4x entre los brazos y la barra;
- brazos desplazados 2 píxeles físicos en 4x;
- zona central desplazada 8 píxeles físicos en 4x;
- posición lógica final `Y=20`.

El marco activo se recoloreó a oro oscuro para aproximarlo al adorno. Se evitó
recolorear el adorno completo porque eso habría alterado una pieza ya aceptada y
habría propagado el cambio a más zonas visuales.

Los materiales rechazados anteriores siguen archivados en:

`D:\Juegos\VCMI-Builds\work\focus-ui-rejected-20260915`

La referencia del medallón está en:

`D:\Juegos\VCMI-Builds\work\focus-medallion-reference`

Otros directorios de pruebas deterministas:

- `D:\Juegos\VCMI-Builds\work\focus-bar-deterministic-test`
- `D:\Juegos\VCMI-Builds\work\focus-bar-arm-variants`
- `D:\Juegos\VCMI-Builds\work\focus-bar-extended-end`

## Krita y edición de imágenes

Se instaló la integración `krita-codex-mcp` y el preflight llegó a indicar:

```text
Krita 5.3.3 (git 858d352): compatible
Ready for productive use.
```

Componentes conocidos:

- Krita 5.3.3;
- ejecutable: `C:\Program Files\Krita (x64)\bin\krita.exe`;
- `uv/uvx` 0.12.14;
- servidor MCP `krita-codex-mcp` 0.2.0;
- plugin de Codex `krita-codex-mcp@krita-codex`;
- puente Python:
  `C:\Users\Eduardo\AppData\Roaming\krita\pykrita\kritacodexbridge`;
- configuración:
  `C:\Users\Eduardo\AppData\Local\krita-codex-mcp\config.json`.

La versión 0.1.0 que puede aparecer en la caché del plugin corresponde al paquete
de integración; ese paquete invoca el servidor MCP 0.2.0 y no supone un conflicto.

Por seguridad, el MCP sólo tiene autorizado:

`D:\Juegos\VCMI-Builds\work\krita-mcp`

No tiene acceso directo al repositorio. El flujo previsto con Krita es copiar un
asset a esa carpeta, trabajar sobre una copia o KRA, exportar allí, revisar y sólo
entonces copiar la variante aprobada al repositorio.

En esta iteración, Krita resultó útil para la composición y el ajuste visual manual,
pero poco fiable para transformaciones de tamaño y color que exigían medidas
exactas. Para recortes, extensiones, preservación de remates y variantes numéricas
se prefiere edición determinista fuera de Krita. Krita queda reservado para trabajo
pictórico o composiciones que necesiten juicio visual manual.

Scripts locales usados durante esta fase:

- `D:\Juegos\VCMI-Builds\tooling\generate-focus-bar-color-test.py`;
- `D:\Juegos\VCMI-Builds\tooling\generate-focus-bar-arm-variants.py`;
- `D:\Juegos\VCMI-Builds\tooling\generate-focus-bar-transition-variants.py`;
- `D:\Juegos\VCMI-Builds\tooling\extend-focus-bar-frame.ps1`.

Estos scripts y sus resultados son herramientas locales y no forman parte del
repositorio fuente.

## Build local y verificación

Antes de compilar hay que leer:

`D:\Juegos\VCMI-Builds\docs\Local_Rebuild_Workflow.md`

Herramienta de build:

`D:\Juegos\VCMI-Builds\tooling\build-vcmi.ps1`

Run preservado del commit de hito `f82db7a81` (no reutilizar):

`D:\Juegos\VCMI-Builds\runs\1.8.0-feature-battle-focus-93dd4eeac1-20260910-000212`

Instalación de prueba:

`D:\Juegos\VCMI-Builds\runs\1.8.0-feature-battle-focus-93dd4eeac1-20260910-000212\install`

Compilación histórica de la geometría de tres cargas:

- fecha: 2026-09-16;
- estado: `Success`;
- logs:
  `D:\Juegos\VCMI-Builds\runs\1.8.0-feature-battle-focus-93dd4eeac1-20260910-000212\logs\20260916-163406`;
- duración: 00:02:04;
- toolset MSVC: 14.50.35717.

Build de las variantes de cuatro/cinco, terminada el 2026-09-30:

- run nuevo: `D:\Juegos\VCMI-Builds\runs\1.8.0-feature-battle-focus-f82db7a81e-20260930-090728`;
- cliente: `install\VCMI_client.exe` dentro de ese run;
- evidencia: `logs\20260930-090828\summary.txt`, `Status: Success`;
- tres procesos, tests habilitados, duración 01:26:33;
- el intento `Clean` inicial falló antes de compilar por acceso a Conan; se reanudó
  como `Incremental` en el run nuevo vacío, sin reutilizar el del hito anterior;
- 55/55 pruebas focales, informe `logs\20260930-090828\focus-tests.xml`;
- ocho variantes instaladas verificadas por SHA-256; estado Git idéntico antes y
  después de la build, PNG originales/backups intactos y validador de 31 criaturas correcto.

La corrección posterior del orden de capas (rombos por encima del medallón) se
compiló e instaló en el mismo run mediante una única build incremental de tres
procesos con tests habilitados: `logs\20260930-230227\summary.txt`,
`Status: Success`, duración 00:07:32. Las 55 pruebas focales volvieron a pasar;
el cliente instalado coincide por SHA-256 con el compilado y ningún PNG original
ni backup cambió. `install/config/gameConfig.json` conserva el nuevo máximo
base de cinco y sus comentarios. El usuario aceptó cuatro/cinco el 2026-10-01.

Después se corrigió un crash en `getDeliberateActivationSources`: el propietario
de la lista de bonuses debe vivir durante todo el recorrido. No era un defecto
de assets. Build incremental de tres procesos con tests habilitados:
`logs\20261001-002540\summary.txt`, `Status: Success`, duración 00:41:16.
Pasaron 56/56 pruebas focales, incluida la prueba determinista de vida útil;
la DLL instalada coincide por SHA-256 con la compilada. El usuario confirmó que
la versión corregida funciona bien. No se lanzó otra build por esta documentación.

La comprobación visual anterior fue manual dentro del juego y el ajuste de +5 píxeles
fue aceptado. Las nuevas variantes de cuatro/cinco están compiladas e instaladas;
fueron aceptadas por el usuario. El asistente no debe abrir ni controlar el juego para estas
comprobaciones salvo que el usuario lo solicite expresamente.

## Commits relevantes

- `15d1f5cc1 Improve battle focus bar visuals`
  - mejora del rombo vacío, marco inicial, separador y renderizado;
- `d30c7bf76 Refine battle Focus bar layout and layering`
  - posición, tamaño, medallón, prioridad respecto a criaturas y separadores;
- `f410f3c04 Refine inactive battle focus medallion`
  - nuevo medallón inactivo 1x y 4x;
- `2af21d9c7 Add focus bar ornament`
  - introducción del adorno 1x y 4x;
- `5db2b7b3f Refine focus bar ornament integration`
  - integración final del adorno, marco de oro oscuro y transición de brazos.

El commit que contiene este documento añade además la extensión final de 5 píxeles
del marco sin modificar las posiciones previamente aprobadas.

## Estado de Git y archivos del usuario

Hay dos PNG sin seguimiento que pertenecen al usuario:

- `Mods/vcmi/Content/Sprites4x/battle/focus/backup -FocusBarOrnament.png`
- `Mods/vcmi/Content/Sprites4x/battle/focus/backup Icon_Energy.png`

No deben borrarse, modificarse ni incluirse en commits salvo petición explícita.

## Guía para cambios futuros

1. Leer `AGENTS.md` y este documento.
2. No cambiar posiciones ya aprobadas para resolver un problema de tamaño del
   asset; separar explícitamente geometría del marco y geometría del contenido.
3. Preparar primero una composición o una variante determinista cuando el cambio
   sea puramente visual.
4. Mantener sincronizadas las versiones 1x y 4x.
5. Preservar la relación de aspecto de medallón y adorno.
6. Comprobar el orden de capas: adorno, rombos y medallón tienen dependencias
   visuales explícitas.
7. Cerrar `VCMI_client` antes de compilar o instalar, porque puede bloquear DLL y
   ejecutables.
8. Ejecutar el build incremental mediante el script local y revisar su
   `summary.txt`.
9. Dejar la comprobación manual in-game al usuario, salvo petición contraria.
10. Hacer commit sólo después de la aceptación visual y excluir siempre las dos
    copias PNG sin seguimiento indicadas arriba.
