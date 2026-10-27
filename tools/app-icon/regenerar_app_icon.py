"""T020 D5: regenera el icono compilado de la app desde resources/macos/AppIcon.icon.

Uso (desde cualquier directorio; requiere Xcode 26 o posterior):

    tools/app-icon/regenerar_app_icon            # regenera y reescribe el manifiesto
    tools/app-icon/regenerar_app_icon --check    # regenera en un temporal y compara

Minimo de macOS (--minimum-deployment-target): el SATCFDI_MACOS_MINIMUM_VERSION
del build. Se toma, por prioridad, de --minimo-macos, de la variable de
entorno SATCFDI_MACOS_MINIMUM_VERSION o del LSMinimumSystemVersion del bundle
de --build-dir (por omision build/; requiere haber compilado satcfdi_app).
Queda en el manifiesto (`minimo-macos`) y el ctest lo compara con el del build
que se prueba.

Pasos:
1. Sincroniza las capas: copia resources/branding/icono/icono-<capa>.svg a
   AppIcon.icon/Assets/ (en --check solo comprueba que sean iguales).
2. xcrun actool compila el .icon a Assets.car + AppIcon.icns en un temporal.
3. Exige en Assets.car las apariencias Aqua, DarkAqua y Tintable de AppIcon y
   CFBundleIconName/CFBundleIconFile = AppIcon en el plist parcial.
4. Sin --check: copia las salidas a resources/macos/AppIcon-compilado/ y escribe
   resources/macos/AppIcon.manifiesto (hash de fuentes y salidas, comando y
   xcodebuild -version). Con --check compara contra lo versionado:
   - AppIcon.icns: bytes iguales con la misma version de Xcode; con otra, los
     PNG extraidos con iconutil.
   - Assets.car: bytes iguales si coinciden; actool 26.2 no es determinista
     (nombres de rendition con UUID y Timestamp), asi que si difieren se
     compara `assetutil --info` normalizado. Con otra version de Xcode solo se
     compara la estructura (tipos, nombres y apariencias de las renditions).
   - Hashes de fuentes y salidas versionadas contra el manifiesto.

Solo usa la biblioteca estandar de Python 3.
"""

import argparse
import filecmp
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ICON = "resources/macos/AppIcon.icon"
COMPILADO = "resources/macos/AppIcon-compilado"
MANIFIESTO = "resources/macos/AppIcon.manifiesto"
CAPAS = ["carpeta", "pestana", "hoja-posterior", "hoja-anterior", "frente"]
SALIDAS = ["Assets.car", "AppIcon.icns"]
APARIENCIAS = {"NSAppearanceNameAqua", "NSAppearanceNameDarkAqua", "ISAppearanceTintable"}


def ruta(rel):
    return os.path.join(RAIZ, rel)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for bloque in iter(lambda: f.read(1 << 16), b""):
            h.update(bloque)
    return h.hexdigest()


def falla(mensaje):
    print("error: " + mensaje, file=sys.stderr)
    sys.exit(1)


