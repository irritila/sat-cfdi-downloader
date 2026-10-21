# satcfdi_presentation: view models C++ y modulo QML SatCfdiDownloader (DA1).
# Depende solo de application, domain y Qt Core/Qml/Quick/QuickControls2.
# Sin Widgets, Sql ni Network. Los view models reciben servicios abstractos;
# las implementaciones concretas las elige el composition root (src/app).
qt_add_library(satcfdi_presentation STATIC)

set(_satcfdi_qml_files
    qml/Main.qml
    qml/screens/SolicitudesPage.qml
    qml/screens/NuevaSolicitudPage.qml
    qml/screens/DetalleSolicitudPage.qml
    qml/screens/PerfilesSatPage.qml
    qml/screens/ExploradorCfdiPage.qml
    # T023.4: pantalla Facturas (qt-interface-engineer, corte 2).
    qml/screens/FacturasPage.qml
    # T023.5: Historial (qt-interface-engineer, corte 3).
    qml/screens/HistorialPage.qml
    qml/components/FranjaMes.qml
    qml/components/ListaMesesFacturas.qml
    qml/components/EFirmaDialogo.qml
    qml/components/EstadoBadge.qml
    qml/components/BotonAccion.qml
    qml/components/BotonCopiar.qml
    qml/components/CampoDetalle.qml
    qml/components/EncabezadoPagina.qml
    qml/components/DialogoConfirmacion.qml
    qml/components/Etiquetas.js
    qml/components/Icono.qml
    qml/components/AnilloFoco.qml
    qml/components/AvisoEnLinea.qml
    qml/components/FranjaAvisoRaizPaquetes.qml
    qml/components/CampoArchivo.qml
    qml/components/CampoCombo.qml
    qml/components/CampoFormulario.qml
    qml/components/CampoTexto.qml
    qml/components/EstadoVacio.qml
    qml/components/EventoHistorial.qml
    qml/components/FilaCfdi.qml
    qml/components/VisorPdfCfdi.qml
    qml/components/FilaPaquete.qml
    qml/components/FilaPerfil.qml
    qml/components/FilaSolicitud.qml
    qml/components/ResumenEstado.qml
    qml/components/SelectorSegmentado.qml
    qml/Theme.qml
)

