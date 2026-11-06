# FT-710 Dashboard for macOS · Yaesu FT-710 Remote Control
**Ported & Developed for macOS by**: Yury Jajitzky (**YY3BIG**)  
**Based on**: [FT-710 Dashboard for Windows by M0NXD](https://github.com/M0NXD/FT-710Dashboard)  
**License**: [Apache License 2.0](LICENSE) (See [NOTICE](NOTICE) and [DISCLAIMER.md](DISCLAIMER.md))

---

**Languages / Idiomas / Langues / Línguas / Lingue**:  
[English](README_EN.md) | [Español](README_ES.md) | [Français](README_FR.md) | [Português](README_PT.md) | [Italiano](README_IT.md)

---

> [!CAUTION]
> **AVISO DE SOFTWARE EXPERIMENTAL PARA RADIOAFICIONADOS**  
> Este software es estrictamente experimental y se distribuye bajo los principios del Servicio de Radioaficionados. El **Operador de Control** es el único y exclusivo responsable legal del funcionamiento técnico, potencia de RF, límites de frecuencia, relación de onda estacionaria (ROE/SWR) y transmisiones de PTT de su estación. Para pruebas iniciales, utilice siempre una **carga ficticia (Dummy Load)** de 50 Ω. Consulte el documento legal completo en [DISCLAIMER.md](DISCLAIMER.md).

---

## Características en macOS

* **Compilación Nativa Apple Silicon & Intel**: Código C++20 optimizado para macOS (`arm64` y `x86_64`) con Apple Clang.
* **Control CAT Completo**: Control bidireccional de VFO A y B, modos, filtros, ganancias de RF/AF, medidores de transmisión y recepción, DNR, NB y funciones del Yaesu FT-710 mediante Hamlib 4.7.2.
* **Detección Automática de Puertos USB**: Escaneo y conexión automática con el transceptor en `/dev/cu.SLAB_USBtoUART*` o `/dev/cu.usbserial*`.
* **Servidor NET rigctl Compartido (Puerto 4532)**: Permite que programas de radio en Mac (como **WSJT-X**, **JS8Call**, **fldigi**, **MacLoggerDX**, **RUMlogNG**) compartan simultáneamente la conexión con la radio.
* **Audio Bidireccional de Baja Latencia (CoreAudio)**: Integración con la tarjeta de sonido interna del FT-710 (`USB Audio CODEC`) mediante PortAudio y CoreAudio nativo.
* **Servidor Web y Móvil HTTPS (Puerto 8888)**: Control remoto y escucha de audio desde cualquier iPhone, iPad, Mac o navegador web en la red local.
* **Espectro de Audio y Cascada en Tiempo Real**: FFT Hann de 4096 puntos en JavaScript/WebGL.
* **RF Scope FTDI FT4222H**: Soporte para la captura de espectro de radiofrecuencia de alta resolución sobre USB-SPI con la opción **SCU-LAN10 = ON**.

---

## Instalación y Compilación en macOS

### 1. Requisitos Previos (Homebrew)
Instale las dependencias necesarias mediante Homebrew:
```bash
brew install cmake hamlib portaudio openssl@3
```

### 2. Compilar

Por defecto, se compila sin mensajes de depuración para rendimiento óptimo:
```bash
cmake -B build-mac -S .
cmake --build build-mac
```

**Modo depuración**: Para ver mensajes detallados de construcción JSON, uso de memoria, etc., habilite `BUILD_DEBUG`:
```bash
cmake -B build-mac -S . -DBUILD_DEBUG=ON
cmake --build build-mac
```

El ejecutable generado se ubicará en: `build-mac/FT710Dashboard-macOS`.

---

## Uso y Ejecución

### Modo Demo (Sin radio física)
Para explorar la interfaz y probar el sistema sin conectar el transceptor:
```bash
./build-mac/FT710Dashboard-macOS --demo
```

### Conexión con el Yaesu FT-710
1. Conecte el cable USB posterior del FT-710 a su Mac.
2. Encienda la radio y ejecute:
```bash
./build-mac/FT710Dashboard-macOS
```
El sistema detectará automáticamente el puerto serie. Si desea forzar un puerto o velocidad:
```bash
./build-mac/FT710Dashboard-macOS --port /dev/cu.SLAB_USBtoUART --baud 38400
```

### Opciones de Línea de Comandos
```text
  --demo            Ejecuta en modo simulación con backend Hamlib Dummy
  --port <ruta>     Ruta del puerto serie Enhanced CAT-1 (ej: /dev/cu.SLAB_USBtoUART)
  --baud <tasa>     Velocidad en baudios de CAT (por defecto: 38400)
  --passcode <cod>  Contraseña fija de acceso a la web
  --no-browser      No abre el navegador automáticamente al iniciar
```

---

## Acceso Web y Audio en iPhone / iPad

1. Al iniciar el servidor, abra en su navegador: `http://localhost:8888/setup` (o reemplace `localhost` por la IP local de su Mac).
2. Descargue el perfil de configuración e instálelo en iOS (**Ajustes &rarr; Perfil descargado &rarr; Instalar**).
3. Active la confianza del certificado en iOS (**Ajustes &rarr; General &rarr; Información &rarr; Ajustes de confianza de certificados &rarr; Activar confianza para M0NXD / YY3BIG**).
4. Abra `https://<ip-de-su-mac>:8888/` para disfrutar del control remoto y audio bidireccional desde su teléfono.

---

## Licencia y Atribución

Este proyecto se distribuye bajo la licencia **Apache License 2.0**.
Conforme a la cláusula 4(d) de la licencia:
* Cualquier redistribución, modificación o bifurcación (fork) **DEBE conservar el archivo `NOTICE`** y otorgar crédito explícito y visible a:  
  **"Yury Jajitzky (YY3BIG) - FT-710 Dashboard for macOS"**
* Proyecto original de Windows desarrollado por **M0NXD** (https://github.com/M0NXD/FT-710Dashboard).
