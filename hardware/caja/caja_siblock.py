# Caja imprimible de SiBlock para un ESP32 DevKit (WROOM-32, 30 pines).
#
# Uso (FreeCAD 1.x, sin ventana):
#   freecadcmd caja_siblock.py            -> escribe base.stl, tapa.stl, caja_siblock.step y .FCStd
#   SALIDA=/otra/carpeta freecadcmd caja_siblock.py
#
# Todas las medidas están en milímetros. Las de la placa son las del DevKit V1 de 30 pines;
# si tu placa es otra (por ejemplo la DevKitC de 38 pines, 54.4 x 27.9), mídela y cambia
# PCB_L / PCB_W. La tapa no lleva agujeros sueltos: tiene una ventana sobre el extremo del USB,
# donde todas las variantes ponen el LED azul (GPIO2) y el botón BOOT (GPIO0).
import os

import FreeCAD as App
import Mesh
import MeshPart
import Part

# --- Placa ---
PCB_L = 51.5        # largo (eje X), el USB queda en X = 0
PCB_W = 28.5        # ancho (eje Y)
PCB_T = 1.6         # grosor
BAJO_PCB = 11.0     # espacio bajo la placa para los pines (plástico del conector + pines)
SOBRE_PCB = 7.0     # espacio sobre la placa: el módulo WROOM (3.3) llega al borde y el labio de la tapa baja 3

# --- USB (micro-USB o USB-C, centrado en el lado corto) ---
USB_ANCHO = 12.0    # deja pasar la funda del cable, no solo el conector
USB_ALTO = 7.5
USB_SOBRE_PCB = 1.6  # centro del conector medido desde la cara de arriba de la placa

# --- Caja ---
HOLGURA = 0.5       # aire entre la placa y la pared, por lado
PARED = 2.0
PISO = 2.0
TAPA = 2.0
LABIO = 3.0         # cuánto entra la tapa en la caja
JUEGO_TAPA = 0.2    # aire entre el labio de la tapa y la pared (ajuste a presión)
APOYO = 2.5         # repisa en los lados cortos donde descansa la placa (sin pines ahí)
RADIO = 3.0         # redondeo de las esquinas verticales

# --- Ventana del LED y BOOT, y ventilación ---
VENTANA_L = 9.0     # largo de la ventana desde el extremo del USB
VENTANA_W = PCB_W - 8.0
RANURAS = 5         # ranuras de ventilación por cada lado largo
RANURA_L = 6.0
RANURA_A = 1.6
TEXTO_ALTO = 5.0    # alto de las letras grabadas; se achica solo si no cabe

# --- Ojales para colgar en la pared (tornillo de 3 a 3.5 mm) ---
OJAL_GRANDE = 7.0
OJAL_CHICO = 3.6
OJAL_LARGO = 6.0

SALIDA = os.environ.get("SALIDA", os.path.dirname(os.path.abspath(__file__)))

# Medidas derivadas
IN_L = PCB_L + 2 * HOLGURA
IN_W = PCB_W + 2 * HOLGURA
IN_H = BAJO_PCB + PCB_T + SOBRE_PCB
EXT_L = IN_L + 2 * PARED
EXT_W = IN_W + 2 * PARED
EXT_H = PISO + IN_H
Z_PCB_ABAJO = PISO + BAJO_PCB
Z_PCB_ARRIBA = Z_PCB_ABAJO + PCB_T


def caja_redondeada(largo, ancho, alto, radio, x=0.0, y=0.0, z=0.0):
    """Prisma con las 4 aristas verticales redondeadas."""
    radio = min(radio, largo / 2 - 0.01, ancho / 2 - 0.01)
    forma = Part.makeBox(largo, ancho, alto, App.Vector(x, y, z))
    if radio > 0.05:
        verticales = [a for a in forma.Edges
                      if abs(a.Vertexes[0].Point.z - a.Vertexes[1].Point.z) > 1e-6]
        forma = forma.makeFillet(radio, verticales)
    return forma


def ranura_redonda(largo, alto, centro, eje="x", profundidad=10.0):
    """Ranura con puntas redondas, horizontal, que atraviesa una pared."""
    r = alto / 2
    if eje == "x":   # atraviesa en Y, se alarga en X
        caja = Part.makeBox(largo - alto, profundidad, alto,
                            App.Vector(centro.x - (largo - alto) / 2, centro.y - profundidad / 2, centro.z - r))
        c1 = Part.makeCylinder(r, profundidad, App.Vector(centro.x - (largo - alto) / 2, centro.y - profundidad / 2, centro.z), App.Vector(0, 1, 0))
        c2 = Part.makeCylinder(r, profundidad, App.Vector(centro.x + (largo - alto) / 2, centro.y - profundidad / 2, centro.z), App.Vector(0, 1, 0))
    else:            # atraviesa en X, se alarga en Y (no se usa en Z)
        caja = Part.makeBox(profundidad, largo - alto, alto,
                            App.Vector(centro.x - profundidad / 2, centro.y - (largo - alto) / 2, centro.z - r))
        c1 = Part.makeCylinder(r, profundidad, App.Vector(centro.x - profundidad / 2, centro.y - (largo - alto) / 2, centro.z), App.Vector(1, 0, 0))
        c2 = Part.makeCylinder(r, profundidad, App.Vector(centro.x - profundidad / 2, centro.y + (largo - alto) / 2, centro.z), App.Vector(1, 0, 0))
    return caja.fuse([c1, c2])