# T013 D3: Theme es singleton del modulo (import SatCfdiDownloader -> Theme).
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# T013 D4: iconos de linea de la entrega (docs/design/ui-ux-v2/iconos/), solo
# los que usan el traspaso y Theme.estado. Se publican en
# qrc:/qt/qml/SatCfdiDownloader/assets/icons/<nombre>.svg (lo carga Icono.qml).
file(GLOB _satcfdi_iconos RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qml/assets/icons/*.svg)
foreach(_icono IN LISTS _satcfdi_iconos)
    string(REPLACE "qml/" "" _alias_icono "${_icono}")
    set_source_files_properties("${_icono}" PROPERTIES QT_RESOURCE_ALIAS "${_alias_icono}")
endforeach()

# Todos los archivos QML/JS se publican en la raiz del modulo para que se vean
# entre si (y vean los tipos C++) sin imports por ruta.
foreach(_archivo IN LISTS _satcfdi_qml_files)
    get_filename_component(_nombre "${_archivo}" NAME)
    set_source_files_properties("${_archivo}" PROPERTIES QT_RESOURCE_ALIAS "${_nombre}")
endforeach()

qt_add_qml_module(satcfdi_presentation
    URI SatCfdiDownloader
    VERSION 1.0
    PLUGIN_TARGET satcfdi_presentationplugin
    # El directorio de salida termina en la ruta del URI para qmllint/qmlls.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/SatCfdiDownloader
    QML_FILES ${_satcfdi_qml_files}
    RESOURCES ${_satcfdi_iconos}
    SOURCES
        viewmodels/AppViewModel.h
        viewmodels/AppViewModel.cpp
        viewmodels/SolicitudesListModel.h
        viewmodels/SolicitudesListModel.cpp
        viewmodels/SolicitudesFiltroModel.h
        viewmodels/SolicitudesFiltroModel.cpp
        viewmodels/Portapapeles.h
        viewmodels/Portapapeles.cpp
        viewmodels/PerfilesDisponiblesModel.h
        viewmodels/PerfilesDisponiblesModel.cpp
        viewmodels/PerfilesSatListModel.h
        viewmodels/PerfilesSatListModel.cpp
        viewmodels/PerfilesSatViewModel.h
        viewmodels/PerfilesSatViewModel.cpp
        viewmodels/EFirmaFormViewModel.h
        viewmodels/EFirmaFormViewModel.cpp
        viewmodels/NuevaSolicitudViewModel.h
        viewmodels/NuevaSolicitudViewModel.cpp
        viewmodels/SolicitudDetailViewModel.h
        viewmodels/SolicitudDetailViewModel.cpp
        viewmodels/AccionesSolicitud.h
        viewmodels/AccionesFinder.h
        viewmodels/CatalogoMensajes.h
        viewmodels/CatalogoMensajes.cpp
        viewmodels/ConsultaExistenciaPaquetes.h
        viewmodels/ConsultaCatalogoCfdi.h
        viewmodels/CfdiListModel.h
        viewmodels/CfdiListModel.cpp
        viewmodels/ExploradorCfdiViewModel.h
        viewmodels/ExploradorCfdiViewModel.cpp
        viewmodels/PrevisualizacionCfdi.h
        viewmodels/VisorPdfCfdiViewModel.h
        viewmodels/VisorPdfCfdiViewModel.cpp
        # T023.4: pantalla Facturas (perfil, meses, franja, pestanas, lista).
        viewmodels/FuenteFacturasMes.h
        viewmodels/FranjaMesViewModel.h
        viewmodels/FranjaMesViewModel.cpp
        viewmodels/MesesFacturasModel.h
        viewmodels/MesesFacturasModel.cpp
        viewmodels/FacturasListModel.h
        viewmodels/FacturasListModel.cpp
        viewmodels/FacturasViewModel.h
        viewmodels/FacturasViewModel.cpp
        viewmodels/FuenteHistorial.h
        viewmodels/HistorialListModel.h
        viewmodels/HistorialListModel.cpp
        viewmodels/HistorialViewModel.h
        viewmodels/HistorialViewModel.cpp
        estilo/EstiloVisual.h
        estilo/EstiloVisual.cpp
        componentes/IconoSvg.h
        componentes/IconoSvg.cpp
        viewmodels/FormatoFechas.h
        viewmodels/FormatoFechas.cpp
        viewmodels/PresentacionViewModels.h
        viewmodels/PresentacionViewModels.cpp
        viewmodels/AvisoRaizPaquetesViewModel.h
        viewmodels/AvisoRaizPaquetesViewModel.cpp
)

# Includes: "presentation/viewmodels/X.h" desde src/, y "X.h" para el codigo
# generado por qmltyperegistrar.
target_include_directories(satcfdi_presentation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/viewmodels
        ${CMAKE_CURRENT_SOURCE_DIR}/componentes
)

# QSvgRenderer para IconoSvg (T013 D4).
find_package(Qt6 REQUIRED COMPONENTS Svg)
# T015.4 (T015 D7, D12): visor del PDF del CFDI con QtQuick.Pdf. Se enlaza
# PdfQuick (y Pdf) para que el despliegue y la firma incluyan el modulo QML.
find_package(Qt6 REQUIRED COMPONENTS Pdf PdfQuick)

target_link_libraries(satcfdi_presentation
    PUBLIC
        satcfdi_application
        satcfdi_domain
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Svg
        Qt6::Pdf
        Qt6::PdfQuick
)

satcfdi_enable_warnings(satcfdi_presentation)
