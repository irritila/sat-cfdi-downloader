#!/bin/sh
# Genera fixtures SINTETICOS de e.firma para las pruebas de T005 (DA7, DC2).
# No contiene ni produce e.firmas reales: todo es autofirmado y desechable.
#
# Uso: generar_fixtures.sh <openssl> <directorio-salida>
#   La contrasena se recibe en la variable de entorno FIXTURE_PASSWORD (no en
#   argv). El directorio debe existir (la prueba usa un QTemporaryDir).
#
# Imita la forma de una e.firma real (evidencia sanitizada, bitacora 03:40):
# - subject SIN OU; RFC solo en x500UniqueIdentifier (OID 2.5.4.45) con la
#   forma "RFC / CURP"; CURP en serialNumber con la forma "/ CURP";
# - keyUsage: digitalSignature, nonRepudiation, dataEncipherment, keyAgreement;
# - extendedKeyUsage: emailProtection, clientAuth;
# - .cer en DER; .key en PKCS#8 DER cifrado (PBES2 con 3DES, como el SAT).
#
# Salida:
#   efirma.cer / efirma.key        par valido (RFC GOMA800101AB1), vigencia
#                                  2025-01-01 .. 2029-01-01 UTC
#   efirma_aes.key                 la misma llave, PBES2 AES-256-CBC
#   efirma_pbe1.key                la misma llave, PKCS#5 v1.5 (PBE-SHA1-3DES)
#   efirma.pem                     el certificado en PEM (formato no admitido)
#   llave_sin_cifrar.key           la misma llave, PKCS#8 DER SIN cifrar
#   csd.cer / csd.key              mismo RFC pero con OU (CSD) -> NoEsEFirma
#   otro_rfc.cer / otro_rfc.key    RFC distinto (XEXX010101AB2)
#   segundo_par.key                llave de otro par -> ParejaIncompatible
set -eu

OPENSSL="$1"
OUT="$2"
: "${FIXTURE_PASSWORD:?FIXTURE_PASSWORD requerida}"
[ -d "$OUT" ] || { echo "directorio de salida inexistente" >&2; exit 2; }

umask 077
cd "$OUT"

NOT_BEFORE=20250101000000Z
NOT_AFTER=20290101000000Z

config() {
    # $1 = archivo, $2 = RFC, $3 = CURP, $4 = OU (vacio = sin OU)
    {
        echo "[req]"
        echo "distinguished_name = dn"
        echo "prompt = no"
        echo "utf8 = yes"
        echo "string_mask = utf8only"
        echo "x509_extensions = ext"
        echo "[dn]"
        echo "CN = PERSONA DE PRUEBA SINTETICA"
        echo "name = PERSONA DE PRUEBA SINTETICA"
        echo "O = PERSONA DE PRUEBA SINTETICA"
        if [ -n "$4" ]; then echo "OU = $4"; fi
        echo "C = MX"
        echo "emailAddress = prueba@example.invalid"
        echo "x500UniqueIdentifier = $2 / $3"
        echo "serialNumber = / $3"
        echo "[ext]"
        echo "keyUsage = critical, digitalSignature, nonRepudiation, dataEncipherment, keyAgreement"
        echo "extendedKeyUsage = emailProtection, clientAuth"
        echo "basicConstraints = critical, CA:FALSE"
    } > "$1"
}

llave() {
    "$OPENSSL" genpkey -quiet -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$1"
}

certificado() {
    # $1 = config, $2 = llave PEM, $3 = serie hex, $4 = salida DER
    "$OPENSSL" req -new -x509 -config "$1" -key "$2" \
        -not_before "$NOT_BEFORE" -not_after "$NOT_AFTER" \
        -set_serial "$3" -outform DER -out "$4"
}

pkcs8() {
    # $1 = llave PEM, $2 = salida DER, resto = opciones de cifrado
    entrada="$1"; salida="$2"; shift 2
    "$OPENSSL" pkcs8 -topk8 -in "$entrada" -outform DER "$@" \
        -passout env:FIXTURE_PASSWORD -out "$salida"
}

# Par valido.
llave efirma.pem.key
config efirma.cnf GOMA800101AB1 GOMA800101HDFRRN09 ""
certificado efirma.cnf efirma.pem.key 0x3030303031303030303030373030303030303031 efirma.cer
pkcs8 efirma.pem.key efirma.key -v2 des3
pkcs8 efirma.pem.key efirma_aes.key -v2 aes-256-cbc
pkcs8 efirma.pem.key efirma_pbe1.key -v1 PBE-SHA1-3DES
"$OPENSSL" pkcs8 -topk8 -in efirma.pem.key -outform DER -nocrypt -out llave_sin_cifrar.key
"$OPENSSL" x509 -inform DER -in efirma.cer -outform PEM -out efirma.pem

# CSD: mismo RFC, con OU.
llave csd.pem.key
config csd.cnf GOMA800101AB1 GOMA800101HDFRRN09 "SUCURSAL MATRIZ"
certificado csd.cnf csd.pem.key 0x3030303031303030303030373030303030303032 csd.cer
pkcs8 csd.pem.key csd.key -v2 des3

# Otro RFC.
llave otro_rfc.pem.key
config otro_rfc.cnf XEXX010101AB2 XEXX010101HDFRRN01 ""
certificado otro_rfc.cnf otro_rfc.pem.key 0x3030303031303030303030373030303030303033 otro_rfc.cer
pkcs8 otro_rfc.pem.key otro_rfc.key -v2 des3

# Segundo par (solo la llave).
llave segundo_par.pem.key
pkcs8 segundo_par.pem.key segundo_par.key -v2 des3

# Las llaves PEM sin cifrar no se dejan en el directorio.
rm -f ./*.pem.key ./*.cnf