def base():
    cuerpo = caja_redondeada(EXT_L, EXT_W, EXT_H, RADIO)
    hueco = caja_redondeada(IN_L, IN_W, IN_H + 1, max(RADIO - PARED, 0.5), PARED, PARED, PISO)
    cuerpo = cuerpo.cut(hueco)

    # Repisas en los lados cortos: la placa descansa en sus extremos, donde no hay pines.
    for x in (PARED, PARED + IN_L - APOYO):
        cuerpo = cuerpo.fuse(Part.makeBox(APOYO, IN_W, BAJO_PCB, App.Vector(x, PARED, PISO)))

    # Hueco del USB en el lado corto de X = 0.
    zc = Z_PCB_ARRIBA + USB_SOBRE_PCB
    cuerpo = cuerpo.cut(Part.makeBox(PARED * 3, USB_ANCHO, USB_ALTO,
                                     App.Vector(-PARED, EXT_W / 2 - USB_ANCHO / 2, zc - USB_ALTO / 2)))

    # Ventilación baja en los dos lados largos (el aire entra abajo y sale por la tapa).
    paso = (IN_L - 2 * APOYO) / (RANURAS + 1)
    for i in range(1, RANURAS + 1):
        x = PARED + APOYO + i * paso
        for y in (PARED / 2, EXT_W - PARED / 2):
            cuerpo = cuerpo.cut(ranura_redonda(RANURA_L, RANURA_A, App.Vector(x, y, PISO + 3.0), "x", PARED * 3))

    # Ojales para colgar: círculo grande para la cabeza del tornillo y ranura hacia arriba (+X).
    for y in (EXT_W / 2,):
        for x in (EXT_L * 0.3, EXT_L * 0.7):
            grande = Part.makeCylinder(OJAL_GRANDE / 2, PISO * 3, App.Vector(x, y, -PISO))
            canal = Part.makeBox(OJAL_LARGO, OJAL_CHICO, PISO * 3, App.Vector(x, y - OJAL_CHICO / 2, -PISO))
            punta = Part.makeCylinder(OJAL_CHICO / 2, PISO * 3, App.Vector(x + OJAL_LARGO, y, -PISO))
            cuerpo = cuerpo.cut(grande.fuse([canal, punta]))
    return cuerpo.removeSplitter()


def tapa():
    # Se modela boca abajo respecto a la caja: placa arriba (Z de 0 a TAPA), labio hacia abajo.
    placa = caja_redondeada(EXT_L, EXT_W, TAPA, RADIO)
    lab_l = IN_L - 2 * JUEGO_TAPA
    lab_w = IN_W - 2 * JUEGO_TAPA
    labio = caja_redondeada(lab_l, lab_w, LABIO, max(RADIO - PARED, 0.5),
                            PARED + JUEGO_TAPA, PARED + JUEGO_TAPA, -LABIO)
    vacio = caja_redondeada(lab_l - 2 * 1.2, lab_w - 2 * 1.2, LABIO + 0.01, 0.5,
                            PARED + JUEGO_TAPA + 1.2, PARED + JUEGO_TAPA + 1.2, -LABIO - 0.01)
    forma = placa.fuse(labio.cut(vacio))

    # Ventana sobre el extremo del USB (LED y BOOT); el labio se corta ahí para no tapar el USB.
    ventana = caja_redondeada(VENTANA_L, VENTANA_W, TAPA + LABIO + 2, 1.5,
                              PARED + HOLGURA + 1.0, EXT_W / 2 - VENTANA_W / 2, -LABIO - 1)
    forma = forma.cut(ventana)
    forma = forma.cut(Part.makeBox(PARED + 1.5, USB_ANCHO + 2, LABIO + 0.01,
                                   App.Vector(PARED - 0.5, EXT_W / 2 - USB_ANCHO / 2 - 1, -LABIO - 0.01)))

    # Rejilla de ventilación en el extremo de la antena (lejos del USB).
    for i in range(4):
        x = EXT_L - PARED - 4.0 - i * 3.2
        forma = forma.cut(caja_redondeada(1.6, VENTANA_W, TAPA + LABIO + 2, 0.7,
                                          x - 0.8, EXT_W / 2 - VENTANA_W / 2, -LABIO - 1))

    # Nombre grabado (0.6 mm) si hay una fuente a mano.
    fuente = next((f for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                               "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf")
                   if os.path.exists(f)), None)
    if fuente:
        try:
            # Espacio libre entre el final de la ventana y la primera ranura de la rejilla.
            x0 = PARED + HOLGURA + 1.0 + VENTANA_L + 2.0
            x1 = EXT_L - PARED - 4.0 - 3 * 3.2 - 0.8 - 2.0
            alto = TEXTO_ALTO
            letras = Part.makeWireString("SiBlock", fuente, alto, 0)
            caras = []
            for w in letras:
                caras.append(Part.makeFace(w if isinstance(w, list) else [w], "Part::FaceMakerBullseye"))
            texto = Part.makeCompound(caras)
            escala = min(1.0, (x1 - x0) / texto.BoundBox.XLength)
            texto.scale(escala)
            bb = texto.BoundBox
            # El texto se lee desde afuera: la tapa puesta tiene su cara de afuera en Z = TAPA.
            texto.translate(App.Vector((x0 + x1) / 2 - bb.XLength / 2 - bb.XMin,
                                       EXT_W / 2 - bb.YLength / 2 - bb.YMin, TAPA - 0.6))
            forma = forma.cut(texto.extrude(App.Vector(0, 0, 1.0)))
        except Exception as e:  # sin texto la caja sigue sirviendo
            print("sin texto grabado:", e)
    return forma.removeSplitter()


