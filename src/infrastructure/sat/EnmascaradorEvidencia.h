#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

namespace satcfdi::sat::evidencia {

// Enmascarado de evidencia del spike (T006, D6). Propio de la evidencia
// versionada; NO reemplaza a RegexLogSanitizer (que conserva RFC e Ids a
// proposito). Funcion pura, determinista e IDEMPOTENTE:
// enmascarar(enmascarar(x)) == enmascarar(x).
//
// Reemplaza el contenido (no la estructura) por marcadores entre corchetes:
// - elementos AutenticaResult, BinarySecurityToken, SignatureValue,
//   X509Certificate -> [token] / [certificado] / [firma] / [certificado];
//   DigestValue -> [digest] (derivado de datos enmascarados);
//   X509IssuerName -> [issuer]; X509SerialNumber -> [serial];
//   Paquete -> [paquete:N bytes] (N = bytes decodificados; el contenido nunca
//   se conserva); IdsPaquetes -> [IdPaquete]; RfcReceptor -> [RFC];
// - POR CONTEXTO, sin importar el formato del valor (UUID, opaco, con
//   espacios entre comillas): IdSolicitud -> [IdSolicitud]; IdPaquete e
//   IdsPaquetes -> [IdPaquete]; RfcSolicitante, RfcEmisor, RfcReceptor,
//   RfcReceptores, RfcACuentaTerceros -> [RFC]. Aplica a elementos XML
//   (<IdSolicitud>v</IdSolicitud>), atributos (IdSolicitud="v") y lineas de
//   texto (IdSolicitud: v, IdSolicitud=v, con o sin comillas; claves sin
//   distinguir mayusculas; IdsPaquetes/RfcReceptores aceptan listas "a, b").
//   Un valor sin clave reconocible solo se enmascara si coincide con un
//   patron libre (abajo) o con ocultarValores();
// - header WRAP access_token="..." -> WRAP access_token="[token]";
// - y en cualquier parte del texto: RFC (patron SAT), UUID (con sufijo _NN
//   opcional) -> [Id], enteros de 30 o mas digitos -> [serial] y Base64 de 200
//   o mas caracteres -> [base64:N].
QString enmascarar(QString texto);

// Para valores CONOCIDOS por la CLI (Ids del ledger, RFC del certificado) en
// textos sin clave de contexto (p. ej. rutas o nombres de archivo):
// - enmascararId(v): "[Id]" (vacio si `v` esta vacio);
// - ocultarValores(texto, valores, marcador): reemplaza cada aparicion literal
//   de cada valor no vacio (los mas largos primero) por `marcador`.
// Ambas son idempotentes y compatibles con enmascarar().
QString enmascararId(QStringView id);
QString ocultarValores(QString texto, QStringList valores, QStringView marcador = u"[oculto]");

// Para consola (confirmacion de la CLI): conserva los 2 primeros y el ultimo
// caracter del RFC, p. ej. "EK*********9". No es evidencia versionable.
QString rfcParaConsola(QStringView rfc);

} // namespace satcfdi::sat::evidencia