def ejecutar(args, **kw):
    r = subprocess.run(args, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        falla("fallo %s:\n%s%s" % (" ".join(args), r.stdout, r.stderr))
    return r.stdout


def version_xcode():
    return " / ".join(ejecutar(["xcodebuild", "-version"]).strip().splitlines())


MINIMO = re.compile(r"^[0-9]+\.[0-9]+(\.[0-9]+)?$")


def minimo_macos(argumento, build_dir):
    """SATCFDI_MACOS_MINIMUM_VERSION: argumento, entorno o bundle del build.

    El build no guarda la variable en CMakeCache.txt (sin
    CMAKE_OSX_DEPLOYMENT_TARGET se deriva del SDK), pero la escribe tal cual en
    LSMinimumSystemVersion del Info.plist de satcfdi_app.
    """
    valor, origen = argumento, "--minimo-macos"
    if not valor:
        valor, origen = os.environ.get("SATCFDI_MACOS_MINIMUM_VERSION"), "entorno"
    if not valor:
        plist = os.path.join(build_dir, "src/app/satcfdi_app.app/Contents/Info.plist")
        origen = plist
        if os.path.isfile(plist):
            valor = ejecutar(["plutil", "-extract", "LSMinimumSystemVersion", "raw", plist]).strip()
    if not valor or not MINIMO.match(valor):
        falla("minimo de macOS invalido o ausente (%s); compilar el build, usar "
              "--minimo-macos o SATCFDI_MACOS_MINIMUM_VERSION" % origen)
    print("minimo de macOS: %s (%s)" % (valor, origen))
    return valor


def fuentes():
    """Fuentes del icono en orden estable (rutas relativas a la raiz)."""
    lista = ["resources/branding/icono/icono-%s.svg" % c for c in CAPAS]
    icon = []
    for base, _, archivos in os.walk(ruta(ICON)):
        for a in archivos:
            if a == ".DS_Store":
                continue
            icon.append(os.path.relpath(os.path.join(base, a), RAIZ))
    return lista + sorted(icon)


def sincronizar_capas(check):
    for c in CAPAS:
        origen = ruta("resources/branding/icono/icono-%s.svg" % c)
        destino = ruta("%s/Assets/icono-%s.svg" % (ICON, c))
        if os.path.exists(destino) and filecmp.cmp(origen, destino, shallow=False):
            continue
        if check:
            falla("la capa %s del .icon no coincide con resources/branding/icono "
                  "(ejecutar regenerar_app_icon sin --check)" % c)
        shutil.copyfile(origen, destino)
        print("capa sincronizada: icono-%s.svg" % c)


def comando_actool(salida, minimo):
    return ["xcrun", "actool", ruta(ICON), "--compile", salida, "--platform", "macosx",
            "--minimum-deployment-target", minimo, "--app-icon", "AppIcon",
            "--output-partial-info-plist", os.path.join(salida, "partial.plist")]


def compilar(salida, minimo):
    os.makedirs(salida, exist_ok=True)
    ejecutar(comando_actool(salida, minimo))
    plist = ejecutar(["plutil", "-convert", "json", "-o", "-", os.path.join(salida, "partial.plist")])
    parcial = json.loads(plist)
    if parcial.get("CFBundleIconName") != "AppIcon" or parcial.get("CFBundleIconFile") != "AppIcon":
        falla("plist parcial inesperado: %s" % parcial)
    for s in SALIDAS:
        if not os.path.isfile(os.path.join(salida, s)):
            falla("actool no produjo %s" % s)
    info = info_car(os.path.join(salida, "Assets.car"))
    apariencias = {r.get("Appearance") for r in info[1:]
                   if r.get("AssetType") == "IconImageStack" and r.get("Name") == "AppIcon"}
    if not APARIENCIAS <= apariencias:
        falla("Assets.car sin apariencias %s" % sorted(APARIENCIAS - apariencias))


def info_car(car):
    return json.loads(ejecutar(["xcrun", "assetutil", "--info", car]))


UUID = re.compile(r"_[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}-[0-9A-F-]+(\.png)$")


def normalizar(info, estructura):
    """assetutil --info sin lo que actool cambia en cada corrida.

    estructura=True (otra version de Xcode): solo tipo, nombre, apariencia,
    idiom, tamano y escala de cada rendition.
    """
    renditions = []
    for r in info[1:]:
        r = dict(r)
        if "RenditionName" in r:
            r["RenditionName"] = UUID.sub(r"_UUID\1", r["RenditionName"])
            if r["RenditionName"].endswith("_UUID.png"):
                # El digest de estas renditions cubre el nombre con UUID.
                r.pop("SHA1Digest", None)
        if estructura:
            r = {k: r.get(k) for k in ("AssetType", "Name", "Appearance", "Idiom", "Size", "Scale")}
        renditions.append(json.dumps(r, sort_keys=True))
    return sorted(renditions)


def pngs_icns(icns, destino):
    conjunto = os.path.join(destino, "AppIcon.iconset")
    ejecutar(["iconutil", "-c", "iconset", icns, "-o", conjunto])
    return {a: sha256(os.path.join(conjunto, a)) for a in sorted(os.listdir(conjunto))}


def leer_manifiesto():
    datos = {"fuente": {}, "salida": {}}
    with open(ruta(MANIFIESTO), encoding="utf-8") as f:
        for linea in f:
            linea = linea.rstrip("\n")
            if not linea or linea.startswith("#"):
                continue
            partes = linea.split("\t")
            if partes[0] in ("fuente", "salida"):
                datos[partes[0]][partes[2]] = partes[1]
            else:
                datos[partes[0]] = partes[1]
    return datos


def escribir_manifiesto(xcode, minimo):
    comando = " ".join(comando_actool("<tmp>", minimo)).replace(RAIZ + "/", "")
    lineas = [
        "# T020 D5: manifiesto del icono compilado. Lo escribe",
        "# tools/app-icon/regenerar_app_icon; no editar a mano. Campos separados por",
        "# tabulador. satcfdi_app_icon_manifiesto (ctest, sin Xcode) recalcula los",
        "# hashes y falla si una fuente o salida no coincide.",
        "xcode\t" + xcode,
        "minimo-macos\t" + minimo,
        "comando\t" + comando,
    ]
    for rel in fuentes():
        lineas.append("fuente\t%s\t%s" % (sha256(ruta(rel)), rel))
    for s in SALIDAS:
        rel = "%s/%s" % (COMPILADO, s)
        lineas.append("salida\t%s\t%s" % (sha256(ruta(rel)), rel))
    with open(ruta(MANIFIESTO), "w", encoding="utf-8") as f:
        f.write("\n".join(lineas) + "\n")


def comprobar(tmp, xcode, minimo):
    manifiesto = leer_manifiesto()
    errores = []
    if manifiesto.get("minimo-macos") != minimo:
        errores.append("minimo de macOS del manifiesto (%s) distinto del actual (%s)"
                       % (manifiesto.get("minimo-macos"), minimo))
    for rel in fuentes():
        if manifiesto["fuente"].get(rel) != sha256(ruta(rel)):
            errores.append("fuente distinta del manifiesto: " + rel)
    if set(manifiesto["fuente"]) != set(fuentes()):
        errores.append("el manifiesto no lista exactamente las fuentes actuales")
    for s in SALIDAS:
        rel = "%s/%s" % (COMPILADO, s)
        if manifiesto["salida"].get(rel) != sha256(ruta(rel)):
            errores.append("salida versionada distinta del manifiesto: " + rel)

    misma = manifiesto.get("xcode") == xcode
    print("Xcode del manifiesto: %s; actual: %s (%s)" % (
        manifiesto.get("xcode"), xcode, "misma version" if misma else "OTRA version"))
    nuevo = os.path.join(tmp, "out")
    for s in SALIDAS:
        igual = filecmp.cmp(os.path.join(nuevo, s), ruta("%s/%s" % (COMPILADO, s)), shallow=False)
        print("%s: bytes %s" % (s, "iguales" if igual else "distintos"))
        if s == "AppIcon.icns" and misma and not igual:
            errores.append("AppIcon.icns distinto con la misma version de Xcode")

    a = normalizar(info_car(ruta(COMPILADO + "/Assets.car")), not misma)
    b = normalizar(info_car(os.path.join(nuevo, "Assets.car")), not misma)
    print("Assets.car: assetutil --info normalizado (%s) %s" % (
        "completo" if misma else "estructura", "igual" if a == b else "DISTINTO"))
    if a != b:
        errores.append("Assets.car no equivale al versionado")

    if not misma:
        pv = pngs_icns(ruta(COMPILADO + "/AppIcon.icns"), os.path.join(tmp, "versionado"))
        pn = pngs_icns(os.path.join(nuevo, "AppIcon.icns"), os.path.join(tmp, "nuevo"))
        print("PNG del .icns: %s" % ("iguales" if pv == pn else "DISTINTOS"))
        if pv != pn:
            errores.append("PNG extraidos del .icns distintos")

    if errores:
        for e in errores:
            print("error: " + e, file=sys.stderr)
        sys.exit(1)
    print("check OK")


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--check", action="store_true", help="regenera en un temporal y compara")
    p.add_argument("--minimo-macos", help="SATCFDI_MACOS_MINIMUM_VERSION explicito (p. ej. 26.0)")
    p.add_argument("--build-dir", default=ruta("build"),
                   help="build del que leer SATCFDI_MACOS_MINIMUM_VERSION (por omision build/)")
    args = p.parse_args()

    xcode = version_xcode()
    minimo = minimo_macos(args.minimo_macos, args.build_dir)
    sincronizar_capas(args.check)
    with tempfile.TemporaryDirectory(prefix="app-icon-") as tmp:
        compilar(os.path.join(tmp, "out"), minimo)
        if args.check:
            comprobar(tmp, xcode, minimo)
            return
        os.makedirs(ruta(COMPILADO), exist_ok=True)
        for s in SALIDAS:
            shutil.copyfile(os.path.join(tmp, "out", s), ruta("%s/%s" % (COMPILADO, s)))
    escribir_manifiesto(xcode, minimo)
    for s in SALIDAS:
        print("%s %s" % (sha256(ruta("%s/%s" % (COMPILADO, s))), s))
    print("manifiesto: " + MANIFIESTO)


if __name__ == "__main__":
    main()
