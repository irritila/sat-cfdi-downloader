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
    qml/components/EFirmaDialogo.qml
    qml/components/EstadoBadge.qml
    qml/components/BotonAccion.qml
    qml/components/CampoDetalle.qml
    qml/components/EncabezadoPagina.qml
    qml/components/DialogoConfirmacion.qml
    qml/components/Etiquetas.js
)

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
    SOURCES
        viewmodels/AppViewModel.h
        viewmodels/AppViewModel.cpp
        viewmodels/SolicitudesListModel.h
        viewmodels/SolicitudesListModel.cpp
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
        viewmodels/PresentacionViewModels.h
        viewmodels/PresentacionViewModels.cpp
)

# Includes: "presentation/viewmodels/X.h" desde src/, y "X.h" para el codigo
# generado por qmltyperegistrar.
target_include_directories(satcfdi_presentation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/viewmodels
)

target_link_libraries(satcfdi_presentation
    PUBLIC
        satcfdi_application
        satcfdi_domain
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

satcfdi_enable_warnings(satcfdi_presentation)