def placa_simulada():
    """Volumen que ocupa la placa: PCB, módulo WROOM, USB y las dos filas de pines de abajo."""
    x0, y0 = PARED + HOLGURA, PARED + HOLGURA
    pcb = Part.makeBox(PCB_L, PCB_W, PCB_T, App.Vector(x0, y0, Z_PCB_ABAJO))
    modulo = Part.makeBox(25.5, 18.0, 3.3, App.Vector(x0 + PCB_L - 25.5, y0 + (PCB_W - 18.0) / 2, Z_PCB_ARRIBA))
    usb = Part.makeBox(5.5, 8.0, 3.0, App.Vector(x0 - 0.5, y0 + PCB_W / 2 - 4.0, Z_PCB_ARRIBA))
    largo_pines = 15 * 2.54
    pines = [Part.makeBox(largo_pines, 2.54, BAJO_PCB - 0.5,
                          App.Vector(x0 + (PCB_L - largo_pines) / 2, y, Z_PCB_ABAJO - (BAJO_PCB - 0.5)))
             for y in (y0 + 1.0, y0 + PCB_W - 1.0 - 2.54)]
    return pcb.fuse([modulo, usb] + pines)


def main():
    os.makedirs(SALIDA, exist_ok=True)
    b = base()
    t = tapa()
    for nombre, forma in (("base", b), ("tapa", t)):
        assert forma.isValid(), f"{nombre}: sólido no válido"
        assert len(forma.Solids) == 1, f"{nombre}: {len(forma.Solids)} sólidos"

    # La placa cabe: no choca con la base ni con la tapa puesta (descansa sobre las repisas).
    placa = placa_simulada()
    t_puesta = t.copy()
    t_puesta.translate(App.Vector(0, 0, EXT_H))
    for nombre, forma in (("base", b), ("tapa", t_puesta)):
        choque = placa.common(forma).Volume
        assert choque < 0.01, f"la placa choca con la {nombre}: {choque:.2f} mm3"
    print("la placa cabe sin chocar")

    # Para imprimir: la tapa se voltea, con la cara de afuera (la del texto) sobre la cama y el
    # labio hacia arriba, así no lleva soportes.
    t_imp = t.copy()
    t_imp.rotate(App.Vector(EXT_L / 2, EXT_W / 2, TAPA / 2), App.Vector(1, 0, 0), 180)
    t_imp.translate(App.Vector(0, 0, -t_imp.BoundBox.ZMin))

    for nombre, forma in (("base", b), ("tapa", t_imp)):
        malla = MeshPart.meshFromShape(Shape=forma, LinearDeflection=0.02, AngularDeflection=0.2,
                                       Relative=False)
        assert malla.isSolid() and not malla.hasNonManifolds(), f"{nombre}: malla con huecos"
        malla.write(os.path.join(SALIDA, f"{nombre}.stl"))  # STL binario
        print(f"{nombre}.stl: {malla.CountFacets} triángulos")

    # Ensamble para ver: la tapa puesta sobre la caja.
    doc = App.newDocument("caja_siblock")
    doc.addObject("Part::Feature", "Base").Shape = b
    doc.addObject("Part::Feature", "Tapa").Shape = t_puesta
    doc.recompute()
    Part.export(doc.Objects, os.path.join(SALIDA, "caja_siblock.step"))
    doc.saveAs(os.path.join(SALIDA, "caja_siblock.FCStd"))

    print(f"exterior: {EXT_L:.1f} x {EXT_W:.1f} x {EXT_H + TAPA:.1f} mm")
    print(f"base: {b.Volume / 1000:.1f} cm3, tapa: {t.Volume / 1000:.1f} cm3")


main()
